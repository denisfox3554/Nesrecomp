#include "runner.h"

PPU ppu;

/* =========================================================================
   Palette — точная 2C02 NTSC палитра (ARGB8888)
   ========================================================================= */
static const uint32_t PALETTE[64] = {
    0xFF626262, 0xFF002CA8, 0xFF1212C8, 0xFF5200B0,
    0xFF7C007C, 0xFF880020, 0xFF780000, 0xFF5C1000,
    0xFF302800, 0xFF004400, 0xFF005000, 0xFF004C14,
    0xFF003C5C, 0xFF000000, 0xFF000000, 0xFF000000,

    0xFFABABAB, 0xFF1060FC, 0xFF4040FC, 0xFF8000FC,
    0xFFBC00CC, 0xFFD40060, 0xFFD00010, 0xFFA42000,
    0xFF6C4400, 0xFF226400, 0xFF007400, 0xFF006C30,
    0xFF006090, 0xFF000000, 0xFF000000, 0xFF000000,

    0xFFFFFFFF, 0xFF60B4FC, 0xFF8490FC, 0xFFCC78FC,
    0xFFF460FC, 0xFFFC60B0, 0xFFFC6054, 0xFFF07828,
    0xFFCC9C00, 0xFF78BC00, 0xFF40CC00, 0xFF2CC840,
    0xFF2CC8A4, 0xFF444444, 0xFF000000, 0xFF000000,

    0xFFFFFFFF, 0xFFB8E4FC, 0xFFCCD8FC, 0xFFE8C8FC,
    0xFFFCC4FC, 0xFFFCC4D8, 0xFFFCBCB0, 0xFFF0D0A0,
    0xFFE4E090, 0xFFC8F08C, 0xFFAEF4AC, 0xFFA8F0CC,
    0xFFA8EEF0, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

/* =========================================================================
   VRAM / nametable mirroring
   ========================================================================= */
static uint16_t mirror_nt(uint16_t addr) {
    addr &= 0x0FFF;
    switch (mapper.mirroring) {
    case 0: /* horizontal: $2000=$2400, $2800=$2C00 */
        return (addr & 0x03FF) | ((addr >= 0x0800) ? 0x0400 : 0);
    case 1: /* vertical: $2000=$2800, $2400=$2C00 */
        return addr & 0x07FF;
    case 2: return addr & 0x0FFF;      /* 4-screen */
    case 3: return addr & 0x03FF;      /* single lower */
    case 4: return 0x0400|(addr&0x03FF); /* single upper */
    }
    return addr & 0x03FF;
}

static uint8_t vram_read(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) return mapper_chr_read(addr);
    if (addr < 0x3F00) return ppu.vram[mirror_nt(addr - 0x2000)];
    addr &= 0x1F;
    if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C)
        addr &= 0x0F;
    return ppu.vram[0x3F00 | addr] & 0x3F;
}

static void vram_write(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;
    if (addr < 0x2000) { mapper_chr_write(addr, val); return; }
    if (addr < 0x3F00) { ppu.vram[mirror_nt(addr - 0x2000)] = val; return; }
    addr &= 0x1F;
    if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C)
        addr &= 0x0F;
    ppu.vram[0x3F00 | addr] = val;
}

/* =========================================================================
   PPU register read/write
   ========================================================================= */
uint8_t ppu_read(uint8_t reg) {
    switch (reg & 7) {
    case 2: { /* PPUSTATUS */
        uint8_t s = (ppu.regs[2] & 0xE0) | (ppu.open_bus & 0x1F);
        /* FIX: чтение $2002 подавляет NMI если оно ещё не было отправлено */
        ppu.nmi_suppressed = 1;
        ppu.regs[2] &= ~0x80;  /* clear VBL flag */
        ppu.write_toggle = 0;
        return s;
    }
    case 4: return ppu.oam[ppu.regs[3]]; /* OAMDATA */
    case 7: { /* PPUDATA */
        uint16_t a = ppu.v_addr & 0x3FFF;
        uint8_t val;
        if (a < 0x3F00) {
            val = ppu.data_buf;
            ppu.data_buf = vram_read(a);
        } else {
            ppu.data_buf = vram_read(a & 0x2FFF); /* nametable behind palette */
            val = vram_read(a);
        }
        ppu.v_addr = (ppu.v_addr + ((ppu.regs[0] & 4) ? 32 : 1)) & 0x7FFF;
        return val;
    }
    }
    return ppu.open_bus;
}

