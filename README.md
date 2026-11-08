# Ultrasonic Flow Measurement & Signal Processing

**Multi-Point Calibration · Flow Compensation · Measurement Averaging · Digital Filtering**

A hardware-independent engineering study of the signal-processing methods used to obtain **accurate, stable and responsive flow estimates** from ultrasonic time-of-flight measurements.

> **Project scope:** This repository documents general algorithms and an independently developed demonstration. It does not contain company firmware, proprietary calibration values, hardware-specific implementations or production test data.

## Overview

Transit-time ultrasonic measurement estimates flow from the difference between acoustic propagation times in opposite directions. Converting this small time difference into a useful flow value requires more than a single equation: systematic error must be corrected, residual measurement effects considered, and noisy samples processed without masking real changes in flow.

The approach in this repository emphasizes three complementary stages:

| Core stage | Primary purpose | Key engineering question |
| --- | --- | --- |
| **01 — Multi-Point Calibration** | Reduce flow-dependent systematic error | How can correction vary across the measurement range? |
| **02 — Flow Compensation** | Address residual, model-dependent measurement effects | Which physical or signal-dependent influences need further correction? |
| **03 — Measurement Averaging** | Reduce noise in the reported output | How can stability improve without excessive response delay? |

Measurement validation and digital filters support these stages, rather than replacing them.

## Measurement Principle

Let \(t_{up}\) and \(t_{down}\) represent acoustic transit times against and along the flow, respectively. A common sign convention defines:

\[
\Delta t = t_{up} - t_{down}
\]

For acoustic path length \(L\), speed of sound \(c\), and flow component \(v\cos\theta\), the idealized relationships are:

\[
t_{up}=\frac{L}{c-v\cos\theta},\qquad
 t_{down}=\frac{L}{c+v\cos\theta}
\]

The time difference can be related to fluid velocity and, with the appropriate geometry and velocity-profile assumptions, volumetric flow rate. The **exact conversion is system-dependent**; this repository does not claim to publish a particular device's physical model.

## Processing Architecture

```text
Upstream / Downstream Time-of-Flight
                |
                v
       Time Difference (Δt)
                |
                v
      Measurement Validation
       + Noise / Spike Checks
                |
                v
       Raw Flow Estimation
                |
                v
  01  MULTI-POINT CALIBRATION
      Flow-dependent correction
                |
                v
  02  FLOW COMPENSATION
      Additional model-based correction
                |
                v
  03  MEASUREMENT AVERAGING
      Accepted-sample / robust averaging
                |
                v
      Output Stabilization
      EMA / Rate / Drop Checks
                |
                v
       Reported Flow Estimate
```

*Conceptual sequence for this independent demonstration; not a claim about the precise execution order of any proprietary firmware.*

## 01 — Multi-Point Calibration

A single multiplier often cannot represent measurement bias over the full flow range. **Multi-point calibration** uses reference measurements at several operating points to establish a flow-dependent correction relationship.

### Method

1. Acquire representative raw flow estimates at multiple known reference values.
2. Estimate a correction coefficient for each selected point.
3. Store ordered raw-flow / coefficient pairs in a calibration table.
4. Interpolate a coefficient between adjacent calibration points.
5. Apply it to the current flow estimate and evaluate points *between* the references.

A generalized multiplicative model is:

\[
Q_{cal}=Q_{raw}\,K(Q_{raw})
\]

An initial estimate of a point coefficient is \(K_i=Q_{ref,i}/Q_{raw,i}\), when \(Q_{raw,i}\ne0\). For neighboring calibration nodes \((Q_1,K_1)\) and \((Q_2,K_2)\), linear interpolation gives:

\[
K(Q)=K_1+\frac{Q-Q_1}{Q_2-Q_1}(K_2-K_1)
\]

This reduces discontinuities between correction points. Behavior beyond the calibration range must also be defined and tested rather than assumed.

**What calibration does not solve:** It cannot, by itself, remove short-lived noise spikes or guarantee a fast dynamic response.

## 02 — Flow Compensation

**Flow compensation** is the additional evaluation and correction of effects that a reference-point calibration model alone may not capture. Depending on the system, these can be associated with measurement physics, operating conditions or residual model bias.

A general way to express an *optional* compensation stage is:

\[
Q_{comp}=f(Q_{cal},\,x)
\]

Here \(x\) represents any **independently measured and validated** influence used by the compensation model. This is a conceptual interface, **not a verified production equation**.

