#include "runner.h"
#include <SDL2/SDL.h>

/* =========================================================================
   ROM loading
   ========================================================================= */
static int load_rom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[runner] Cannot open ROM: %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t*)malloc(sz);
    if (!data) { fclose(f); return 0; }
    fread(data, 1, sz, f);
    fclose(f);

    if (data[0]!='N'||data[1]!='E'||data[2]!='S'||data[3]!=0x1A) {
        fprintf(stderr, "[runner] Not a valid iNES ROM\n");
        free(data); return 0;
    }
    int prg_banks   = data[4];
    int chr_banks   = data[5];
    int flags6      = data[6];
    int flags7      = data[7];
    int mapper_id   = (flags7 & 0xF0) | (flags6 >> 4);
    int mirroring   = (flags6 & 1) ? 1 : 0;
    int has_trainer = (flags6 & 4) ? 1 : 0;

    fprintf(stderr, "[runner] Mapper=%d PRG=%d CHR=%d mirror=%s trainer=%d\n",
            mapper_id, prg_banks, chr_banks, mirroring ? "V" : "H", has_trainer);

    int offset = 16 + (has_trainer ? 512 : 0);
    prg_rom_size = (uint32_t)prg_banks * 16384;
    if (prg_rom_size > PRG_ROM_MAX) prg_rom_size = PRG_ROM_MAX;
    memcpy(prg_rom, data + offset, prg_rom_size);

if (chr_banks > 0) {
    uint32_t chr_size = (uint32_t)chr_banks * 8192;
    if (chr_size > sizeof(ppu.chr)) chr_size = sizeof(ppu.chr);
    memcpy(ppu.chr, data + offset + prg_rom_size, chr_size);
    fprintf(stderr, "[runner] Loaded %d bytes of CHR-ROM\n", chr_size);
} else {
    fprintf(stderr, "[runner] Initializing CHR-RAM (8KB)\n");
    memset(ppu.chr, 0x00, 0x2000);  /* CHR-RAM инициализируем нулями */
}

    mapper_init(mapper_id, prg_banks, chr_banks, mirroring);
    free(data);
    return 1;
}

/* =========================================================================
   Audio
   ========================================================================= */
#define RING_SIZE  32768
#define RING_MASK  (RING_SIZE-1)
#define AUDIO_SAMPLES 512

static float        ring_buf[RING_SIZE];
static volatile int ring_write = 0;
static volatile int ring_read  = 0;
static SDL_AudioDeviceID audio_dev = 0;

static void audio_callback(void *ud, Uint8 *stream, int len) {
    (void)ud;
    float *out = (float*)stream;
    int n = len / (int)sizeof(float);
    static float last_sample = 0.0f;
    for (int i = 0; i < n; i++) {
        int next_read = (ring_read + 1) & RING_MASK;
        if (next_read != ring_write) {
            last_sample = ring_buf[ring_read];
            out[i] = last_sample;
            ring_read = next_read;
        } else {
            out[i] = last_sample;
        }
    }
}

static inline void audio_push(float s) {
    int nw = (ring_write + 1) & RING_MASK;
    if (nw != ring_read) { ring_buf[ring_write] = s; ring_write = nw; }
}

/* =========================================================================
   Input
   ========================================================================= */
static void handle_key(SDL_Keycode k, int down) {
    uint8_t bit = 0;
    int player = 0;
    
    switch (k) {
    /* Player 1 */
    case SDLK_z:      bit = 0x80; player = 0; break;
    case SDLK_x:      bit = 0x40; player = 0; break;
    case SDLK_RSHIFT: bit = 0x20; player = 0; break;
    case SDLK_RETURN: bit = 0x10; player = 0; break;
    case SDLK_UP:     bit = 0x08; player = 0; break;
    case SDLK_DOWN:   bit = 0x04; player = 0; break;
    case SDLK_LEFT:   bit = 0x02; player = 0; break;
    case SDLK_RIGHT:  bit = 0x01; player = 0; break;
    
    /* Player 2 */
    case SDLK_a:      bit = 0x80; player = 1; break;
    case SDLK_s:      bit = 0x40; player = 1; break;
    case SDLK_q:      bit = 0x20; player = 1; break;
    case SDLK_2:      bit = 0x10; player = 1; break;
    case SDLK_w:      bit = 0x08; player = 1; break;
    case SDLK_e:      bit = 0x04; player = 1; break;
    case SDLK_d:      bit = 0x02; player = 1; break;
    case SDLK_c:      bit = 0x01; player = 1; break;
    
    default: return;
    }
    
    if (down) controller[player] |=  bit;
    else      controller[player] &= ~bit;
}

/* =========================================================================
   SDL objects
   ========================================================================= */
static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *texture  = NULL;

