#include "runner.h"
#include <math.h>

APU apu;

/* =========================================================================
   Tables
   ========================================================================= */
static const uint8_t LENGTH_TABLE[32] = {
    10,254,20, 2,40, 4,80, 6,160, 8,60,10,14,12,26,14,
    12, 16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
};

static const uint8_t DUTY_SEQ[4][8] = {
    {0,1,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0},
    {0,1,1,1,1,0,0,0},
    {1,0,0,1,1,1,1,1},
};

static const uint8_t TRI_SEQ[32] = {
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
     0, 1, 2, 3, 4, 5,6,7,8,9,10,11,12,13,14,15
};

static const uint16_t NOISE_PERIOD[16] = {
    4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
};

/* =========================================================================
   Register writes
   ========================================================================= */
void apu_write(uint16_t addr, uint8_t val) {
    switch (addr) {
    /* Pulse 1 */
    case 0x4000:
        apu.pulse[0].duty         = val >> 6;
        apu.pulse[0].length_halt  = (val >> 5) & 1;
        apu.pulse[0].constant_vol = (val >> 4) & 1;
        apu.pulse[0].volume       = val & 0xF;
        break;
    case 0x4001:
        apu.pulse[0].sweep_en     = (val >> 7) & 1;
        apu.pulse[0].sweep_period = (val >> 4) & 7;
        apu.pulse[0].sweep_negate = (val >> 3) & 1;
        apu.pulse[0].sweep_shift  = val & 7;
        apu.pulse[0].sweep_reload = 1;
        break;
    case 0x4002:
        apu.pulse[0].timer_reload = (apu.pulse[0].timer_reload & 0x700) | val;
        break;
    case 0x4003:
        apu.pulse[0].timer_reload = (apu.pulse[0].timer_reload & 0xFF) | ((uint16_t)(val&7)<<8);
        if (apu.pulse[0].enabled) apu.pulse[0].length = LENGTH_TABLE[val >> 3];
        apu.pulse[0].envelope_start = 1;
        apu.pulse[0].seq_pos = 0;
        break;
    /* Pulse 2 */
    case 0x4004:
        apu.pulse[1].duty         = val >> 6;
        apu.pulse[1].length_halt  = (val >> 5) & 1;
        apu.pulse[1].constant_vol = (val >> 4) & 1;
        apu.pulse[1].volume       = val & 0xF;
        break;
    case 0x4005:
        apu.pulse[1].sweep_en     = (val >> 7) & 1;
        apu.pulse[1].sweep_period = (val >> 4) & 7;
        apu.pulse[1].sweep_negate = (val >> 3) & 1;
        apu.pulse[1].sweep_shift  = val & 7;
        apu.pulse[1].sweep_reload = 1;
        break;
    case 0x4006:
        apu.pulse[1].timer_reload = (apu.pulse[1].timer_reload & 0x700) | val;
        break;
    case 0x4007:
        apu.pulse[1].timer_reload = (apu.pulse[1].timer_reload & 0xFF) | ((uint16_t)(val&7)<<8);
        if (apu.pulse[1].enabled) apu.pulse[1].length = LENGTH_TABLE[val >> 3];
        apu.pulse[1].envelope_start = 1;
        apu.pulse[1].seq_pos = 0;
        break;
    /* Triangle */
    case 0x4008:
        apu.tri.control          = (val >> 7) & 1;
        apu.tri.linear_reload_val = val & 0x7F;
        break;
    case 0x400A:
        apu.tri.timer_reload = (apu.tri.timer_reload & 0x700) | val;
        break;
    case 0x400B:
        apu.tri.timer_reload = (apu.tri.timer_reload & 0xFF) | ((uint16_t)(val&7)<<8);
        if (apu.tri.enabled) apu.tri.length = LENGTH_TABLE[val >> 3];
        apu.tri.linear_reload = 1;
        break;
    /* Noise */
    case 0x400C:
        apu.noise.length_halt  = (val >> 5) & 1;
        apu.noise.constant_vol = (val >> 4) & 1;
        apu.noise.volume       = val & 0xF;
        break;
    case 0x400E:
        apu.noise.mode         = (val >> 7) & 1;
        apu.noise.timer_reload = NOISE_PERIOD[val & 0xF];
        break;
    case 0x400F:
        if (apu.noise.enabled) apu.noise.length = LENGTH_TABLE[val >> 3];
        apu.noise.envelope_start = 1;
        break;
    /* DMC */
    case 0x4010:
        apu.dmc.irq_en   = (val >> 7) & 1;
        apu.dmc.loop     = (val >> 6) & 1;
        apu.dmc.rate_idx = val & 0xF;
        break;
    case 0x4011:
        apu.dmc.output = val & 0x7F;
        break;
    case 0x4012:
        apu.dmc.sample_addr = 0xC000 | ((uint16_t)val << 6);
        break;
    case 0x4013:
        apu.dmc.sample_len = ((uint16_t)val << 4) + 1;
        break;
    /* Status $4015 */
    case 0x4015:
        apu.pulse[0].enabled = (val >> 0) & 1;
        apu.pulse[1].enabled = (val >> 1) & 1;
        apu.tri.enabled      = (val >> 2) & 1;
        apu.noise.enabled    = (val >> 3) & 1;
        apu.dmc.enabled      = (val >> 4) & 1;
        if (!apu.pulse[0].enabled) apu.pulse[0].length = 0;
        if (!apu.pulse[1].enabled) apu.pulse[1].length = 0;
        if (!apu.tri.enabled)      apu.tri.length = 0;
        if (!apu.noise.enabled)    apu.noise.length = 0;
        break;
    /* Frame counter $4017 */
    case 0x4017:
        apu.frame_counter_mode = (val >> 7) & 1;
        apu.frame_irq_inhibit  = (val >> 6) & 1;
        apu.frame_clock = 0;
        /* 5-step: clock immediately */
        if (apu.frame_counter_mode) {
            /* clock half+quarter frame now */
        }
        break;
    }
}

