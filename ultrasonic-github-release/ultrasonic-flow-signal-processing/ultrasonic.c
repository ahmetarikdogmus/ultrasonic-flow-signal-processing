/*
 * Ultrasonic flow signal-processing study, portable C99.
 * Independently written example; not extracted from embedded product firmware.
 * All demonstration coefficients are synthetic. NO proprietary calibration,
 * proprietary compensation equation, registers, drivers, or hardware configuration.
 *
 * Processing:
 * validate DIFF -> jump/outlier detection -> median -> illustrative DIFF-to-flow
 * -> interpolated multi-point K calibration -> trimmed mean -> adaptive EMA
 * -> rate limiter -> downward-change confirmation -> valid-only window average.
 *
 * This code intentionally does NOT claim to reproduce the flow compensation
 * stage in a separately published scientific presentation: its equation is
 * not established by the supplied task source. Do not invent that equation.
 */
#include "ultrasonic.h"
#include <math.h>
#include <string.h>

static float absf(float x) { return x < 0.0f ? -x : x; }
static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static float interpolate_gain(const UltrasonicConfig *c, float flow) {
    const UltrasonicCalibrationPoint *t = c->calibration;
    size_t n = c->calibration_count;
    if (flow <= t[0].raw_flow_lps) return t[0].gain;
    if (flow >= t[n-1].raw_flow_lps) return t[n-1].gain;
    for (size_t i = 1; i < n; ++i) {
        if (flow <= t[i].raw_flow_lps) {
            float f = (flow - t[i-1].raw_flow_lps) /
                      (t[i].raw_flow_lps - t[i-1].raw_flow_lps);
            return t[i-1].gain + f * (t[i].gain - t[i-1].gain);
        }
    }
    return t[n-1].gain;
}

static float sorted_median(const float *v, size_t n) {
    float a[ULTRASONIC_MEDIAN_CAPACITY];
    for (size_t i = 0; i < n; ++i) a[i] = v[i];
    for (size_t i = 1; i < n; ++i) {
        float item = a[i];
        size_t j = i;
        while (j > 0 && a[j-1] > item) { a[j] = a[j-1]; --j; }
        a[j] = item;
    }
    return a[n/2];
}

static float trimmed_mean(const float *v, size_t n) {
    float sum = 0.0f, mn = v[0], mx = v[0];
    for (size_t i = 0; i < n; ++i) {
        float x = v[i];
        sum += x;
        if (x < mn) mn = x;
        if (x > mx) mx = x;
    }
    return n >= 5 ? (sum-mn-mx)/(float)(n-2) : sum/(float)n;
}

bool Ultrasonic_Init(UltrasonicProcessor *p, const UltrasonicConfig *c) {
    if (!p || !c || !c->calibration || !c->calibration_count ||
        !isfinite(c->diff_to_flow_lps_per_us) || c->diff_to_flow_lps_per_us <= 0 ||
        !isfinite(c->min_diff_us) || !isfinite(c->max_diff_us) ||
        c->min_diff_us >= c->max_diff_us ||
        !isfinite(c->hard_jump_us) || c->hard_jump_us <= 0 ||
        !isfinite(c->outlier_base_us) || c->outlier_base_us < 0 ||
        !isfinite(c->outlier_scale) || c->outlier_scale < 0 ||
        c->median_length == 0 || c->median_length > ULTRASONIC_MEDIAN_CAPACITY ||
        c->median_length % 2 == 0 ||
        c->trimmed_length == 0 || c->trimmed_length > ULTRASONIC_TRIM_CAPACITY ||
        c->publish_window == 0 || c->publish_window > ULTRASONIC_WINDOW_CAPACITY ||
        c->min_valid_publish == 0 || c->min_valid_publish > c->publish_window ||
        c->invalid_limit == 0 || c->drop_confirm_samples == 0 ||
        !isfinite(c->max_change_lps) || c->max_change_lps <= 0 ||
        !isfinite(c->drop_threshold_lps) || c->drop_threshold_lps < 0 ||
        !isfinite(c->drop_band_lps) || c->drop_band_lps < 0 ||
        !isfinite(c->alpha_low) || !(c->alpha_low > 0 && c->alpha_low <= 1) ||
        !isfinite(c->alpha_mid) || !(c->alpha_mid > 0 && c->alpha_mid <= 1) ||
        !isfinite(c->alpha_high) || !(c->alpha_high > 0 && c->alpha_high <= 1) ||
        !isfinite(c->low_flow_boundary_lps) ||
        !isfinite(c->high_flow_boundary_lps) ||
        c->low_flow_boundary_lps >= c->high_flow_boundary_lps) return false;

    for (size_t i = 0; i < c->calibration_count; ++i) {
        if (!isfinite(c->calibration[i].raw_flow_lps) ||
            !isfinite(c->calibration[i].gain) ||
            c->calibration[i].gain <= 0) return false;
        if (i > 0 && c->calibration[i].raw_flow_lps <=
                     c->calibration[i-1].raw_flow_lps) return false;
    }
    memset(p, 0, sizeof(*p));
    p->cfg = *c;
    return true;
}

