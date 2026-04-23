#!/usr/bin/env python3
"""
NES Static Recompiler — nesrecomp.py
Reads a .nes ROM, discovers all reachable 6502 code via BFS from
RESET/NMI/IRQ vectors + JSR/JMP tracing, then emits C functions
and a call_by_address() dispatch table.

Usage:
    python nesrecomp.py game.nes [--cfg game.cfg] [--out generated/]
"""

import sys
import os
import argparse
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# iNES header
# ---------------------------------------------------------------------------

@dataclass
class INesHeader:
    prg_banks: int        # 16KB each
    chr_banks: int        # 8KB each
    mapper: int
    mirroring: int        # 0=horizontal,1=vertical
    has_battery: bool
    has_trainer: bool

    @property
    def prg_size(self): return self.prg_banks * 16384
    @property
    def chr_size(self): return self.chr_banks * 8192

def parse_ines(data: bytes) -> Tuple[INesHeader, bytes, bytes]:
    if data[:4] != b'NES\x1a':
        raise ValueError("Not a valid iNES file")
    prg_banks = data[4]
    chr_banks  = data[5]
    flags6     = data[6]
    flags7     = data[7]
    mapper     = (flags7 & 0xF0) | (flags6 >> 4)
    mirroring  = flags6 & 1
    has_battery = bool(flags6 & 2)
    has_trainer = bool(flags6 & 4)
    hdr = INesHeader(prg_banks, chr_banks, mapper, mirroring, has_battery, has_trainer)
    offset = 16 + (512 if has_trainer else 0)
    prg = data[offset: offset + hdr.prg_size]
    chr_ = data[offset + hdr.prg_size: offset + hdr.prg_size + hdr.chr_size]
    return hdr, prg, chr_

# ---------------------------------------------------------------------------
# 6502 instruction table
# ---------------------------------------------------------------------------

@dataclass
class Op:
    mnemonic: str
    mode: str       # addr mode abbreviation
    size: int       # bytes (including opcode)
    cycles: int

# Addressing modes:
#  imp  = implied/accumulator
#  imm  = immediate  #$nn
#  zp   = zero page  $nn
#  zpx  = zero page,X
#  zpy  = zero page,Y
#  abs  = absolute   $nnnn
#  abx  = absolute,X
#  aby  = absolute,Y
#  ind  = indirect   ($nnnn)
#  izx  = (indirect,X)
#  izy  = (indirect),Y
#  rel  = relative   (branch)

