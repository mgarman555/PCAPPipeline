#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPStageBridge.generated.h"

class UStageConfigAsset;
class UWorld;

// Lightweight read-out of an Epic Mocap Manager stage — a UPCapSessionTemplate,
// the asset the Stage tab authors and every session is created from — gathered
// by reflection so PCAPTool never needs the Workflow plugin's private headers.
// Mirrors FPCAPPerformerInfo / FPCAPPropInfo (PCAPMocapData.h) so the two read
// layers feel like one system: name, the durable AssetUID pairing key, a soft
// ref to the asset, then the handful of fields PCAPTool actually reconciles.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPStageInfo
{
    GENERATED_BODY()

    // The template asset's own name. UPCapSessionTemplate carries no self-name
    // field — ProductionName/SessionName below name the *session* it seeds, not
    // the template — so the asset name is the stable label for a stage.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName StageName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid AssetUID;                 // UPCapDataAsset.AssetUID — durable pairing key

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> Asset;  // the Epic session template itself

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString ProductionName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString SessionName;

    // UPCapSessionTemplate.RecordingClockSource as its short enumerator name
    // ("Timecode", "Platform", …). Kept as a string so this public header stays
    // clear of MovieScene, which PCAPTool depends on privately.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString RecordingClockSource;

    // "Epic says X" and "we could not read Epic's field" are different answers
    // and only one is safe to act on, so the reads that matter carry a flag.
    // The divergence report must never read a failed lookup as agreement.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bRecordingClockSourceRead = false;

    // The template's edit lock (UPCapSessionTemplate.bIsEditable): false once a
    // session has been created from it, which freezes its tokenised strings into
    // that session's serialized data. Defaults false and is only trusted when
    // bIsEditableRead is true — an unreadable lock is treated as locked.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bIsEditable = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bIsEditableRead = false;
};

// Why a stage config and its paired Epic stage differ. Only Mismatch is a real
// disagreement of values; the rest are the reasons a comparison could not be
// made, which the operator must read differently from "these agree".
UENUM(BlueprintType)
enum class EPCAPStageDivergenceKind : uint8
{
    Mismatch   UMETA(DisplayName = "Mismatch"),      // both sides readable, values differ
    NotPaired  UMETA(DisplayName = "Not paired"),    // no Epic stage to compare against
    Unreadable UMETA(DisplayName = "Unreadable"),    // paired, but the Epic property did not resolve
    Unmapped   UMETA(DisplayName = "Unmapped"),      // PCAPTool holds a value Epic has no field for
    Locked     UMETA(DisplayName = "Locked")         // template locked; nothing can be pushed to it
};

// One human-readable difference between a UStageConfigAsset and the Epic stage
// it is paired to. Deliberately flat strings — a panel can list it verbatim and
// the log can print it without knowing anything about either data model.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPStageDivergence
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString Field;                  // "Timecode source"

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString PCAPToolValue;          // what the stage config says

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FString MocapManagerValue;      // what the Epic stage says

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    EPCAPStageDivergenceKind Kind = EPCAPStageDivergenceKind::Mismatch;
};

