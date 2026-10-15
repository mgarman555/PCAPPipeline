#include "Misc/AutomationTest.h"
#include "PCAPToolStatics.h"
#include "HMCCameraAgent.h"

#if WITH_DEV_AUTOMATION_TESTS

// ─────────────────────────────────────────────────────────────────────────────
// HMC checker on generated frames. Each test states what the checker CLAIMS to
// detect (see HMCSkills.cpp / AnalyzeFrameBGRA) and checks it on a synthetic frame
// built to show exactly that case. These prove the logic, not the thresholds: the
// thresholds still need tuning on real OneHMC footage against Epic's good/bad images.
// ─────────────────────────────────────────────────────────────────────────────

namespace PCAPCheckerTest
{
    constexpr int32 W = 480, H = 640;   // portrait, like a head-mount camera

    // Luma (0..255) per pixel from a function of normalized (x, y) -> grey BGRA frame.
    TArray<uint8> MakeFrame(TFunctionRef<float(float, float)> Luma)
    {
        TArray<uint8> Px;
        Px.SetNumUninitialized(W * H * 4);
        for (int32 y = 0; y < H; ++y)
            for (int32 x = 0; x < W; ++x)
            {
                const uint8 V = (uint8)FMath::Clamp(FMath::RoundToInt(Luma((x + 0.5f) / W, (y + 0.5f) / H)), 0, 255);
                const int32 i = (y * W + x) * 4;
                Px[i] = Px[i + 1] = Px[i + 2] = V; Px[i + 3] = 255;
            }
        return Px;
    }

    // An evenly lit "face": bright textured ellipse on a dark background. Texture gives the
    // focus measure something to bite on; Sharp=false makes it a smooth gradient instead.
    float Face(float x, float y, float Cx = 0.5f, float Cy = 0.5f, float Rx = 0.32f, float Ry = 0.36f,
               float Level = 170.f, bool bSharp = true)
    {
        const float dx = (x - Cx) / Rx, dy = (y - Cy) / Ry;
        if (dx * dx + dy * dy > 1.f) return 15.f;
        const float Tex = bSharp ? ((((int32)(x * 96) + (int32)(y * 96)) & 1) ? 18.f : -18.f) : 0.f;
        return Level + Tex;
    }

    FHMCImageMetrics Analyze(const TArray<uint8>& Px, const FPipelineCheckProfile& P)
    {
        return UPCAPToolStatics::AnalyzeFrameBGRA(Px, W, H, P.FocusRegionCenter, P.FocusRegionExtent);
    }

