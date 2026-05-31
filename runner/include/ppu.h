#ifndef PPU_H
#define PPU_H

/*
 * ppu.h — PPU 2C02: рендеринг, scroll (Loopy), спрайты, VBlank/NMI
 *
 * Фокус: только визуальная часть NES.
 * Если что-то отображается неправильно — смотри ppu.c.
 * Этот файл НЕ знает про APU и CPU напрямую.
 *
 * Ключевые моменты:
 *   - ppu_step() = 1 PPU dot (pixel clock)
 *   - ppu_run(n) = n dot за раз (вызывается из runner.c)
 *   - NMI: ppu.c вызывает nes_nmi() когда scanline=241, dot=1
 *   - mapper_scanline() вызывается в конце каждого scanline (для MMC3 IRQ)
 */

#include <stdint.h>

#define SCREEN_W  256
#define SCREEN_H  240
#define OAM_SIZE  256

typedef struct {
    /* Регистры $2000-$2007 */
    uint8_t  regs[8];

    /* OAM (Object Attribute Memory) — 64 спрайта × 4 байта */
    uint8_t  oam[OAM_SIZE];

    /* VRAM: nametables ($2000-$2FFF) + палитры ($3F00-$3FFF) */
    uint8_t  vram[0x4000];

    /* CHR: паттерн-таблицы из ROM/RAM (до 512KB) */
    uint8_t  chr[0x80000];

    /* Внутренние регистры Loopy (scroll) */
    uint16_t t_addr;      /* временный адрес (scroll target) */
    uint16_t v_addr;      /* текущий VRAM адрес */
    uint8_t  fine_x;      /* горизонтальное fine-scroll (0-7) */
    uint8_t  write_toggle; /* переключатель первого/второго байта */

    /* Буфер чтения $2007 (delayed read) */
    uint8_t  data_buf;

    /* Счётчики */
    uint32_t cycle;      /* текущий dot (0-340) */
    int      scanline;   /* текущий scanline (0-261) */

    /* Флаги состояния */
    uint8_t  frame_ready;     /* кадр готов к отображению */
    uint8_t  nmi_suppressed;  /* $2002 прочитан на том же dot что VBL */
    uint8_t  open_bus;        /* последнее записанное значение (bus latch) */

    /* Shifters фонового рендеринга (16-битные для плавного сдвига) */
    uint16_t bg_lo,     bg_hi;      /* паттерн (биты 0 и 1 цвета) */
    uint16_t bg_pal_lo, bg_pal_hi;  /* палитра (биты 2 и 3 цвета) */

    /* Спрайты текущего scanline */
    uint8_t  sprite_count;           /* кол-во найденных спрайтов (0-8) */
    uint8_t  sp_pattern_lo[8];       /* паттерн lo */
    uint8_t  sp_pattern_hi[8];       /* паттерн hi */
    uint8_t  sp_attr[8];             /* атрибуты (палитра, приоритет, flip) */
    uint8_t  sp_x[8];                /* X-координаты */
    uint8_t  sp_zero_on_line;        /* спрайт 0 на этой строке? */

    /* Framebuffer — ARGB8888, 256×240 */
    uint32_t framebuf[SCREEN_W * SCREEN_H];

    /* Неиспользуемый latch (совместимость) */
    uint8_t  fine_x_latch;
} PPU;

extern PPU ppu;

/* Чтение/запись PPU регистров (вызывается из memory.c) */
uint8_t ppu_read(uint8_t reg);
void    ppu_write(uint8_t reg, uint8_t val);

/* Тактирование */
void ppu_step(void);          /* 1 PPU dot */
void ppu_run(int clocks);     /* N PPU dots */

#endif /* PPU_H */
