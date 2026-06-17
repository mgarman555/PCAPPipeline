# HMC Watcher Core — Implementation Plan (definition + detection)

> **For agentic workers:** authored on macOS (no UE 5.7 toolchain). Verification per task = **brace/symbol balance check + reasoning over synthetic cases + subagent compile-review**; the real compile is Madi's Windows build. There is **no local test runner** for UE Slate/subsystem code, so steps use brace-check + compile-review in place of `pytest`. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make the HMC Capture Monitor resolve its checks by `Pipeline × CaptureConfiguration` and detect the full documented good/bad set — adding capture-configuration awareness, lighting-direction classification, nasolabial-weighted focus, explicit too-close/too-far, and a per-pipeline target fps.

**Architecture:** Pure additions to the existing engine. `FPipelineCheckProfile` becomes the per-(pipeline×config) *definition*; `AnalyzeFrameBGRA` gains lighting-direction + region-weighted focus; `MapMetricsToAutoFlags`/`EvaluateCameraIssues` consume the richer profile. No new dependencies, no ML — heuristic watcher (#1), interface left open for #2.

**Tech Stack:** UE 5.7 C++ (`Plugins/PCAPTool`), Slate untouched in this plan (UI is a follow-up plan).

**Scope:** This plan is the watcher *core*. UI (clean Preview, Setup config picker + rubric, scan-readiness gate, focus helper) is the follow-up plan `2026-06-16-hmc-watcher-ui.md`.

---

## File structure

- `Public/PCAPToolTypes.h` — `ECaptureConfiguration`, `EHMCLightDir`, new `EHMCIssueFlag` bits, `FPipelineCheckProfile` fields, `FHMCImageMetrics` fields, `FHMCDeviceConfig.CaptureConfig`.
- `Public/PCAPToolStatics.h` / `Private/PCAPToolStatics.cpp` — `GetDefinition()`, analyzer changes, flag mapping, severity/banner/lighting hint, fps.
- `Private/PCAPToolSubsystem.cpp` — call `GetDefinition`, pass focus region to analyzer, persist `CaptureConfig`, `GetLightingHint`.

---

## Task 1: Capture configuration enum + device field

**Files:** Modify `Public/PCAPToolTypes.h`

- [ ] **Step 1: Add the enum** next to `ECapturePipeline` (search for `enum class ECapturePipeline`):

```cpp
// Camera setup for a device. The watcher resolves its definition by
// Pipeline x CaptureConfiguration — checks differ across these (see spec).
UENUM(BlueprintType)
enum class ECaptureConfiguration : uint8
{
    MonoTripod     UMETA(DisplayName = "Mono - Tripod"),
    MonoHeadMount  UMETA(DisplayName = "Mono - Head Mount"),
    StereoHeadMount UMETA(DisplayName = "Stereo - Head Mount")
};
```

- [ ] **Step 2: Add the device field** in `FHMCDeviceConfig`, right after the `Pipeline` field (line ~268):

```cpp
    // Camera setup — selects the watcher definition together with Pipeline.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECaptureConfiguration CaptureConfig = ECaptureConfiguration::StereoHeadMount;
```

- [ ] **Step 3: Brace-check** — `python3 -c "s=open('Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolTypes.h').read(); print('braces',s.count('{')-s.count('}'))"` → expect `braces 0`.

- [ ] **Step 4: Commit** — `git add -A && git commit -m "feat(pcap): ECaptureConfiguration enum + device field"`

---

## Task 2: Lighting-direction enum + metrics fields

**Files:** Modify `Public/PCAPToolTypes.h`

- [ ] **Step 1: Add the enum** just above `FPipelineCheckProfile`:

```cpp
// Which way the light is wrong (Epic's documented bad-lighting cases). Even = OK.
UENUM(BlueprintType)
enum class EHMCLightDir : uint8
{
    Even   UMETA(DisplayName = "Even"),
    Below  UMETA(DisplayName = "Lit from below"),
    Side   UMETA(DisplayName = "Lit from side"),
    Back   UMETA(DisplayName = "Back-lit"),
    Shadow UMETA(DisplayName = "Shadows present")
};
```

- [ ] **Step 2: Add fields** to `FHMCImageMetrics` (after `SubjectSize`, line ~461):

```cpp
    UPROPERTY(BlueprintReadOnly) EHMCLightDir LightDir = EHMCLightDir::Even;
    UPROPERTY(BlueprintReadOnly) float TopMean    = 0.f;   // 0..1, upper-half mean luma
    UPROPERTY(BlueprintReadOnly) float BottomMean = 0.f;   // lower-half
    UPROPERTY(BlueprintReadOnly) float LeftMean   = 0.f;
    UPROPERTY(BlueprintReadOnly) float RightMean  = 0.f;
    UPROPERTY(BlueprintReadOnly) float CenterMean = 0.f;   // central region (for back-lit)
```

- [ ] **Step 3: Brace-check** (as Task 1 Step 3) → `braces 0`.
- [ ] **Step 4: Commit** — `git commit -am "feat(pcap): EHMCLightDir + per-region luma metrics"`

---

## Task 3: New issue-flag bits (too close / too far)

**Files:** Modify `Public/PCAPToolTypes.h`

- [ ] **Step 1:** In `EHMCIssueFlag`, after `HMC_Issue_LowFPS = 1 << 15,` add (manual bits occupy 16–20; use 21–22):

```cpp
    HMC_Issue_TooClose      = 1 << 21,  // subject size above the pipeline max (red)
    HMC_Issue_TooFar        = 1 << 22,  // subject size below the pipeline min (red)
```

- [ ] **Step 2: Brace-check** → `braces 0`.
- [ ] **Step 3: Commit** — `git commit -am "feat(pcap): TooClose/TooFar issue bits"`

---

## Task 4: Profile additions (fps target, focus region, light-dir toggle)

**Files:** Modify `Public/PCAPToolTypes.h`

- [ ] **Step 1:** In `FPipelineCheckProfile`, after `BumpHoldSeconds` (line ~443) add:

```cpp
    float     TargetFPS         = 30.f;   // below this -> LowFPS (MetaHuman: 60 ideal, 30 for now)
    bool      bClassifyLightDir = true;   // run lighting-direction classification
    FVector2D FocusRegionCenter = FVector2D(0.5, 0.55);  // nasolabial band centre (normalized)
    float     FocusRegionExtent = 0.28f;  // half-extent of the focus window; >=0.5 = whole frame
```

- [ ] **Step 2: Brace-check** → `braces 0`.
- [ ] **Step 3: Commit** — `git commit -am "feat(pcap): profile TargetFPS + nasolabial focus region + light-dir toggle"`

---

## Task 5: GetDefinition(Pipeline, Configuration)

**Files:** Modify `Public/PCAPToolStatics.h`, `Private/PCAPToolStatics.cpp`

- [ ] **Step 1: Declare** in `PCAPToolStatics.h` next to `GetPipelineProfile`:

```cpp
    // Resolve the watcher definition for a pipeline + camera configuration.
    // Starts from the pipeline profile, then applies configuration tweaks.
    static FPipelineCheckProfile GetDefinition(ECapturePipeline Pipeline,
                                               ECaptureConfiguration Config);
```

- [ ] **Step 2: Implement** in `PCAPToolStatics.cpp` after `GetPipelineProfile`:

```cpp
FPipelineCheckProfile UPCAPToolStatics::GetDefinition(ECapturePipeline Pipeline,
                                                      ECaptureConfiguration Config)
{
    FPipelineCheckProfile P = GetPipelineProfile(Pipeline);

    // Configuration tweaks. Mono/tripod is a head-and-shoulders shot (smaller face
    // fraction, wider tolerances); head-mounts are tight, face fills the frame.
    switch (Config)
    {
        case ECaptureConfiguration::MonoTripod:
            P.FramingSizeMin = 0.20f;   // face fills less of frame
            P.FramingSizeMax = 0.60f;
            P.FramingDriftTol = 0.12f;  // performer free in space -> looser
            break;
        case ECaptureConfiguration::MonoHeadMount:
        case ECaptureConfiguration::StereoHeadMount:
        default:
            break;  // tight-face defaults already match the head-mount docs
    }
    return P;
}
```

- [ ] **Step 3: Brace-check** both files → `braces 0`.
- [ ] **Step 4: Compile-review** — dispatch a subagent (see "Compile-review" note) to confirm the .cpp/.h compile on UE 5.7.
- [ ] **Step 5: Commit** — `git commit -am "feat(pcap): GetDefinition(pipeline,config) resolver"`

---

## Task 6: Nasolabial-weighted focus in AnalyzeFrameBGRA

**Files:** Modify `Public/PCAPToolStatics.h`, `Private/PCAPToolStatics.cpp`

- [ ] **Step 1: Change the signature** in `.h` and `.cpp` to accept the focus region (defaults preserve whole-frame behavior):

```cpp
static FHMCImageMetrics AnalyzeFrameBGRA(const TArray<uint8>& BGRA, int32 Width, int32 Height,
                                         FVector2D FocusRegionCenter = FVector2D(0.5,0.5),
                                         float FocusRegionExtent = 1.0f);
```

- [ ] **Step 2:** In the Laplacian loop (currently `for gy in [1,GH-1)`), gate accumulation to the region. Replace the focus loop body's accumulation guard:

```cpp
    const int32 rx0 = FMath::Clamp(int32((FocusRegionCenter.X - FocusRegionExtent) * GW), 1, GW-2);
    const int32 rx1 = FMath::Clamp(int32((FocusRegionCenter.X + FocusRegionExtent) * GW), 1, GW-2);
    const int32 ry0 = FMath::Clamp(int32((FocusRegionCenter.Y - FocusRegionExtent) * GH), 1, GH-2);
    const int32 ry1 = FMath::Clamp(int32((FocusRegionCenter.Y + FocusRegionExtent) * GH), 1, GH-2);
    double LapSum = 0.0, LapSq = 0.0; int32 LapN = 0;
    for (int32 gy = ry0; gy <= ry1; ++gy)
        for (int32 gx = rx0; gx <= rx1; ++gx)
        { /* existing Laplacian computation, unchanged */ }
```

- [ ] **Step 3: Brace-check** → `braces 0`. Confirm the region clamps keep `rx1>=rx0` for extent 1.0 (whole frame).
- [ ] **Step 4: Commit** — `git commit -am "feat(pcap): focus measured over the nasolabial region"`

---

## Task 7: Lighting-direction classification in AnalyzeFrameBGRA

**Files:** Modify `Private/PCAPToolStatics.cpp`

- [ ] **Step 1:** In the single metrics pass, also accumulate a central region. After the quadrant accumulation, add center accumulation inside the existing `gx/gy` loop:

```cpp
            const bool bCenter = FMath::Abs(nx - 0.5) < 0.18 && FMath::Abs(ny - 0.5) < 0.18;
            if (bCenter) { CenterSum += v; ++CenterN; }
```
(declare `double CenterSum=0.0; int32 CenterN=0;` with the other accumulators).

- [ ] **Step 2:** After computing `qm[0..3]`, derive region means + classify:

```cpp
    const double topM = (qm[0]+qm[1])*0.5, botM = (qm[2]+qm[3])*0.5;
    const double leftM = (qm[0]+qm[2])*0.5, rightM = (qm[1]+qm[3])*0.5;
    const double ctrM = (CenterN>0) ? CenterSum/CenterN : Mean;
    M.TopMean=(float)(topM/255.0); M.BottomMean=(float)(botM/255.0);
    M.LeftMean=(float)(leftM/255.0); M.RightMean=(float)(rightM/255.0);
    M.CenterMean=(float)(ctrM/255.0);
    const double inv = (Mean>KINDA_SMALL_NUMBER)?1.0/Mean:0.0;
    const double below = (botM-topM)*inv, side = FMath::Abs(leftM-rightM)*inv, back=(Mean-ctrM)*inv;
    if      (below > 0.30)                 M.LightDir = EHMCLightDir::Below;
    else if (side  > 0.30)                 M.LightDir = EHMCLightDir::Side;
    else if (back  > 0.30)                 M.LightDir = EHMCLightDir::Back;
    else if (M.RegionSpread > 0.40)        M.LightDir = EHMCLightDir::Shadow;
    else                                   M.LightDir = EHMCLightDir::Even;
```

- [ ] **Step 2b:** Set `M.LightDir = EHMCLightDir::Even;` for the no-subject early return path (it already returns a default `M`, so default `Even` holds — confirm).
- [ ] **Step 3: Reason over synthetic cases** (no runner): uniform gray → Even; bottom-bright buffer → Below; left-bright → Side; dark-center/bright-edges → Back; one black quadrant → Shadow. Confirm the thresholds give those.
- [ ] **Step 4: Commit** — `git commit -am "feat(pcap): classify lighting direction (below/side/back/shadow)"`

---

## Task 8: Map new metrics to flags

**Files:** Modify `Private/PCAPToolStatics.cpp` (`MapMetricsToAutoFlags`)

- [ ] **Step 1:** Replace the lighting block to use the classifier when enabled, and add size→too-close/far:

```cpp
    if (P.bCheckLighting)
    {
        if (P.bClassifyLightDir ? (M.LightDir != EHMCLightDir::Even)
                                : (M.RegionSpread > P.RegionSpreadMax))
            Flags |= HMC_Issue_UnevenLight;
    }
    if (P.bCheckFraming)
    {
        if (M.SubjectSize > P.FramingSizeMax) Flags |= HMC_Issue_TooClose;
        if (M.SubjectSize < P.FramingSizeMin) Flags |= HMC_Issue_TooFar;
    }
```

- [ ] **Step 2: Brace-check** → `braces 0`.
- [ ] **Step 3: Commit** — `git commit -am "feat(pcap): map light-dir + size to flags"`

---

## Task 9: TargetFPS in EvaluateCameraIssues

**Files:** Modify `Public/PCAPToolStatics.h`, `Private/PCAPToolStatics.cpp`, callers in `PCAPToolSubsystem.cpp`

- [ ] **Step 1:** Add a param: `static int32 EvaluateCameraIssues(const FHMCDeviceStatus& S, int32 CameraIndex, float TargetFPS = 30.f);`
- [ ] **Step 2:** Replace `if (S.FPS > 0.f && S.FPS < 55.f)` with `if (S.FPS > 0.f && S.FPS < TargetFPS * 0.92f)`.
- [ ] **Step 3:** At call sites in the subsystem, pass `GetDefinition(Cfg.Pipeline, Cfg.CaptureConfig).TargetFPS`.
- [ ] **Step 4: Brace-check** + **compile-review**.
- [ ] **Step 5: Commit** — `git commit -am "feat(pcap): per-pipeline TargetFPS (30 now)"`

---

## Task 10: Severity, banner text, lighting hint

**Files:** Modify `Private/PCAPToolStatics.cpp`, `Public/PCAPToolStatics.h`; `PCAPToolSubsystem.{h,cpp}`

- [ ] **Step 1:** Add `HMC_Issue_TooClose | HMC_Issue_TooFar` to `RedMask` in `GetIssueSeverity`.
- [ ] **Step 2:** In `GetIssueBannerText`, before the UnevenLight line add:

```cpp
    if (Flags & HMC_Issue_TooClose)      return TEXT("Too close · Pull the camera back");
    if (Flags & HMC_Issue_TooFar)        return TEXT("Too far · Move the camera closer");
```

- [ ] **Step 3:** Add a static hint: `static FString GetLightingHintText(EHMCLightDir Dir);` returning "lit from below/side / back-lit / shadows on the face / "" ".
- [ ] **Step 4:** Add `FString UPCAPToolSubsystem::GetLightingHint(const FString& Device, int32 Cam) const` that reads the stored `FHMCImageMetrics.LightDir` (mirror of `GetFramingHint`).
- [ ] **Step 5: Brace-check** + **compile-review**.
- [ ] **Step 6: Commit** — `git commit -am "feat(pcap): too-close/far banners + lighting-direction hint"`

---

## Task 11: Persist CaptureConfig + wire GetDefinition into the monitor

**Files:** Modify `Private/PCAPToolSubsystem.cpp`

- [ ] **Step 1:** In `OnVideoFrameResponse`, compute `const FPipelineCheckProfile Profile = UPCAPToolStatics::GetDefinition(Cfg.Pipeline, Cfg.CaptureConfig);` (replacing the `GetPipelineProfile` call) and pass `Profile.FocusRegionCenter, Profile.FocusRegionExtent` to `AnalyzeFrameBGRA`.
- [ ] **Step 2:** In `SaveConfig`, write `CaptureConfig` (as int): `Obj->SetNumberField(TEXT("captureConfig"), (int32)Cfg.CaptureConfig);`
- [ ] **Step 3:** In `LoadConfig`, read it: `Cfg.CaptureConfig = (ECaptureConfiguration)Obj->GetIntegerField(TEXT("captureConfig"));` guarded with `HasField`.
- [ ] **Step 4: Brace-check** + **compile-review** the whole subsystem.
- [ ] **Step 5: Commit** — `git commit -am "feat(pcap): resolve definition by pipeline x config + persist CaptureConfig"`

---

## Compile-review note

For tasks marked compile-review, dispatch a subagent: *"Compile-review these UE 5.7 C++ changes for `Plugins/PCAPTool` (paste diff). Check: types/signatures match across .h/.cpp, all referenced symbols exist, USTRUCT/UENUM macros valid, no unity-build collisions, delimiter balance. Report BLOCKER/MAJOR/MINOR only."* Fix BLOCKER/MAJOR before commit.

## Self-review (done)
- **Spec coverage:** config axis (T1,T5,T11) · lighting direction (T2,T7,T8,T10) · nasolabial focus (T4,T6) · too-close/far (T3,T8,T10) · target fps 30 (T4,T9) · persistence (T11). Scan-readiness gate + focus helper + clean-feed UI = follow-up UI plan (out of scope here, by design).
- **Type consistency:** `GetDefinition` used in T9/T11 matches T5; `EHMCLightDir` used in T7/T8/T10 matches T2; flag names match T3.
- **No placeholders:** every code step shows code.