    FPipelineCheckProfile Stereo() { return UPCAPToolStatics::GetDefinition(ECapturePipeline::MetaHumanHMC, ECaptureConfiguration::StereoHeadMount); }
}
using namespace PCAPCheckerTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerCenteredFaceTest,
    "PCAP.Checker.CenteredFaceIsClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerCenteredFaceTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    const FHMCImageMetrics M = Analyze(MakeFrame([](float x, float y){ return Face(x, y); }), P);
    TestTrue(TEXT("valid"), M.bValid);
    TestTrue(TEXT("subject found"), M.bHasSubject);
    TestTrue(TEXT("centred x"), FMath::Abs(M.SubjectCenter.X - 0.5) < 0.03);
    TestTrue(TEXT("centred y"), FMath::Abs(M.SubjectCenter.Y - 0.5) < 0.03);
    TestEqual(TEXT("even light"), M.LightDir, EHMCLightDir::Even);
    AddInfo(FString::Printf(TEXT("centered face: size %.3f (band %.2f-%.2f) luma %.3f blown %.3f focus %.4f"),
        M.SubjectSize, P.FramingSizeMin, P.FramingSizeMax, M.MeanLuma, M.BlownFrac, M.FocusScore));
    TestEqual(TEXT("no flags on a good head-mount frame"), UPCAPToolStatics::MapMetricsToAutoFlags(M, P, FHMCFramingRef()), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerEmptyFrameTest,
    "PCAP.Checker.EmptyFrameHasNoFace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerEmptyFrameTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    const TArray<uint8> Dark = MakeFrame([](float, float){ return 10.f; });
    TestFalse(TEXT("dark empty frame: no subject (FrameHasSubject)"), UPCAPToolStatics::FrameHasSubject(Dark, W, H));
    TestFalse(TEXT("dark empty frame: no subject (metrics)"), Analyze(Dark, P).bHasSubject);

    // A lit, featureless wall is also "no face". The brightness heuristic can't tell.
    const TArray<uint8> Wall = MakeFrame([](float, float){ return 140.f; });
    TestFalse(TEXT("lit empty wall: no subject"), Analyze(Wall, P).bHasSubject);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerBlownOutTest,
    "PCAP.Checker.BlownOutIsOverexposed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerBlownOutTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    const FHMCImageMetrics M = Analyze(MakeFrame([](float x, float y){ return Face(x, y, 0.5f, 0.5f, 0.32f, 0.36f, 255.f, false); }), P);
    TestTrue(TEXT("blown fraction high"), M.BlownFrac > P.BlownFracMax);
    TestTrue(TEXT("overexposed flag"), (UPCAPToolStatics::MapMetricsToAutoFlags(M, P, FHMCFramingRef()) & HMC_Issue_Overexposed) != 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerUnderexposedTest,
    "PCAP.Checker.DimFaceIsUnderexposed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerUnderexposedTest::RunTest(const FString&)
{
    // A face that is there but dim (mean luma under the 40/255 floor).
    const FPipelineCheckProfile P = Stereo();
    const FHMCImageMetrics M = Analyze(MakeFrame([](float x, float y){ return Face(x, y, 0.5f, 0.5f, 0.32f, 0.36f, 70.f); }), P);
    AddInfo(FString::Printf(TEXT("dim face: mean luma %.3f (floor %.3f), subject=%d"), M.MeanLuma, P.MeanLumaMin, M.bHasSubject ? 1 : 0));
    TestTrue(TEXT("dim face still counts as a subject"), M.bHasSubject);
    TestTrue(TEXT("underexposed flag"), (UPCAPToolStatics::MapMetricsToAutoFlags(M, P, FHMCFramingRef()) & HMC_Issue_Underexposed) != 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerLitFromBelowTest,
    "PCAP.Checker.LitFromBelowIsUneven",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerLitFromBelowTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    // Same face, brightness rising strongly toward the chin.
    const FHMCImageMetrics M = Analyze(MakeFrame([](float x, float y)
    {
        const float F = Face(x, y, 0.5f, 0.5f, 0.32f, 0.36f, 60.f + 160.f * y);
        return F;
    }), P);
    TestEqual(TEXT("classified lit from below"), M.LightDir, EHMCLightDir::Below);
    TestTrue(TEXT("uneven-light flag"), (UPCAPToolStatics::MapMetricsToAutoFlags(M, P, FHMCFramingRef()) & HMC_Issue_UnevenLight) != 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerBlurTest,
    "PCAP.Checker.BlurLowersFocusScore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerBlurTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    const FHMCImageMetrics Sharp = Analyze(MakeFrame([](float x, float y){ return Face(x, y); }), P);
    const FHMCImageMetrics Soft  = Analyze(MakeFrame([](float x, float y){ return Face(x, y, 0.5f, 0.5f, 0.32f, 0.36f, 170.f, false); }), P);
    AddInfo(FString::Printf(TEXT("focus sharp %.5f vs blurred %.5f; FocusMin %.4f"), Sharp.FocusScore, Soft.FocusScore, P.FocusMin));
    TestTrue(TEXT("blurred scores lower than sharp"), Soft.FocusScore < Sharp.FocusScore * 0.5f);
    // The flag only fires once FocusMin is tuned (> 0) on real footage; Epic gives no number
    // (only "focus on the nasolabial area"). Surface it every run until it is tuned.
    if (P.FocusMin <= 0.f)
        AddWarning(TEXT("Focus flag is OFF: FocusMin not yet tuned on real OneHMC footage."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerBumpTest,
    "PCAP.Checker.BumpLatchesThenClears",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerBumpTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    FHMCCameraAgent Agent;
    Agent.Configure(ECapturePipeline::MetaHumanHMC, ECaptureConfiguration::StereoHeadMount);
    const FHMCImageMetrics Still  = Analyze(MakeFrame([](float x, float y){ return Face(x, y); }), P);
    const FHMCImageMetrics Jolted = Analyze(MakeFrame([](float x, float y){ return Face(x, y, 0.62f, 0.5f); }), P);

    FHMCSkillContext C; C.Profile = P;
    double T = 100.0;
    auto Tick = [&](const FHMCImageMetrics& M) { C.Metrics = M; C.NowSeconds = T; Agent.Tick(C); T += 0.2; };   // ~5 Hz
    for (int32 i = 0; i < 6; ++i) Tick(Still);
    TestEqual(TEXT("no bump while still"), Agent.GetStableFlags() & HMC_Issue_Bumped, 0);
    Tick(Jolted);
    TestTrue(TEXT("bump flagged immediately"), (Agent.GetStableFlags() & HMC_Issue_Bumped) != 0);
    for (int32 i = 0; i < 4; ++i) Tick(Jolted);   // ~0.8 s later, still inside the 1.2 s hold
    TestTrue(TEXT("bump still latched"), (Agent.GetStableFlags() & HMC_Issue_Bumped) != 0);
    for (int32 i = 0; i < 6; ++i) Tick(Jolted);   // past the hold, camera steady in its new place
    TestEqual(TEXT("bump clears after the hold"), Agent.GetStableFlags() & HMC_Issue_Bumped, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerDebounceTest,
    "PCAP.Checker.OneBadFrameDoesNotAlarm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerDebounceTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    FHMCCameraAgent Agent;
    Agent.Configure(ECapturePipeline::MetaHumanHMC, ECaptureConfiguration::StereoHeadMount);
    const FHMCImageMetrics Good  = Analyze(MakeFrame([](float x, float y){ return Face(x, y); }), P);
    const FHMCImageMetrics Blown = Analyze(MakeFrame([](float x, float y){ return Face(x, y, 0.5f, 0.5f, 0.32f, 0.36f, 255.f, false); }), P);
    FHMCSkillContext C; C.Profile = P;
    double T = 0.0;
    auto Tick = [&](const FHMCImageMetrics& M) { C.Metrics = M; C.NowSeconds = (T += 0.2); Agent.Tick(C); };
    Tick(Good); Tick(Blown); Tick(Good);
    TestEqual(TEXT("single blown frame does not flag"), Agent.GetStableFlags() & HMC_Issue_Overexposed, 0);
    for (int32 i = 0; i < 6; ++i) Tick(Blown);
    TestTrue(TEXT("sustained blown flags"), (Agent.GetStableFlags() & HMC_Issue_Overexposed) != 0);
    for (int32 i = 0; i < 6; ++i) Tick(Good);
    TestEqual(TEXT("clears when fixed"), Agent.GetStableFlags() & HMC_Issue_Overexposed, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerBoardTest,
    "PCAP.Checker.StereoBoardStates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerBoardTest::RunTest(const FString&)
{
    const FPipelineCheckProfile P = Stereo();
    TestTrue(TEXT("stereo arms the board check"), P.bCheckBoard);
    // Centred checkerboard filling ~60% of the frame on a mid-grey background.
    auto Board = [](float Cx, float Half) { return [Cx, Half](float x, float y) -> float
    {
        if (FMath::Abs(x - Cx) > Half || FMath::Abs(y - 0.5f) > Half) return 60.f;
        return ((((int32)(x * 24)) + ((int32)(y * 24))) & 1) ? 230.f : 30.f;
    }; };
    const FHMCImageMetrics Good = Analyze(MakeFrame(Board(0.5f, 0.30f)), P);
    AddInfo(FString::Printf(TEXT("board: edge %.4f (min %.4f) size %.3f (%.2f-%.2f)"), Good.EdgeEnergy, P.BoardEdgeMin, Good.SubjectSize, P.BoardSizeMin, P.BoardSizeMax));
    TestEqual(TEXT("centred board OK"), UPCAPToolStatics::ClassifyBoardFrame(Good, P), EHMCBoardState::Good);
    const FHMCImageMetrics Smooth = Analyze(MakeFrame([](float x, float y){ return Face(x, y, 0.5f, 0.5f, 0.32f, 0.36f, 170.f, false); }), P);
    TestEqual(TEXT("smooth face = no board"), UPCAPToolStatics::ClassifyBoardFrame(Smooth, P), EHMCBoardState::NotDetected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPCheckerEpicFpsTest,
    "PCAP.Checker.StereoTargetIs60fps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPCheckerEpicFpsTest::RunTest(const FString&)
{
    // Epic, Capture Device Requirements: stereo HMC minimum 60 fps (handoff §2).
    TestTrue(TEXT("stereo HMC target >= 60 fps"), Stereo().TargetFPS >= 60.f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
