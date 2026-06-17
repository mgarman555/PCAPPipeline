#include "HMCCameraAgent.h"
#include "PCAPToolStatics.h"

// ─────────────────────────────────────────────────────────────────────────────
// Concrete skills — thin ports of the detection already in UPCAPToolStatics
// (MapMetricsToAutoFlags split per element; ClassifyBoardFrame). Each tracks +
// flags ONE element, taught from the MetaHuman documentation. File-local, named
// with an FHMC prefix to avoid unity-build symbol collisions (no anon namespace).
// Each self-gates on its profile flag, so a pipeline that disables a check (e.g.
// Faceware) simply produces no flags from that skill.
// ─────────────────────────────────────────────────────────────────────────────

class FHMCFocusSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Focus"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R;
        R.bActive = C.Profile.bCheckFocus && C.Profile.FocusMin > 0.f;   // 0 = off until tuned
        if (R.bActive && C.Metrics.bValid && C.Metrics.bHasSubject &&
            C.Metrics.FocusScore < C.Profile.FocusMin)
            R.Flags |= HMC_Issue_OutOfFocus;
        return R;
    }
};

class FHMCExposureSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Exposure"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R;
        R.bActive = C.Profile.bCheckExposure;
        if (R.bActive && C.Metrics.bValid && C.Metrics.bHasSubject)
        {
            if (C.Metrics.BlownFrac > C.Profile.BlownFracMax) R.Flags |= HMC_Issue_Overexposed;
            if (C.Metrics.MeanLuma  < C.Profile.MeanLumaMin)  R.Flags |= HMC_Issue_Underexposed;
        }
        return R;
    }
};

class FHMCLightingSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Lighting"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R;
        R.bActive = C.Profile.bCheckLighting;
        if (R.bActive && C.Metrics.bValid && C.Metrics.bHasSubject)
        {
            const bool bUneven = C.Profile.bClassifyLightDir
                ? (C.Metrics.LightDir != EHMCLightDir::Even)
                : (C.Metrics.RegionSpread > C.Profile.RegionSpreadMax);
            if (bUneven) R.Flags |= HMC_Issue_UnevenLight;
            R.Reason = UPCAPToolStatics::GetLightingHintText(C.Metrics.LightDir);
        }
        return R;
    }
};

class FHMCFramingSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Framing"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R;
        R.bActive = C.Profile.bCheckFraming;
        if (!R.bActive || !C.Metrics.bValid || !C.Metrics.bHasSubject) return R;

        // Absolute size band (independent of the captured reference): too close / far.
        if (C.Metrics.SubjectSize > C.Profile.FramingSizeMax) R.Flags |= HMC_Issue_TooClose;
        if (C.Metrics.SubjectSize < C.Profile.FramingSizeMin) R.Flags |= HMC_Issue_TooFar;

        // Drift from the captured reference (only once it's set).
        if (C.FramingRef.bSet)
        {
            const float Drift     = (float)FVector2D::Distance(C.Metrics.SubjectCenter, C.FramingRef.Center);
            const float SizeDelta = FMath::Abs(C.Metrics.SubjectSize - C.FramingRef.Size);
            if (Drift > C.Profile.FramingDriftTol || SizeDelta > C.Profile.FramingDriftTol * 1.5f)
                R.Flags |= HMC_Issue_FramingDrift;
        }
        return R;
    }
};

class FHMCStabilitySkill : public IHMCSkill
{
    TArray<FVector2D> History;       // ~last 2s of subject centroids at the ~5Hz rate
    double            BumpUntil = 0.0;
public:
    const TCHAR* Name() const override { return TEXT("Stability"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R;
        R.bActive = C.Profile.bCheckFraming;   // stability shares the framing gate
        if (!R.bActive || !C.Metrics.bValid || !C.Metrics.bHasSubject) return R;

        // Sudden one-sample jump = a bump → latch it for the hold window.
        if (History.Num() > 0 &&
            FVector2D::Distance(C.Metrics.SubjectCenter, History.Last()) > C.Profile.BumpJumpMin)
            BumpUntil = C.NowSeconds + C.Profile.BumpHoldSeconds;

        History.Add(C.Metrics.SubjectCenter);
        while (History.Num() > 10) History.RemoveAt(0);

        // High centroid variance over the window = an unstable / wobbling mount.
        if (History.Num() >= 5)
        {
            FVector2D Mean(0.0, 0.0);
            for (const FVector2D& P : History) Mean += P;
            Mean /= (double)History.Num();
            double Var = 0.0;
            for (const FVector2D& P : History) Var += FVector2D::DistSquared(P, Mean);
            if (FMath::Sqrt(Var / History.Num()) > C.Profile.InstabilityStdMax)
                R.Flags |= HMC_Issue_Unstable;
        }

        if (C.NowSeconds < BumpUntil) R.Flags |= HMC_Issue_Bumped;   // latched (agent applies immediately)
        return R;
    }
};

class FHMCBoardSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Board"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R;
        R.bActive = C.Profile.bCheckBoard;   // stereo head-mount only
        if (!R.bActive) return R;
        R.bHasBoardState = true;
        R.BoardState = UPCAPToolStatics::ClassifyBoardFrame(C.Metrics, C.Profile);   // display-only (no feed flag)
        return R;
    }
};