OPTABLE: Dict[int, Op] = {
    # LDA
    0xA9: Op("LDA","imm",2,2), 0xA5: Op("LDA","zp",2,3), 0xB5: Op("LDA","zpx",2,4),
    0xAD: Op("LDA","abs",3,4), 0xBD: Op("LDA","abx",3,4), 0xB9: Op("LDA","aby",3,4),
    0xA1: Op("LDA","izx",2,6), 0xB1: Op("LDA","izy",2,5),
    # LDX
    0xA2: Op("LDX","imm",2,2), 0xA6: Op("LDX","zp",2,3), 0xB6: Op("LDX","zpy",2,4),
    0xAE: Op("LDX","abs",3,4), 0xBE: Op("LDX","aby",3,4),
    # LDY
    0xA0: Op("LDY","imm",2,2), 0xA4: Op("LDY","zp",2,3), 0xB4: Op("LDY","zpx",2,4),
    0xAC: Op("LDY","abs",3,4), 0xBC: Op("LDY","abx",3,4),
    # STA
    0x85: Op("STA","zp",2,3), 0x95: Op("STA","zpx",2,4),
    0x8D: Op("STA","abs",3,4), 0x9D: Op("STA","abx",3,5), 0x99: Op("STA","aby",3,5),
    0x81: Op("STA","izx",2,6), 0x91: Op("STA","izy",2,6),
    # STX
    0x86: Op("STX","zp",2,3), 0x96: Op("STX","zpy",2,4), 0x8E: Op("STX","abs",3,4),
    # STY
    0x84: Op("STY","zp",2,3), 0x94: Op("STY","zpx",2,4), 0x8C: Op("STY","abs",3,4),
    # Transfer
    0xAA: Op("TAX","imp",1,2), 0x8A: Op("TXA","imp",1,2),
    0xA8: Op("TAY","imp",1,2), 0x98: Op("TYA","imp",1,2),
    0xBA: Op("TSX","imp",1,2), 0x9A: Op("TXS","imp",1,2),
    # Stack
    0x48: Op("PHA","imp",1,3), 0x68: Op("PLA","imp",1,4),
    0x08: Op("PHP","imp",1,3), 0x28: Op("PLP","imp",1,4),
    # Arithmetic
    0x69: Op("ADC","imm",2,2), 0x65: Op("ADC","zp",2,3), 0x75: Op("ADC","zpx",2,4),
    0x6D: Op("ADC","abs",3,4), 0x7D: Op("ADC","abx",3,4), 0x79: Op("ADC","aby",3,4),
    0x61: Op("ADC","izx",2,6), 0x71: Op("ADC","izy",2,5),
    0xE9: Op("SBC","imm",2,2), 0xE5: Op("SBC","zp",2,3), 0xF5: Op("SBC","zpx",2,4),
    0xED: Op("SBC","abs",3,4), 0xFD: Op("SBC","abx",3,4), 0xF9: Op("SBC","aby",3,4),
    0xE1: Op("SBC","izx",2,6), 0xF1: Op("SBC","izy",2,5),
    # Increment/Decrement
    0xE8: Op("INX","imp",1,2), 0xC8: Op("INY","imp",1,2),
    0xCA: Op("DEX","imp",1,2), 0x88: Op("DEY","imp",1,2),
    0xE6: Op("INC","zp",2,5), 0xF6: Op("INC","zpx",2,6),
    0xEE: Op("INC","abs",3,6), 0xFE: Op("INC","abx",3,7),
    0xC6: Op("DEC","zp",2,5), 0xD6: Op("DEC","zpx",2,6),
    0xCE: Op("DEC","abs",3,6), 0xDE: Op("DEC","abx",3,7),
    # Logical
    0x29: Op("AND","imm",2,2), 0x25: Op("AND","zp",2,3), 0x35: Op("AND","zpx",2,4),
    0x2D: Op("AND","abs",3,4), 0x3D: Op("AND","abx",3,4), 0x39: Op("AND","aby",3,4),
    0x21: Op("AND","izx",2,6), 0x31: Op("AND","izy",2,5),
    0x09: Op("ORA","imm",2,2), 0x05: Op("ORA","zp",2,3), 0x15: Op("ORA","zpx",2,4),
    0x0D: Op("ORA","abs",3,4), 0x1D: Op("ORA","abx",3,4), 0x19: Op("ORA","aby",3,4),
    0x01: Op("ORA","izx",2,6), 0x11: Op("ORA","izy",2,5),
    0x49: Op("EOR","imm",2,2), 0x45: Op("EOR","zp",2,3), 0x55: Op("EOR","zpx",2,4),
    0x4D: Op("EOR","abs",3,4), 0x5D: Op("EOR","abx",3,4), 0x59: Op("EOR","aby",3,4),
    0x41: Op("EOR","izx",2,6), 0x51: Op("EOR","izy",2,5),
    # Shift/Rotate
    0x0A: Op("ASL","imp",1,2), 0x06: Op("ASL","zp",2,5), 0x16: Op("ASL","zpx",2,6),
    0x0E: Op("ASL","abs",3,6), 0x1E: Op("ASL","abx",3,7),
    0x4A: Op("LSR","imp",1,2), 0x46: Op("LSR","zp",2,5), 0x56: Op("LSR","zpx",2,6),
    0x4E: Op("LSR","abs",3,6), 0x5E: Op("LSR","abx",3,7),
    0x2A: Op("ROL","imp",1,2), 0x26: Op("ROL","zp",2,5), 0x36: Op("ROL","zpx",2,6),
    0x2E: Op("ROL","abs",3,6), 0x3E: Op("ROL","abx",3,7),
    0x6A: Op("ROR","imp",1,2), 0x66: Op("ROR","zp",2,5), 0x76: Op("ROR","zpx",2,6),
    0x6E: Op("ROR","abs",3,6), 0x7E: Op("ROR","abx",3,7),
    # Compare
    0xC9: Op("CMP","imm",2,2), 0xC5: Op("CMP","zp",2,3), 0xD5: Op("CMP","zpx",2,4),
    0xCD: Op("CMP","abs",3,4), 0xDD: Op("CMP","abx",3,4), 0xD9: Op("CMP","aby",3,4),
    0xC1: Op("CMP","izx",2,6), 0xD1: Op("CMP","izy",2,5),
    0xE0: Op("CPX","imm",2,2), 0xE4: Op("CPX","zp",2,3), 0xEC: Op("CPX","abs",3,4),
    0xC0: Op("CPY","imm",2,2), 0xC4: Op("CPY","zp",2,3), 0xCC: Op("CPY","abs",3,4),
    # BIT
    0x24: Op("BIT","zp",2,3), 0x2C: Op("BIT","abs",3,4),
    # Branches
    0x90: Op("BCC","rel",2,2), 0xB0: Op("BCS","rel",2,2),
    0xF0: Op("BEQ","rel",2,2), 0xD0: Op("BNE","rel",2,2),
    0x30: Op("BMI","rel",2,2), 0x10: Op("BPL","rel",2,2),
    0x50: Op("BVC","rel",2,2), 0x70: Op("BVS","rel",2,2),
    # Jump/Call/Return
    0x4C: Op("JMP","abs",3,3), 0x6C: Op("JMP","ind",3,5),
    0x20: Op("JSR","abs",3,6),
    0x60: Op("RTS","imp",1,6), 0x40: Op("RTI","imp",1,6),
    # Flags
    0x18: Op("CLC","imp",1,2), 0x38: Op("SEC","imp",1,2),
    0x58: Op("CLI","imp",1,2), 0x78: Op("SEI","imp",1,2),
    0xD8: Op("CLD","imp",1,2), 0xF8: Op("SED","imp",1,2),
    0xB8: Op("CLV","imp",1,2),
    # Misc
    0xEA: Op("NOP","imp",1,2), 0x00: Op("BRK","imp",1,7),
    # Undocumented (sized NOPs / common aliases)
    0x1A: Op("NOP","imp",1,2), 0x3A: Op("NOP","imp",1,2),
    0x5A: Op("NOP","imp",1,2), 0x7A: Op("NOP","imp",1,2),
    0xDA: Op("NOP","imp",1,2), 0xFA: Op("NOP","imp",1,2),
    0x80: Op("NOP","imm",2,2), 0x82: Op("NOP","imm",2,2),
    0x89: Op("NOP","imm",2,2), 0xC2: Op("NOP","imm",2,2),
    0xE2: Op("NOP","imm",2,2),
    0x04: Op("NOP","zp",2,3),  0x44: Op("NOP","zp",2,3),
    0x64: Op("NOP","zp",2,3),
    0x0C: Op("NOP","abs",3,4),
    0x14: Op("NOP","zpx",2,4), 0x34: Op("NOP","zpx",2,4),
    0x54: Op("NOP","zpx",2,4), 0x74: Op("NOP","zpx",2,4),
    0xD4: Op("NOP","zpx",2,4), 0xF4: Op("NOP","zpx",2,4),
    0x1C: Op("NOP","abx",3,4), 0x3C: Op("NOP","abx",3,4),
    0x5C: Op("NOP","abx",3,4), 0x7C: Op("NOP","abx",3,4),
    0xDC: Op("NOP","abx",3,4), 0xFC: Op("NOP","abx",3,4),
    # LAX, SAX (common undocumented)
    0xA7: Op("LAX","zp",2,3),  0xB7: Op("LAX","zpy",2,4),
    0xAF: Op("LAX","abs",3,4), 0xBF: Op("LAX","aby",3,4),
    0xA3: Op("LAX","izx",2,6), 0xB3: Op("LAX","izy",2,5),
    0x87: Op("SAX","zp",2,3),  0x97: Op("SAX","zpy",2,4),
    0x8F: Op("SAX","abs",3,4), 0x83: Op("SAX","izx",2,6),
}

