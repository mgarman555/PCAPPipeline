#pragma once

#include "CoreMinimal.h"

// ─────────────────────────────────────────────────────────────────────────────
// Offline curve smoothing for a recorded VCam take — a native rebuild of the 4.26
// VcamSequencer post-pass (vendored DSPFilters Butterworth + one-pole slerp).
//
// Faithful to the original's parameters: 6th-order Butterworth low-pass, default
// cutoff 2 Hz, run at the resampling fps (default 60), CAUSAL (forward-only) with the
// group delay compensated by shifting keys earlier (GroupDelayAdvanceSamples) at the
// keying stage. Only the transform is smoothed (location + rotation); focal length /
// focus / aperture are left untouched, exactly as in 4.26. Default method is None
// (opt-in, non-destructive — the caller keeps the originals and can re-run).
//
// PURE (CoreMinimal math only) → host-unit-testable. The MovieScene glue (resample the
// take's double channels to fps, call these, shift keys by the advance, re-key) lives
// in the editor subsystem.
// ─────────────────────────────────────────────────────────────────────────────

enum class EVCamSmoothMethod : uint8
{
    None          = 0,   // leave the recorded curve as-is (non-destructive default)
    LowPassFilter = 1,   // 6th-order Butterworth low-pass
    Slerp         = 2,   // rotation only: one-pole quaternion slerp
};

struct FVCamSmoothingSettings
{
    EVCamSmoothMethod TranslationMethod = EVCamSmoothMethod::None;
    EVCamSmoothMethod RotationMethod    = EVCamSmoothMethod::None;   // None / LowPassFilter / Slerp

    float ResamplingFps       = 60.f;   // samples are assumed uniform at this rate
    float TranslationCutoffHz = 2.0f;
    float RotationCutoffHz    = 2.0f;
    float RotationSlerpBlend  = 0.8f;   // one-pole slerp: higher = follows input faster = less smoothing
};

class PCAPTOOL_API FVCamCurveSmoothing
{
public:
    // 6th-order causal Butterworth low-pass (cascade of 3 Butterworth biquads) with warm-up
    // padding, in place. Causal → introduces group delay; compensate at keying time with
    // GroupDelayAdvanceSamples. No-op for <8 samples, non-positive rate/cutoff, or cutoff>=Nyquist.
    static void ButterworthLowPass(TArray<float>& Data, float CutoffHz, float SampleRateHz);

    // Integer key-shift (in samples at SampleRateHz) that compensates the low-pass group delay,
    // matching VcamSequencer's advanceTime = floor(1.25 * fps / cutoff / 2).
    static int32 GroupDelayAdvanceSamples(float CutoffHz, float SampleRateHz);

    // Smooth a recorded position curve per TranslationMethod (None or LowPassFilter).
    static void SmoothPositions(TArray<FVector>& InOut, const FVCamSmoothingSettings& Settings);

    // Smooth a recorded rotation curve per RotationMethod:
    //  • LowPassFilter — unwound per-Euler-channel Butterworth (matches 4.26 default LPF path),
    //  • Slerp         — one-pole quaternion slerp (q = Slerp(q, sample, blend)).
    static void SmoothRotations(TArray<FQuat>& InOut, const FVCamSmoothingSettings& Settings);
};
