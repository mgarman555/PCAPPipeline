# HMC Watcher UI — Implementation Plan (clean feed + config picker + scan-readiness gate + focus helper)

> **For agentic workers:** authored on macOS (no UE 5.7 toolchain). Verification = brace/symbol check + subagent compile-review; real compile is the Windows build. Builds on the watcher *core* (`2026-06-16-hmc-watcher-core.md`). Steps use checkbox syntax.

**Goal:** Surface the watcher to the operator — Preview stays a clean feed with a one-line reason; Setup gets a capture-configuration picker, a per-actor scan-readiness gate (neutral/teeth/ROM saved as stills), and an in-tool focus-tuning helper.

**Architecture:** UI-layer changes in the two Slate panels + supporting subsystem methods. No new image math. Identity stills are encoded from the cached decoded BGRA via `IImageWrapper`.

---

## File structure
- `Private/SHMCPreviewPanel.cpp` — append lighting hint to the status line (Preview is already a clean feed).
- `Public/PCAPToolTypes.h` — `FHMCDeviceConfig` scan-readiness + focus-override fields.
- `Public/PCAPToolSubsystem.h` / `Private/PCAPToolSubsystem.cpp` — config setter, still capture + persistence, focus override, ready-to-scan logic, cached BGRA.
- `Public/SHMCSetupPanel.h` / `Private/SHMCSetupPanel.cpp` — remove crosshair overlay; capture-config picker; scan-readiness gate; focus helper.

---

## Task 1: Preview — append lighting hint

**Files:** Modify `Private/SHMCPreviewPanel.cpp` (`DeviceErrorText`, ~line 420)

- [ ] **Step 1:** After the `HMC_Issue_FramingDrift` hint block, add:
```cpp
        if (Flags & HMC_Issue_UnevenLight)
        {
            const FString LHint = Sub->GetLightingHint(DeviceName, Cam);
            if (!LHint.IsEmpty()) Msg += FString::Printf(TEXT(" (%s)"), *LHint);
        }
```
- [ ] **Step 2:** Brace-check → 0. Commit `feat(pcap): Preview shows lighting-direction reason`.

---

## Task 2: Setup — remove the crosshair overlay (clean feed)

**Files:** Modify `Private/SHMCSetupPanel.cpp` (the two crosshair `SOverlay::Slot()` blocks ~632–652)

- [ ] **Step 1:** Delete the vertical + horizontal crosshair overlay slots (the `SBox` width/height 2.f `SBorder` "WhiteBrush" at 22% alpha), leaving the feed image, issue banner, "No Feed", and label. The feed is now clean.
- [ ] **Step 2:** Brace-check → 0. Commit `feat(pcap): clean Setup feed (drop framing crosshair)`.

---

## Task 3: Capture-configuration picker

**Files:** `Public/PCAPToolSubsystem.h`/`.cpp`, `Public/SHMCSetupPanel.h`, `Private/SHMCSetupPanel.cpp`

- [ ] **Step 1 (subsystem):** Add, mirroring `GetDevicePipeline`/`SetDevicePipeline`:
```cpp
// .h
ECaptureConfiguration GetDeviceCaptureConfig(const FString& DeviceName) const;
void SetDeviceCaptureConfig(const FString& DeviceName, ECaptureConfiguration Config);
```
```cpp
// .cpp
ECaptureConfiguration UPCAPToolSubsystem::GetDeviceCaptureConfig(const FString& DeviceName) const
{ const FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName); return C ? C->CaptureConfig : ECaptureConfiguration::StereoHeadMount; }
void UPCAPToolSubsystem::SetDeviceCaptureConfig(const FString& DeviceName, ECaptureConfiguration Config)
{ if (FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName)) { C->CaptureConfig = Config; SaveConfig(); } }
```
- [ ] **Step 2 (panel .h):** Declare `static FString ConfigName(ECaptureConfiguration);`, `TSharedRef<SWidget> BuildConfigDropdown();`, `void OnConfigChosen(int32 ConfigValue);`.
- [ ] **Step 3 (panel .cpp):** Implement `ConfigName` (Mono - Tripod / Mono - Head Mount / Stereo - Head Mount), `BuildConfigDropdown` (clone `BuildPipelineDropdown` with the 3 configs), `OnConfigChosen` (`Sub->SetDeviceCaptureConfig(ActiveDeviceName, (ECaptureConfiguration)ConfigValue)`).
- [ ] **Step 4:** In `BuildCaptureMonitor`, add a CONFIG row mirroring the PIPELINE row, above the read-outs.
- [ ] **Step 5:** Brace-check both files → 0. Commit `feat(pcap): Setup capture-configuration picker`.

