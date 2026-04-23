# ============================================================
#  NES Static Recompiler — Makefile for MinGW (mingw32-make)
#  Usage:
#    mingw32-make           — build with stub (no ROM)
#    mingw32-make GAME=mygame — build with generated/mygame_full.c
#    mingw32-make recomp ROM=game.nes GAME=mygame — run recompiler then build
#    mingw32-make clean
# ============================================================

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter \
           -Irunner/include \
           $(shell sdl2-config --cflags 2>NUL || echo -I$(MSYS2_PREFIX)/include/SDL2 -DSDL_MAIN_HANDLED)
LDFLAGS = $(shell sdl2-config --libs 2>NUL || echo -L$(MSYS2_PREFIX)/lib -lSDL2main -lSDL2) \
           -lmingw32 -mwindows
# For a console window (debug), replace -mwindows with -mconsole

BINDIR  = bin
TARGET  = $(BINDIR)/nesrecomp.exe

RUNNER_SRCS = \
    runner/src/memory.c \
    runner/src/cpu_interp.c \
    runner/src/ppu.c \
    runner/src/apu.c \
    runner/src/mapper.c \
    runner/src/runner.c

# Game generated code — override GAME= to use your game
GAME ?= stub

FULL_SRC     = generated/$(GAME)_full.c
DISPATCH_SRC = generated/$(GAME)_dispatch.c

# If dispatch file exists, use it; otherwise fall back to stub
ifeq ($(wildcard $(DISPATCH_SRC)),)
    GAME_SRCS = $(FULL_SRC)
else
    GAME_SRCS = $(FULL_SRC) $(DISPATCH_SRC)
endif

ALL_SRCS = $(RUNNER_SRCS) $(GAME_SRCS)

# Object files go into build/
OBJDIR = build
OBJS   = $(patsubst %.c,$(OBJDIR)/%.o,$(ALL_SRCS))

# ============================================================
#  Targets
# ============================================================

.PHONY: all clean recomp dirs

all: dirs $(TARGET)

$(TARGET): $(OBJS)
	@echo [LINK] $@
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo Build successful: $@

$(OBJDIR)/%.o: %.c
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	@echo [CC] $<
	$(CC) $(CFLAGS) -c $< -o $@

dirs:
	@if not exist $(BINDIR) mkdir $(BINDIR)
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	@if not exist generated  mkdir generated

# ============================================================
#  Run the Python recompiler, then build
# ============================================================
recomp:
ifndef ROM
	$(error ROM is not set. Use: mingw32-make recomp ROM=game.nes GAME=mygame)
endif
ifndef GAME
	$(error GAME is not set. Use: mingw32-make recomp ROM=game.nes GAME=mygame)
endif
	@echo [RECOMP] $(ROM) -> generated/$(GAME)_full.c
	python recompiler/nesrecomp.py $(ROM) --out generated --game $(GAME)
	$(MAKE) GAME=$(GAME)

# ============================================================
#  Clean
# ============================================================
clean:
	@if exist $(OBJDIR) rmdir /S /Q $(OBJDIR)
	@if exist $(BINDIR) rmdir /S /Q $(BINDIR)
	@echo Cleaned.