uint8_t apu_read_status(void) {
    return ((apu.pulse[0].length > 0) ? 1 : 0)
         | ((apu.pulse[1].length > 0) ? 2 : 0)
         | ((apu.tri.length      > 0) ? 4 : 0)
         | ((apu.noise.length    > 0) ? 8 : 0);
}

/* =========================================================================
   Envelope clocking
   ========================================================================= */
static void clock_envelope(int ch) {
    if (apu.pulse[ch].envelope_start) {
        apu.pulse[ch].envelope_start   = 0;
        apu.pulse[ch].envelope_decay   = 15;
        apu.pulse[ch].envelope_divider = apu.pulse[ch].volume;
    } else {
        if (apu.pulse[ch].envelope_divider == 0) {
            apu.pulse[ch].envelope_divider = apu.pulse[ch].volume;
            if (apu.pulse[ch].envelope_decay > 0)
                apu.pulse[ch].envelope_decay--;
            else if (apu.pulse[ch].length_halt)
                apu.pulse[ch].envelope_decay = 15;
        } else {
            apu.pulse[ch].envelope_divider--;
        }
    }
}

static void clock_noise_envelope(void) {
    if (apu.noise.envelope_start) {
        apu.noise.envelope_start   = 0;
        apu.noise.envelope_decay   = 15;
        apu.noise.envelope_divider = apu.noise.volume;
    } else {
        if (apu.noise.envelope_divider == 0) {
            apu.noise.envelope_divider = apu.noise.volume;
            if (apu.noise.envelope_decay > 0)
                apu.noise.envelope_decay--;
            else if (apu.noise.length_halt)
                apu.noise.envelope_decay = 15;
        } else {
            apu.noise.envelope_divider--;
        }
    }
}

