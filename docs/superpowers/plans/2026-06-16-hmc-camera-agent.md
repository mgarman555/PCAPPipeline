# HMC Camera Agent — Implementation Plan (per-camera agent + skills + orchestrator)

> **For agentic workers:** authored on macOS (no UE 5.7 toolchain). Verification per task = brace/symbol check + subagent compile-review; the real compile is Madi's Windows build. The bar is **behavior parity** with the current monitor (same green/red + reasons), plus the TooClose/TooFar surfacing fix. Steps use checkbox syntax.

**Goal:** Re-architect the HMC monitor so each camera is watched by its own **agent** running a **checklist of skills** (Focus / Exposure / Lighting / Framing / Stability / Board), composed per **pipeline × configuration** by an **orchestrator** — entirely in-engine, doc-grounded, behavior-preserving.

**Architecture:** Pure C++ (no UObject) helpers in the plugin. The heavy pixel pass (`AnalyzeFrameBGRA`) stays once-per-frame; skills only *consume* its metrics. The agent owns the per-camera image-check state (hysteresis, centroid history, bump latch) currently scattered in the subsystem; the orchestrator owns one agent per `Device_Cam` and composes skills from `GetDefinition(pipeline, config)`.

**Tech Stack:** UE 5.7 C++ (`Plugins/PCAPTool`).

---

## File structure
- **Create `Public/HMCCameraAgent.h`** — `FHMCSkillContext`, `FHMCSkillResult`, `IHMCSkill`, `FHMCCameraAgent`, `FHMCAgentOrchestrator` (declarations; plain C++).
- **Create `Private/HMCSkills.cpp`** — the six concrete skills + agent + orchestrator implementations.
- **Modify `Private/PCAPToolSubsystem.cpp` / `Public/PCAPToolSubsystem.h`** — own an orchestrator; route frames to it; `GetEffectiveIssueFlags` reads the agent; retire the migrated members/helpers.
- **Modify `Private/SHMCSetupPanel.cpp`** — `BoardState()` reads the agent's board state (instead of calling the static directly), so board state flows through the agent too.

Skills reuse the pure logic already in `UPCAPToolStatics` (`MapMetricsToAutoFlags`, `ClassifyBoardFrame`) — they don't reimplement detection.

---

## Task 1: Skill interfaces + result/context

**Files:** Create `Public/HMCCameraAgent.h`

- [ ] **Step 1:** Define the value types + skill interface:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "PCAPToolTypes.h"

// Everything a skill reads for one frame of one camera.
struct FHMCSkillContext
{
    FHMCImageMetrics       Metrics;        // output of AnalyzeFrameBGRA
    FPipelineCheckProfile  Profile;        // resolved GetDefinition(pipeline,config)
    FHMCFramingRef         FramingRef;     // this camera's reference (bSet=false if none)
    int32                  CameraIndex = 0;
    double                 NowSeconds  = 0.0;
};

// One skill's verdict for one frame.
struct FHMCSkillResult
{
    int32           Flags    = 0;          // EHMCIssueFlag bits raised
    bool            bActive  = true;       // false => not evaluated (greys, never false-green)
    bool            bBypassHysteresis = false;  // true => flag shows immediately (bump latch)
    FString         Reason;                // status hint, e.g. "lit from below"
    EHMCBoardState  BoardState = EHMCBoardState::Good;  // Board skill only; ignored elsewhere
    bool            bHasBoardState = false;
};

// A camera-watch skill. May hold per-camera state. One instance per agent.
class IHMCSkill
{
public:
    virtual ~IHMCSkill() {}
    virtual const TCHAR* Name() const = 0;
    virtual FHMCSkillResult Evaluate(const FHMCSkillContext& Ctx) = 0;
};
```
- [ ] **Step 2:** Brace-check the header → 0. Commit `feat(pcap): HMC skill interface (context/result/ISkill)`.

---

## Task 2: Agent + orchestrator declarations

**Files:** Modify `Public/HMCCameraAgent.h`

- [ ] **Step 1:** Append:
```cpp
// One inspector per camera: owns its skill set + their state + the per-flag hysteresis
// + the aggregated stable flags. Tick() runs the checklist for one frame.
class FHMCCameraAgent
{
public:
    void Configure(ECapturePipeline Pipeline, ECaptureConfiguration Config);   // (re)build skill set
    void Tick(const FHMCSkillContext& Ctx);     // run skills, aggregate, debounce
    int32 GetStableFlags() const { return StableFlags; }
    FString GetReason(int32 FlagMask) const;    // first active reason whose flag is in mask
    EHMCBoardState GetBoardState() const { return BoardState; }
    FString GetLightingReason() const { return LightingReason; }
    FString GetFramingReason() const { return FramingReason; }
private:
    TArray<TUniquePtr<IHMCSkill>> Skills;
    ECapturePipeline      ConfiguredPipeline = ECapturePipeline::MetaHumanHMC;
    ECaptureConfiguration ConfiguredConfig   = ECaptureConfiguration::StereoHeadMount;
    bool   bConfigured = false;
    int32  StableFlags = 0;
    TMap<int32,int32> Hold;       // per-flag-bit integrator (was AutoFlagHold)
    EHMCBoardState BoardState = EHMCBoardState::Good;
    FString LightingReason, FramingReason;
};

