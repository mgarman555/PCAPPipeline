// Host-side tests for PCAPTool's pure VCam logic. Compiles the REAL FVCamInputLayer and
// FPCAPVCamProcessor against the UE-math stubs (no engine needed) and asserts the WVCAM
// ground-truth formulas. Build/run with ./run.sh (or `make`).

#include "CoreMinimal.h"
#include "VCamInputLayer.h"
#include "VCamConfig.h"
#include "VCamProcessor.h"
#include "VCamCurveSmoothing.h"

#include <cstdio>
#include <cmath>

static int g_fail = 0, g_total = 0;

static void Check(bool ok, const char* what)
{
    ++g_total;
    if (!ok) { ++g_fail; std::printf("  FAIL: %s\n", what); }
    else     { std::printf("  ok  : %s\n", what); }
}
static bool Near(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }

static FVCamControllerInput Idle()
{
    FVCamControllerInput In;
    In.LeftGain = 2048.f; In.RightGain = 2048.f;   // mid-scale wheels
    return In;
}
static const float Dt = 1.f / 60.f;

// ─────────────────────────────────────────────────────────────────────────────
static void TestInputLayer()
{
    std::printf("[FVCamInputLayer]\n");

    // Shift = left_x held.
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
        Check(!L.Process(Idle(), Dt).bShifted, "idle not shifted");
        FVCamControllerInput In = Idle(); In.LeftX = true;
        Check(L.Process(In, Dt).bShifted, "left_x held = shifted");
    }

    // Default STANDARD: right_a -> zero (edge only).
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default; L.Process(Idle(), Dt);
        FVCamControllerInput RA = Idle(); RA.RightA = true;
        Check(L.Process(RA, Dt).bZeroEverything, "right_a -> zero everything");
        Check(!L.Process(RA, Dt).bZeroEverything, "held right_a does not re-fire");
    }

    // Default SHIFTED: right_y -> world scale next; left_a -> toggle flight.
    {
        FVCamControllerInput Shift = Idle(); Shift.LeftX = true;
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default; L.Process(Shift, Dt);
        FVCamControllerInput W = Shift; W.RightY = true;
        Check(L.Process(W, Dt).bWorldScaleNext, "shifted right_y -> world scale next");

        FVCamInputLayer L2; L2.Layout = EVCamButtonLayout::Default; L2.Process(Shift, Dt);
        FVCamControllerInput F = Shift; F.LeftA = true;
        Check(L2.Process(F, Dt).bToggleFlightMode, "shifted left_a -> toggle flight");
    }

    // Translation gain + speed: finalTu = counts * (left_gain/4095) * 10 * (1/800).
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
        FVCamControllerInput Base = Idle(); Base.LeftGain = 0.f; L.Process(Base, Dt);
        FVCamControllerInput G = Idle(); G.LeftGain = 4095.f; G.LeftLeftY = 1000.f;
        const FVCamInputIntents O = L.Process(G, Dt);
        Check(Near(O.TranslationGain, 1.0f, 0.001f), "translation gain ~1.0 at left_gain=4095");
        Check(Near(O.TranslationSpeed.X, 12.5f), "translation speed = 1000*1*10/800 = 12.5");
    }

    // Zoom inverted gain: trim = 1 - right_gain/4095; finalZoom = counts * trim * 10 * (1/100).
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
        FVCamControllerInput Base = Idle(); Base.RightGain = 0.f; L.Process(Base, Dt);
        FVCamControllerInput Z = Idle(); Z.RightGain = 0.f; Z.RightRightY = 1000.f;
        const FVCamInputIntents O = L.Process(Z, Dt);
        Check(Near(O.ZoomGainTrim, 1.0f, 0.001f), "zoom trim ~1.0 at right_gain=0");
        Check(Near(O.ZoomSpeed, 100.f), "zoom speed = 1000*1*10/100 = 100");
    }

    // Inverted-gain sanity: right_gain at full scale -> trim 0 -> no zoom.
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
        FVCamControllerInput Base = Idle(); Base.RightGain = 4095.f; L.Process(Base, Dt);
        FVCamControllerInput Z = Idle(); Z.RightGain = 4095.f; Z.RightRightY = 1000.f;
        const FVCamInputIntents O = L.Process(Z, Dt);
        Check(Near(O.ZoomGainTrim, 0.0f, 0.001f), "zoom trim ~0 at right_gain=4095");
        Check(Near(O.ZoomSpeed, 0.f), "zoom speed ~0 when trim is 0 (inverted gain)");
    }

    // Deadband: sub-100-count stick -> zero.
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default; L.Process(Idle(), Dt);
        FVCamControllerInput Small = Idle(); Small.LeftGain = 4095.f; Small.LeftLeftY = 50.f;
        Check(L.Process(Small, Dt).TranslationSpeed.IsNearlyZero(), "sub-deadband stick = zero speed");
    }

    // Shifted suppresses zoom (right stick becomes rotation); standard suppresses rotation.
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
        FVCamControllerInput Base = Idle(); Base.LeftGain = 4095.f; Base.RightGain = 0.f; L.Process(Base, Dt);
        FVCamControllerInput Sh = Base; Sh.LeftX = true; Sh.RightRightX = 1000.f;  // roll axis
        const FVCamInputIntents O = L.Process(Sh, Dt);
        Check(Near(O.ZoomSpeed, 0.f), "shifted: no zoom");
        Check(!O.RotationSpeed.IsNearlyZero(), "shifted: right stick drives rotation");
    }

    // Sony: latched map cycles on left_x hold-once (init SONY -> STANDARD after >2s).
    {
        FVCamInputLayer L; L.Layout = EVCamButtonLayout::Sony;
        Check((int)L.Process(Idle(), Dt).Mapping == (int)EVCamMapping::Sony, "Sony init map = SONY");
        FVCamControllerInput Hold = Idle(); Hold.LeftX = true;
        EVCamMapping M = EVCamMapping::Sony;
        for (int i = 0; i < 200; ++i) { M = L.Process(Hold, Dt).Mapping; }
        Check((int)M == (int)EVCamMapping::Standard, "Sony left_x hold-once: SONY -> STANDARD");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void TestProcessor()
{
    std::printf("[FPCAPVCamProcessor]\n");

    // Passthrough: default config, first frame -> output == raw position.
    {
        UPCAPVCamConfig C; FPCAPVCamRuntimeState S;
        const FTransform Out = FPCAPVCamProcessor::Process(C, S, FTransform(FQuat::Identity, FVector(10,20,30)), Dt);
        const FVector P = Out.GetLocation();
        Check(Near(P.X,10) && Near(P.Y,20) && Near(P.Z,30), "passthrough position on first frame");
    }

    // Kill roll.
    {
        UPCAPVCamConfig C; FPCAPVCamRuntimeState S; S.bKillRoll = true;
        const FTransform Raw(FRotator(0,0,30).Quaternion(), FVector::ZeroVector);
        const FTransform Out = FPCAPVCamProcessor::Process(C, S, Raw, Dt);
        Check(Near(Out.GetRotation().Rotator().Roll, 0.f, 0.05f), "kill roll zeroes roll");
    }

    // World scale 2x doubles position.
    {
        UPCAPVCamConfig C; C.Scaling.WorldSpaceScale = FVector(2,2,2); FPCAPVCamRuntimeState S;
        const FTransform Out = FPCAPVCamProcessor::Process(C, S, FTransform(FQuat::Identity, FVector(1,2,3)), Dt);
        const FVector P = Out.GetLocation();
        Check(Near(P.X,2) && Near(P.Y,4) && Near(P.Z,6), "world scale 2x");
    }

    // ZeroSpace: zeroing at pose P, processing P yields the origin.
    {
        UPCAPVCamConfig C;
        const FTransform P(FRotator(15,40,0).Quaternion(), FVector(100,-50,25));
        FPCAPVCamProcessor::ZeroSpace(C, P);
        FPCAPVCamRuntimeState S;
        const FTransform Out = FPCAPVCamProcessor::Process(C, S, P, Dt);
        Check(Out.GetLocation().Size() < 0.1f, "zeroed pose maps to origin (location)");
    }

    // Navigate accumulation: world-axis vs flight-mode local.
    {
        UPCAPVCamConfig C;
        FPCAPVCamProcessor::AccumulateNavigate(C, FVector(100,0,0), FQuat::Identity, false, 1.0f);
        Check(Near(C.Navigate.Translation.X, 100.f), "world-axis rate accumulates into Navigate");

        UPCAPVCamConfig C2;
        const FQuat Yaw90 = FRotator(0,90,0).Quaternion();   // facing +Y
        FPCAPVCamProcessor::AccumulateNavigate(C2, FVector(100,0,0), Yaw90, true, 1.0f);
        const FVector T = C2.Navigate.Translation;
        Check(Near(T.Y, 100.f, 0.1f) && Near(T.X, 0.f, 0.1f),
              "flight-mode local +X maps to world +Y at yaw 90");
    }

    // Zoom integration + clamp to [Min,Max].
    {
        UPCAPVCamConfig C; C.ActiveFocalLength = 50.f;
        FPCAPVCamProcessor::AccumulateZoom(C, 100.f, 1.0f);   // +100mm/s for 1s
        Check(Near(C.ActiveFocalLength, 100.f), "zoom clamps up to MaxFocalLength (100)");
        FPCAPVCamProcessor::AccumulateZoom(C, -1000.f, 1.0f); // big negative
        Check(Near(C.ActiveFocalLength, 18.f), "zoom clamps down to MinFocalLength (18)");
    }

    // Rotation accumulation maps (Roll,Yaw,Pitch) rate onto Navigate.Rotation.
    {
        UPCAPVCamConfig C;
        FPCAPVCamProcessor::AccumulateNavigateRotation(C, FVector(/*roll*/0, /*yaw*/10, /*pitch*/0), 1.0f);
        Check(Near(C.Navigate.Rotation.Yaw, 10.f), "rotation rate accumulates yaw");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
static void TestSmoothing()
{
    std::printf("[FVCamCurveSmoothing]\n");
    const float Fs = 60.f, Cut = 2.f;   // VcamSequencer defaults

    auto Amplitude = [](const TArray<float>& D) {
        float lo = 1e9f, hi = -1e9f;                    // interior only (skip warm-up)
        for (int i = D.Num() / 4; i < 3 * D.Num() / 4; ++i) { lo = std::min(lo, D[i]); hi = std::max(hi, D[i]); }
        return 0.5f * (hi - lo);
    };
    auto Sine = [](int N, float fHz, float fs) { TArray<float> D; for (int i = 0; i < N; ++i) { D.Add(std::sin(2.f * PI * fHz * i / fs)); } return D; };
    auto TotalVariation = [](const TArray<float>& D) { float t = 0.f; for (int i = 1; i < D.Num(); ++i) { t += std::fabs(D[i] - D[i - 1]); } return t; };

    // Constant signal is unchanged (DC gain 1, state seeded).
    {
        TArray<float> D; for (int i = 0; i < 120; ++i) { D.Add(1.0f); }
        FVCamCurveSmoothing::ButterworthLowPass(D, Cut, Fs);
        bool flat = true;
        for (int i = 0; i < D.Num(); ++i) { if (!Near(D[i], 1.0f, 0.01f)) { flat = false; } }
        Check(flat, "constant signal passes through unchanged");
    }

    // Low frequency (0.5 Hz) passes; high frequency (10 Hz) is strongly attenuated. Cutoff 2 Hz.
    {
        TArray<float> Lo = Sine(600, 0.5f, Fs);
        FVCamCurveSmoothing::ButterworthLowPass(Lo, Cut, Fs);
        Check(Amplitude(Lo) > 0.8f, "low-freq (0.5Hz) preserved through 2Hz low-pass");

        TArray<float> Hi = Sine(600, 10.f, Fs);
        FVCamCurveSmoothing::ButterworthLowPass(Hi, Cut, Fs);
        Check(Amplitude(Hi) < 0.1f, "high-freq (10Hz) attenuated by 6th-order 2Hz low-pass");
    }

    // Group-delay advance matches VcamSequencer: floor(1.25 * fps / cutoff / 2).
    Check(FVCamCurveSmoothing::GroupDelayAdvanceSamples(Cut, Fs) == 18, "advance = floor(1.25*60/2/2) = 18");

    // Default method is None → no-op (non-destructive).
    {
        TArray<FVector> P; for (int i = 0; i < 60; ++i) { P.Add(FVector((float)i, 0.f, 0.f)); }
        FVCamSmoothingSettings Off;   // TranslationMethod defaults to None
        FVCamCurveSmoothing::SmoothPositions(P, Off);
        Check(Near(P[30].X, 30.f) && Near(P[59].X, 59.f), "method None leaves the curve untouched");
    }

    // LowPassFilter reduces position jitter (total variation) by an order of magnitude.
    {
        TArray<FVector> P;
        for (int i = 0; i < 300; ++i) { const float n = (i % 2 ? 5.f : -5.f); P.Add(FVector((float)i + n, 0.f, 0.f)); }
        TArray<float> InX; for (int i = 0; i < P.Num(); ++i) { InX.Add(P[i].X); }
        FVCamSmoothingSettings S; S.TranslationMethod = EVCamSmoothMethod::LowPassFilter;
        FVCamCurveSmoothing::SmoothPositions(P, S);
        TArray<float> OutX; for (int i = 0; i < P.Num(); ++i) { OutX.Add(P[i].X); }
        Check(TotalVariation(OutX) < 0.2f * TotalVariation(InX), "low-pass smooths position jitter (TV drops >5x)");
    }

    // Rotation slerp: follows a step toward the target and stays unit-length.
    {
        TArray<FQuat> R;
        const FQuat A = FRotator(0.f, 0.f, 0.f).Quaternion();
        const FQuat B = FRotator(0.f, 90.f, 0.f).Quaternion();
        for (int i = 0; i < 120; ++i) { R.Add(i < 30 ? A : B); }
        FVCamSmoothingSettings S; S.RotationMethod = EVCamSmoothMethod::Slerp;   // blend 0.8
        FVCamCurveSmoothing::SmoothRotations(R, S);
        bool unit = true;
        for (int i = 0; i < R.Num(); ++i)
        { if (!Near(std::sqrt(FMath::Square(R[i].X)+FMath::Square(R[i].Y)+FMath::Square(R[i].Z)+FMath::Square(R[i].W)), 1.f, 0.01f)) { unit = false; } }
        Check(unit, "slerp-smoothed rotations stay unit-length");
        Check(Near(R[119].Rotator().Yaw, 90.f, 2.f), "slerp converges to the target orientation");
    }

    // Rotation low-pass: a constant orientation is preserved and stays unit-length.
    {
        TArray<FQuat> R;
        const FQuat Fixed = FRotator(0.f, 30.f, 0.f).Quaternion();
        for (int i = 0; i < 120; ++i) { R.Add(Fixed); }
        FVCamSmoothingSettings S; S.RotationMethod = EVCamSmoothMethod::LowPassFilter;
        FVCamCurveSmoothing::SmoothRotations(R, S);
        Check(Near(R[60].Rotator().Yaw, 30.f, 0.5f), "low-pass rotation preserves a constant orientation");
    }
}

int main()
{
    TestInputLayer();
    TestProcessor();
    TestSmoothing();
    std::printf("\n%d/%d checks passed (%d failed)\n", g_total - g_fail, g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