TERMINATORS = {"RTS", "RTI", "JMP", "BRK"}
BRANCH_OPS  = {"BCC","BCS","BEQ","BNE","BMI","BPL","BVC","BVS"}

# ---------------------------------------------------------------------------
# CPU state & C emission helpers
# ---------------------------------------------------------------------------

def addr_expr(op: Op, operand: int) -> str:
    """Return C expression that evaluates to the effective address."""
    m = op.mode
    if m == "zp":   return f"0x{operand:02X}"
    if m == "zpx":  return f"(uint8_t)(0x{operand:02X} + cpu.X)"
    if m == "zpy":  return f"(uint8_t)(0x{operand:02X} + cpu.Y)"
    if m == "abs":  return f"0x{operand:04X}"
    if m == "abx":  return f"(uint16_t)(0x{operand:04X} + cpu.X)"
    if m == "aby":  return f"(uint16_t)(0x{operand:04X} + cpu.Y)"
    if m == "izx":  return f"izx_addr(0x{operand:02X})"
    if m == "izy":  return f"izy_addr(0x{operand:02X})"
    if m == "ind":  return f"ind_addr(0x{operand:04X})"
    return ""

def emit_read(op: Op, operand: int) -> str:
    m = op.mode
    if m == "imm": return f"0x{operand:02X}"
    return f"mem_read({addr_expr(op, operand)})"

