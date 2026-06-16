#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPToolTypes.h"

class UActorRosterEntry;

#include "PCAPToolStatics.generated.h"

UCLASS()
class PCAPTOOL_API UPCAPToolStatics : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // Format: DayNumber(3) + ShotSlot(3) + "_" + TakeNumber(3)
    // Example: GenerateTakeID("001", "001", "003") -> "001001_003"
    UFUNCTION(BlueprintCallable, Category="PCAP|Takes")
    static FString GenerateTakeID(const FString& DayNumber, const FString& ShotSlot, const FString& TakeNumber);

    // Returns next zero-padded take number for the shot ("001", "002", etc.)
    UFUNCTION(BlueprintCallable, Category="PCAP|Takes")
    static FString GenerateNextTakeNumber(const FShot& Shot);

    // 901=Calibration, 902=TestShot, 903=Retargeting, 001+=Production
    UFUNCTION(BlueprintCallable, Category="PCAP|Takes")
    static FString ShotSlotForType(EShotType ShotType, int32 ShotIndex = 1);

    // Seeds a new shoot day with standard calibration/test/retarget/production shots.
    UFUNCTION(BlueprintCallable, Category="PCAP|Days")
    static FShootDay SeedNewShootDay(const FString& DayID, const FDateTime& CalendarDate);

    // ── Roster ────────────────────────────────────────────────────────────────

    // In-memory copy of an actor's roster defaults into a shot subject — the data
    // half of "call actor to shot". Sets bHasBodyStream/bHasFaceStream from whether
    // the default Live Link subject names are set. CharacterName is left blank
    // (shot-level) and bIsActive defaults to false. No asset I/O.
    UFUNCTION(BlueprintCallable, Category="PCAP|Roster")
    static FShotSubject MakeShotSubjectFromRoster(const UActorRosterEntry* Entry);

    // ── HMC issue evaluation (single source of truth for both monitor paths) ──

    // Hardware issue bitmask (EHMCIssueFlag) for one camera of a polled device.
    // Combines per-camera signals (streaming/exposure/dropped) with device-wide
    // ones (battery/storage/cpu/temp/clip). CameraIndex 0 = Top, 1 = Bot.
    // TargetFPS: the pipeline's minimum frame rate (MetaHuman: 60 ideal, 30 for now).
    UFUNCTION(BlueprintCallable, Category="PCAP|HMC")
    static int32 EvaluateCameraIssues(const FHMCDeviceStatus& Status, int32 CameraIndex, float TargetFPS = 30.f);

    // Rolls a flag bitmask up to a single severity for border color.
    // Red wins over Amber wins over None. Accepts hardware | manual flags.
    UFUNCTION(BlueprintPure, Category="PCAP|HMC")
    static EHMCIssueSeverity GetIssueSeverity(int32 IssueFlags);

    // Highest-priority human-readable banner line for a flag bitmask.
    // Returns "All clear · Ready to record" when no flags are set.
    UFUNCTION(BlueprintPure, Category="PCAP|HMC")
    static FString GetIssueBannerText(int32 IssueFlags);

    // Short reason for a lighting-direction classification (for the status area),
    // e.g. "lit from below". Empty for Even. Mirrors the framing-hint pattern.
    static FString GetLightingHintText(EHMCLightDir Dir);

    // Cheap "is a subject in the frame" heuristic over a decoded BGRA buffer
    // (sampled mean luminance vs threshold). Interim stand-in for real face
    // detection — IR head-cams light the face brightly when it's in the box.
    // Not BlueprintCallable (raw pixel buffer); called from the monitor classes.
    static bool FrameHasSubject(const TArray<uint8>& BGRA, int32 Width, int32 Height);

    // ── Automatic image analysis (Capture Monitor) ──────────────────────────
    // Per-frame metrics from a decoded BGRA buffer: focus (variance-of-Laplacian),
    // luma stats (mean / blown / crushed), regional luma spread, and a coarse
    // brightness-weighted subject centroid/size. Pure; runs on a downsampled grid.
    // FocusRegion* weights the focus measure to the nasolabial band (the doc's
    // focus target). Defaults to the whole frame so existing callers are unchanged.
    static FHMCImageMetrics AnalyzeFrameBGRA(const TArray<uint8>& BGRA, int32 Width, int32 Height,
                                             FVector2D FocusRegionCenter = FVector2D(0.5, 0.5),
                                             float FocusRegionExtent = 1.0f);

    // Active-pipeline check bundle (active checks + thresholds + framing target).
    static FPipelineCheckProfile GetPipelineProfile(ECapturePipeline Pipeline);

    // Resolve the watcher definition for a pipeline + camera configuration. Starts
    // from the pipeline profile, then applies configuration-specific tweaks.
    static FPipelineCheckProfile GetDefinition(ECapturePipeline Pipeline,
                                               ECaptureConfiguration Config);

    // Coarse stereo calibration-board classification (edge energy + bright-region
    // position/size) + a label for the status area. Fine pose cases stay operator-judged.
    static EHMCBoardState ClassifyBoardFrame(const FHMCImageMetrics& Metrics,
                                             const FPipelineCheckProfile& Profile);
    static FString GetBoardStateText(EHMCBoardState State);

    // Maps one frame's metrics to auto EHMCIssueFlag bits, honoring the pipeline's
    // active checks/thresholds and the captured framing reference (framing drift is
    // only evaluated when Ref.bSet). Pure — hysteresis is applied by the caller.
    static int32 MapMetricsToAutoFlags(const FHMCImageMetrics& Metrics,
                                       const FPipelineCheckProfile& Profile,
                                       const FHMCFramingRef& Ref);
};
