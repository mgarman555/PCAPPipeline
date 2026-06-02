#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"
#include "PCAPDatabase.generated.h"

UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPDatabase : public UDataAsset
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    TArray<FProduction> Productions;

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    FProduction* GetProductionByCode(const FString& ProjectCode);

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    FShootDay* GetDay(const FString& ProjectCode, const FString& DayID);

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    FSession* GetSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID);

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    FShot* GetShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID);

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    FTake* GetTake(const FString& ProjectCode, const FString& DayID, const FString& ShotID, const FString& TakeID);

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    TArray<FTake*> GetTakesByLabel(ETakeLabel Label);

    UFUNCTION(BlueprintCallable, Category="PCAP|Database")
    TArray<FTake*> GetUnprocessedQueuedTakes();
};
