#include "Misc/AutomationTest.h"
#include "VCamInputLayer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FVCamControllerInput Idle() { return FVCamControllerInput(); }
}

// Shift follows the shift trigger (held-state, not an edge).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInShiftTest,
    "PCAP.VCam.Input.ShiftState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInShiftTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    TestFalse(TEXT("idle not shifted"), L.Process(Idle(), 1.f / 60.f).bShifted);

    FVCamControllerInput In = Idle(); In.LeftTrigger = true;
    TestTrue(TEXT("Default: left_trigger held = shifted"), L.Process(In, 1.f / 60.f).bShifted);
    return true;
}

// Default STANDARD actions fire on the right buttons, and only on the press edge.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInStdActionsTest,
    "PCAP.VCam.Input.DefaultStandardActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInStdActionsTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    L.Process(Idle(), 1.f / 60.f);                       // baseline

    FVCamControllerInput A = Idle(); A.LeftA = true;
    TestTrue(TEXT("left_a -> zero everything"), L.Process(A, 1.f / 60.f).bZeroEverything);
    TestFalse(TEXT("held left_a does not re-fire"), L.Process(A, 1.f / 60.f).bZeroEverything);

    FVCamInputLayer L2; L2.Layout = EVCamButtonLayout::Default;
    L2.Process(Idle(), 1.f / 60.f);
    FVCamControllerInput B = Idle(); B.LeftB = true;
    TestTrue(TEXT("left_b -> save position"), L2.Process(B, 1.f / 60.f).bSavePosition);

    FVCamInputLayer L3; L3.Layout = EVCamButtonLayout::Default;
    L3.Process(Idle(), 1.f / 60.f);
    FVCamControllerInput RB = Idle(); RB.RightB = true;
    TestTrue(TEXT("right_b -> lens next"), L3.Process(RB, 1.f / 60.f).bLensNext);
    return true;
}

// Default SHIFTED remaps the same buttons (delete cone, world scale).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInShiftedActionsTest,
    "PCAP.VCam.Input.DefaultShiftedActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInShiftedActionsTest::RunTest(const FString&)
{
    FVCamControllerInput Shift = Idle(); Shift.LeftTrigger = true;

    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    L.Process(Shift, 1.f / 60.f);
    FVCamControllerInput D = Shift; D.LeftB = true;
    TestTrue(TEXT("shifted left_b -> delete position"), L.Process(D, 1.f / 60.f).bDeletePosition);

    FVCamInputLayer L2; L2.Layout = EVCamButtonLayout::Default;
    L2.Process(Shift, 1.f / 60.f);
    FVCamControllerInput W = Shift; W.RightB = true;
    TestTrue(TEXT("shifted right_b -> world scale next"), L2.Process(W, 1.f / 60.f).bWorldScaleNext);
    return true;
}

// Translation gain accumulates from the gain encoder (SHIFTED), clamped 0.01–1.0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInGainTest,
    "PCAP.VCam.Input.TransGainEncoder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInGainTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    FVCamControllerInput Shift = Idle(); Shift.LeftTrigger = true; Shift.LeftEnc = 0.f;
    L.Process(Shift, 1.f / 60.f);                        // baseline (gain 0.5)

    FVCamControllerInput G = Shift; G.LeftEnc = 16384.f; // +1 rev -> +1.0 -> clamp to 1.0
    const FVCamInputIntents O = L.Process(G, 1.f / 60.f);
    TestTrue(TEXT("encoder sets translation gain"), O.bSetTranslationGain);
    TestTrue(TEXT("gain clamped to 1.0"), FMath::IsNearlyEqual(O.TranslationGain, 1.0f, 0.001f));
    return true;
}

// Variation 1 d-pad drives a translation rate while held, zero when released.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInDpadTest,
    "PCAP.VCam.Input.Variation1Dpad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInDpadTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Variation1;
    FVCamControllerInput Up = Idle(); Up.RightUp = true;          // +U
    TestTrue(TEXT("right_up -> +U rate"), L.Process(Up, 1.f / 60.f).NavigateRate.X > 0.f);
    TestTrue(TEXT("released -> zero rate"), L.Process(Idle(), 1.f / 60.f).NavigateRate.IsNearlyZero());
    return true;
}

// Inverted layout flips the shift trigger to the right side.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInInvertedShiftTest,
    "PCAP.VCam.Input.InvertedShift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInInvertedShiftTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::InvertedDefault;
    FVCamControllerInput In = Idle(); In.RightTrigger = true;
    TestTrue(TEXT("Inverted: right_trigger = shifted"), L.Process(In, 1.f / 60.f).bShifted);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
