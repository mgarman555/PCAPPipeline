#include "VCamCurveSmoothing.h"

namespace
{
    // 6th-order Butterworth = cascade of three 2nd-order sections with these pole Q's
    // (Q_k = 1 / (2 cos(pi(2k+1)/12)) for k=0,1,2).
    constexpr float ButterQ6[3] = { 0.51763809f, 0.70710678f, 1.93185165f };

    // One causal forward pass of a 2nd-order low-pass biquad (Direct Form I), state seeded
    // from the first sample to suppress the startup transient.
    void BiquadForward(TArray<float>& X, float CutoffHz, float Fs, float Q)
    {
        const int32 N = X.Num();
        if (N == 0) { return; }
        const float w0    = 2.f * PI * CutoffHz / Fs;
        const float cosw0 = FMath::Cos(w0);
        const float sinw0 = FMath::Sin(w0);
        const float alpha = sinw0 / (2.f * Q);
        const float a0    = 1.f + alpha;
        const float b0 = ((1.f - cosw0) * 0.5f) / a0;
        const float b1 = (1.f - cosw0)          / a0;
        const float b2 = b0;
        const float a1 = (-2.f * cosw0)         / a0;
        const float a2 = (1.f - alpha)          / a0;

        float x1 = X[0], x2 = X[0], y1 = X[0], y2 = X[0];
        for (int32 i = 0; i < N; ++i)
        {
            const float x0 = X[i];
            const float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x0;
            y2 = y1; y1 = y0;
            X[i] = y0;
        }
    }

    // Make a degree channel continuous so the filter doesn't see ±360 wrap jumps.
    void Unwrap(TArray<float>& V)
    {
        for (int32 i = 1; i < V.Num(); ++i)
        {
            while (V[i] - V[i - 1] >  180.f) { V[i] -= 360.f; }
            while (V[i] - V[i - 1] < -180.f) { V[i] += 360.f; }
        }
    }
}

void FVCamCurveSmoothing::ButterworthLowPass(TArray<float>& Data, float CutoffHz, float SampleRateHz)
{
    const int32 N = Data.Num();
    if (N < 8 || SampleRateHz <= 0.f || CutoffHz <= 0.f) { return; }
    if (CutoffHz >= 0.5f * SampleRateHz) { return; }

    // Warm-up padding = ~10 time-constants of the first value (matches VcamSequencer:
    // 10 * (1/cutoff) seconds), capped so a tiny cutoff can't allocate unbounded.
    const int32 Pad = FMath::Clamp((int32)(10.f * SampleRateHz / CutoffHz + 0.5f), 0, 4096);

    TArray<float> Buf; Buf.SetNum(Pad + N);
    for (int32 i = 0; i < Pad; ++i) { Buf[i] = Data[0]; }
    for (int32 i = 0; i < N;   ++i) { Buf[Pad + i] = Data[i]; }

    for (int32 s = 0; s < 3; ++s) { BiquadForward(Buf, CutoffHz, SampleRateHz, ButterQ6[s]); }

    for (int32 i = 0; i < N; ++i) { Data[i] = Buf[Pad + i]; }
}

int32 FVCamCurveSmoothing::GroupDelayAdvanceSamples(float CutoffHz, float SampleRateHz)
{
    if (CutoffHz <= 0.f || SampleRateHz <= 0.f) { return 0; }
    return (int32)FMath::Max(0.f, 1.25f * SampleRateHz / CutoffHz / 2.f);   // floor
}

void FVCamCurveSmoothing::SmoothPositions(TArray<FVector>& InOut, const FVCamSmoothingSettings& Settings)
{
    if (Settings.TranslationMethod != EVCamSmoothMethod::LowPassFilter) { return; }
    const int32 N = InOut.Num();
    if (N < 8) { return; }

    TArray<float> X; X.SetNum(N);
    TArray<float> Y; Y.SetNum(N);
    TArray<float> Z; Z.SetNum(N);
    for (int32 i = 0; i < N; ++i) { X[i] = InOut[i].X; Y[i] = InOut[i].Y; Z[i] = InOut[i].Z; }

    ButterworthLowPass(X, Settings.TranslationCutoffHz, Settings.ResamplingFps);
    ButterworthLowPass(Y, Settings.TranslationCutoffHz, Settings.ResamplingFps);
    ButterworthLowPass(Z, Settings.TranslationCutoffHz, Settings.ResamplingFps);

    for (int32 i = 0; i < N; ++i) { InOut[i] = FVector(X[i], Y[i], Z[i]); }
}

void FVCamCurveSmoothing::SmoothRotations(TArray<FQuat>& InOut, const FVCamSmoothingSettings& Settings)
{
    const int32 N = InOut.Num();
    if (N < 2 || Settings.RotationMethod == EVCamSmoothMethod::None) { return; }

    if (Settings.RotationMethod == EVCamSmoothMethod::Slerp)
    {
        // One-pole quaternion IIR: q follows each sample by RotationSlerpBlend.
        const float Blend = FMath::Clamp(Settings.RotationSlerpBlend, 0.f, 1.f);
        FQuat Q = InOut[0];
        for (int32 i = 0; i < N; ++i)
        {
            Q = FMath::Slerp(Q, InOut[i], Blend);
            InOut[i] = Q;
        }
        return;
    }

    // LowPassFilter: unwound per-Euler-channel Butterworth (the 4.26 default LPF path).
    if (N < 8) { return; }
    TArray<float> P; P.SetNum(N);
    TArray<float> Yaw; Yaw.SetNum(N);
    TArray<float> R; R.SetNum(N);
    for (int32 i = 0; i < N; ++i)
    {
        const FRotator Rot = InOut[i].Rotator();
        P[i] = Rot.Pitch; Yaw[i] = Rot.Yaw; R[i] = Rot.Roll;
    }
    Unwrap(P); Unwrap(Yaw); Unwrap(R);

    ButterworthLowPass(P,   Settings.RotationCutoffHz, Settings.ResamplingFps);
    ButterworthLowPass(Yaw, Settings.RotationCutoffHz, Settings.ResamplingFps);
    ButterworthLowPass(R,   Settings.RotationCutoffHz, Settings.ResamplingFps);

    for (int32 i = 0; i < N; ++i) { InOut[i] = FRotator(P[i], Yaw[i], R[i]).Quaternion(); }
}
