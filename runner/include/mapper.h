#ifndef MAPPER_H
#define MAPPER_H

/*
 * mapper.h — маппер картриджа: PRG/CHR банкинг, IRQ
 *
 * Фокус: только переключение банков и IRQ от картриджа.
 * Если игра показывает неправильные тайлы или вылетает при скролле —
 * смотри mapper.c.
 *
 * Поддерживаемые mappers:
 *   0  NROM       — Donkey Kong, Super Mario Bros
 *   1  MMC1       — Metroid, Mega Man 2
 *   2  UNROM      — Castlevania, Mega Man
 *   3  CNROM      — Gradius
 *   4  MMC3       — Mega Man 4, Battletoads, Aa Yakyuu
 *   7  AxROM      — Battletoads (US)
 */

#include <stdint.h>

typedef struct {
    int     id;           /* номер маппера */
    uint8_t prg_banks;    /* кол-во PRG банков по 16KB */
    uint8_t chr_banks;    /* кол-во CHR банков по 8KB (0 = CHR-RAM) */
    uint8_t mirroring;    /* 0=H, 1=V, 2=4screen, 3=single0, 4=single1 */

    /* MMC1 (mapper 1) */
    uint8_t m1_shift;         /* сдвиговый регистр (init=0x10) */
    uint8_t m1_shift_count;
    uint8_t m1_ctrl;          /* управляющий регистр */
    uint8_t m1_prg_bank;
    uint8_t m1_chr_bank0;
    uint8_t m1_chr_bank1;

    /* MMC3 (mapper 4) */
    uint8_t m4_bank_select;   /* регистр выбора банка ($8000) */
    uint8_t m4_banks[8];      /* R0-R7: CHR[0-5], PRG[6-7] */
    uint8_t m4_irq_latch;
    uint8_t m4_irq_counter;
    uint8_t m4_irq_enable;
    uint8_t m4_irq_reload;
} Mapper;

extern Mapper mapper;

/* Инициализация при загрузке ROM */
void    mapper_init(int id, int prg_banks, int chr_banks, int mirroring);

/* PRG-ROM чтение/запись ($8000-$FFFF) */
uint8_t mapper_prg_read(uint16_t addr);
void    mapper_prg_write(uint16_t addr, uint8_t val);

/* CHR чтение/запись ($0000-$1FFF через PPU) */
uint8_t mapper_chr_read(uint16_t addr);
void    mapper_chr_write(uint16_t addr, uint8_t val);

/*
 * mapper_scanline() — вызывается из ppu.c в конце каждого scanline.
 * Нужен для MMC3 IRQ (счётчик scanline'ов для split-screen).
 */
void    mapper_scanline(void);

#endif /* MAPPER_H */
