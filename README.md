# NES Static Recompiler

Конвертирует `.nes` ROM → C-код → нативный exe на Windows (MinGW).  
**Не эмулятор** — каждая инструкция 6502 транслируется в C один раз при сборке.

```
game.nes
  ↓  python recompiler/nesrecomp.py
generated/<game>_full.c       ← все функции 6502→C
generated/<game>_dispatch.c   ← call_by_address() + nes_reset/nmi/irq
  ↓  mingw32-make GAME=<game>
bin/nesrecomp.exe
```

## Требования

- **MSYS2 / MinGW-w64**
- `gcc`, `mingw32-make`
- **SDL2** для MinGW: `pacman -S mingw-w64-x86_64-SDL2`
- **Python 3.8+** (только для recompiler)

## Быстрый старт

```cmd
:: 1. Сгенерировать C-код из ROM + собрать
mingw32-make recomp ROM=donkeykong.nes GAME=donkeykong

:: 2. Запустить
bin\nesrecomp.exe donkeykong.nes

:: Или: только собрать (с уже готовым generated/)
mingw32-make GAME=donkeykong

:: Stub-билд (без ROM, для проверки компиляции runner)
mingw32-make
```

## Управление

| Клавиша | NES |
|---------|-----|
| Z | A |
| X | B |
| RShift | Select |
| Enter | Start |
| Стрелки | D-pad |
| Esc | Выход |

## Структура проекта

```
nesrecomp/
├── recompiler/
│   └── nesrecomp.py       ← Python: 6502→C генератор
├── runner/
│   ├── include/
│   │   └── runner.h       ← общий заголовок (CPU, PPU, APU, mapper)
│   └── src/
│       ├── memory.c       ← карта памяти NES, контроллеры
│       ├── ppu.c          ← полная эмуляция PPU 2C02
│       ├── apu.c          ← APU (pulse, triangle, noise, DMC)
│       ├── mapper.c       ← mapper 0/1/2/3/4 (NROM/MMC1/UNROM/CNROM/MMC3)
│       └── runner.c       ← SDL2 окно, game loop, загрузка ROM, main()
├── generated/
│   └── stub_full.c        ← заглушка (пока нет ROM)
├── Makefile               ← mingw32-make
└── game.cfg.example       ← пример конфига для recompiler
```

## Поддерживаемые маппера

| Mapper | Название | Покрытие |
|--------|----------|----------|
| 0 | NROM | Donkey Kong, Mario Bros, Balloon Fight |
| 1 | MMC1 | Super Mario Bros 3, Metroid, Mega Man 2 |
| 2 | UNROM | Mega Man, Castlevania |
| 3 | CNROM | Gradius, Q*bert |
| 4 | MMC3 | Super Mario Bros 3 (alt), Kirby's Adventure |

## Добавление функций вручную

Если у игры есть dispatch-таблица (напр. AI-хендлеры через `JMP ($addr)`),
добавь адреса в `game.cfg`:

```
extra_func = E4A0
extra_func = E502
```

Затем перезапусти `mingw32-make recomp ROM=... GAME=...`.

## Лицензия

MIT
