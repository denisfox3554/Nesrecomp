#include "runner.h"

/* =========================================================================
   Global state
   ========================================================================= */
CPU      cpu;
uint8_t  ram[RAM_SIZE];
uint8_t  sram[SRAM_SIZE];
uint8_t  prg_rom[PRG_ROM_MAX];
uint32_t prg_rom_size = 0;

uint8_t controller[2]  = {0, 0};
uint8_t ctrl_shift[2]  = {0, 0};
static uint8_t ctrl_strobe = 0;

/* =========================================================================
   Controller — NES standard shift-register protocol
   
   Write $4016 bit0=1: strobe ON  → shift always mirrors live buttons
   Write $4016 bit0=0: strobe OFF → latch current state, begin serial read
   Read  $4016/$4017:  return next bit (A B Sel St Up Dn L R), MSB first
   ========================================================================= */
void ctrl_write(uint8_t val) {
    uint8_t new_strobe = val & 1;
    /* Falling edge (1→0): latch current button state */
    if (ctrl_strobe && !new_strobe) {
        ctrl_shift[0] = controller[0];
        ctrl_shift[1] = controller[1];
    }
    ctrl_strobe = new_strobe;
    /* While strobe is high, shift mirrors live state continuously */
    if (ctrl_strobe) {
        ctrl_shift[0] = controller[0];
        ctrl_shift[1] = controller[1];
    }
}

uint8_t ctrl_read(int port) {
    if (ctrl_strobe) {
        /* Strobe high: always return A button (bit 7) */
        return (controller[port] >> 7) & 1;
    }
    /* Shift out next bit, MSB first */
    uint8_t bit = (ctrl_shift[port] >> 7) & 1;
    ctrl_shift[port] <<= 1;
    ctrl_shift[port] |= 1; /* bus returns 1 after all 8 bits shifted out */
    return bit;
}

/* =========================================================================
   Memory read
   ========================================================================= */
uint8_t mem_read(uint16_t addr) {
    /* RAM $0000-$1FFF (mirrored every 2KB) */
    if (addr < 0x2000) return ram[addr & 0x07FF];
    /* PPU registers $2000-$3FFF (mirrored every 8 bytes) */
    if (addr < 0x4000) return ppu_read((uint8_t)(addr & 7));
    /* APU / IO $4000-$4017 */
    if (addr == 0x4015) return apu_read_status();
    if (addr == 0x4016) return ctrl_read(0);
    if (addr == 0x4017) return ctrl_read(1);
    if (addr < 0x4020) return 0xFF; /* open bus */
    /* SRAM $6000-$7FFF */
    if (addr >= 0x6000 && addr < 0x8000) return sram[addr - 0x6000];
    /* PRG-ROM $8000-$FFFF */
    if (addr >= 0x8000) return mapper_prg_read(addr);
    return 0xFF;
}

/* =========================================================================
   Memory write
   ========================================================================= */
void mem_write(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) { ram[addr & 0x07FF] = val; return; }
    if (addr < 0x4000) { ppu_write((uint8_t)(addr & 7), val); return; }

    /* OAM DMA $4014 */
    if (addr == 0x4014) {
        uint16_t base = (uint16_t)val << 8;
        for (int i = 0; i < 256; i++)
            ppu.oam[i] = mem_read(base + i);
        /* DMA costs 513/514 CPU cycles — approximated here */
        return;
    }
    if (addr == 0x4016) { ctrl_write(val); return; }
    if (addr >= 0x4000 && addr <= 0x4017) { apu_write(addr, val); return; }
    if (addr >= 0x6000 && addr < 0x8000) { sram[addr - 0x6000] = val; return; }
    if (addr >= 0x8000) { mapper_prg_write(addr, val); return; }
}

/* =========================================================================
   BRK
   ========================================================================= */
void cpu_brk(void) {
    uint16_t ret = cpu.PC + 2;
    stack_push((ret >> 8) & 0xFF);
    stack_push(ret & 0xFF);
    stack_push(get_P() | 0x30);
    cpu.I = 1;
    cpu.PC = mem_read(0xFFFE) | ((uint16_t)mem_read(0xFFFF) << 8);
}