---

## Task 4: Scan-readiness data model

**Files:** Modify `Public/PCAPToolTypes.h` (`FHMCDeviceConfig`)

- [ ] **Step 1:** Add after `bPreppedForPreview`:
```cpp
    // ── Scan readiness (per actor on this device) ──
    UPROPERTY() bool bPerformerPrepConfirmed = false;
    UPROPERTY() bool bNeutralCaptured = false;
    UPROPERTY() bool bTeethCaptured   = false;
    UPROPERTY() bool bROMCaptured     = false;
    UPROPERTY() FString NeutralStillPath;   // PNG on disk (Saved/PCAPTool/Identity)
    UPROPERTY() FString TeethStillPath;
    UPROPERTY() FString ROMTakeLabel;       // marked device take id/label
    // ── Focus helper (per-device tuned override; <0 = use pipeline default) ──
    UPROPERTY() float FocusMinOverride = -1.f;
```
- [ ] **Step 2:** Brace-check → 0. Commit `feat(pcap): scan-readiness + focus-override fields`.

---

## Task 5: Subsystem — still capture, ROM mark, focus override, ready logic, persistence

**Files:** `Public/PCAPToolSubsystem.h`, `Private/PCAPToolSubsystem.cpp`

- [ ] **Step 1: Cache decoded BGRA** in `OnVideoFrameResponse` right after `ImageMetrics.Add(CamKey, M);`:
```cpp
                LastFrameBGRA.Add(CamKey, Uncompressed);
                LastFrameDims.Add(CamKey, FIntPoint(IW->GetWidth(), IW->GetHeight()));
```
Add members `TMap<FString, TArray<uint8>> LastFrameBGRA; TMap<FString, FIntPoint> LastFrameDims;` to the .h.

- [ ] **Step 2: Identity still capture** (encode cached BGRA → PNG):
```cpp
bool UPCAPToolSubsystem::CaptureIdentityStill(const FString& DeviceName, int32 CameraIndex, bool bTeeth)
{
    FHMCDeviceConfig* C = RegisteredConfigs.Find(DeviceName);
    if (!C) return false;
    const FString CamKey = FString::Printf(TEXT("%s_%d"), *DeviceName, CameraIndex);
    const TArray<uint8>* BGRA = LastFrameBGRA.Find(CamKey);
    const FIntPoint* Dim = LastFrameDims.Find(CamKey);
    if (!BGRA || !Dim || BGRA->Num() < Dim->X * Dim->Y * 4) return false;

    IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> PNG = Mod.CreateImageWrapper(EImageFormat::PNG);
    if (!PNG.IsValid() || !PNG->SetRaw(BGRA->GetData(), BGRA->Num(), Dim->X, Dim->Y, ERGBFormat::BGRA, 8))
        return false;
    const TArray64<uint8>& Png = PNG->GetCompressed();
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("PCAPTool/Identity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    const FString Path = Dir / FString::Printf(TEXT("%s_%s.png"), *DeviceName, bTeeth ? TEXT("teeth") : TEXT("neutral"));
    if (!FFileHelper::SaveArrayToFile(TArray<uint8>(Png.GetData(), Png.Num()), *Path)) return false;

    if (bTeeth) { C->TeethStillPath = Path; C->bTeethCaptured = true; }
    else        { C->NeutralStillPath = Path; C->bNeutralCaptured = true; }
    SaveConfig();
    return true;
}
```
Pick the camera with a subject (prefer cam 0); the panel passes the camera it captured from.

