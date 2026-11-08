/* Independent educational demo. Not copied from product firmware. */
#include "ultrasonic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float interpolate_gain(const UltrasonicCalibrationPoint *pts, size_t n, float x) {
    if (x <= pts[0].raw_flow) return pts[0].gain;
    if (x >= pts[n-1].raw_flow) return pts[n-1].gain;
    for (size_t i=0; i+1<n; ++i) {
        if (x <= pts[i+1].raw_flow) {
            float f=(x-pts[i].raw_flow)/(pts[i+1].raw_flow-pts[i].raw_flow);
            return pts[i].gain+f*(pts[i+1].gain-pts[i].gain);
        }
    }
    return pts[n-1].gain;
}

/* Example residual correction function. It is NOT a production compensation model. */
static float interpolate_residual(const UltrasonicCompensationPoint *pts, size_t n, float x) {
    if (!pts || !n) return 0.0f;
    if (x <= pts[0].flow) return pts[0].residual;
    if (x >= pts[n-1].flow) return pts[n-1].residual;
    for (size_t i=0; i+1<n; ++i) {
        if (x <= pts[i+1].flow) {
            float f=(x-pts[i].flow)/(pts[i+1].flow-pts[i].flow);
            return pts[i].residual+f*(pts[i+1].residual-pts[i].residual);
        }
    }
    return 0.0f;
}

bool Ultrasonic_Init(UltrasonicProcessor *p, const UltrasonicConfig *c) {
    if (!p || !c || !c->calibration || c->calibration_count==0 ||
        c->window_size==0 || c->window_size>ULTRASONIC_WINDOW_CAPACITY ||
        !(c->ema_alpha>0.0f && c->ema_alpha<=1.0f) ||
        (c->compensation_count>0 && !c->compensation)) return false;
    for (size_t i=1;i<c->calibration_count;i++)
        if (!(c->calibration[i].raw_flow>c->calibration[i-1].raw_flow)) return false;
    for (size_t i=1;i<c->compensation_count;i++)
        if (!(c->compensation[i].flow>c->compensation[i-1].flow)) return false;
    memset(p,0,sizeof(*p)); p->config=*c; return true;
}

void Ultrasonic_Reset(UltrasonicProcessor *p) {
    if (!p) return;
    UltrasonicConfig cfg=p->config;
    memset(p,0,sizeof(*p)); p->config=cfg;
}

UltrasonicOutput Ultrasonic_Process(UltrasonicProcessor *p, float raw, bool valid) {
    UltrasonicOutput o={0};
    if (!p) return o;
    if (!valid || !isfinite(raw) || raw<0.0f) { p->rejected++; return o; }
    o.valid=true; o.raw=raw;
    o.calibrated=raw*interpolate_gain(p->config.calibration,p->config.calibration_count,raw);
    o.compensated=o.calibrated+interpolate_residual(p->config.compensation,p->config.compensation_count,o.calibrated);
    if (o.compensated<0) o.compensated=0;
    p->window[p->write_index]=o.compensated;
    p->write_index=(p->write_index+1)%p->config.window_size;
    if (p->count<p->config.window_size) p->count++;
    float sum=0;
    for(size_t i=0;i<p->count;i++) sum+=p->window[i];
    float mean=sum/(float)p->count;
    if (!p->has_ema) { p->ema=mean; p->has_ema=true; }
    else p->ema += p->config.ema_alpha*(mean-p->ema);
    o.averaged=p->ema; o.window_samples=p->count; p->accepted++;
    return o;
}

#ifdef ULTRASONIC_DEMO
/* Synthetic coefficients, unrelated to any product calibration or compensation. */
int main(void) {
    const UltrasonicCalibrationPoint cal[]={{0.0f,0.92f},{5.0f,0.95f},{10.0f,0.98f}};
    const UltrasonicCompensationPoint comp[]={{0.0f,0.0f},{5.0f,0.08f},{10.0f,0.0f}};
    const UltrasonicConfig cfg={cal,3,comp,3,0.35f,4};
    UltrasonicProcessor proc;
    if (!Ultrasonic_Init(&proc,&cfg)) return 1;
    const float samples[]={4.2f,4.4f,4.3f,4.5f,8.3f,8.2f,8.1f};
    puts("raw,calibrated,compensated,averaged");
    for(size_t i=0;i<sizeof(samples)/sizeof(samples[0]);i++){
        UltrasonicOutput o=Ultrasonic_Process(&proc,samples[i],true);
        printf("%.3f,%.3f,%.3f,%.3f\n",o.raw,o.calibrated,o.compensated,o.averaged);
    }
    return 0;
}
#endif
