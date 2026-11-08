#ifndef ULTRASONIC_H
#define ULTRASONIC_H

/* Independent educational DSP implementation. Not production firmware. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ULTRASONIC_MEDIAN_CAPACITY 9U
#define ULTRASONIC_TRIM_CAPACITY 15U
#define ULTRASONIC_WINDOW_CAPACITY 128U

typedef struct {
    float raw_flow_lps;
    float gain;
} UltrasonicCalibrationPoint;

typedef enum {
    ULTRA_ACCEPTED = 0,
    ULTRA_REJECT_STATUS,
    ULTRA_REJECT_NONFINITE,
    ULTRA_REJECT_RANGE,
    ULTRA_REJECT_JUMP,
    ULTRA_REJECT_OUTLIER
} UltrasonicSampleStatus;

typedef struct {
    /* Array is caller-owned; it must remain alive during processing. */
    const UltrasonicCalibrationPoint *calibration;
    size_t calibration_count;

    /* Input DIFF is in microseconds. Conversion is illustrative, not physical hardware calibration. */
    float diff_to_flow_lps_per_us;
    float min_diff_us;
    float max_diff_us;
    float hard_jump_us;
    float outlier_base_us;
    float outlier_scale;
    size_t median_length;        /* odd: 1, 3, 5, 7, 9 */
    size_t trimmed_length;       /* 1..15; min/max removed once length >= 5 */
    float alpha_low;
    float alpha_mid;
    float alpha_high;
    float low_flow_boundary_lps;
    float high_flow_boundary_lps;
    float max_change_lps;       /* max allowed output step per ACCEPTED sample */
    float drop_threshold_lps;
    float drop_band_lps;
    unsigned drop_confirm_samples;
    unsigned invalid_limit;     /* rejected consecutive samples before invalidation */
    size_t publish_window;      /* processed samples per published estimate */
    size_t min_valid_publish;   /* minimum accepted samples for fresh publication */
} UltrasonicConfig;

typedef struct {
    float raw_diff_us;
    bool measurement_ok;
} UltrasonicInput;

typedef struct {
    UltrasonicSampleStatus status;
    bool accepted;
    bool holding_previous;
    bool output_valid;
    float median_diff_us;
    float raw_flow_lps;
    float calibration_gain;
    float calibrated_flow_lps;
    float trimmed_flow_lps;
    float ema_flow_lps;
    float final_flow_lps;
    unsigned consecutive_invalid;
} UltrasonicOutput;

typedef struct {
    bool ready;
    bool valid;
    float flow_lps;
    size_t good_samples;
    size_t bad_samples;
    uint32_t window_number;
} UltrasonicWindowResult;

typedef struct {
    UltrasonicConfig cfg;
    float median_history[ULTRASONIC_MEDIAN_CAPACITY];
    size_t median_count, median_head;
    float trim_history[ULTRASONIC_TRIM_CAPACITY];
    size_t trim_count, trim_head;
    float last_accepted_diff_us;
    bool has_last_diff;
    float ema, last_output;
    bool has_ema, has_output;
    float pending_drop;
    unsigned pending_drop_count;
    unsigned consecutive_invalid;
    float window_sum;
    size_t window_good, window_bad, window_processed;
    uint32_t window_number, accepted_total, rejected_total;
    float last_published;
    bool has_last_published;
} UltrasonicProcessor;

bool Ultrasonic_Init(UltrasonicProcessor *p, const UltrasonicConfig *cfg);
void Ultrasonic_Reset(UltrasonicProcessor *p);
UltrasonicOutput Ultrasonic_Process(UltrasonicProcessor *p, UltrasonicInput in,
                                     UltrasonicWindowResult *window_result);
const char *Ultrasonic_StatusName(UltrasonicSampleStatus status);

#ifdef __cplusplus
}
#endif
#endif
