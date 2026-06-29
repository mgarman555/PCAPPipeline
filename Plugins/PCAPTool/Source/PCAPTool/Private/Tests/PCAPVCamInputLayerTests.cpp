#include "Misc/AutomationTest.h"
#include "VCamInputLayer.h"

#if WITH_DEV_AUTOMATION_TESTS

// These mirror the host-side tests (Plugins/PCAPTool/HostTests) so the logic is verified both
// in-editor and standalone. Ground truth: vcam_default.py / vcam_sony.py.

namespace
{
    FVCamControllerInput Idle()
    {
        // Centre the absolute thumbwheels mid-scale so gains aren't pinned to 0/1 by default.
        FVCamControllerInput In;
        In.LeftGain = 2048.f; In.RightGain = 2048.f;
        return In;
    }
    const float kDt = 1.f / 60.f;
}

// Default: SHIFT is the left_x button held (not a trigger).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInShiftTest,
    "PCAP.VCam.Input.ShiftIsLeftX",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInShiftTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    TestFalse(TEXT("idle not shifted"), L.Process(Idle(), kDt).bShifted);
    FVCamControllerInput In = Idle(); In.LeftX = true;
    TestTrue(TEXT("left_x held = shifted"), L.Process(In, kDt).bShifted);
    return true;
}

// Default STANDARD actions fire on press edge only.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInStdActionsTest,
    "PCAP.VCam.Input.DefaultStandardActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInStdActionsTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    L.Process(Idle(), kDt);

    FVCamControllerInput RA = Idle(); RA.RightA = true;
    TestTrue(TEXT("right_a -> zero everything"), L.Process(RA, kDt).bZeroEverything);
    TestFalse(TEXT("held right_a does not re-fire"), L.Process(RA, kDt).bZeroEverything);

    FVCamInputLayer L2; L2.Layout = EVCamButtonLayout::Default; L2.Process(Idle(), kDt);
    FVCamControllerInput RB = Idle(); RB.RightB = true;
    TestTrue(TEXT("right_b -> lens prev"), L2.Process(RB, kDt).bLensPrev);

    FVCamInputLayer L3; L3.Layout = EVCamButtonLayout::Default; L3.Process(Idle(), kDt);
    FVCamControllerInput LB = Idle(); LB.LeftB = true;
    TestTrue(TEXT("left_b -> save position"), L3.Process(LB, kDt).bSavePosition);
    return true;
}

// Default SHIFTED remaps right_y/right_b to world scale and left_a to flight-mode toggle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInShiftedActionsTest,
    "PCAP.VCam.Input.DefaultShiftedActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInShiftedActionsTest::RunTest(const FString&)
{
    FVCamControllerInput Shift = Idle(); Shift.LeftX = true;

    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default; L.Process(Shift, kDt);
    FVCamControllerInput W = Shift; W.RightY = true;
    TestTrue(TEXT("shifted right_y -> world scale next"), L.Process(W, kDt).bWorldScaleNext);

    FVCamInputLayer L2; L2.Layout = EVCamButtonLayout::Default; L2.Process(Shift, kDt);
    FVCamControllerInput F = Shift; F.LeftA = true;
    TestTrue(TEXT("shifted left_a -> toggle flight"), L2.Process(F, kDt).bToggleFlightMode);
    return true;
}

// Translation gain comes from the absolute left_gain axis (left_gain/4095), scaling translation speed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInGainTest,
    "PCAP.VCam.Input.LeftGainTrim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInGainTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    FVCamControllerInput Base = Idle(); Base.LeftGain = 0.f;
    L.Process(Base, kDt);   // baseline captured at full-scale stick zero

    // Push left_left_y well past deadband, gain at full scale.
    FVCamControllerInput G = Idle(); G.LeftGain = 4095.f; G.LeftLeftY = 1000.f;
    const FVCamInputIntents O = L.Process(G, kDt);
    TestTrue(TEXT("translation gain ~1.0"), FMath::IsNearlyEqual(O.TranslationGain, 1.0f, 0.001f));
    // finalTu = 1000 * 1.0 * 10 * (1/800) = 12.5
    TestTrue(TEXT("translation speed = counts*gain*10/800"),
        FMath::IsNearlyEqual(O.TranslationSpeed.X, 12.5f, 0.01f));
    return true;
}

// Zoom gain stage-0 is INVERTED: right_gain low -> trim high; and zoom only when NOT shifted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInZoomTest,
    "PCAP.VCam.Input.ZoomInvertedGain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInZoomTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    FVCamControllerInput Base = Idle(); Base.RightGain = 0.f; L.Process(Base, kDt);

    FVCamControllerInput Z = Idle(); Z.RightGain = 0.f; Z.RightRightY = 1000.f;  // trim = 1-0 = 1
    const FVCamInputIntents O = L.Process(Z, kDt);
    TestTrue(TEXT("zoom trim ~1.0 at right_gain=0"), FMath::IsNearlyEqual(O.ZoomGainTrim, 1.0f, 0.001f));
    // finalZoom = 1000 * 1.0 * 10 * (1/100) = 100
    TestTrue(TEXT("zoom speed = counts*trim*10/100"), FMath::IsNearlyEqual(O.ZoomSpeed, 100.f, 0.01f));
    return true;
}

// Deadband: a stick movement below 100 counts produces zero speed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInDeadbandTest,
    "PCAP.VCam.Input.Deadband",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInDeadbandTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Default;
    L.Process(Idle(), kDt);
    FVCamControllerInput Small = Idle(); Small.LeftGain = 4095.f; Small.LeftLeftY = 50.f;  // < 100
    TestTrue(TEXT("sub-deadband stick = zero"), L.Process(Small, kDt).TranslationSpeed.IsNearlyZero());
    return true;
}

// Sony: holding left_x past the 2s hold-once cycles the latched map (init SONY -> STANDARD).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamInSonyMapTest,
    "PCAP.VCam.Input.SonyMapCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamInSonyMapTest::RunTest(const FString&)
{
    FVCamInputLayer L; L.Layout = EVCamButtonLayout::Sony;
    TestEqual(TEXT("init map = SONY"), (int32)L.Process(Idle(), kDt).Mapping, (int32)EVCamMapping::Sony);

    // Hold left_x for >2s of accumulated dt, then it should advance to STANDARD on the hold-once.
    FVCamControllerInput Hold = Idle(); Hold.LeftX = true;
    EVCamMapping M = EVCamMapping::Sony;
    for (int32 i = 0; i < 200; ++i) { M = L.Process(Hold, kDt).Mapping; }   // ~3.3s
    TestEqual(TEXT("after left_x hold-once: SONY -> STANDARD"), (int32)M, (int32)EVCamMapping::Standard);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
