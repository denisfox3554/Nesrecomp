#ifndef RUNNER_H
#define RUNNER_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
   CPU state
   ========================================================================= */
typedef struct {
    uint8_t  A, X, Y, SP;
    uint16_t PC;
    uint8_t  N, V, D, I, Z, C;
} CPU;

extern CPU cpu;

#define SET_NZ(v)  do { cpu.N = ((v) >> 7) & 1; cpu.Z = ((v) == 0) ? 1 : 0; } while(0)

static inline uint8_t get_P(void) {
    return (cpu.N << 7) | (cpu.V << 6) | 0x20 |
           (cpu.D << 3) | (cpu.I << 2) | (cpu.Z << 1) | cpu.C;
}
static inline void set_P(uint8_t p) {
    cpu.N = (p >> 7) & 1; cpu.V = (p >> 6) & 1;
    cpu.D = (p >> 3) & 1; cpu.I = (p >> 2) & 1;
    cpu.Z = (p >> 1) & 1; cpu.C =  p       & 1;
}

/* =========================================================================
   Memory map
   ========================================================================= */
#define RAM_SIZE    0x0800
#define SRAM_SIZE   0x2000
#define PRG_ROM_MAX 0x80000  /* 512KB — Aa Yakyuu = 32×16KB = 512KB */

extern uint8_t ram[RAM_SIZE];
extern uint8_t sram[SRAM_SIZE];
extern uint8_t prg_rom[PRG_ROM_MAX];
extern uint32_t prg_rom_size;

uint8_t  mem_read(uint16_t addr);
void     mem_write(uint16_t addr, uint8_t val);

static inline uint16_t izx_addr(uint8_t base) {
    uint8_t a = base + cpu.X;
    return mem_read(a) | ((uint16_t)mem_read((uint8_t)(a+1)) << 8);
}
static inline uint16_t izy_addr(uint8_t base) {
    uint16_t t = mem_read(base) | ((uint16_t)mem_read((uint8_t)(base+1)) << 8);
    return t + cpu.Y;
}
static inline uint16_t ind_addr(uint16_t base) {
    uint16_t hi_addr = (base & 0xFF00) | ((base + 1) & 0x00FF);
    return mem_read(base) | ((uint16_t)mem_read(hi_addr) << 8);
}

/* =========================================================================
   Stack
   ========================================================================= */
static inline void stack_push(uint8_t v) {
    mem_write(0x0100 | cpu.SP, v);
    cpu.SP--;
}
static inline uint8_t stack_pop(void) {
    cpu.SP++;
    return mem_read(0x0100 | cpu.SP);
}

/* =========================================================================
   Interrupt / dispatch
   ========================================================================= */
void call_by_address(uint16_t addr);
void nes_reset(void);
void nes_nmi(void);
void nes_irq(void);
void cpu_brk(void);

/* =========================================================================
   PPU
   ========================================================================= */
#define SCREEN_W  256
#define SCREEN_H  240
#define OAM_SIZE  256

typedef struct {
    uint8_t  regs[8];
    uint8_t  oam[OAM_SIZE];
    uint8_t  vram[0x4000];
    uint8_t  chr[0x80000];  /* 512KB for large CHR-ROM (Mega Man 4 = 256KB) */
    uint16_t vaddr;
    uint8_t  fine_x;
    uint8_t  write_toggle;
    uint8_t  data_buf;
    uint32_t cycle;
    int      scanline;
    uint8_t  frame_ready;
    uint8_t  nmi_suppressed;  /* для корректного NMI */
    uint32_t framebuf[SCREEN_W * SCREEN_H];

    uint16_t t_addr, v_addr;
    uint8_t  fine_x_latch;
    uint16_t bg_lo,     bg_hi;
    uint16_t bg_pal_lo, bg_pal_hi;
    uint8_t  sprite_count;
    uint8_t  sp_pattern_lo[8], sp_pattern_hi[8];
    uint8_t  sp_attr[8], sp_x[8];
    uint8_t  sp_zero_on_line;
    uint8_t  open_bus;
} PPU;

extern PPU ppu;

uint8_t  ppu_read(uint8_t reg);
void     ppu_write(uint8_t reg, uint8_t val);
void     ppu_step(void);
void     ppu_run(int clocks);

/* =========================================================================
   APU
   ========================================================================= */
typedef struct {
    struct {
        uint8_t  duty, length_halt, constant_vol, volume;
        uint16_t timer, timer_reload;
        uint8_t  length, enabled;
        uint8_t  sweep_en, sweep_period, sweep_negate, sweep_shift;
        uint8_t  sweep_reload, sweep_divider;
        uint8_t  envelope_start, envelope_divider, envelope_decay;
        uint32_t seq_pos;
    } pulse[2];
    struct {
        uint8_t  control, linear_reload_val;
        uint16_t timer, timer_reload;
        uint8_t  length, enabled;
        uint8_t  linear_counter, linear_reload;
        uint32_t seq_pos;
    } tri;
    struct {
        uint8_t  length_halt, constant_vol, volume;
        uint16_t timer, timer_reload;
        uint8_t  length, enabled, mode;
        uint16_t shift_reg;
        uint8_t  envelope_start, envelope_divider, envelope_decay;
    } noise;
    struct {
        uint8_t  irq_en, loop, rate_idx, enabled;
        uint8_t  output;
        uint16_t sample_addr, sample_len;
        uint16_t cur_addr, bytes_remaining;
        uint8_t  sample_buf, bits_remaining, silence;
    } dmc;
    uint8_t  frame_counter_mode, frame_irq_inhibit;
    uint32_t frame_clock;
    float    sample_buf[4096];
    int      sample_count;
} APU;

extern APU apu;

void    apu_write(uint16_t addr, uint8_t val);
uint8_t apu_read_status(void);
void    apu_step(void);
void    apu_fill_buffer(float *buf, int samples);

/* =========================================================================
   Mapper
   ========================================================================= */
typedef struct {
    int      id;
    uint8_t  prg_banks;
    uint8_t  chr_banks;
    uint8_t  mirroring;
    uint8_t  m1_shift, m1_shift_count;
    uint8_t  m1_ctrl, m1_prg_bank, m1_chr_bank0, m1_chr_bank1;
    uint8_t  m4_bank_select;
    uint8_t  m4_banks[8];
    uint8_t  m4_irq_latch, m4_irq_counter;
    uint8_t  m4_irq_enable, m4_irq_reload;
} Mapper;

extern Mapper mapper;

void    mapper_init(int id, int prg_banks, int chr_banks, int mirroring);
uint8_t mapper_prg_read(uint16_t addr);
void    mapper_prg_write(uint16_t addr, uint8_t val);
uint8_t mapper_chr_read(uint16_t addr);
void    mapper_chr_write(uint16_t addr, uint8_t val);
void    mapper_scanline(void);

/* =========================================================================
   Controller
   ========================================================================= */
extern uint8_t controller[2];
extern uint8_t ctrl_shift[2];
uint8_t ctrl_read(int port);
void    ctrl_write(uint8_t val);

/* =========================================================================
   Runner lifecycle
   ========================================================================= */
int  runner_init(const char *title, const char *rom_path);
void runner_run(void);
void runner_quit(void);

/* =========================================================================
   6502 Interpreter
   ========================================================================= */
int  cpu_interp_step(void);
void cpu_interp_run(uint16_t entry);

#endif