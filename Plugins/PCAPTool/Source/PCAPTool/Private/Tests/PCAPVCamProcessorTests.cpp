#include "Misc/AutomationTest.h"
#include "VCamConfig.h"
#include "VCamProcessor.h"

#if WITH_DEV_AUTOMATION_TESTS

// Identity: default config, no modes, first frame → output equals the raw input.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamPassthroughTest,
    "PCAP.VCam.Processor.Passthrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamPassthroughTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    FPCAPVCamRuntimeState S;
    const FTransform Raw(FQuat::Identity, FVector(10.f, 20.f, 30.f));

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Raw, 1.f / 60.f);
    TestTrue(TEXT("position passes through on first frame"),
        Out.GetLocation().Equals(FVector(10.f, 20.f, 30.f), 0.01f));
    return true;
}

// Kill Roll: a rolled input comes out with ~zero roll.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamKillRollTest,
    "PCAP.VCam.Processor.KillRoll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamKillRollTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    FPCAPVCamRuntimeState S;
    S.bKillRoll = true;
    const FTransform Raw(FRotator(0.f, 0.f, 30.f).Quaternion(), FVector::ZeroVector);  // 30° roll

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Raw, 1.f / 60.f);
    TestTrue(TEXT("roll zeroed"), FMath::IsNearlyZero(Out.GetRotation().Rotator().Roll, 0.01f));
    return true;
}

// World scale: WorldSpaceScale 2x doubles the input position.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamWorldScaleTest,
    "PCAP.VCam.Processor.WorldScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamWorldScaleTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    C->Scaling.WorldSpaceScale = FVector(2.f, 2.f, 2.f);
    FPCAPVCamRuntimeState S;
    const FTransform Raw(FQuat::Identity, FVector(1.f, 2.f, 3.f));

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Raw, 1.f / 60.f);
    TestTrue(TEXT("position scaled 2x"), Out.GetLocation().Equals(FVector(2.f, 4.f, 6.f), 0.01f));
    return true;
}

// Lock Position: position freezes at the value captured the frame before the lock engaged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamLockPositionTest,
    "PCAP.VCam.Processor.LockPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamLockPositionTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    C->Smoothing.bSmoothPosition = false;   // isolate the lock from interpolation
    FPCAPVCamRuntimeState S;

    // Frame 1: lock off, establishes LockedPosition + primes smoothing at (5,0,0).
    FPCAPVCamProcessor::Process(*C, S, FTransform(FQuat::Identity, FVector(5.f, 0.f, 0.f)), 1.f / 60.f);
    // Frame 2: lock on, input moves to (99,0,0) — output must stay at (5,0,0).
    S.bLockPosition = true;
    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, FTransform(FQuat::Identity, FVector(99.f, 0.f, 0.f)), 1.f / 60.f);

    TestTrue(TEXT("position held"), Out.GetLocation().Equals(FVector(5.f, 0.f, 0.f), 0.01f));
    return true;
}

// ZeroSpace: after zeroing at pose P, processing pose P yields the origin.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamZeroSpaceTest,
    "PCAP.VCam.Processor.ZeroSpace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamZeroSpaceTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    const FTransform P(FRotator(15.f, 40.f, 0.f).Quaternion(), FVector(100.f, -50.f, 25.f));

    FPCAPVCamProcessor::ZeroSpace(*C, P);

    FPCAPVCamRuntimeState S;
    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, P, 1.f / 60.f);
    TestTrue(TEXT("zeroed pose maps to origin (location)"),
        Out.GetLocation().Equals(FVector::ZeroVector, 0.05f));
    TestTrue(TEXT("zeroed pose maps to origin (rotation)"),
        Out.GetRotation().Rotator().IsNearlyZero(0.1f));
    return true;
}

// Hold release: after holding pose P and releasing against a different live pose Q,
// the next processed frame at Q outputs the held pose P.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamHoldReleaseTest,
    "PCAP.VCam.Processor.HoldRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamHoldReleaseTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    C->Smoothing.bSmoothPosition = false;
    C->Smoothing.bSmoothRotation = false;
    FPCAPVCamRuntimeState S;

    const FTransform P(FQuat::Identity, FVector(5.f, 5.f, 5.f));
    const FTransform Q(FQuat::Identity, FVector(50.f, 0.f, 0.f));

    FPCAPVCamProcessor::Process(*C, S, P, 1.f / 60.f);   // prime smoothed at P
    FPCAPVCamProcessor::SetHold(*C, S, true,  P);        // snapshot Held = P
    FPCAPVCamProcessor::SetHold(*C, S, false, Q);        // release against Q → Setup re-solved

    const FTransform Out = FPCAPVCamProcessor::Process(*C, S, Q, 1.f / 60.f);
    TestTrue(TEXT("output stays at held pose after release"),
        Out.GetLocation().Equals(FVector(5.f, 5.f, 5.f), 0.05f));
    return true;
}

// Navigate accumulation: world-axis rate stacks into Navigate; flight-mode rate follows facing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVCamNavigateAccumTest,
    "PCAP.VCam.Processor.NavigateAccum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVCamNavigateAccumTest::RunTest(const FString&)
{
    UPCAPVCamConfig* C = NewObject<UPCAPVCamConfig>();
    FPCAPVCamProcessor::AccumulateNavigate(*C, FVector(100.f, 0.f, 0.f), FQuat::Identity, /*flight*/false, 1.0f);
    TestTrue(TEXT("world-axis rate accumulates into Navigate"),
        C->Navigate.Translation.Equals(FVector(100.f, 0.f, 0.f), 0.01f));

    UPCAPVCamConfig* C2 = NewObject<UPCAPVCamConfig>();
    const FQuat Yaw90 = FRotator(0.f, 90.f, 0.f).Quaternion();   // facing +Y
    FPCAPVCamProcessor::AccumulateNavigate(*C2, FVector(100.f, 0.f, 0.f), Yaw90, /*flight*/true, 1.0f);
    TestTrue(TEXT("flight-mode local +X maps to world +Y at yaw 90"),
        C2->Navigate.Translation.Equals(FVector(0.f, 100.f, 0.f), 0.1f));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