- [ ] **Step 3: ROM mark + prep + focus override + getters/ready:**
```cpp
void UPCAPToolSubsystem::SetPerformerPrepConfirmed(const FString& D, bool b){ if(auto*C=RegisteredConfigs.Find(D)){C->bPerformerPrepConfirmed=b;SaveConfig();} }
void UPCAPToolSubsystem::MarkROMCaptured(const FString& D, const FString& Label){ if(auto*C=RegisteredConfigs.Find(D)){C->bROMCaptured=true;C->ROMTakeLabel=Label;SaveConfig();} }
void UPCAPToolSubsystem::ClearScanReadiness(const FString& D){ if(auto*C=RegisteredConfigs.Find(D)){C->bPerformerPrepConfirmed=C->bNeutralCaptured=C->bTeethCaptured=C->bROMCaptured=false;C->NeutralStillPath.Empty();C->TeethStillPath.Empty();C->ROMTakeLabel.Empty();SaveConfig();} }
FHMCDeviceConfig UPCAPToolSubsystem::GetDeviceConfig(const FString& D) const { const auto*C=RegisteredConfigs.Find(D); return C?*C:FHMCDeviceConfig(); }
void UPCAPToolSubsystem::SetFocusMinOverride(const FString& D, float V){ if(auto*C=RegisteredConfigs.Find(D)){C->FocusMinOverride=V;SaveConfig();} }
bool UPCAPToolSubsystem::IsReadyToScan(const FString& D) const {
    const auto* C = RegisteredConfigs.Find(D); if (!C) return false;
    const bool bChecks = GetIssueSeverityForDevice(D) == EHMCIssueSeverity::None;   // helper or inline both cams
    return C->bPerformerPrepConfirmed && C->bNeutralCaptured && C->bROMCaptured && bChecks; // teeth recommended, not required
}
```
For `bChecks`, inline: both cameras' `GetEffectiveIssueFlags` roll up to None via `UPCAPToolStatics::GetIssueSeverity`.

- [ ] **Step 4: Apply focus override** in `OnVideoFrameResponse` after resolving `Profile`:
```cpp
                if (const FHMCDeviceConfig* DCfg = RegisteredConfigs.Find(DeviceName))
                    if (DCfg->FocusMinOverride >= 0.f) const_cast<FPipelineCheckProfile&>(Profile).FocusMin = DCfg->FocusMinOverride;
```
(or make `Profile` non-const). Prefer making `Profile` a non-const local.

- [ ] **Step 5: Persist** the new fields in Save/LoadConfig (bools + string paths + FocusMinOverride), guarded reads.
- [ ] **Step 6:** Declare all new methods + members in the .h. Brace-check, compile-review. Commit `feat(pcap): identity stills + ROM/prep state + focus override + ready-to-scan`.

---

## Task 6: Setup — scan-readiness gate UI

**Files:** `Public/SHMCSetupPanel.h`, `Private/SHMCSetupPanel.cpp`

- [ ] **Step 1:** Declare `TSharedRef<SWidget> BuildScanReadinessGate();` + handlers `FReply OnConfirmPrep(); FReply OnCaptureNeutral(); FReply OnCaptureTeeth(); FReply OnRecordROM();` and a helper `int32 SubjectCameraIndex() const;` (returns the cam with a subject, default 0).
- [ ] **Step 2:** `BuildScanReadinessGate` renders a titled section with four rows (Performer prep / Neutral / Teeth / ROM), each: a state glyph (✓ green when captured, ○ grey), a label, a button. Below: a "READY TO SCAN" pill (green) when `Sub->IsReadyToScan(ActiveDeviceName)`, else red "NOT READY · <missing>". State read live via `Sub->GetDeviceConfig(ActiveDeviceName)`.
- [ ] **Step 3:** Handlers call the subsystem (`OnCaptureNeutral` → `CaptureIdentityStill(ActiveDeviceName, SubjectCameraIndex(), false)`, teeth → `true`, ROM → `MarkROMCaptured(ActiveDeviceName, GetStatus(ActiveDeviceName).CurrentTakeName)`, prep → toggle `SetPerformerPrepConfirmed`).
- [ ] **Step 4:** Add `BuildScanReadinessGate()` into `BuildCaptureMonitor` after the read-outs.
- [ ] **Step 5:** Brace-check, compile-review. Commit `feat(pcap): Setup scan-readiness gate (neutral/teeth/ROM)`.