// ---------------------------------------------------------------------------
// PCAPTool stage config ▸ Mocap Manager stage.
//
// UStageConfigAsset stays the source of truth for what hardware a stage runs —
// which body/face/audio/vcam systems, which Vicon DataStream feed, which Live
// Link preset, where timecode comes from. Epic's Mocap Manager keeps its own
// stage concept: a UPCapSessionTemplate asset (what the Stage tab authors, and
// what every session is created from) plus an APerformanceCaptureStageRoot actor
// placed in the level (BP_DemoStage is a Blueprint subclass of it). This bridge
// makes the two one thing instead of two the operator configures by hand:
//
//   GetAllStages()                  read Epic's session templates (Asset Registry + reflection)
//   FindPlacedStageRoots()          the level-side stage actors, read-only
//   PairStageConfig()               durable link, by the Epic asset's AssetUID
//   ApplyStageConfigToSession()     push what PCAPTool owns onto the paired template
//   CompareStageConfigToSession()   what the two disagree about, before shoot day
//
// The two models overlap far less than their names suggest — the template is a
// naming/foldering/recording contract, not a hardware description — so the apply
// side is deliberately narrow and most of UStageConfigAsset stays PCAPTool-only.
// See docs/specs/2026-08-09-pcap-stage-alignment-design.md.
//
// The Workflow plugin's stage classes carry no API macro and live in a private
// module folder, so every Epic-side read and write here is by reflection —
// resolved by /Script path and FProperty name, logging and no-opping when a
// lookup misses (the pattern from UPCAPMocapData / UPCAPTakeRecorderSubsystem).
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPStageBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ── Read side ───────────────────────────────────────────────────────────

    // True when UPCapSessionTemplate resolves (plugin present, /Script path good).
    UFUNCTION(BlueprintPure, Category="PCAP|Stage")
    static bool IsStageModelAvailable();

    // Every Mocap Manager session template in the project (incl. Blueprint
    // subclasses), read via the Asset Registry + reflection. Empty when the
    // template class does not resolve.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static TArray<FPCAPStageInfo> GetAllStages();

    // The Epic stage carrying this AssetUID. False (and OutStage untouched) if
    // no session template in the project has it.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static bool FindStageByUID(const FGuid& StageUID, FPCAPStageInfo& OutStage);

    // The Mocap Manager stage-root actors placed in World. The base class
    // APerformanceCaptureStageRoot is Abstract — every real stage in a level is a
    // Blueprint subclass of it (BP_DemoStage and duplicates) — so this only ever
    // finds placed instances. The bridge never spawns one.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static TArray<AActor*> FindPlacedStageRoots(UWorld* World);

    // ── Pairing (UStageConfigAsset.PCapStageUID) ────────────────────────────

    // The Epic session template a stage config is paired to, or null. Uses the
    // cached soft ref when it still carries the paired UID, else re-finds the
    // asset by UID — so a renamed or moved template keeps its pairing.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static UObject* ResolvePairedStage(const UStageConfigAsset* StageConfig);

    // Pair a stage config to an Epic session template by that template's
    // AssetUID (minting one if the asset has none yet), caching the soft ref
    // alongside. Saves both packages. False if either side is invalid or
    // StageAsset is not a session template.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static bool PairStageConfig(UStageConfigAsset* StageConfig, UObject* StageAsset);

    // Drop the pairing (UID + cached soft ref). False only if StageConfig is null.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static bool UnpairStageConfig(UStageConfigAsset* StageConfig);

    // The stage config paired to this Epic template UID, or null — the reverse
    // lookup, mirroring UPCAPMocapData::FindPerformerExtension.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static UStageConfigAsset* FindStageConfigForStage(const FGuid& StageUID);

    // ── The edit lock ───────────────────────────────────────────────────────

    // Whether the paired template will accept a write. A session template is
    // locked (bIsEditable = false) once a session has been created from it, so
    // that its tokenised folder paths and take names freeze into that session's
    // data — writing a locked template would retroactively re-point an in-flight
    // session's output. Fails closed: an unreadable lock reads as locked.
    // OutReason is operator-facing and always set when this returns false.
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static bool IsPairedStageEditable(const UStageConfigAsset* StageConfig, FString& OutReason);

    // ── Apply side ──────────────────────────────────────────────────────────

    // Push what PCAPTool owns onto the Epic session template this config is
    // paired to, so picking a PCAPTool stage config configures the Mocap
    // Manager's stage instead of the operator configuring both. Today that is
    // exactly one field — timecode source → RecordingClockSource; everything
    // else on the template is naming/foldering the stage config has no opinion
    // about, and PCAPTool does not invent one. Refuses outright on a locked
    // template. Returns the number of fields written (0 = unpaired, locked,
    // unresolved, or unmapped; every case logged).
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static int32 ApplyStageConfigToSession(const UStageConfigAsset* StageConfig);

    // ── Divergence ──────────────────────────────────────────────────────────

    // What a stage config and its paired Epic session template disagree about,
    // or why they could not be compared. Empty means they agree; it never means
    // "could not check". Compares against the *template* (what the next session
    // will use), not against a session record (what a past session ran with).
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static TArray<FPCAPStageDivergence> CompareStageConfigToSession(const UStageConfigAsset* StageConfig);

    // CompareStageConfigToSession() as one line per entry, e.g.
    //   "Timecode source — PCAPTool: Hardware (→ Timecode) · Mocap Manager: Platform"
    UFUNCTION(BlueprintCallable, Category="PCAP|Stage")
    static TArray<FString> DescribeStageDivergence(const UStageConfigAsset* StageConfig);
};
