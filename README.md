# NESRecomp for MinGW 🎮

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![NESRecomp](https://img.shields.io/badge/Base-NESRecomp%20by%20mstan-blue)](https://github.com/mstan/nesrecomp)

**Static recompilation of NES games to native code without Visual Studio!**

This is a fork of [NESRecomp](https://github.com/mstan/nesrecomp), adapted for building with **MinGW-w64** and **Makefile**.

> **All adaptation work was done in dialogue with Claude AI (Anthropic).**
> The code is completely open source. Doesn't require Visual Studio.

---

## ✨ Features

- ✅ **Build without Visual Studio** — MinGW-w64, SDL2, and Python only
- ✅ **Extended mapper support** — MMC1, UNROM, CNROM, MMC3 (partial)
- ✅ **Portable builds** — one EXE + ROM = a ready-to-play game
- ✅ **Automatic stubs** — for unrecognized functions
- ✅ **Simple Makefile** — `mingw32-make GAME=GameName`

---

## 🎮 Supported Games

| Game | Mapper | Status |
|------|--------|--------|
| Donkey Kong | 0 (NROM) | ✅ Complete |
| Super Mario Bros. | 0 (NROM) | ✅ Working |
| Adventure Island | 3 (CNROM) | ✅ Complete |
| Castlevania | 2 (UNROM) | ✅ Complete |
| DuckTales | 2 (UNROM) | ✅ Complete |
| Mega Man | 2 (UNROM) | ✅ Complete |
| Dragons of Flame | 1 (MMC1) | ✅ Complete |
| Mega Man 4 | 4 (MMC3) | ⚠️ In Progress |

---

## 🚀 Quick Start

### Installing Dependencies (One-Time)

```bash
# Install MSYS2 from here: https://www.msys2.org/
# Then in the MSYS2/MinGW64 terminal:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 make python

git clone https://github.com/ВАШ_ЛОГИН/nesrecomp-mingw.git
cd nesrecomp-mingw

# Положите ваш ROM в папку roms/
# Например: roms/dk.nes

# Соберите и запустите
mingw32-make recomp ROM=roms/dk.nes GAME=DonkeyKong
bin/nesrecomp.exe roms/dk.nes