---

## Task 7: Setup — focus helper

**Files:** `Public/SHMCSetupPanel.h`, `Private/SHMCSetupPanel.cpp`

- [ ] **Step 1:** Members: `float FocusSharpSample = -1.f; float FocusSoftSample = -1.f;` Handlers `FReply OnCaptureFocusSharp(); FReply OnCaptureFocusSoft(); FReply OnUseFocusMin();` + `TSharedRef<SWidget> BuildFocusHelper();`.
- [ ] **Step 2:** Sharp/Soft read the live metric: `FocusSharpSample = Sub->GetImageMetrics(ActiveDeviceName, SubjectCameraIndex()).FocusScore;` (same for soft). `OnUseFocusMin` proposes `0.5*(sharp+soft)` (only when both ≥0 and sharp>soft) and calls `Sub->SetFocusMinOverride(ActiveDeviceName, Proposed)`.
- [ ] **Step 3:** `BuildFocusHelper` shows two capture buttons with their captured values, the proposed `FocusMin`, and a "Use" button; a live read of the current override.
- [ ] **Step 4:** Add into `BuildCaptureMonitor`. Brace-check, compile-review. Commit `feat(pcap): Setup focus calibration helper`.

---

## Task 8: Stereo board-calibration mode (added)

**Files:** `PCAPToolTypes.h`, `PCAPToolStatics.{h,cpp}`, `PCAPToolSubsystem.{h,cpp}`, `SHMCSetupPanel.{h,cpp}`

- [x] `EHMCBoardState`; `FHMCImageMetrics.EdgeEnergy` (whole-frame var-of-Laplacian); board fields on `FPipelineCheckProfile` (`bCheckBoard`/`BoardEdgeMin`/`BoardSizeMin`/`BoardSizeMax`/`BoardCenterTol`); calibration fields on `FHMCDeviceConfig`.
- [x] `AnalyzeFrameBGRA` computes `EdgeEnergy`; `GetDefinition` enables `bCheckBoard` for `StereoHeadMount`; `ClassifyBoardFrame` + `GetBoardStateText`.
- [x] `CaptureCalibrationStill` (shares a new `SaveCameraStillPng` helper with identity capture); start/end calibration persisted.
- [x] `BuildCalibrationSection` (stereo-only via Visibility): live coarse board state per camera (OK / too close / too far / off-centre / no-board-occluded), start/end capture rows, and an eyes-on checklist for the fine cases (inverted / horizontal / over-rotated / hand / actor) that need CV.
- Board thresholds are first-guess (need on-rig tuning). Fine pose detection is the #2 (CV) follow-up.

## Self-review
- Spec coverage: clean Preview (T1, plus already-clean feed) · clean Setup feed (T2) · config picker (T3) · scan-readiness gate incl. teeth saved (T4–T6) · focus helper (T7). Stereo board-calibration *mode* — built in Task 8 (coarse edge-energy board check + start/end capture + eyes-on checklist); fine pose detection (inverted/over-rotated/hand) is the #2 CV follow-up.
- Type consistency: `CaptureIdentityStill(device,cam,bTeeth)`, `MarkROMCaptured(device,label)`, `IsReadyToScan(device)`, `GetDeviceConfig`, `SetFocusMinOverride` names match across subsystem + panel call sites.
- Risk: PNG encode + file IO — guarded with null/size checks; failure returns false and the gate stays "not captured".