def emit_write(op: Op, operand: int, val: str) -> str:
    return f"mem_write({addr_expr(op, operand)}, {val})"

def branch_target(pc: int, operand: int) -> int:
    offset = operand if operand < 0x80 else operand - 0x100
    return (pc + 2 + offset) & 0xFFFF

# ---------------------------------------------------------------------------
# 6502 instruction → C statement(s)
# ---------------------------------------------------------------------------

def emit_instruction(op: Op, operand: int, pc: int, labels: Set[int]) -> List[str]:
    mn = op.mnemonic
    m  = op.mode
    lines: List[str] = []

    # Helper shortcuts
    def rd(): return emit_read(op, operand)
    def wr(v): return emit_write(op, operand, v)
    def ae(): return addr_expr(op, operand)

    if mn == "LDA":
        lines += [f"cpu.A = {rd()};", "SET_NZ(cpu.A);"]
    elif mn == "LDX":
        lines += [f"cpu.X = {rd()};", "SET_NZ(cpu.X);"]
    elif mn == "LDY":
        lines += [f"cpu.Y = {rd()};", "SET_NZ(cpu.Y);"]
    elif mn == "STA":
        lines += [f"{wr('cpu.A')};"]
    elif mn == "STX":
        lines += [f"{wr('cpu.X')};"]
    elif mn == "STY":
        lines += [f"{wr('cpu.Y')};"]
    elif mn == "LAX":
        lines += [f"cpu.A = cpu.X = {rd()};", "SET_NZ(cpu.A);"]
    elif mn == "SAX":
        lines += [f"{wr('(uint8_t)(cpu.A & cpu.X)')};"]
    elif mn == "TAX": lines += ["cpu.X = cpu.A;", "SET_NZ(cpu.X);"]
    elif mn == "TXA": lines += ["cpu.A = cpu.X;", "SET_NZ(cpu.A);"]
    elif mn == "TAY": lines += ["cpu.Y = cpu.A;", "SET_NZ(cpu.Y);"]
    elif mn == "TYA": lines += ["cpu.A = cpu.Y;", "SET_NZ(cpu.A);"]
    elif mn == "TSX": lines += ["cpu.X = cpu.SP;", "SET_NZ(cpu.X);"]
    elif mn == "TXS": lines += ["cpu.SP = cpu.X;"]
    elif mn == "PHA": lines += ["stack_push(cpu.A);"]
    elif mn == "PLA": lines += ["cpu.A = stack_pop();", "SET_NZ(cpu.A);"]
    elif mn == "PHP": lines += ["stack_push(get_P() | 0x30);"]
    elif mn == "PLP": lines += ["set_P(stack_pop());"]
    elif mn == "ADC":
        lines += [
            f"{{ uint16_t r = cpu.A + {rd()} + cpu.C;",
            "  cpu.V = (~(cpu.A ^ " + rd() + ") & (cpu.A ^ r) & 0x80) >> 7;" if m=="imm"
                else "  { uint8_t _v=" + rd() + "; cpu.V = (~(cpu.A ^ _v) & (cpu.A ^ r) & 0x80) >> 7; }",
            "  cpu.C = (r > 0xFF) ? 1 : 0;",
            "  cpu.A = (uint8_t)r;",
            "  SET_NZ(cpu.A); }",
        ]
    elif mn == "SBC":
        lines += [
            f"{{ uint16_t r = cpu.A - {rd()} - (1 - cpu.C);",
            "  cpu.V = ((cpu.A ^ " + rd() + ") & (cpu.A ^ r) & 0x80) >> 7;" if m=="imm"
                else "  { uint8_t _v=" + rd() + "; cpu.V = ((cpu.A ^ _v) & (cpu.A ^ r) & 0x80) >> 7; }",
            "  cpu.C = (r < 0x100) ? 1 : 0;",
            "  cpu.A = (uint8_t)r;",
            "  SET_NZ(cpu.A); }",
        ]
    elif mn == "AND":
        lines += [f"cpu.A &= {rd()};", "SET_NZ(cpu.A);"]
    elif mn == "ORA":
        lines += [f"cpu.A |= {rd()};", "SET_NZ(cpu.A);"]
    elif mn == "EOR":
        lines += [f"cpu.A ^= {rd()};", "SET_NZ(cpu.A);"]
    elif mn == "BIT":
        lines += [
            f"{{ uint8_t _t = {rd()};",
            "  cpu.N = (_t >> 7) & 1;",
            "  cpu.V = (_t >> 6) & 1;",
            "  cpu.Z = (cpu.A & _t) ? 0 : 1; }",
        ]
    elif mn == "CMP":
        lines += [f"{{ uint8_t _t = {rd()}; cpu.C = (cpu.A >= _t) ? 1:0; SET_NZ((uint8_t)(cpu.A - _t)); }}"]
    elif mn == "CPX":
        lines += [f"{{ uint8_t _t = {rd()}; cpu.C = (cpu.X >= _t) ? 1:0; SET_NZ((uint8_t)(cpu.X - _t)); }}"]
    elif mn == "CPY":
        lines += [f"{{ uint8_t _t = {rd()}; cpu.C = (cpu.Y >= _t) ? 1:0; SET_NZ((uint8_t)(cpu.Y - _t)); }}"]
    elif mn in ("ASL","LSR","ROL","ROR"):
        if m == "imp":  # accumulator
            if mn == "ASL": lines += ["cpu.C = (cpu.A >> 7) & 1;", "cpu.A = (uint8_t)(cpu.A << 1);", "SET_NZ(cpu.A);"]
            elif mn == "LSR": lines += ["cpu.C = cpu.A & 1;", "cpu.A >>= 1;", "SET_NZ(cpu.A);"]
            elif mn == "ROL": lines += ["{ uint8_t _c = cpu.C; cpu.C = (cpu.A >> 7) & 1; cpu.A = (uint8_t)((cpu.A << 1) | _c); SET_NZ(cpu.A); }"]
            elif mn == "ROR": lines += ["{ uint8_t _c = cpu.C; cpu.C = cpu.A & 1; cpu.A = (uint8_t)((cpu.A >> 1) | (_c << 7)); SET_NZ(cpu.A); }"]
        else:
            a = ae()
            if mn == "ASL": lines += [f"{{ uint8_t _t = mem_read({a}); cpu.C = (_t >> 7) & 1; _t = (uint8_t)(_t << 1); mem_write({a}, _t); SET_NZ(_t); }}"]
            elif mn == "LSR": lines += [f"{{ uint8_t _t = mem_read({a}); cpu.C = _t & 1; _t >>= 1; mem_write({a}, _t); SET_NZ(_t); }}"]
            elif mn == "ROL": lines += [f"{{ uint8_t _t = mem_read({a}), _c = cpu.C; cpu.C = (_t >> 7) & 1; _t = (uint8_t)((_t << 1) | _c); mem_write({a}, _t); SET_NZ(_t); }}"]
            elif mn == "ROR": lines += [f"{{ uint8_t _t = mem_read({a}), _c = cpu.C; cpu.C = _t & 1; _t = (uint8_t)((_t >> 1) | (_c << 7)); mem_write({a}, _t); SET_NZ(_t); }}"]
    elif mn == "INC":
        a = ae()
        lines += [f"{{ uint8_t _t = mem_read({a}) + 1; mem_write({a}, _t); SET_NZ(_t); }}"]
    elif mn == "DEC":
        a = ae()
        lines += [f"{{ uint8_t _t = mem_read({a}) - 1; mem_write({a}, _t); SET_NZ(_t); }}"]
    elif mn == "INX": lines += ["cpu.X++; SET_NZ(cpu.X);"]
    elif mn == "DEX": lines += ["cpu.X--; SET_NZ(cpu.X);"]
    elif mn == "INY": lines += ["cpu.Y++; SET_NZ(cpu.Y);"]
    elif mn == "DEY": lines += ["cpu.Y--; SET_NZ(cpu.Y);"]
    elif mn == "CLC": lines += ["cpu.C = 0;"]
    elif mn == "SEC": lines += ["cpu.C = 1;"]
    elif mn == "CLI": lines += ["cpu.I = 0;"]
    elif mn == "SEI": lines += ["cpu.I = 1;"]
    elif mn == "CLD": lines += ["cpu.D = 0;"]
    elif mn == "SED": lines += ["cpu.D = 1;"]
    elif mn == "CLV": lines += ["cpu.V = 0;"]
    elif mn == "NOP": lines += ["/* NOP */"]
    elif mn == "BRK": lines += ["cpu_brk();"]
    elif mn == "JSR":
        tgt = operand & 0xFFFF
        lines += [f"func_{tgt:04X}();"]
    elif mn == "JMP":
        if m == "abs":
            tgt = operand & 0xFFFF
            if tgt in labels:
                lines += [f"goto lbl_{tgt:04X};"]
            else:
                lines += [f"func_{tgt:04X}(); return;"]
        else:  # indirect — runtime dispatch
            lines += [f"call_by_address(ind_addr(0x{operand:04X}));", "return;"]
    elif mn == "RTS":
        lines += ["return;"]
    elif mn == "RTI":
        lines += ["set_P(stack_pop());", "return; /* RTI */"]
    elif mn in BRANCH_OPS:
        tgt = branch_target(pc, operand)
        cond = {
            "BCC": "!cpu.C", "BCS": "cpu.C",
            "BEQ": "cpu.Z",  "BNE": "!cpu.Z",
            "BMI": "cpu.N",  "BPL": "!cpu.N",
            "BVC": "!cpu.V", "BVS": "cpu.V",
        }[mn]
        if tgt in labels:
            lines += [f"if ({cond}) goto lbl_{tgt:04X};"]
        else:
            # branch crosses function boundary (rare but possible)
            lines += [f"if ({cond}) {{ func_{tgt:04X}(); return; }}"]
    else:
        lines += [f"/* UNHANDLED {mn} */"]
    return lines

