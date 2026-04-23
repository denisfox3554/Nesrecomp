#include "runner.h"

Mapper mapper;

void mapper_init(int id, int prg_banks, int chr_banks, int mirroring) {
    memset(&mapper, 0, sizeof(mapper));
    mapper.id        = id;
    mapper.prg_banks = prg_banks;
    mapper.chr_banks = chr_banks;
    
    /* Castlevania требует вертикальное зеркалирование */
    if (id == 2) {
        mapper.mirroring = 1;  /* Вертикальное */
    } else {
        mapper.mirroring = mirroring;
    }
    
    /* MMC1 defaults */
    if (id == 1) {
        mapper.m1_shift       = 0x10;
        mapper.m1_shift_count = 0;
        mapper.m1_ctrl        = 0x0C;
    }
    
    /* MMC3 defaults */
    if (id == 4) {
        mapper.m4_banks[6]    = prg_banks * 2 - 2;
        mapper.m4_banks[7]    = prg_banks * 2 - 1;
        mapper.m4_irq_counter = 0;
        mapper.m4_irq_latch   = 0;
        mapper.m4_irq_enable  = 0;
        mapper.m4_irq_reload  = 0;
    }
}

/* =========================================================================
   PRG-ROM read
   ========================================================================= */
uint8_t mapper_prg_read(uint16_t addr) {
    if (addr < 0x8000) return 0xFF;
    
    uint32_t offset;
    
    switch (mapper.id) {
    case 0: /* NROM */
        offset = addr - 0x8000;
        if (mapper.prg_banks == 1) offset &= 0x3FFF;
        return prg_rom[offset % prg_rom_size];
        
    case 1: { /* MMC1 */
            int prg_mode = (mapper.m1_ctrl >> 2) & 3;
            uint32_t bank = mapper.m1_prg_bank & 0x0F;
            uint32_t mmc1_off;
            if (prg_mode <= 1) {
                mmc1_off = (bank & ~1u) * 0x4000 + (addr - 0x8000);
            } else if (prg_mode == 2) {
                mmc1_off = (addr < 0xC000) ? (uint32_t)(addr - 0x8000)
                                           : bank * 0x4000 + (addr - 0xC000);
            } else {
                mmc1_off = (addr < 0xC000) ? bank * 0x4000 + (addr - 0x8000)
                                           : (uint32_t)(mapper.prg_banks - 1) * 0x4000 + (addr - 0xC000);
            }
            return prg_rom[mmc1_off % prg_rom_size];
        }
        
    case 2: /* UNROM — Castlevania */
        if (addr < 0xC000) {
            offset = (uint32_t)mapper.m1_prg_bank * 0x4000 + (addr - 0x8000);
        } else {
            /* Последний банк фиксирован */
            offset = (uint32_t)(mapper.prg_banks - 1) * 0x4000 + (addr - 0xC000);
        }
        return prg_rom[offset % prg_rom_size];
        
    case 3: /* CNROM */
        offset = addr - 0x8000;
        if (mapper.prg_banks == 1) offset &= 0x3FFF;
        return prg_rom[offset % prg_rom_size];
        
    case 4: /* MMC3 */
        {
            int prg_mode = (mapper.m4_bank_select >> 6) & 1;
            if (addr < 0xA000) {
                uint8_t b = prg_mode ? (mapper.prg_banks * 2 - 2) : mapper.m4_banks[6];
                offset = (uint32_t)b * 0x2000 + (addr - 0x8000);
            } else if (addr < 0xC000) {
                offset = (uint32_t)mapper.m4_banks[7] * 0x2000 + (addr - 0xA000);
            } else if (addr < 0xE000) {
                uint8_t b = prg_mode ? mapper.m4_banks[6] : (mapper.prg_banks * 2 - 2);
                offset = (uint32_t)b * 0x2000 + (addr - 0xC000);
            } else {
                offset = (uint32_t)(mapper.prg_banks * 2 - 1) * 0x2000 + (addr - 0xE000);
            }
        }
        return prg_rom[offset % prg_rom_size];
        
    default:
        offset = addr - 0x8000;
        return prg_rom[offset % prg_rom_size];
    }
}

/* =========================================================================
   PRG-ROM write (mapper registers)
   ========================================================================= */
void mapper_prg_write(uint16_t addr, uint8_t val) {
    switch (mapper.id) {
    case 0: /* NROM */
        break;
        
    case 1: /* MMC1 */
        if (val & 0x80) {
            mapper.m1_shift       = 0x10;
            mapper.m1_shift_count = 0;
            mapper.m1_ctrl       |= 0x0C;
        } else {
            mapper.m1_shift = (mapper.m1_shift >> 1) | ((val & 1) << 4);
            mapper.m1_shift_count++;
            if (mapper.m1_shift_count == 5) {
                uint8_t v             = mapper.m1_shift & 0x1F;
                mapper.m1_shift       = 0x10;
                mapper.m1_shift_count = 0;
                if      (addr < 0xA000) { mapper.m1_ctrl      = v; mapper.mirroring = v & 3; }
                else if (addr < 0xC000)   mapper.m1_chr_bank0 = v;
                else if (addr < 0xE000)   mapper.m1_chr_bank1 = v;
                else                      mapper.m1_prg_bank  = v & 0x0F;
            }
        }
        break;
        
    case 2: /* UNROM — Castlevania */
        mapper.m1_prg_bank = val & 0x07;  /* 3 бита для 8 банков */
        break;
        
    case 3: /* CNROM */
        mapper.m1_chr_bank0 = val & 3;
        break;
        
    case 4: /* MMC3 */
        if (addr < 0xA000) {
            if (addr & 1) {
                int reg = mapper.m4_bank_select & 7;
                mapper.m4_banks[reg] = val;
                if (reg == 0 || reg == 1) mapper.m4_banks[reg] &= 0xFE;
                if (reg >= 6) mapper.m4_banks[reg] &= 0x3F;
            } else {
                mapper.m4_bank_select = val;
            }
        } else if (addr < 0xC000) {
            if (!(addr & 1)) mapper.mirroring = val & 1;
        } else if (addr < 0xE000) {
            if (!(addr & 1)) mapper.m4_irq_latch = val;
            else { mapper.m4_irq_counter = 0; mapper.m4_irq_reload = 1; }
        } else {
            mapper.m4_irq_enable = (addr & 1) ? 1 : 0;
        }
        break;
    }
}

