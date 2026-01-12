/*
 * Space Echo Audio FX Plugin
 *
 * RE-201 style tape delay with:
 * - Time: Delay time from 50ms to 800ms
 * - Feedback: Echo repeats with soft saturation (max 95%)
 * - Mix: Dry/wet blend
 * - Tone: High-frequency rolloff on repeats (1kHz to 8kHz)
 * - Flutter: Tape wow/flutter pitch modulation (~5Hz LFO)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "audio_fx_api_v1.h"

#define SAMPLE_RATE 44100

/* Delay line size: ~800ms at 44100Hz plus some margin for flutter */
#define DELAY_SIZE 36000

/* Plugin state */
static const host_api_v1_t *g_host = NULL;
static audio_fx_api_v1_t g_fx_api;

/* Parameters (0.0 to 1.0) */
static float g_time = 0.4f;      /* Delay time: 50ms to 800ms */
static float g_feedback = 0.4f;  /* Feedback amount (max 95%) */
static float g_mix = 0.35f;      /* Dry/wet mix */
static float g_tone = 0.6f;      /* Tone filter: 1kHz to 8kHz */
static float g_flutter = 0.15f;  /* Flutter depth: 0 to ~2ms */

/* Delay line (stereo) */
static float g_delay_l[DELAY_SIZE];
static float g_delay_r[DELAY_SIZE];
static int g_write_idx = 0;

/* Tone filter state (one-pole lowpass on feedback path) */
static float g_filter_l = 0.0f;
static float g_filter_r = 0.0f;

/* Flutter LFO state */
static float g_lfo_phase = 0.0f;

/* Logging helper */
static void fx_log(const char *msg) {
    if (g_host && g_host->log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "[spacecho] %s", msg);
        g_host->log(buf);
    }
}

/* Linear interpolation for fractional delay read */
static inline float delay_read_interp(float *delay, int write_idx, float delay_samples) {
    /* Calculate fractional read position */
    float read_pos = (float)write_idx - delay_samples;

    /* Wrap to buffer bounds */
    while (read_pos < 0.0f) read_pos += DELAY_SIZE;
    while (read_pos >= DELAY_SIZE) read_pos -= DELAY_SIZE;

    /* Integer and fractional parts */
    int i0 = (int)read_pos;
    int i1 = i0 + 1;
    if (i1 >= DELAY_SIZE) i1 = 0;

    float frac = read_pos - (float)i0;

    /* Linear interpolation */
    return delay[i0] * (1.0f - frac) + delay[i1] * frac;
}

/* === Audio FX API Implementation === */

static int fx_on_load(const char *module_dir, const char *config_json) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Space Echo loading from: %s", module_dir);
    fx_log(msg);

    /* Clear delay lines */
    memset(g_delay_l, 0, sizeof(g_delay_l));
    memset(g_delay_r, 0, sizeof(g_delay_r));
    g_write_idx = 0;

    /* Reset filter state */
    g_filter_l = 0.0f;
    g_filter_r = 0.0f;

    /* Reset LFO phase */
    g_lfo_phase = 0.0f;

    fx_log("Space Echo initialized");
    return 0;
}

static void fx_on_unload(void) {
    fx_log("Space Echo unloading");
}