# ---------------------------------------------------------------------------
# Function discovery (BFS)
# ---------------------------------------------------------------------------

class Disassembler:
    def __init__(self, prg: bytes, mapper: int, prg_banks: int):
        self.prg = prg
        self.mapper = mapper
        self.prg_banks = prg_banks
        # For simple mappers: last 16KB always at $C000, first 16KB at $8000 (or same bank if 1 bank)
        self.functions: Dict[int, List[Tuple[int, Op, int]]] = {}  # pc -> [(pc,op,operand),...]
        self.extra_funcs: Set[int] = set()
        self.data_regions: Set[range] = set()

    def prg_read(self, addr: int) -> int:
        """Simplified mapper 0/1/4 read at CPU address."""
        if addr < 0x8000:
            return 0xFF
        offset = addr - 0x8000
        # If 1 bank (16KB), mirror
        if self.prg_banks == 1:
            offset = offset % 0x4000
        elif offset >= len(self.prg):
            offset = offset % len(self.prg)
        return self.prg[offset] if offset < len(self.prg) else 0xFF

    def read_vector(self, addr: int) -> int:
        lo = self.prg_read(addr)
        hi = self.prg_read(addr + 1)
        return lo | (hi << 8)

    def decode_at(self, pc: int) -> Optional[Tuple[Op, int]]:
        opcode = self.prg_read(pc)
        op = OPTABLE.get(opcode)
        if op is None:
            return None
        operand = 0
        if op.size >= 2:
            operand = self.prg_read(pc + 1)
        if op.size >= 3:
            operand |= self.prg_read(pc + 2) << 8
        return op, operand

    def discover(self, seeds: List[int]):
        visited: Set[int] = set()
        queue: List[int] = list(seeds)

        while queue:
            entry = queue.pop(0)
            if entry in visited or entry < 0x8000:
                continue
            visited.add(entry)

            insns: List[Tuple[int, Op, int]] = []
            pc = entry
            while True:
                dec = self.decode_at(pc)
                if dec is None:
                    break
                op, operand = dec
                insns.append((pc, op, operand))

                # Follow JSR/JMP abs
                if op.mnemonic == "JSR":
                    tgt = operand & 0xFFFF
                    if tgt not in visited:
                        queue.append(tgt)
                elif op.mnemonic == "JMP" and op.mode == "abs":
                    tgt = operand & 0xFFFF
                    # If target is within same function body → not a new func
                    if tgt not in visited:
                        queue.append(tgt)
                elif op.mnemonic in BRANCH_OPS:
                    tgt = branch_target(pc, operand)
                    if tgt not in visited:
                        queue.append(tgt)

                if op.mnemonic in TERMINATORS:
                    break
                pc += op.size

            if insns:
                self.functions[entry] = insns

        # Extra functions from cfg
        for ea in self.extra_funcs:
            if ea not in self.functions:
                queue = [ea]
                self.discover(queue)