/* =========================================================================
   CHR read/write
   ========================================================================= */
uint8_t mapper_chr_read(uint16_t addr) {
    addr &= 0x1FFF;
    
    /* CHR-RAM (Castlevania) */
    if (mapper.chr_banks == 0) {
        return ppu.chr[addr];
    }
    
    uint32_t chr_size = (uint32_t)mapper.chr_banks * 8192;
    uint32_t off;
    
    switch (mapper.id) {
    case 0: /* NROM */
    case 2: /* UNROM */
        return ppu.chr[addr % chr_size];
        
    case 1: /* MMC1 */
        {
            int chr_mode = (mapper.m1_ctrl >> 4) & 1;
            if (chr_mode == 0) {
                off = (uint32_t)(mapper.m1_chr_bank0 & 0x1E) * 0x1000 + addr;
            } else {
                if (addr < 0x1000)
                    off = (uint32_t)mapper.m1_chr_bank0 * 0x1000 + addr;
                else
                    off = (uint32_t)mapper.m1_chr_bank1 * 0x1000 + (addr - 0x1000);
            }
            return ppu.chr[off % chr_size];
        }
        
    case 3: /* CNROM */
        off = (uint32_t)mapper.m1_chr_bank0 * 0x2000 + addr;
        return ppu.chr[off % chr_size];
        
case 4: /* MMC3 */
    {
        /* CHR-RAM: Aa Yakyuu, некоторые MMC3 игры используют 8KB flat CHR-RAM.
           MMC3 не банкует CHR-RAM — просто flat доступ по addr & 0x1FFF */
        if (mapper.chr_banks == 0) {
            return ppu.chr[addr & 0x1FFF];
        }
        
        /* CHR-ROM — Dragons of Flame и другие */
        uint32_t chr_size = (uint32_t)mapper.chr_banks * 8192;
        int inv = (mapper.m4_bank_select >> 7) & 1;
        uint16_t a = inv ? (addr ^ 0x1000) : addr;
        uint8_t bank;
        uint32_t off;
        
        if (a < 0x0800) {
            bank = mapper.m4_banks[0] & 0xFE;
            off = (uint32_t)bank * 0x400 + (a & 0x7FF);
        } else if (a < 0x1000) {
            bank = mapper.m4_banks[1] & 0xFE;
            off = (uint32_t)bank * 0x400 + (a & 0x7FF);
        } else if (a < 0x1400) {
            bank = mapper.m4_banks[2];
            off = (uint32_t)bank * 0x400 + (a & 0x3FF);
        } else if (a < 0x1800) {
            bank = mapper.m4_banks[3];
            off = (uint32_t)bank * 0x400 + (a & 0x3FF);
        } else if (a < 0x1C00) {
            bank = mapper.m4_banks[4];
            off = (uint32_t)bank * 0x400 + (a & 0x3FF);
        } else {
            bank = mapper.m4_banks[5];
            off = (uint32_t)bank * 0x400 + (a & 0x3FF);
        }
        
        return ppu.chr[off % chr_size];
    }
        
    default:
        return ppu.chr[addr % chr_size];
    }
}

void mapper_chr_write(uint16_t addr, uint8_t val) {
    /* CHR-RAM — Castlevania использует это */
    if (mapper.chr_banks == 0) {
        ppu.chr[addr & 0x1FFF] = val;
    }
}

/* =========================================================================
   MMC3 Scanline IRQ
   ========================================================================= */
void mapper_scanline(void) {
    if (mapper.id != 4) return;

    /* MMC3 IRQ — точный алгоритм по nesdev wiki:
       При каждом rising edge A12 (эмулируем как каждый scanline):
       1. Если counter == 0 или reload flag установлен:
          counter = latch, reload = 0
       2. Иначе: counter--
       3. Если counter == 0 и IRQ enabled: fire IRQ
       Важно: fire происходит ПОСЛЕ загрузки/декремента */
    uint8_t old_counter = mapper.m4_irq_counter;

    if (old_counter == 0 || mapper.m4_irq_reload) {
        mapper.m4_irq_counter = mapper.m4_irq_latch;
        mapper.m4_irq_reload  = 0;
    } else {
        mapper.m4_irq_counter--;
    }

    /* Fire если counter достиг 0 (был >0, стал 0) */
    if (mapper.m4_irq_counter == 0 && mapper.m4_irq_enable) {
        nes_irq();
    }
}