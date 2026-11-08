#ifndef ULTRASONIC_H
#define ULTRASONIC_H

#include <stdbool.h>
#include <stddef.h>

typedef struct { float raw_flow; float gain; } UltrasonicCalibrationPoint;
typedef struct { float flow; float residual; } UltrasonicCompensationPoint;

typedef struct {
    const UltrasonicCalibrationPoint *calibration;
    size_t calibration_count;
    const UltrasonicCompensationPoint *compensation;
    size_t compensation_count;
    float ema_alpha;       /* 0 < alpha <= 1 */
    size_t window_size;    /* 1..ULTRASONIC_WINDOW_CAPACITY */
} UltrasonicConfig;

#define ULTRASONIC_WINDOW_CAPACITY 32U

typedef struct {
    UltrasonicConfig config;
    float window[ULTRASONIC_WINDOW_CAPACITY];
    size_t count, write_index;
    bool has_ema;
    float ema;
    unsigned long accepted, rejected;
} UltrasonicProcessor;

typedef struct {
    bool valid;
    float raw, calibrated, compensated, averaged;
    size_t window_samples;
} UltrasonicOutput;

bool Ultrasonic_Init(UltrasonicProcessor *processor, const UltrasonicConfig *config);
UltrasonicOutput Ultrasonic_Process(UltrasonicProcessor *processor, float raw_flow, bool valid);
void Ultrasonic_Reset(UltrasonicProcessor *processor);

#endif