Good compensation design requires identifying the influencing variable, collecting appropriate reference data, validating the correction independently and checking that the new stage does not amplify noise or introduce discontinuities.

> **Implementation status:** The supplied task-level firmware shows multi-point gain calibration and several averaging/filter stages, but does not establish a separate flow-compensation function or equation. Any future public compensation example will therefore be explicitly labeled as an independent illustrative model.

## 03 — Measurement Averaging

Even when the mean flow is constant, successive ultrasonic measurements can fluctuate. Averaging improves repeatability, but the number of samples and acceptance rules determine how much response delay is introduced.

### Accepted-sample arithmetic mean

Only accepted measurements should contribute to a normal measurement average:

\[
\bar{Q}=\frac{1}{N_{valid}}\sum_{i\in valid}Q_i
\]

Invalid measurements should **not** silently count as zero flow. A reporting window can also track accepted/rejected counts and apply an explicit fallback when too few valid samples exist.

### Trimmed mean

A robust average can remove extreme accepted values before computing the mean. With the illustrative sequence `7.9, 8.0, 8.1, 8.0, 12.5, 7.8, 8.2 L/s`, dropping the minimum and maximum gives **8.04 L/s**. This example is synthetic.

### Temporal smoothing

An exponential moving average can further smooth accepted estimates:

\[
EMA_n=EMA_{n-1}+\alpha(Q_n-EMA_{n-1}),\qquad 0<\alpha\le1
\]

Smaller \(\alpha\) increases smoothing but slows response; larger \(\alpha\) follows changes faster but retains more fluctuation. In an adaptive approach, \(\alpha\) can vary with operating conditions, provided its selection is tested.

**Design trade-off:** Improved steady-state stability must be checked against rising/falling step response, startup behavior and long periods of invalid measurements.

## Supporting Validation & Filtering

These mechanisms protect the primary correction stages from unsuitable inputs and abrupt output artifacts:

| Method | Purpose | Risk if over-applied |
| --- | --- | --- |
| Validity and range checks | Exclude invalid or implausible input | Reject legitimate edge cases |
| Sudden-jump detection | Detect discontinuities between measurements | Delay real fast transitions |
| Adaptive outlier rejection | Apply a tolerance informed by measurement context | Freeze tracking if thresholds are too strict |
| Median filtering | Suppress isolated spikes | Add group delay |
| EMA smoothing | Reduce residual short-term fluctuation | Slow response |
| Rate limiting | Bound change per processing step | Distort legitimate steps |
| Drop confirmation | Require persistent evidence before a major decrease | Delay genuine flow reductions |
| Last-valid-value hold | Avoid artificial zeros during brief invalid readings | Conceal sustained sensor failure unless bounded |

All filter states, initialization rules and prolonged-failure behavior must be defined explicitly in a real implementation.

## Validation Plan

An algorithm should be assessed in **both steady-state and dynamic conditions**:

| Test | What to evaluate |
| --- | --- |
| Multiple reference flow points | Calibration error across the range and between nodes |
| Constant flow with noise | Bias, standard deviation, accepted-sample ratio |
| Isolated spikes | Outlier rejection without distorting the mean |
| Gradual changes | Tracking and lag |
| Step increase / decrease | Response delay and overshoot |
| Zero flow | False positive flow and recovery |
| Short / prolonged invalid input | Hold, timeout, reset and recovery policy |

Suggested metrics include **MAE, signed bias, standard deviation, valid-sample ratio and step-response time**. Relative percentage errors should not be divided by a zero-flow reference.

**Data integrity:** Future examples and graphs will use clearly labeled *synthetic* measurements. No accuracy percentage or performance improvement is claimed here without a reproducible test result.

## Repository Layout

The planned public implementation will intentionally remain simple:

```text
ultrasonic-flow-signal-processing/
├── README.md
├── ultrasonic.c       # Independent example implementation (planned)
├── ultrasonic.h       # Public configuration / API (planned)
└── figures/           # Illustrative plots (added separately)
```

The two C files will contain the algorithm example without proprietary driver code, RTOS integration, production constants or calibration tables. **They are not included in this README-only stage.**

## Scope & Attribution

This is a personal, independently documented engineering demonstration of general ultrasonic flow-processing techniques. It is not an official product, an original product repository or a release of an employer's firmware. Any future public source code or figures must be independently prepared and reviewed for sharing permissions.

---

**Topics:** Embedded C · Ultrasonic Time-of-Flight · Multi-Point Calibration · Flow Compensation · Robust Averaging · Digital Signal Processing
