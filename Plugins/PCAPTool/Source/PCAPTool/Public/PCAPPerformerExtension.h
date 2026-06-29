#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // FFaceStreamEntry / FAudioStreamEntry
#include "PCAPPerformerExtension.generated.h"

class UHMCRigEntry;

// ---------------------------------------------------------------------------
// PCAPTool extension for an Epic Performance Capture performer.
//
// Epic's UPCapPerformerDataAsset (Mocap Manager, canonical) owns the core
// performer fields — name, body Live Link subject, base/proportioned mesh, IK
// rig. PCAPTool does NOT duplicate those; this sidecar attaches the things
// Epic's model has no place for (face/HMC capture, audio, the digital-double
// extras) and links back to the Epic asset by its AssetUID.
//
// Pairing: PCapPerformerUID == UPCapPerformerDataAsset.AssetUID (read via
// reflection in UPCAPMocapData). Saved under the session's Performer folder
// next to the Epic asset.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPPerformerExtension : public UDataAsset
{
    GENERATED_BODY()

public:
    // Link to the Epic performer asset this extends (its UPCapDataAsset AssetUID).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Link")
    FGuid PCapPerformerUID;

    // Convenience soft-ref to the same Epic asset (resolves by path; the UID is
    // the durable key). Typed as UObject to avoid a dep on the private PCap header.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Link")
    TSoftObjectPtr<UObject> PerformerAsset;

    // ── Face / HMC capture (Epic's performer covers body Live Link only) ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face")
    FFaceStreamEntry FaceStream;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face")
    TSoftObjectPtr<UHMCRigEntry> HMCRig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face")
    TSoftObjectPtr<UObject> FaceScan;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face")
    bool bUseFaceScanOnMetaHuman = false;

    // ── Audio ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    TArray<FAudioStreamEntry> AudioStreams;

    // ── Misc PCAPTool metadata ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity")
    TSoftObjectPtr<UTexture2D> Headshot;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="History")
    TArray<FString> ProductionHistory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString Notes;
};