void Ultrasonic_Reset(UltrasonicProcessor *p) {
    if (!p) return;
    UltrasonicConfig cfg = p->cfg;
    memset(p, 0, sizeof(*p));
    p->cfg = cfg;
}

const char *Ultrasonic_StatusName(UltrasonicSampleStatus r) {
    switch (r) {
        case ULTRA_ACCEPTED: return "ACCEPTED";
        case ULTRA_REJECT_STATUS: return "STATUS";
        case ULTRA_REJECT_NONFINITE: return "NONFINITE";
        case ULTRA_REJECT_RANGE: return "RANGE";
        case ULTRA_REJECT_JUMP: return "JUMP";
        case ULTRA_REJECT_OUTLIER: return "OUTLIER";
        default: return "UNKNOWN";
    }
}

static void publish_if_ready(UltrasonicProcessor *p, UltrasonicWindowResult *r) {
    if (p->window_processed < p->cfg.publish_window) return;
    ++p->window_number;
    r->ready = true;
    r->window_number = p->window_number;
    r->good_samples = p->window_good;
    r->bad_samples = p->window_bad;
    if (p->window_good >= p->cfg.min_valid_publish) {
        r->flow_lps = p->window_sum / (float)p->window_good;
        r->valid = true;
        p->last_published = r->flow_lps;
        p->has_last_published = true;
    } else {
        r->flow_lps = p->has_last_published ? p->last_published : 0.0f;
        /* Held value is explicitly marked INVALID, never silently fresh data. */
        r->valid = false;
    }
    p->window_processed = p->window_good = p->window_bad = 0;
    p->window_sum = 0.0f;
}

UltrasonicOutput Ultrasonic_Process(UltrasonicProcessor *p, UltrasonicInput in,
                                     UltrasonicWindowResult *window_result) {
    UltrasonicOutput o = {0};
    UltrasonicWindowResult local_window = {0};
    if (!window_result) window_result = &local_window;
    memset(window_result, 0, sizeof(*window_result));
    if (!p) return o;

    const UltrasonicConfig *c = &p->cfg;
    UltrasonicSampleStatus r = ULTRA_ACCEPTED;
    if (!in.measurement_ok) r = ULTRA_REJECT_STATUS;
    else if (!isfinite(in.raw_diff_us)) r = ULTRA_REJECT_NONFINITE;
    else if (in.raw_diff_us < c->min_diff_us || in.raw_diff_us > c->max_diff_us)
        r = ULTRA_REJECT_RANGE;
    else if (p->has_last_diff && p->median_count >= 2U) {
        float step = absf(in.raw_diff_us - p->last_accepted_diff_us);
        if (step > c->hard_jump_us) r = ULTRA_REJECT_JUMP;
        else if (step > c->outlier_base_us +
                         c->outlier_scale * absf(p->last_accepted_diff_us))
            r = ULTRA_REJECT_OUTLIER;
    }

    if (r != ULTRA_ACCEPTED) {
        o.status = r;
        ++p->rejected_total;
        if (p->consecutive_invalid < UINT32_MAX) ++p->consecutive_invalid;
        if (p->consecutive_invalid >= c->invalid_limit) {
            /* Fail invalid, not 'zero flow': freshness is a separate signal. */
            p->has_output = false;
            p->has_ema = false;
            p->pending_drop_count = 0;
            p->median_count = p->median_head = 0;
            p->trim_count = p->trim_head = 0;
            p->has_last_diff = false;
        }
        o.holding_previous = p->has_output;
        o.output_valid = false;
        o.final_flow_lps = p->has_output ? p->last_output : 0.0f;
        o.consecutive_invalid = p->consecutive_invalid;
        ++p->window_bad;
        ++p->window_processed;
        publish_if_ready(p, window_result);
        return o;
    }

    p->consecutive_invalid = 0;
    ++p->accepted_total;
    p->last_accepted_diff_us = in.raw_diff_us;
    p->has_last_diff = true;
    p->median_history[p->median_head] = in.raw_diff_us;
    p->median_head = (p->median_head + 1) % c->median_length;
    if (p->median_count < c->median_length) ++p->median_count;
    float med = sorted_median(p->median_history, p->median_count);
    float qraw = med * c->diff_to_flow_lps_per_us;
    float gain = interpolate_gain(c, qraw);
    float qcal = qraw * gain;

    p->trim_history[p->trim_head] = qcal;
    p->trim_head = (p->trim_head + 1) % c->trimmed_length;
    if (p->trim_count < c->trimmed_length) ++p->trim_count;
    float qtrim = trimmed_mean(p->trim_history, p->trim_count);
    float a = qcal < c->low_flow_boundary_lps ? c->alpha_low :
              qcal < c->high_flow_boundary_lps ? c->alpha_mid : c->alpha_high;
    float candidate_ema = p->has_ema ? p->ema + a * (qtrim-p->ema) : qtrim;
    float limited = p->has_output ?
        p->last_output + clampf(candidate_ema-p->last_output,
                                -c->max_change_lps, c->max_change_lps) : candidate_ema;
    float final_flow = limited;
    bool held = false;

    if (p->has_output && limited < p->last_output - c->drop_threshold_lps &&
        c->drop_confirm_samples > 1) {
        if (!p->pending_drop_count || absf(limited-p->pending_drop) > c->drop_band_lps) {
            p->pending_drop = limited;
            p->pending_drop_count = 1;
        } else ++p->pending_drop_count;
        if (p->pending_drop_count < c->drop_confirm_samples) {
            final_flow = p->last_output;
            held = true;
        } else p->pending_drop_count = 0;
    } else p->pending_drop_count = 0;

    /* Do not advance EMA when the drop-confirm output was held. */
    if (!held) { p->ema = candidate_ema; p->has_ema = true; }
    p->last_output = final_flow;
    p->has_output = true;

    o.status = ULTRA_ACCEPTED;
    o.accepted = true;
    o.output_valid = true;
    o.holding_previous = held;
    o.median_diff_us = med;
    o.raw_flow_lps = qraw;
    o.calibration_gain = gain;
    o.calibrated_flow_lps = qcal;
    o.trimmed_flow_lps = qtrim;
    o.ema_flow_lps = p->ema;
    o.final_flow_lps = final_flow;
    ++p->window_good;
    ++p->window_processed;
    p->window_sum += final_flow;
    publish_if_ready(p, window_result);
    return o;
}