# ---------------------------------------------------------------------------
# C Code emitter
# ---------------------------------------------------------------------------

class CEmitter:
    def __init__(self, dis: Disassembler, cfg: dict):
        self.dis = dis
        self.cfg = cfg

    def emit_full(self, out_dir: str, game: str):
        os.makedirs(out_dir, exist_ok=True)
        full_path     = os.path.join(out_dir, f"{game}_full.c")
        dispatch_path = os.path.join(out_dir, f"{game}_dispatch.c")
        self._emit_full_c(full_path, game)
        self._emit_dispatch_c(dispatch_path, game)
        print(f"[nesrecomp] Wrote {full_path}")
        print(f"[nesrecomp] Wrote {dispatch_path}")

    def _collect_labels(self, entry: int) -> Set[int]:
        """All addresses targeted by branches inside this function."""
        insns = self.dis.functions.get(entry, [])
        labels: Set[int] = set()
        for pc, op, operand in insns:
            if op.mnemonic in BRANCH_OPS:
                labels.add(branch_target(pc, operand))
            elif op.mnemonic == "JMP" and op.mode == "abs":
                labels.add(operand & 0xFFFF)
        return labels

    def _emit_full_c(self, path: str, game: str):
        lines = []
        lines.append(f'/* Auto-generated by nesrecomp — {game} */')
        lines.append('#include "runner.h"')
        lines.append('')

        # Forward declarations (non-static so dispatch.c can reference them)
        lines.append('/* --- Forward declarations --- */')
        for entry in sorted(self.dis.functions):
            lines.append(f'void func_{entry:04X}(void);')
        lines.append('')

        # Function bodies
        for entry in sorted(self.dis.functions):
            insns  = self.dis.functions[entry]
            labels = self._collect_labels(entry)
            # Labels that are branch targets within this function
            internal = labels & {pc for pc, _, _ in insns}

            lines.append(f'void func_{entry:04X}(void) {{')
            for pc, op, operand in insns:
                if pc in internal:
                    lines.append(f'  lbl_{pc:04X}:;')
                comment = f'  /* ${pc:04X}  {op.mnemonic} */'
                stmts = emit_instruction(op, operand, pc, internal)
                if stmts:
                    lines.append(comment)
                    for s in stmts:
                        lines.append(f'  {s}')
            lines.append('}')
            lines.append('')

        with open(path, 'w') as f:
            f.write('\n'.join(lines))

    def _emit_dispatch_c(self, path: str, game: str):
        known = set(self.dis.functions.keys())
        reset = self.dis.read_vector(0xFFFC)
        nmi   = self.dis.read_vector(0xFFFA)
        irq   = self.dis.read_vector(0xFFFE)

        lines = []
        lines.append(f'/* Auto-generated dispatch table — {game} */')
        lines.append('#include "runner.h"')
        lines.append('#include <stdint.h>')
        lines.append('')
        # Non-static forward decls — functions are defined in _full.c
        for entry in sorted(known):
            lines.append(f'void func_{entry:04X}(void);')
        lines.append('')
        lines.append('void call_by_address(uint16_t addr) {')
        lines.append('  switch (addr) {')
        for entry in sorted(known):
            lines.append(f'    case 0x{entry:04X}: func_{entry:04X}(); return;')
        lines.append('    default:')
        lines.append('      fprintf(stderr, "[nesrecomp] Unknown dispatch $%04X\\n", addr);')
        lines.append('      break;')
        lines.append('  }')
        lines.append('}')
        lines.append('')
        # Entry points — guard against undiscovered vectors
        def body(addr):
            if addr in known:
                return f'func_{addr:04X}();' 
            return f'fprintf(stderr, "[nesrecomp] vector ${addr:04X} not found\\n");' 
        # nes_reset/nmi/irq are defined in runner.c
        # We only expose the generated entry points as callable symbols
        lines.append(f'/* RESET=${reset:04X}  NMI=${nmi:04X}  IRQ=${irq:04X} */')
        lines.append(f'void nes_entry_reset(void) {{ {body(reset)} }}')
        lines.append(f'void nes_entry_nmi(void)   {{ {body(nmi)} }}')
        lines.append(f'void nes_entry_irq(void)   {{ {body(irq)} }}')

        with open(path, 'w') as f:
            f.write('\n'.join(lines) + '\n')
