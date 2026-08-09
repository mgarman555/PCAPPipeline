#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // enums + FRetargetConfig
#include "StageConfigAsset.generated.h"

// Stage hardware configuration. One DataAsset per stage setup
// (saved to Content/Mocap/_Roster/StageConfigs/[configName].uasset).
// Replaces the former FStageConfig struct so configs are first-class,
// shareable assets referenced by soft-ptr from FProduction / FShootDay.
UCLASS(BlueprintType)
class PCAPTOOL_API UStageConfigAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString ConfigName;          // "Home_Xsens"

    // Visual reference — a mesh laying out the physical mocap stage.
    // Shown as the Stage's card thumbnail, so you can see the stage + (with the systems below) what
    // tools it uses at a glance. Any UObject is accepted and any UObject thumbnails, but only a
    // UStaticMesh is ever drawn in the level: APCAPVolumeVisualizer::RefreshFromStageConfig casts to
    // UStaticMesh and draws nothing for anything else. The Stage Database says so on the card rather
    // than letting the choice read as "the Volume Visualizer is broken" two panels away.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    TSoftObjectPtr<UObject> StageReferenceMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EBodySystem BodySystem = EBodySystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EFaceSystem FaceSystem = EFaceSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EAudioSystem AudioSystem = EAudioSystem::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    EVCamSystem VCamSystem = EVCamSystem::None;

    // Object path of the ULiveLinkPreset asset that brings this stage's Live Link sources up —
    // "/Game/Mocap/Presets/Home_Xsens.Home_Xsens", not a file on disk (the ".llp" the Phase 1 spec
    // wrote was informal notation; UE keeps presets as assets). Nothing applies it automatically:
    // the Stage Database's "Apply now" beside this field is the only reader, and picking the stage
    // in the Call Sheet does not bring Live Link up.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString LiveLinkPresetPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    ETimecodeSource TimecodeSource = ETimecodeSource::Software;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FRetargetConfig RetargetChain;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage")
    FString Notes;

    // ── Volume Visualizer ──────────────────────────────────────────────
    // Vicon DataStream address for this stage's raw-marker feed (Phase 2 / SDK).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
    FString DataStreamHost = TEXT("localhost:801");

    // Meant to pick the Volume Visualizer's marker source per stage — "Phase 2 auto-connects the
    // DataStream at DataStreamHost if bAutoConnectDataStream; otherwise the Live Link stand-in"
    // (2026-06-15 volume-visualizer design:45, plan step 3). NOT READ YET: APCAPVolumeVisualizer::
    // EnsureSource still gates on its own bUseRawMarkers and never consults this, so setting it
    // changes nothing today. Kept because the spec still calls for it — the read side belongs in
    // PCAPVolumeVisualizer.cpp, not here.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
    bool bAutoConnectDataStream = true;

    // Calibration that registers Vicon space onto this stage's FBX. Tuned once, saved here.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
    float VizUnitScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
    FVector VizOriginOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Volume")
    float VizYaw = 0.0f;

    // ── Mocap Manager pairing ──────────────────────────────────────────
    // Durable link to the Epic Performance Capture stage this config describes —
    // a UPCapSessionTemplate, keyed by its UPCapDataAsset AssetUID. Same pairing
    // mechanism as UPCAPPerformerExtension::PCapPerformerUID and
    // UPCAPPropExtension::PCapPropUID: the GUID is the key, so the pairing
    // survives the Epic asset being renamed, moved, or re-pathed.
    // Invalid = not paired; every UPCAPStageBridge call then logs and no-ops.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Mocap Manager")
    FGuid PCapStageUID;

    // Convenience soft ref to the same Epic asset — a cache, never the key.
    // UPCAPStageBridge::ResolvePairedStage uses it only while it still carries
    // PCapStageUID, and falls back to a UID search when it has gone stale.
    // Typed as UObject to avoid a dependency on the Workflow plugin's private header.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stage|Mocap Manager")
    TSoftObjectPtr<UObject> PCapStageAsset;
};
