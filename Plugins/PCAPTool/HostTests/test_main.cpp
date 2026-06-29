// Host-side tests for PCAPTool's pure VCam logic. Compiles the REAL FVCamInputLayer and
// FPCAPVCamProcessor against the UE-math stubs (no engine needed) and asserts the WVCAM
// ground-truth formulas. Build/run with ./run.sh (or `make`).

#include "CoreMinimal.h"
#include "VCamInputLayer.h"
#include "VCamConfig.h"
#include "VCamProcessor.h"

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

int main()
{
    TestInputLayer();
    TestProcessor();
    std::printf("\n%d/%d checks passed (%d failed)\n", g_total - g_fail, g_total, g_fail);
    return g_fail == 0 ? 0 : 1;
}