// ─── Agent ───────────────────────────────────────────────────────────────────

void FHMCCameraAgent::Configure(ECapturePipeline Pipeline, ECaptureConfiguration Config)
{
    if (bConfigured && Pipeline == ConfiguredPipeline && Config == ConfiguredConfig) return;
    ConfiguredPipeline = Pipeline;
    ConfiguredConfig   = Config;
    bConfigured        = true;

    const FPipelineCheckProfile P = UPCAPToolStatics::GetDefinition(Pipeline, Config);
    Skills.Reset();
    Skills.Add(MakeUnique<FHMCFocusSkill>());
    Skills.Add(MakeUnique<FHMCExposureSkill>());
    Skills.Add(MakeUnique<FHMCLightingSkill>());
    Skills.Add(MakeUnique<FHMCFramingSkill>());
    Skills.Add(MakeUnique<FHMCStabilitySkill>());
    if (P.bCheckBoard) Skills.Add(MakeUnique<FHMCBoardSkill>());

    // Start fresh so stale debounce / board state doesn't leak across a config change.
    StableFlags = 0;
    Hold.Reset();
    BoardState = EHMCBoardState::Good;
}

void FHMCCameraAgent::Tick(const FHMCSkillContext& Ctx)
{
    int32 Raw = 0;
    for (TUniquePtr<IHMCSkill>& S : Skills)
    {
        const FHMCSkillResult R = S->Evaluate(Ctx);
        Raw |= R.Flags;
        if (R.bHasBoardState) BoardState = R.BoardState;
    }

    // Debounce the image-check flags (~1s set/clear at the ~5Hz analysis rate). This
    // list includes TooClose/TooFar, which the old fixed hysteresis list dropped.
    static const int32 Debounced[] = {
        HMC_Issue_Overexposed, HMC_Issue_Underexposed, HMC_Issue_OutOfFocus,
        HMC_Issue_UnevenLight,  HMC_Issue_FramingDrift, HMC_Issue_Unstable,
        HMC_Issue_TooClose,     HMC_Issue_TooFar
    };
    const int32 HOLD = 5;
    for (int32 Bit : Debounced)
    {
        int32& H = Hold.FindOrAdd(Bit);
        if (Raw & Bit) H = FMath::Min(HOLD, H + 1);
        else           H = FMath::Max(0, H - 1);
        if      (H >= HOLD) StableFlags |= Bit;
        else if (H <= 0)    StableFlags &= ~Bit;
    }

    // Bump bypasses hysteresis — the Stability skill already latches it (BumpHoldSeconds),
    // so a one-frame knock stays visible without the slow integrator.
    if (Raw & HMC_Issue_Bumped) StableFlags |=  HMC_Issue_Bumped;
    else                        StableFlags &= ~HMC_Issue_Bumped;
}

// ─── Orchestrator ────────────────────────────────────────────────────────────

void FHMCAgentOrchestrator::OnFrame(const FString& DeviceName, int32 CameraIndex,
    ECapturePipeline Pipeline, ECaptureConfiguration Config, const FHMCSkillContext& Ctx)
{
    TUniquePtr<FHMCCameraAgent>& Agent = Agents.FindOrAdd(Key(DeviceName, CameraIndex));
    if (!Agent.IsValid()) Agent = MakeUnique<FHMCCameraAgent>();
    Agent->Configure(Pipeline, Config);   // idempotent; rebuilds only on change
    Agent->Tick(Ctx);
}

int32 FHMCAgentOrchestrator::GetStableFlags(const FString& DeviceName, int32 CameraIndex) const
{
    const TUniquePtr<FHMCCameraAgent>* Agent = Agents.Find(Key(DeviceName, CameraIndex));
    return (Agent && Agent->IsValid()) ? (*Agent)->GetStableFlags() : 0;
}

EHMCBoardState FHMCAgentOrchestrator::GetBoardState(const FString& DeviceName, int32 CameraIndex) const
{
    const TUniquePtr<FHMCCameraAgent>* Agent = Agents.Find(Key(DeviceName, CameraIndex));
    return (Agent && Agent->IsValid()) ? (*Agent)->GetBoardState() : EHMCBoardState::Good;
}

void FHMCAgentOrchestrator::Remove(const FString& DeviceName)
{
    Agents.Remove(Key(DeviceName, 0));
    Agents.Remove(Key(DeviceName, 1));
}
