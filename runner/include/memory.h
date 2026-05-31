#ifndef MEMORY_H
#define MEMORY_H

/*
 * memory.h — карта памяти NES ($0000-$FFFF), контроллер
 *
 * Фокус: маршрутизация чтения/записи.
 * Если игра неправильно читает/пишет в I/O — смотри memory.c.
 * Контроллер тоже здесь (strobe-протокол).
 *
 * Диапазоны:
 *   $0000-$07FF  RAM (2KB, зеркало до $1FFF)
 *   $2000-$3FFF  PPU регистры (зеркало каждые 8 байт)
 *   $4000-$4017  APU / I/O
 *   $4014        OAM DMA
 *   $4016-$4017  Контроллеры
 *   $6000-$7FFF  SRAM (картридж)
 *   $8000-$FFFF  PRG-ROM (через mapper)
 */

#include <stdint.h>

#define RAM_SIZE    0x0800
#define SRAM_SIZE   0x2000
#define PRG_ROM_MAX 0x80000   /* 512KB — достаточно для mapper 4 (32 банка) */

extern uint8_t  ram[RAM_SIZE];
extern uint8_t  sram[SRAM_SIZE];
extern uint8_t  prg_rom[PRG_ROM_MAX];
extern uint32_t prg_rom_size;

/* Основные функции чтения/записи */
uint8_t mem_read(uint16_t addr);
void    mem_write(uint16_t addr, uint8_t val);

/* Стек — инлайн для скорости */
#include "cpu.h"
static inline void stack_push(uint8_t v) {
    mem_write(0x0100 | cpu.SP, v);
    cpu.SP--;
}
static inline uint8_t stack_pop(void) {
    cpu.SP++;
    return mem_read(0x0100 | cpu.SP);
}

/* Вспомогательные адресные вычисления для cpu_interp.c */
static inline uint16_t izx_addr(uint8_t base) {
    uint8_t a = base + cpu.X;
    return mem_read(a) | ((uint16_t)mem_read((uint8_t)(a + 1)) << 8);
}
static inline uint16_t izy_addr(uint8_t base) {
    uint16_t t = mem_read(base) | ((uint16_t)mem_read((uint8_t)(base + 1)) << 8);
    return t + cpu.Y;
}
static inline uint16_t ind_addr(uint16_t base) {
    /* JMP ($nnnn) — page-wrap bug оригинального 6502 */
    uint16_t hi = (base & 0xFF00) | ((base + 1) & 0x00FF);
    return mem_read(base) | ((uint16_t)mem_read(hi) << 8);
}

/* Контроллер */
extern uint8_t controller[2];   /* живое состояние кнопок */
extern uint8_t ctrl_shift[2];   /* сдвиговый регистр */
uint8_t ctrl_read(int port);
void    ctrl_write(uint8_t val);

#endif /* MEMORY_H */
