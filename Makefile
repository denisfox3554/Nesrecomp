# ============================================================
#  NES Recomp — Makefile
#  Linux:   make
#  Windows: mingw32-make
# ============================================================

CC     = gcc
CFLAGS = -O2 -Wall -Wno-unused-parameter -Wno-unused-variable \
         -Wno-unused-result \
         -Irunner/include \
         $(shell sdl2-config --cflags 2>/dev/null || echo -I/usr/include/SDL2)

LDFLAGS = $(shell sdl2-config --libs 2>/dev/null || echo -lSDL2) -lm

# Windows: добавляем mingw32
ifeq ($(OS),Windows_NT)
    LDFLAGS  += -lmingw32 -mwindows
    EXE       = bin/nesrecomp.exe
    RM        = del /Q /S
    MKDIRP    = if not exist "$1" mkdir "$1"
else
    EXE       = bin/nesrecomp
    RM        = rm -rf
    MKDIRP    = mkdir -p $1
endif

GAME  ?= stub
TARGET = $(EXE)

SRCS = \
    runner/src/memory.c \
    runner/src/cpu_interp.c \
    runner/src/ppu.c \
    runner/src/apu.c \
    runner/src/mapper.c \
    runner/src/runner.c

FULL = generated/$(GAME)_full.c
DISP = generated/$(GAME)_dispatch.c

ifeq ($(wildcard $(DISP)),)
    GSRCS = $(FULL)
else
    GSRCS = $(FULL) $(DISP)
endif

OBJDIR = build
OBJS   = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS) $(GSRCS))

# ─────────────────────────────────────────
.PHONY: all clean recomp run install-deps

all: dirs $(TARGET)
	@echo ""
	@echo "=== Build OK: $(TARGET) ==="
	@echo "Usage: $(TARGET) roms/game.nes"

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo [CC] $<
	@$(CC) $(CFLAGS) -c $< -o $@

dirs:
	@mkdir -p bin build generated roms

# ─── Зависимости (Linux) ─────────────────
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get install -y gcc libsdl2-dev make python3 git

# ─── Recompile ROM → C → exe ─────────────
recomp:
ifndef ROM
	$(error Usage: make recomp ROM=roms/game.nes GAME=GameName)
endif
ifndef GAME
	$(error Usage: make recomp ROM=roms/game.nes GAME=GameName)
endif
	python3 recompiler/nesrecomp.py "$(ROM)" --out generated --game $(GAME)
	$(MAKE) GAME=$(GAME)

# ─── Быстрый запуск ──────────────────────
run: all
ifndef ROM
	$(error Usage: make run ROM=roms/game.nes)
endif
	$(TARGET) "$(ROM)"

clean:
	$(RM) build bin
	@echo Cleaned.