/* =========================================================================
   Sweep unit for pulse channels
   ========================================================================= */
static void clock_sweep(int ch) {
    /* Compute target period */
    int16_t delta = (int16_t)(apu.pulse[ch].timer_reload >> apu.pulse[ch].sweep_shift);
    if (apu.pulse[ch].sweep_negate) delta = (ch == 0) ? -delta - 1 : -delta;
    uint16_t target = (uint16_t)((int16_t)apu.pulse[ch].timer_reload + delta);

    /* Clock divider */
    if (apu.pulse[ch].sweep_reload) {
        apu.pulse[ch].sweep_reload   = 0;
        apu.pulse[ch].sweep_divider  = apu.pulse[ch].sweep_period;
    } else if (apu.pulse[ch].sweep_divider == 0) {
        apu.pulse[ch].sweep_divider  = apu.pulse[ch].sweep_period;
        /* Update period if enabled and not muting */
        if (apu.pulse[ch].sweep_en &&
            apu.pulse[ch].sweep_shift > 0 &&
            apu.pulse[ch].timer_reload >= 8 &&
            target <= 0x7FF) {
            apu.pulse[ch].timer_reload = target;
        }
    } else {
        apu.pulse[ch].sweep_divider--;
    }
}

/* =========================================================================
   Frame sequencer
   ========================================================================= */
/* 4-step: clocks at CPU cycles 3728, 7456, 11185, 14914 */
/* 5-step: clocks at CPU cycles 3728, 7456, 11185, 14914, 18640 */
static const uint32_t SEQ4[4] = {3729, 7457, 11186, 14915};
static const uint32_t SEQ5[5] = {3729, 7457, 11186, 14915, 18641};

static void clock_quarter_frame(void) {
    clock_envelope(0);
    clock_envelope(1);
    clock_noise_envelope();
    /* Triangle linear counter */
    if (apu.tri.linear_reload)
        apu.tri.linear_counter = apu.tri.linear_reload_val;
    else if (apu.tri.linear_counter > 0)
        apu.tri.linear_counter--;
    if (!apu.tri.control) apu.tri.linear_reload = 0;
}

static void clock_half_frame(void) {
    clock_quarter_frame();
    /* Length counters */
    for (int ch = 0; ch < 2; ch++) {
        if (!apu.pulse[ch].length_halt && apu.pulse[ch].length > 0)
            apu.pulse[ch].length--;
        clock_sweep(ch);
    }
    if (!apu.tri.control && apu.tri.length > 0) apu.tri.length--;
    if (!apu.noise.length_halt && apu.noise.length > 0) apu.noise.length--;
}

/* =========================================================================
   Mixer — NES non-linear mixing
   ========================================================================= */
static float mix_output(void) {
    /* Pulse output */
    uint8_t p0 = 0, p1 = 0;
    if (apu.pulse[0].length > 0 && apu.pulse[0].timer_reload >= 8) {
        uint8_t vol = apu.pulse[0].constant_vol ? apu.pulse[0].volume
                                                 : apu.pulse[0].envelope_decay;
        p0 = DUTY_SEQ[apu.pulse[0].duty][apu.pulse[0].seq_pos & 7] * vol;
    }
    if (apu.pulse[1].length > 0 && apu.pulse[1].timer_reload >= 8) {
        uint8_t vol = apu.pulse[1].constant_vol ? apu.pulse[1].volume
                                                 : apu.pulse[1].envelope_decay;
        p1 = DUTY_SEQ[apu.pulse[1].duty][apu.pulse[1].seq_pos & 7] * vol;
    }

    /* Triangle output */
    uint8_t tri = 0;
    if (apu.tri.length > 0 && apu.tri.linear_counter > 0 && apu.tri.timer_reload >= 2)
        tri = TRI_SEQ[apu.tri.seq_pos & 31];

    /* Noise output */
    uint8_t noise = 0;
    if (apu.noise.length > 0) {
        uint8_t vol = apu.noise.constant_vol ? apu.noise.volume
                                             : apu.noise.envelope_decay;
        if (!(apu.noise.shift_reg & 1)) noise = vol;
    }

    /* DMC output */
    uint8_t dmc = apu.dmc.output;

    /* NES APU non-linear mixer formula (nesdev) */
    float pulse_out = 0.0f;
    if (p0 || p1)
        pulse_out = 95.88f / (8128.0f / (p0 + p1) + 100.0f);

    float tnd_out = 0.0f;
    float tnd = tri / 8227.0f + noise / 12241.0f + dmc / 22638.0f;
    if (tnd > 0.0f)
        tnd_out = 159.79f / (1.0f / tnd + 100.0f);

    return (pulse_out + tnd_out);  /* 0..~1.0 */
}

