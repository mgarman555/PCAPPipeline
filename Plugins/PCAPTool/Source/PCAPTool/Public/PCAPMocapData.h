#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPMocapData.generated.h"

class USkeletalMesh;
class UStaticMesh;
class UPCAPPerformerExtension;

// Lightweight read-out of an Epic UPCapPerformerDataAsset (Mocap Manager,
// canonical), gathered by reflection so PCAPTool never needs the plugin's
// private headers. The Epic asset stays the source of truth; PCAPTool's extra
// data lives on the paired UPCAPPerformerExtension (matched by AssetUID).
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPPerformerInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName PerformerName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName LiveLinkSubject;          // UPCapPerformerDataAsset.LiveLinkSubject.Name

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid AssetUID;                 // UPCapDataAsset.AssetUID — durable pairing key

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> Asset;  // the Epic performer asset itself

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<USkeletalMesh> BaseSkeletalMesh;
};

// Read-out of an Epic UPCapPropDataAsset.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPPropInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName PropName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName LiveLinkSubject;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid AssetUID;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> Asset;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UStaticMesh> PropStaticMesh;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<USkeletalMesh> PropSkeletalMesh;
};

// Read-out of an Epic UPCapCharacterDataAsset.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPCharacterInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName CharacterName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FGuid AssetUID;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> Asset;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

    // Source performer asset + retargeter, kept as UObject soft-refs (the concrete
    // types live in the plugin's private module / IKRig).
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> SourcePerformerAsset;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> Retargeter;
};

// ---------------------------------------------------------------------------
// Reflection read-layer over Epic's Performance Capture data model.
//
// The PCap data-asset classes live in the Workflow plugin's *private* module
// (/Script/PerformanceCaptureWorkflow), so PCAPTool resolves them by path and
// reads their FProperties by name — the same pattern as PCAPTakeRecorderSubsystem.
// All getters degrade to empty/none if the plugin or a property is absent
// (WITH_PCAP_WORKFLOW=0, or a renamed field), so call sites stay simple.
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPMocapData : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // True when the Performance Capture Workflow plugin's performer class resolves.
    UFUNCTION(BlueprintPure, Category="PCAP|Mocap Data")
    static bool IsWorkflowAvailable();

    // Every UPCapPerformerDataAsset in the project (incl. Blueprint subclasses),
    // read via the Asset Registry + reflection.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Data")
    static TArray<FPCAPPerformerInfo> GetAllPerformers();

    // Every UPCapPropDataAsset in the project.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Data")
    static TArray<FPCAPPropInfo> GetAllProps();

    // Every UPCapCharacterDataAsset in the project.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Data")
    static TArray<FPCAPCharacterInfo> GetAllCharacters();

    // The PCAPTool extension paired to an Epic performer by AssetUID, or null.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Data")
    static UPCAPPerformerExtension* FindPerformerExtension(const FGuid& PerformerUID);
};