void ppu_write(uint8_t reg, uint8_t val) {
    ppu.open_bus = val;
    ppu.regs[reg & 7] = val;
    switch (reg & 7) {
    case 0: /* PPUCTRL */
        ppu.t_addr = (ppu.t_addr & ~0x0C00) | ((uint16_t)(val & 3) << 10);
        /* FIX: если NMI_EN включился и VBL уже установлен — генерировать NMI */
        if ((val & 0x80) && (ppu.regs[2] & 0x80))
            nes_nmi();
        break;
    case 3: break; /* OAMADDR */
    case 4: /* OAMDATA */
        ppu.oam[ppu.regs[3]++] = val;
        break;
    case 5: /* PPUSCROLL */
        if (!ppu.write_toggle) {
            ppu.t_addr = (ppu.t_addr & ~0x001F) | (val >> 3);
            ppu.fine_x = val & 7;
        } else {
            ppu.t_addr = (ppu.t_addr & ~0x73E0)
                       | ((uint16_t)(val & 0x07) << 12)
                       | ((uint16_t)(val & 0xF8) << 2);
        }
        ppu.write_toggle ^= 1;
        break;
    case 6: /* PPUADDR */
        if (!ppu.write_toggle) {
            ppu.t_addr = (ppu.t_addr & 0x00FF) | ((uint16_t)(val & 0x3F) << 8);
        } else {
            ppu.t_addr = (ppu.t_addr & 0xFF00) | val;
            ppu.v_addr = ppu.t_addr;
        }
        ppu.write_toggle ^= 1;
        break;
    case 7: /* PPUDATA */
        vram_write(ppu.v_addr, val);
        ppu.v_addr = (ppu.v_addr + ((ppu.regs[0] & 4) ? 32 : 1)) & 0x7FFF;
        break;
    }
}

/* =========================================================================
   Loopy scroll helpers
   ========================================================================= */
static inline void inc_hori_v(void) {
    if ((ppu.v_addr & 0x001F) == 31) {
        ppu.v_addr &= ~0x001F;
        ppu.v_addr ^= 0x0400;
    } else {
        ppu.v_addr++;
    }
}

static inline void inc_vert_v(void) {
    if ((ppu.v_addr & 0x7000) != 0x7000) {
        ppu.v_addr += 0x1000;
    } else {
        ppu.v_addr &= ~0x7000;
        int y = (ppu.v_addr >> 5) & 31;
        if      (y == 29) { y = 0; ppu.v_addr ^= 0x0800; }
        else if (y == 31)   y = 0;
        else                y++;
        ppu.v_addr = (ppu.v_addr & ~0x03E0) | (y << 5);
    }
}

static inline void copy_hori_v(void) {
    ppu.v_addr = (ppu.v_addr & ~0x041F) | (ppu.t_addr & 0x041F);
}

static inline void copy_vert_v(void) {
    ppu.v_addr = (ppu.v_addr & ~0x7BE0) | (ppu.t_addr & 0x7BE0);
}

/* =========================================================================
   Background tile fetch
   Загружает тайл в НИЖНИЙ байт shifter'ов.
   Вызывается каждые 8 dot во время fetch-периодов.
   ========================================================================= */
