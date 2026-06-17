#pragma once

#include "CoreMinimal.h"
#include "PCAPToolTypes.h"

// ─────────────────────────────────────────────────────────────────────────────
// HMC Camera Agent — one inspector per camera, running a checklist of skills.
//
// Plain C++ (no UObject): these are runtime helpers owned by UPCAPToolSubsystem.
// The heavy pixel pass (UPCAPToolStatics::AnalyzeFrameBGRA) still runs once per
// frame; skills only CONSUME its metrics, so this stays real-time and cheap.
//
//   Orchestrator  → one Agent per "Device_Cam"
//   Agent         → a composed list of Skills (per pipeline × configuration)
//   Skill         → tracks + flags ONE element (focus / exposure / lighting / …),
//                   taught strictly from the MetaHuman documentation.
// ─────────────────────────────────────────────────────────────────────────────

// Everything a skill reads for one frame of one camera.
struct FHMCSkillContext
{
    FHMCImageMetrics      Metrics;          // output of AnalyzeFrameBGRA
    FPipelineCheckProfile Profile;          // resolved GetDefinition(pipeline, config)
    FHMCFramingRef        FramingRef;       // this camera's reference (bSet=false if none)
    int32                 CameraIndex = 0;
    double                NowSeconds  = 0.0; // FPlatformTime::Seconds() at evaluation
};

// One skill's verdict for one frame.
struct FHMCSkillResult
{
    int32          Flags          = 0;     // EHMCIssueFlag bits raised this frame
    bool           bActive        = true;  // false => check not run for this pipeline (UI greys)
    FString        Reason;                 // optional hint (reasons are also derived in the subsystem)
    EHMCBoardState BoardState     = EHMCBoardState::Good;  // Board skill only
    bool           bHasBoardState = false;
};

// A camera-watch skill. May hold per-camera state; one instance lives per agent.
class IHMCSkill
{
public:
    virtual ~IHMCSkill() {}
    virtual const TCHAR*    Name() const = 0;
    virtual FHMCSkillResult Evaluate(const FHMCSkillContext& Ctx) = 0;
};

// One inspector per camera: owns its skill set + their state + the per-flag
// hysteresis + the aggregated stable flags. Tick() runs the checklist each frame.
class FHMCCameraAgent
{
public:
    // (Re)build the skill set for a pipeline × configuration. Idempotent — only
    // rebuilds when the pair changes.
    void Configure(ECapturePipeline Pipeline, ECaptureConfiguration Config);

    // Run the checklist against one frame's context; aggregate + debounce.
    void Tick(const FHMCSkillContext& Ctx);

    int32          GetStableFlags() const { return StableFlags; }
    EHMCBoardState GetBoardState()  const { return BoardState; }

private:
    TArray<TUniquePtr<IHMCSkill>> Skills;
    ECapturePipeline      ConfiguredPipeline = ECapturePipeline::MetaHumanHMC;
    ECaptureConfiguration ConfiguredConfig   = ECaptureConfiguration::StereoHeadMount;
    bool   bConfigured = false;

    int32             StableFlags = 0;     // debounced image-check flags (was StableAutoFlags)
    TMap<int32,int32> Hold;                // per-flag-bit integrator (was AutoFlagHold)
    EHMCBoardState    BoardState  = EHMCBoardState::Good;
};

// One agent per camera per device; composes each agent's skills from the device's
// pipeline × configuration and routes each frame to the right agent.
class FHMCAgentOrchestrator
{
public:
    void OnFrame(const FString& DeviceName, int32 CameraIndex,
                 ECapturePipeline Pipeline, ECaptureConfiguration Config,
                 const FHMCSkillContext& Ctx);

    int32          GetStableFlags(const FString& DeviceName, int32 CameraIndex) const;
    EHMCBoardState GetBoardState(const FString& DeviceName, int32 CameraIndex) const;

    void Remove(const FString& DeviceName);   // prune both cameras on unregister

private:
    static FString Key(const FString& DeviceName, int32 CameraIndex)
    { return FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex); }

    TMap<FString, TUniquePtr<FHMCCameraAgent>> Agents;   // keyed "Device_Cam"
};