static void fx_process_block(int16_t *audio_inout, int frames) {
    const float dt = 1.0f / SAMPLE_RATE;

    /* Calculate delay time in samples */
    /* time=0 -> 50ms (2205 samples), time=1 -> 800ms (35280 samples) */
    float delay_ms = 50.0f + g_time * 750.0f;
    float base_delay_samples = delay_ms * (SAMPLE_RATE / 1000.0f);

    /* Flutter LFO parameters (~5Hz) */
    const float lfo_freq = 5.0f;
    /* Flutter depth: 0 to ~2ms (88 samples at 44100Hz) */
    float flutter_depth = g_flutter * 88.0f;

    /* Tone filter coefficient */
    /* Cutoff from 1kHz (tone=0) to 8kHz (tone=1) */
    float freq = 1000.0f + g_tone * 7000.0f;
    float rc = 1.0f / (2.0f * M_PI * freq);
    float alpha = dt / (rc + dt);

    /* Feedback amount (max 95% to prevent runaway) */
    float feedback = g_feedback * 0.95f;

    for (int i = 0; i < frames; i++) {
        /* Convert input to float (-1.0 to 1.0) */
        float in_l = audio_inout[i * 2] / 32768.0f;
        float in_r = audio_inout[i * 2 + 1] / 32768.0f;

        /* Calculate flutter modulation */
        float flutter_mod = sinf(g_lfo_phase * 2.0f * M_PI) * flutter_depth;

        /* Update LFO phase */
        g_lfo_phase += lfo_freq * dt;
        if (g_lfo_phase >= 1.0f) g_lfo_phase -= 1.0f;

        /* Calculate modulated delay time */
        float delay_samples = base_delay_samples + flutter_mod;

        /* Clamp delay to valid range */
        if (delay_samples < 1.0f) delay_samples = 1.0f;
        if (delay_samples > DELAY_SIZE - 2) delay_samples = DELAY_SIZE - 2;

        /* Read from delay line with interpolation */
        float delayed_l = delay_read_interp(g_delay_l, g_write_idx, delay_samples);
        float delayed_r = delay_read_interp(g_delay_r, g_write_idx, delay_samples);

        /* Apply tone filter to delayed signal (simulates tape high-freq loss) */
        g_filter_l = g_filter_l + alpha * (delayed_l - g_filter_l);
        g_filter_r = g_filter_r + alpha * (delayed_r - g_filter_r);

        float filtered_l = g_filter_l;
        float filtered_r = g_filter_r;

        /* Mix dry and wet signals */
        float out_l = in_l * (1.0f - g_mix) + filtered_l * g_mix;
        float out_r = in_r * (1.0f - g_mix) + filtered_r * g_mix;

        /* Calculate feedback signal with soft saturation (tape compression) */
        float fb_l = tanhf(filtered_l * feedback * 1.2f);
        float fb_r = tanhf(filtered_r * feedback * 1.2f);

        /* Write to delay line: input + saturated feedback */
        g_delay_l[g_write_idx] = in_l + fb_l;
        g_delay_r[g_write_idx] = in_r + fb_r;

        /* Advance write position */
        g_write_idx++;
        if (g_write_idx >= DELAY_SIZE) g_write_idx = 0;

        /* Clamp output and convert back to int16 */
        if (out_l > 1.0f) out_l = 1.0f;
        if (out_l < -1.0f) out_l = -1.0f;
        if (out_r > 1.0f) out_r = 1.0f;
        if (out_r < -1.0f) out_r = -1.0f;

        audio_inout[i * 2] = (int16_t)(out_l * 32767.0f);
        audio_inout[i * 2 + 1] = (int16_t)(out_r * 32767.0f);
    }
}

static void fx_set_param(const char *key, const char *val) {
    float v = atof(val);

    /* Clamp to 0.0-1.0 range */
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    if (strcmp(key, "time") == 0) {
        g_time = v;
    } else if (strcmp(key, "feedback") == 0) {
        g_feedback = v;
    } else if (strcmp(key, "mix") == 0) {
        g_mix = v;
    } else if (strcmp(key, "tone") == 0) {
        g_tone = v;
    } else if (strcmp(key, "flutter") == 0) {
        g_flutter = v;
    }
}

static int fx_get_param(const char *key, char *buf, int buf_len) {
    if (strcmp(key, "time") == 0) {
        return snprintf(buf, buf_len, "%.2f", g_time);
    } else if (strcmp(key, "feedback") == 0) {
        return snprintf(buf, buf_len, "%.2f", g_feedback);
    } else if (strcmp(key, "mix") == 0) {
        return snprintf(buf, buf_len, "%.2f", g_mix);
    } else if (strcmp(key, "tone") == 0) {
        return snprintf(buf, buf_len, "%.2f", g_tone);
    } else if (strcmp(key, "flutter") == 0) {
        return snprintf(buf, buf_len, "%.2f", g_flutter);
    } else if (strcmp(key, "name") == 0) {
        return snprintf(buf, buf_len, "Space Echo");
    }
    return -1;
}

/* === Entry Point === */

audio_fx_api_v1_t* move_audio_fx_init_v1(const host_api_v1_t *host) {
    g_host = host;

    memset(&g_fx_api, 0, sizeof(g_fx_api));
    g_fx_api.api_version = AUDIO_FX_API_VERSION;
    g_fx_api.on_load = fx_on_load;
    g_fx_api.on_unload = fx_on_unload;
    g_fx_api.process_block = fx_process_block;
    g_fx_api.set_param = fx_set_param;
    g_fx_api.get_param = fx_get_param;

    fx_log("Space Echo plugin initialized");

    return &g_fx_api;
}