/* Глобальные флаги прерываний */
static volatile int g_nmi_pending = 0;
static volatile int g_irq_pending = 0;

void nes_nmi(void)   { g_nmi_pending = 1; }
void nes_irq(void)   { if (!cpu.I) g_irq_pending = 1; }
void nes_reset(void) { cpu.PC = mem_read(0xFFFC)|((uint16_t)mem_read(0xFFFD)<<8); }

/* =========================================================================
   runner_init
   ========================================================================= */
int runner_init(const char *title, const char *rom_path) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[runner] SDL_Init: %s\n", SDL_GetError()); return 0;
    }
    window = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W*3, SCREEN_H*3, SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    if (!window) { fprintf(stderr,"[runner] Window: %s\n",SDL_GetError()); return 0; }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

    texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = AUDIO_SAMPLES;
    want.callback = audio_callback;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);

    memset(&cpu,  0, sizeof(cpu));  cpu.SP = 0xFD; cpu.I = 1;
    memset(ram,   0, sizeof(ram));
    memset(sram,  0, sizeof(sram));
    memset(&ppu,  0, sizeof(ppu));
    memset(&apu,  0, sizeof(apu));
    apu.noise.shift_reg = 1;

    if (!load_rom(rom_path)) return 0;

    cpu.PC = mem_read(0xFFFC) | ((uint16_t)mem_read(0xFFFD) << 8);
    return 1;
}

/* =========================================================================
   runner_run
   ========================================================================= */
void runner_run(void) {
    const Uint64 FREQ   = SDL_GetPerformanceFrequency();
    const Uint64 FRAME  = (Uint64)((double)FREQ / 60.0988 + 0.5);

    Uint64 frame_start = SDL_GetPerformanceCounter();
    int    event_divider = 0;
    int    last_frame_scanline = -1;

    while (1) {
        if (++event_divider >= 100) {
            event_divider = 0;
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) return;
                if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) return;
                    handle_key(ev.key.keysym.sym, 1);
                }
                if (ev.type == SDL_KEYUP) handle_key(ev.key.keysym.sym, 0);
            }
        }

        if (g_nmi_pending) {
            g_nmi_pending = 0;
            stack_push((cpu.PC >> 8) & 0xFF);
            stack_push(cpu.PC & 0xFF);
            stack_push(get_P() & ~0x10);
            cpu.I = 1;
            cpu.PC = mem_read(0xFFFA) | ((uint16_t)mem_read(0xFFFB) << 8);
        }

        if (g_irq_pending && !cpu.I) {
            g_irq_pending = 0;
            stack_push((cpu.PC >> 8) & 0xFF);
            stack_push(cpu.PC & 0xFF);
            stack_push(get_P() & ~0x10);
            cpu.I = 1;
            cpu.PC = mem_read(0xFFFE) | ((uint16_t)mem_read(0xFFFF) << 8);
        }

        int cyc = cpu_interp_step();
        if (cyc <= 0) cyc = 1;

        for (int c = 0; c < cyc; c++) apu_step();
        for (int c = 0; c < cyc * 3; c++) ppu_step();

        if (ppu.scanline == 241 && last_frame_scanline != 241) {
            last_frame_scanline = 241;

            float audio_buf[735];
            apu_fill_buffer(audio_buf, 735);
            for (int s = 0; s < 735; s++) audio_push(audio_buf[s]);

            SDL_UpdateTexture(texture, NULL, ppu.framebuf, SCREEN_W * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

            Uint64 target = frame_start + FRAME;
            Uint64 now2 = SDL_GetPerformanceCounter();
            if (now2 < target) {
                /* Спим большую часть времени через SDL_Delay (экономит CPU) */
                Uint64 sleep_ms = (target - now2) * 1000 / FREQ;
                if (sleep_ms > 2) SDL_Delay((Uint32)(sleep_ms - 2));
                /* Busy-wait только последние ~2ms для точности */
                while (SDL_GetPerformanceCounter() < target) { /* spin */ }
            }
            frame_start = target;
            /* Защита от накопления отставания */
            now2 = SDL_GetPerformanceCounter();
            if (now2 > frame_start + FRAME * 3) {
                frame_start = now2;  /* ресинхронизация если сильно отстали */
            }

        } else if (ppu.scanline != 241) {
            last_frame_scanline = ppu.scanline;
        }
    }
}

void runner_quit(void) {
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    if (texture)   SDL_DestroyTexture(texture);
    if (renderer)  SDL_DestroyRenderer(renderer);
    if (window)    SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr,"Usage: %s <rom.nes>\n",argv[0]); return 1; }
    if (!runner_init("NES Recomp", argv[1])) return 1;
    runner_run();
    runner_quit();
    return 0;
}