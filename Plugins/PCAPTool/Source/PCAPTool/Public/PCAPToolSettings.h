#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MocapDatabase.h"
#include "PCAPToolSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PCAP Tool"))
class PCAPTOOL_API UPCAPToolSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:

    UPROPERTY(Config, EditAnywhere, Category="PCAP Tool")
    TSoftObjectPtr<UMocapDatabase> DatabaseAsset;

    static UPCAPToolSettings* Get();
    UMocapDatabase* GetDatabase() const;

    // Returns the assigned database, or creates one in the PCAP tool area
    // (PCAPPaths::MasterDatabase) and self-assigns it if none is set. Editor only.
    UMocapDatabase* GetOrCreateDatabase();

    virtual FName GetCategoryName() const override { return FName("PCAP"); }
    virtual FName GetSectionName()  const override { return FName("PCAP Tool"); }
};