# ---------------------------------------------------------------------------
# Config parser (.cfg)
# ---------------------------------------------------------------------------

def parse_cfg(path: str) -> dict:
    cfg = {"extra_func": [], "data_region": []}
    if not os.path.exists(path):
        return cfg
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                k, v = line.split('=', 1)
                k, v = k.strip(), v.strip()
                if k == "extra_func":
                    cfg["extra_func"].append(int(v, 16))
                elif k == "data_region":
                    parts = v.split(',')
                    cfg["data_region"].append((int(parts[0],16), int(parts[1],16)))
    return cfg

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="NES Static Recompiler")
    ap.add_argument("rom",  help=".nes ROM file")
    ap.add_argument("--cfg", default=None, help="Config file (optional)")
    ap.add_argument("--out", default="generated", help="Output directory")
    ap.add_argument("--game", default=None, help="Game name prefix (default: rom stem)")
    args = ap.parse_args()

    with open(args.rom, "rb") as f:
        data = f.read()

    hdr, prg, chr_ = parse_ines(data)
    game = args.game or os.path.splitext(os.path.basename(args.rom))[0]
    game = game.replace(' ', '_').replace('-', '_')

    print(f"[nesrecomp] ROM: {args.rom}")
    print(f"[nesrecomp] Mapper {hdr.mapper}, PRG={hdr.prg_banks}x16KB, CHR={hdr.chr_banks}x8KB")

    cfg = parse_cfg(args.cfg) if args.cfg else {}

    dis = Disassembler(prg, hdr.mapper, hdr.prg_banks)
    for ea in cfg.get("extra_func", []):
        dis.extra_funcs.add(ea)

    # Entry vectors
    reset = dis.read_vector(0xFFFC)
    nmi   = dis.read_vector(0xFFFA)
    irq   = dis.read_vector(0xFFFE)
    print(f"[nesrecomp] RESET=${reset:04X}  NMI=${nmi:04X}  IRQ=${irq:04X}")

    seeds = [reset, nmi, irq]
    seeds += cfg.get("extra_func", [])
    dis.discover(seeds)

    print(f"[nesrecomp] Discovered {len(dis.functions)} functions")

    emitter = CEmitter(dis, cfg)
    emitter.emit_full(args.out, game)
    print("[nesrecomp] Done.")

if __name__ == "__main__":
    main()