static void fetch_bg_tile(void) {
    uint16_t nt_addr = 0x2000 | (ppu.v_addr & 0x0FFF);
    uint8_t  tile    = vram_read(nt_addr);

    uint16_t at_addr = 0x23C0
                     | (ppu.v_addr & 0x0C00)
                     | ((ppu.v_addr >> 4) & 0x38)
                     | ((ppu.v_addr >> 2) & 0x07);
    uint8_t  attr  = vram_read(at_addr);
    uint8_t  shift = ((ppu.v_addr >> 4) & 4) | (ppu.v_addr & 2);
    uint8_t  pal   = (attr >> shift) & 3;

    uint16_t pt_base = (ppu.regs[0] & 0x10) ? 0x1000 : 0x0000;
    uint8_t  fine_y  = (ppu.v_addr >> 12) & 7;
    uint8_t  lo      = vram_read(pt_base + (uint16_t)tile * 16 + fine_y);
    uint8_t  hi      = vram_read(pt_base + (uint16_t)tile * 16 + fine_y + 8);

    ppu.bg_lo     = (ppu.bg_lo     & 0xFF00) | lo;
    ppu.bg_hi     = (ppu.bg_hi     & 0xFF00) | hi;
    ppu.bg_pal_lo = (ppu.bg_pal_lo & 0xFF00) | ((pal & 1) ? 0xFF : 0x00);
    ppu.bg_pal_hi = (ppu.bg_pal_hi & 0xFF00) | ((pal & 2) ? 0xFF : 0x00);
}

/* =========================================================================
   Sprite evaluation — строим список до 8 спрайтов для следующего scanline
   FIX: сохраняем результат в sp_next_* и применяем на следующем scanline
   ========================================================================= */
