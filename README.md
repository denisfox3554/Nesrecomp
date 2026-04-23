# NES Static Recompiler

Converts .nes ROM → C code → native executable on Windows (MinGW).
**Not an emulator** — each 6502 instruction is translated into C once during compilation.

```
game.nes
↓ python recompiler/nesrecomp.py
generated/<game>_full.c ← all 6502 functions→C
generated/<game>_dispatch.c ← call_by_address() + nes_reset/nmi/irq
↓ mingw32-make GAME=<game>
bin/nesrecomp.exe
```

## Requirements

- **MSYS2 / MinGW-w64**
- `gcc`, `mingw32-make`
- **SDL2** for MinGW: `pacman -S mingw-w64-x86_64-SDL2`
- **Python 3.8+** (for recompiler only)

## Quick Start

``cmd
:: 1. Generate C code from ROM + Build
mingw32-make recomp ROM=donkeykong.nes GAME=donkeykong

:: 2. Run
bin\nesrecomp.exe donkeykong.nes

:: Or: just build (with the generated/)
mingw32-make GAME=donkeykong

:: Stub build (without ROM, to test runner compilation)
mingw32-make
```

## Controls

| Key | NES |
|---------|-----|
| Z | A |
| X | B |
| RShift | Select |
| Enter | Start |
| Arrows | D-pad |
| Esc | Exit |

## Project Structure

```
nesrecomp/
├── recompiler/
│ └── nesrecomp.py ← Python: 6502→C generator
├── runner/
│ ├── include/
│ │ └── runner.h ← common header (CPU, PPU, APU, mapper)
│ └── src/
│ ├── memory.c ← NES memory map, controllers
│ ├── ppu.c ← full PPU 2C02 emulation
│ ├── apu.c ← APU (pulse, triangle, noise, DMC)
│ ├── mapper.c ← mapper 0/1/2/3/4 (NROM/MMC1/UNROM/CNROM/MMC3)
│ └── runner.c ← SDL2 window, game loop, ROM loading, main()
├── generated/
│ └── stub_full.c ← stub (no ROM yet)
├── Makefile ← mingw32-make
└── game.cfg.example ← example config for recompiler
```

## Supported mappers

| Mapper | Name | Coverage |
|--------|----------|----------|
| 0 | NROM | Donkey Kong, Mario Bros, Balloon Fight |
| 1 | MMC1 | Super Mario Bros 3, Metroid, Mega Man 2 |
| 2 | UNROM | Mega Man, Castlevania |
| 3 | CNROM | Gradius, Q*bert |
| 4 | MMC3 | Super Mario Bros 3 (alt), Kirby's Adventure |

## Adding Functions Manually

If the game has a dispatch table (e.g., AI handlers via `JMP ($addr)`),
add the addresses to `game.cfg`:

```
extra_func = E4A0
extra_func = E502
```

Затем перезапусти `mingw32-make recomp ROM=... GAME=...`.

## Лицензия

MIT
