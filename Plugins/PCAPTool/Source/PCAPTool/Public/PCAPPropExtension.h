#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"   // EStreamStatus
#include "PCAPPropExtension.generated.h"

// ---------------------------------------------------------------------------
// PCAPTool extension for an Epic Performance Capture prop.
//
// Epic's UPCapPropDataAsset (canonical) owns name, Live Link subject, meshes,
// offset, prop component. This sidecar adds the PCAPTool-only metadata (history,
// notes, last-known stream status) and links back by the Epic asset's AssetUID.
// ---------------------------------------------------------------------------
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPPropExtension : public UDataAsset
{
    GENERATED_BODY()

public:
    // Link to the Epic prop asset this extends (its UPCapDataAsset AssetUID).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Link")
    FGuid PCapPropUID;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Link")
    TSoftObjectPtr<UObject> PropAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="History")
    TArray<FString> ProductionHistory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Status")
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Notes")
    FString Notes;
};