static void eval_sprites(int scanline) {
    ppu.sprite_count    = 0;
    ppu.sp_zero_on_line = 0;
    uint8_t sprite_h = (ppu.regs[0] & 0x20) ? 16 : 8;

    for (int i = 0; i < 64; i++) {
        int y = (int)ppu.oam[i * 4] + 1;
        if (scanline < y || scanline >= y + sprite_h) continue;
        if (ppu.sprite_count == 8) { ppu.regs[2] |= 0x20; break; }
        if (i == 0) ppu.sp_zero_on_line = 1;

        uint8_t tile = ppu.oam[i*4+1];
        uint8_t attr = ppu.oam[i*4+2];
        uint8_t spx  = ppu.oam[i*4+3];
        int     row  = scanline - y;
        if (attr & 0x80) row = (sprite_h - 1) - row;

        uint16_t pt_base;
        uint8_t  t;
        if (sprite_h == 8) {
            pt_base = (ppu.regs[0] & 0x08) ? 0x1000 : 0x0000;
            t = tile;
        } else {
            pt_base = (tile & 1) ? 0x1000 : 0x0000;
            t = tile & 0xFE;
            if (row >= 8) { t++; row -= 8; }
        }

        uint8_t lo = vram_read(pt_base + (uint16_t)t * 16 + row);
        uint8_t hi = vram_read(pt_base + (uint16_t)t * 16 + row + 8);

        /* Horizontal flip */
        if (attr & 0x40) {
            lo = (uint8_t)(((lo * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32);
            hi = (uint8_t)(((hi * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32);
        }

        int idx = ppu.sprite_count++;
        ppu.sp_pattern_lo[idx] = lo;
        ppu.sp_pattern_hi[idx] = hi;
        ppu.sp_attr[idx]       = attr;
        ppu.sp_x[idx]          = spx;
    }
}

/* =========================================================================
   PPU step — один dot (pixel clock)
   ========================================================================= */
/* =========================================================================
   PPU step — один dot (pixel clock)
   ========================================================================= */
#define BG_EN   (ppu.regs[1] & 0x08)
#define SP_EN   (ppu.regs[1] & 0x10)
#define RENDER  (BG_EN || SP_EN)
#define NMI_EN  (ppu.regs[0] & 0x80)

void ppu_step(void) {
    int dot      = ppu.cycle;
    int scanline = ppu.scanline;

    /* ---- Visible scanlines 0–239 ---- */
    if (scanline < 240) {

        /* Pixel output: dots 1–256 */
        if (dot >= 1 && dot <= 256) {
            int x = dot - 1;

            /* BG pixel */
            uint8_t bg_pixel = 0, bg_pal = 0;
            if (BG_EN && (x >= 8 || (ppu.regs[1] & 0x02))) {
                uint16_t mux = 0x8000 >> ppu.fine_x;
                bg_pixel = ((ppu.bg_lo     & mux) ? 1 : 0)
                         | ((ppu.bg_hi     & mux) ? 2 : 0);
                bg_pal   = ((ppu.bg_pal_lo & mux) ? 1 : 0)
                         | ((ppu.bg_pal_hi & mux) ? 2 : 0);
            }

            /* Sprite pixel */
            uint8_t sp_pixel = 0, sp_pal = 0, sp_priority = 0;
            if (SP_EN && (x >= 8 || (ppu.regs[1] & 0x04))) {
                for (int i = 0; i < ppu.sprite_count; i++) {
                    int sx = x - (int)ppu.sp_x[i];
                    if (sx < 0 || sx > 7) continue;
                    uint8_t lo = (ppu.sp_pattern_lo[i] >> (7 - sx)) & 1;
                    uint8_t hi = (ppu.sp_pattern_hi[i] >> (7 - sx)) & 1;
                    uint8_t p  = lo | (hi << 1);
                    if (!p) continue;
                    /* Sprite-zero hit */
                    if (i == 0 && ppu.sp_zero_on_line && bg_pixel && x < 255)
                        ppu.regs[2] |= 0x40;
                    sp_pixel    = p;
                    sp_pal      = (ppu.sp_attr[i] & 3) + 4;
                    sp_priority = (ppu.sp_attr[i] >> 5) & 1;
                    break;
                }
            }

            /* Mux & palette lookup */
            uint8_t pal_addr;
            if      (!bg_pixel && !sp_pixel) pal_addr = 0;
            else if (!bg_pixel &&  sp_pixel) pal_addr = sp_pal  * 4 + sp_pixel;
            else if ( bg_pixel && !sp_pixel) pal_addr = bg_pal  * 4 + bg_pixel;
            else {
                pal_addr = sp_priority ? (bg_pal * 4 + bg_pixel)
                                       : (sp_pal * 4 + sp_pixel);
            }
            uint8_t color = vram_read(0x3F00 + pal_addr);
            ppu.framebuf[scanline * SCREEN_W + x] = PALETTE[color & 0x3F];

            /* Shift BG shifters */
            ppu.bg_lo     <<= 1; ppu.bg_hi     <<= 1;
            ppu.bg_pal_lo <<= 1; ppu.bg_pal_hi <<= 1;
        }

        /* BG tile fetching */
        if (RENDER) {
            if (dot >= 9 && dot <= 257 && (dot & 7) == 1) {
                fetch_bg_tile();
                if (dot < 257) inc_hori_v();
            }
            if (dot == 256) inc_vert_v();
            if (dot == 257) copy_hori_v();
            if (dot == 329) { fetch_bg_tile(); inc_hori_v(); }
            if (dot == 337) { fetch_bg_tile(); inc_hori_v(); }
        }

        /* Sprite evaluation */
        if (dot == 257)
            eval_sprites(scanline + 1 < 240 ? scanline + 1 : 0);
    }

    /* ---- Pre-render scanline 261 ---- */
    if (scanline == 261) {
        if (dot == 1) {
            ppu.regs[2] &= ~0xE0;
            ppu.nmi_suppressed = 0;
        }
        if (RENDER) {
            if (dot >= 280 && dot <= 304) copy_vert_v();
            if (dot == 256) inc_vert_v();
            if (dot == 257) copy_hori_v();
            if (dot == 329) { fetch_bg_tile(); inc_hori_v(); }
            if (dot == 337) { fetch_bg_tile(); inc_hori_v(); }
        }
        if (dot == 257) eval_sprites(0);
    }

    /* ---- VBlank: scanline 241, dot 1 ---- */
    if (scanline == 241 && dot == 1) {
        ppu.regs[2] |= 0x80;
        ppu.frame_ready = 1;
        ppu.nmi_suppressed = 0;
        if (NMI_EN && !ppu.nmi_suppressed)
            nes_nmi();
    }

    /* ---- Advance dot / scanline ---- */
    ppu.cycle++;
    if (ppu.cycle > 340) {
        ppu.cycle = 0;
        ppu.scanline++;
        
        /* MMC3 IRQ — при переходе на каждый новый scanline 0-239.
           Вызывается ПОСЛЕ инкремента, когда ppu.scanline = новое значение.
           IRQ срабатывает при рендеринге видимых строк (0-239). */
        if (RENDER && ppu.scanline <= 240) {
            mapper_scanline();
        }
        
        if (ppu.scanline > 261) ppu.scanline = 0;
    }
}

void ppu_run(int clocks) {
    for (int i = 0; i < clocks; i++) ppu_step();
}