// One agent per camera per device; composes skills from the device's pipeline×config.
class FHMCAgentOrchestrator
{
public:
    void OnFrame(const FString& DeviceName, int32 CameraIndex,
                 ECapturePipeline Pipeline, ECaptureConfiguration Config,
                 const FHMCSkillContext& Ctx);
    int32 GetStableFlags(const FString& DeviceName, int32 CameraIndex) const;
    EHMCBoardState GetBoardState(const FString& DeviceName, int32 CameraIndex) const;
    FString GetLightingReason(const FString& DeviceName, int32 CameraIndex) const;
    FString GetFramingReason(const FString& DeviceName, int32 CameraIndex) const;
    void Remove(const FString& DeviceName);     // prune both cams on unregister
private:
    TMap<FString, TUniquePtr<FHMCCameraAgent>> Agents;   // key "Device_Cam"
    FHMCCameraAgent* FindOrCreate(const FString& Key, ECapturePipeline, ECaptureConfiguration);
};
```
- [ ] **Step 2:** Brace-check → 0. Commit `feat(pcap): HMC agent + orchestrator declarations`.

---

## Task 3: Concrete skills (ports of existing detection)

**Files:** Create `Private/HMCSkills.cpp` (include `HMCCameraAgent.h`, `PCAPToolStatics.h`)

Each skill is a thin port of logic already in `UPCAPToolStatics::MapMetricsToAutoFlags` (split per element) or `ClassifyBoardFrame`. They do **not** reimplement detection.

- [ ] **Step 1: Focus / Exposure / Lighting / Framing skills.** Each gated on `Metrics.bValid && bHasSubject` and its profile flag. Example (Framing — the others mirror the matching block of `MapMetricsToAutoFlags`):
```cpp
class FFramingSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Framing"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R; R.bActive = C.Profile.bCheckFraming;
        if (!R.bActive || !C.Metrics.bValid || !C.Metrics.bHasSubject) { R.bActive = false; return R; }
        if (C.Metrics.SubjectSize > C.Profile.FramingSizeMax) R.Flags |= HMC_Issue_TooClose;
        if (C.Metrics.SubjectSize < C.Profile.FramingSizeMin) R.Flags |= HMC_Issue_TooFar;
        if (C.FramingRef.bSet)
        {
            const float Drift = (float)FVector2D::Distance(C.Metrics.SubjectCenter, C.FramingRef.Center);
            const float SizeDelta = FMath::Abs(C.Metrics.SubjectSize - C.FramingRef.Size);
            if (Drift > C.Profile.FramingDriftTol || SizeDelta > C.Profile.FramingDriftTol * 1.5f)
                R.Flags |= HMC_Issue_FramingDrift;
        }
        return R;
    }
};
```
- Focus skill → `OutOfFocus` (only when `P.bCheckFocus && P.FocusMin > 0`). Exposure skill → `Overexposed`/`Underexposed` (from `BlownFrac`/`MeanLuma`). Lighting skill → `UnevenLight` (from `LightDir != Even` per `bClassifyLightDir`, else `RegionSpread`), and sets `R.Reason = UPCAPToolStatics::GetLightingHintText(C.Metrics.LightDir)`.
- [ ] **Step 2: Stability skill** — owns `TArray<FVector2D> History` + `double BumpUntil`; ports the inline block from `OnVideoFrameResponse`:
```cpp
class FStabilitySkill : public IHMCSkill
{
    TArray<FVector2D> History; double BumpUntil = 0.0;
public:
    const TCHAR* Name() const override { return TEXT("Stability"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R; R.bActive = C.Profile.bCheckFraming;
        if (!R.bActive || !C.Metrics.bHasSubject) { R.bActive = false; return R; }
        if (History.Num() > 0 && FVector2D::Distance(C.Metrics.SubjectCenter, History.Last()) > C.Profile.BumpJumpMin)
            BumpUntil = C.NowSeconds + C.Profile.BumpHoldSeconds;
        History.Add(C.Metrics.SubjectCenter);
        while (History.Num() > 10) History.RemoveAt(0);
        if (History.Num() >= 5)
        {
            FVector2D M(0,0); for (const FVector2D& P : History) M += P; M /= (double)History.Num();
            double Var = 0; for (const FVector2D& P : History) Var += FVector2D::DistSquared(P, M);
            if (FMath::Sqrt(Var / History.Num()) > C.Profile.InstabilityStdMax) R.Flags |= HMC_Issue_Unstable;
        }
        if (C.NowSeconds < BumpUntil) { R.Flags |= HMC_Issue_Bumped; R.bBypassHysteresis = true; }
        return R;
    }
};
```
- [ ] **Step 3: Board skill** — stereo only; wraps `ClassifyBoardFrame`, carries the state (no feed flag, display-only, preserving current behavior):
```cpp
class FBoardSkill : public IHMCSkill
{
public:
    const TCHAR* Name() const override { return TEXT("Board"); }
    FHMCSkillResult Evaluate(const FHMCSkillContext& C) override
    {
        FHMCSkillResult R; R.bActive = C.Profile.bCheckBoard;
        if (!R.bActive) return R;
        R.bHasBoardState = true;
        R.BoardState = UPCAPToolStatics::ClassifyBoardFrame(C.Metrics, C.Profile);
        return R;
    }
};
```
- [ ] **Step 4:** Brace-check → 0. Commit `feat(pcap): HMC skills (focus/exposure/lighting/framing/stability/board)`.

---

## Task 4: Agent + orchestrator implementation

**Files:** `Private/HMCSkills.cpp`

- [ ] **Step 1: `FHMCCameraAgent::Configure`** — rebuild `Skills` for the pipeline×config (always add Focus/Exposure/Lighting/Framing/Stability; add Board when `GetDefinition(...).bCheckBoard`). Store configured pair; set `bConfigured`.
- [ ] **Step 2: `FHMCCameraAgent::Tick`** — run each skill; OR `bBypassHysteresis` flags straight into `StableFlags`-equivalent; debounce the rest with the `Hold` integrator (port `UpdateAutoFlagHysteresis`, HOLD=5, but iterate **all bits any skill emits** so TooClose/TooFar are included — the fix); capture `LightingReason`/`FramingReason`/`BoardState` from the matching skills' results.
```cpp
void FHMCCameraAgent::Tick(const FHMCSkillContext& Ctx)
{
    int32 Raw = 0, Immediate = 0; LightingReason.Empty(); FramingReason.Empty();
    for (auto& S : Skills)
    {
        const FHMCSkillResult R = S->Evaluate(Ctx);
        if (R.bBypassHysteresis) Immediate |= R.Flags; else Raw |= R.Flags;
        if (R.bHasBoardState) BoardState = R.BoardState;
        if (!R.Reason.IsEmpty()) { if (FCString::Strcmp(S->Name(), TEXT("Lighting"))==0) LightingReason = R.Reason; }
    }
    // Debounce every bit seen in Raw (set or clear), ~1s at 5Hz.
    const int32 HOLD = 5;
    int32 SeenMask = Raw;
    for (const auto& Pair : Hold) SeenMask |= Pair.Key;     // keep clearing bits already tracked
    for (int32 Bit = 1; Bit; Bit <<= 1)
    {
        if (!(SeenMask & Bit)) continue;
        int32& H = Hold.FindOrAdd(Bit);
        if (Raw & Bit) H = FMath::Min(HOLD, H + 1); else H = FMath::Max(0, H - 1);
        if (H >= HOLD) StableFlags |= Bit; else if (H <= 0) StableFlags &= ~Bit;
    }
    StableFlags = (StableFlags & ~HMC_Issue_Bumped) | Immediate;   // bump bypasses hysteresis
}
```
- [ ] **Step 3: Orchestrator** — `FindOrCreate` keys by `Device_Cam`, (re)configures the agent if pipeline/config changed; `OnFrame` builds nothing (caller passes Ctx) and ticks; getters read the agent; `Remove` prunes both cams.
- [ ] **Step 4:** Brace-check → 0. **Compile-review** Tasks 1–4 together. Commit `feat(pcap): HMC agent + orchestrator (tick, hysteresis, composition)`.

---

## Task 5: Wire the subsystem to the orchestrator

**Files:** `Public/PCAPToolSubsystem.h`, `Private/PCAPToolSubsystem.cpp`

- [ ] **Step 1:** Add `#include "HMCCameraAgent.h"` and a member `FHMCAgentOrchestrator AgentOrchestrator;`.
- [ ] **Step 2:** In `OnVideoFrameResponse`, after computing `M` (and resolving `Profile`/`DCfg`/`CaptureCfg` as today), replace the inline stability block + `MapMetricsToAutoFlags` + `UpdateAutoFlagHysteresis` calls with:
```cpp
                FHMCSkillContext Ctx;
                Ctx.Metrics = M; Ctx.Profile = Profile;
                Ctx.FramingRef = GetFramingRef(DeviceName, CameraIndex);
                Ctx.CameraIndex = CameraIndex; Ctx.NowSeconds = NowT;
                AgentOrchestrator.OnFrame(DeviceName, CameraIndex,
                    GetDevicePipeline(DeviceName), CaptureCfg, Ctx);
```
- [ ] **Step 3:** `GetEffectiveIssueFlags` → `Hardware | GetManualIssueFlags | FrameFlags(NoFace) | AgentOrchestrator.GetStableFlags(DeviceName, CameraIndex)` (drop the old `StableAutoFlags`/`BumpUntil` reads — they live in the agent now).
- [ ] **Step 4:** Point `GetFramingHint`/`GetLightingHint` at the agent reasons (`AgentOrchestrator.GetFramingReason/GetLightingReason`) — or keep them computing from `ImageMetrics` (both are equivalent; prefer the agent for single-source). `UnregisterDevice` → `AgentOrchestrator.Remove(DeviceName)`. `AssignActor` still clears the framing ref (the agent's Framing skill reads the live ref each frame, so no extra reset needed; Stability history self-heals).
- [ ] **Step 5:** Remove now-dead members/methods: `StableAutoFlags`, `AutoFlagHold`, `CentroidHistory`, `BumpUntil`, `UpdateAutoFlagHysteresis` (and the inline block). Keep `ImageMetrics`/`LastAnalyzeTime`/`LastFrameBGRA`/`LastFrameDims`.
- [ ] **Step 6:** Brace-check; **compile-review** the subsystem. Commit `feat(pcap): route HMC monitor through the camera-agent orchestrator`.

---

## Task 6: Calibration UI reads board state from the agent

**Files:** `Private/SHMCSetupPanel.cpp`

- [ ] **Step 1:** Change `SHMCSetupPanel::BoardState(cam)` to return `GetSubsystem()->GetAgentBoardState(ActiveDeviceName, cam)` (add a thin subsystem passthrough `EHMCBoardState GetAgentBoardState(device,cam) const { return AgentOrchestrator.GetBoardState(device,cam); }`), so board state flows through the agent rather than re-running the static in the UI.
- [ ] **Step 2:** Brace-check; compile-review. Commit `feat(pcap): calibration board state sourced from the agent`.

---

## Parity checklist (verify on the Windows build)
- Feed borders + per-camera boxes + status lines read identically to before (focus/exposure/lighting/framing/stability), **plus** too-close/too-far now actually surface.
- Bump still latches ~1.2s and bypasses hysteresis; instability still needs ~2s.
- Stereo calibration section shows the same board state; mono/tripod hides it.
- Hysteresis still ~1s set/clear (HOLD=5 @ ~5Hz).

## Self-review
- **Spec coverage:** agent/skill/orchestrator (T1–T4) · doc-grounded skills reuse the documented `MapMetricsToAutoFlags`/`ClassifyBoardFrame` (T3) · composition per pipeline×config (T4 Configure) · in-engine, behavior-preserving (T5 parity) · board via agent (T6).
- **Type consistency:** `FHMCSkillContext/Result`, `IHMCSkill`, `FHMCCameraAgent`, `FHMCAgentOrchestrator`, `GetStableFlags`, `GetBoardState` names consistent across tasks.
- **Bug fix folded in:** the agent debounces all emitted bits (T4 Step 2), surfacing `TooClose`/`TooFar` that the old hardcoded `Bits[]` dropped.
