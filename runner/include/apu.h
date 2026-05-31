#ifndef APU_H
#define APU_H

/*
 * apu.h — APU (Audio Processing Unit): pulse, triangle, noise, DMC
 *
 * Фокус: только генерация звука.
 * Если звук неправильный — смотри apu.c.
 * APU не знает про PPU и mapper.
 *
 * Архитектура:
 *   apu_step()  вызывается 1 раз на CPU цикл (~1.79 MHz)
 *   Сэмплы накапливаются в sample_buf[]
 *   Runner сливает их в ring buffer -> SDL audio callback
 *
 * Каналы:
 *   pulse[0], pulse[1]  — прямоугольные волны с sweep
 *   tri                 — треугольная волна
 *   noise               — шумовой генератор с LFSR
 *   dmc                 — delta modulation (PCM сэмплы из ROM)
 */

#include <stdint.h>

typedef struct {
    /* Pulse 1 & 2 */
    struct {
        uint8_t  duty;           /* форма волны (0-3) */
        uint8_t  length_halt;    /* остановить длину / loop envelope */
        uint8_t  constant_vol;   /* постоянная громкость */
        uint8_t  volume;         /* громкость/период огибающей */
        uint16_t timer;          /* текущий таймер */
        uint16_t timer_reload;   /* период таймера */
        uint8_t  length;         /* счётчик длины */
        uint8_t  enabled;        /* канал включён ($4015) */
        /* Sweep unit */
        uint8_t  sweep_en;
        uint8_t  sweep_period;
        uint8_t  sweep_negate;
        uint8_t  sweep_shift;
        uint8_t  sweep_reload;
        uint8_t  sweep_divider;
        /* Envelope */
        uint8_t  envelope_start;
        uint8_t  envelope_divider;
        uint8_t  envelope_decay;
        /* Sequencer */
        uint32_t seq_pos;
    } pulse[2];

    /* Triangle */
    struct {
        uint8_t  control;           /* linear counter halt */
        uint8_t  linear_reload_val;
        uint16_t timer, timer_reload;
        uint8_t  length, enabled;
        uint8_t  linear_counter, linear_reload;
        uint32_t seq_pos;
    } tri;

    /* Noise */
    struct {
        uint8_t  length_halt, constant_vol, volume;
        uint16_t timer, timer_reload;
        uint8_t  length, enabled, mode;
        uint16_t shift_reg;         /* LFSR, init=1 */
        uint8_t  envelope_start, envelope_divider, envelope_decay;
    } noise;

    /* DMC (Delta Modulation Channel) */
    struct {
        uint8_t  irq_en, loop, rate_idx, enabled;
        uint8_t  output;            /* текущий уровень (0-127) */
        uint16_t sample_addr, sample_len;
        uint16_t cur_addr, bytes_remaining;
        uint8_t  sample_buf, bits_remaining, silence;
    } dmc;

    /* Frame sequencer */
    uint8_t  frame_counter_mode;  /* 0=4-step, 1=5-step */
    uint8_t  frame_irq_inhibit;
    uint32_t frame_clock;

    /* Выходные сэмплы (накапливаются в apu_step, сбрасываются runner'ом) */
    float    sample_buf[4096];
    int      sample_count;
} APU;

extern APU apu;

/* Запись регистра APU (вызывается из memory.c) */
void    apu_write(uint16_t addr, uint8_t val);

/* Чтение $4015 */
uint8_t apu_read_status(void);

/* Один CPU цикл */
void    apu_step(void);

/* Слить накопленные сэмплы в buf (для audio callback) */
void    apu_fill_buffer(float *buf, int samples);

#endif /* APU_H */