/* =========================================================================
   APU step — one CPU cycle
   ========================================================================= */
/* Fixed-point sample accumulator: counts CPU cycles * 100 */
/* Sample threshold = CPU_HZ/SAMPLE_HZ * 100 = 1789773/44100*100 ≈ 4058 */
static int32_t sample_frac = 0;
#define SAMPLE_FRAC_STEP  100
#define SAMPLE_FRAC_MAX   4058   /* 1789773*100/44100 */

void apu_step(void) {
    apu.frame_clock++;

    /* Frame sequencer */
    const uint32_t *seq   = apu.frame_counter_mode ? SEQ5 : SEQ4;
    int             steps = apu.frame_counter_mode ? 5 : 4;
    for (int i = 0; i < steps; i++) {
        if (apu.frame_clock == seq[i]) {
            if (i == 1 || i == 3 || (apu.frame_counter_mode && i == 4))
                clock_half_frame();
            else
                clock_quarter_frame();
            break;
        }
    }
    if (apu.frame_clock >= (uint32_t)(apu.frame_counter_mode ? 18642 : 14916))
        apu.frame_clock = 0;

    /* Clock pulse timers (every 2 CPU cycles = every APU cycle) */
    /* Approximate: clock every cycle for simplicity */
    for (int ch = 0; ch < 2; ch++) {
        if (apu.pulse[ch].timer == 0) {
            apu.pulse[ch].timer = apu.pulse[ch].timer_reload;
            if (apu.pulse[ch].timer_reload >= 8)
                apu.pulse[ch].seq_pos = (apu.pulse[ch].seq_pos + 1) & 7;
        } else {
            apu.pulse[ch].timer--;
        }
    }

    /* Triangle — clocked every CPU cycle */
    if (apu.tri.length > 0 && apu.tri.linear_counter > 0) {
        if (apu.tri.timer == 0) {
            apu.tri.timer = apu.tri.timer_reload;
            apu.tri.seq_pos = (apu.tri.seq_pos + 1) & 31;
        } else {
            apu.tri.timer--;
        }
    }

    /* Noise */
    if (apu.noise.timer == 0) {
        apu.noise.timer = apu.noise.timer_reload;
        uint16_t fb = (apu.noise.shift_reg & 1)
                    ^ ((apu.noise.shift_reg >> (apu.noise.mode ? 6 : 1)) & 1);
        apu.noise.shift_reg = (apu.noise.shift_reg >> 1) | (fb << 14);
    } else {
        apu.noise.timer--;
    }

    /* Sample output */
    sample_frac += SAMPLE_FRAC_STEP;
    if (sample_frac >= SAMPLE_FRAC_MAX) {
        sample_frac -= SAMPLE_FRAC_MAX;
        if (apu.sample_count < 4096)
            apu.sample_buf[apu.sample_count++] = mix_output();
    }
}

void apu_fill_buffer(float *buf, int samples) {
    for (int i = 0; i < samples; i++)
        buf[i] = (i < apu.sample_count) ? apu.sample_buf[i] : 0.0f;
    apu.sample_count = 0;
}