#ifdef ULTRASONIC_DEMO
#include <stdio.h>
int main(void) {
    /* Synthetic, illustrative numbers. Not measured product data. */
    static const UltrasonicCalibrationPoint calibration[] = {
        {0.0f, 0.96f}, {5.0f, 0.98f}, {10.0f, 1.02f}, {15.0f, 1.01f}
    };
    UltrasonicConfig cfg = {
        .calibration = calibration, .calibration_count = 4,
        .diff_to_flow_lps_per_us = 40.0f,
        .min_diff_us = 0.0f, .max_diff_us = 0.5f,
        .hard_jump_us = 0.14f, .outlier_base_us = 0.06f,
        .outlier_scale = 0.25f,
        .median_length = 5, .trimmed_length = 7,
        .alpha_low = 0.25f, .alpha_mid = 0.35f, .alpha_high = 0.45f,
        .low_flow_boundary_lps = 5.0f, .high_flow_boundary_lps = 10.0f,
        .max_change_lps = 1.5f, .drop_threshold_lps = 0.6f,
        .drop_band_lps = 0.5f, .drop_confirm_samples = 3,
        .invalid_limit = 6, .publish_window = 5, .min_valid_publish = 3
    };
    UltrasonicProcessor p;
    if (!Ultrasonic_Init(&p, &cfg)) return 1;
    const UltrasonicInput samples[] = {
        {0.200f,true}, {0.204f,true}, {0.202f,true},
        {0.205f,true}, {0.450f,true}, {0.201f,true},
        {0.198f,true}, {0.185f,true}, {0.184f,true},
        {0.182f,true}, {0.180f,true}, {0.000f,false},
        {0.180f,true}, {0.186f,true}, {0.183f,true}
    };
    puts("sample,diff_us,status,raw_lps,calibrated_lps,trimmed_lps,ema_lps,final_lps,output_valid");
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); ++i) {
        UltrasonicWindowResult w;
        UltrasonicOutput o = Ultrasonic_Process(&p, samples[i], &w);
        printf("%zu,%.3f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%u\n", i+1,
            samples[i].raw_diff_us, Ultrasonic_StatusName(o.status),
            o.raw_flow_lps, o.calibrated_flow_lps, o.trimmed_flow_lps,
            o.ema_flow_lps, o.final_flow_lps, (unsigned)o.output_valid);
        if (w.ready) printf("# window %u: flow=%.3f L/s good=%zu bad=%zu valid=%u\n",
            w.window_number, w.flow_lps, w.good_samples, w.bad_samples, (unsigned)w.valid);
    }
    return 0;
}
#endif
