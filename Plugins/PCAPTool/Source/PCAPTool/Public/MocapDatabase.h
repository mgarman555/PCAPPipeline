#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCAPToolTypes.h"
#include "MocapDatabase.generated.h"

UCLASS(BlueprintType)
class PCAPTOOL_API UMocapDatabase : public UDataAsset
{
    GENERATED_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PCAP")
    TArray<FProduction> Productions;

    // Raw pointer returns cannot be BlueprintCallable — C++ only.
    FProduction* GetProductionByCode(const FString& ProjectCode);
    FShootDay*   GetDay(const FString& ProjectCode, const FString& DayID);
    FSession*    GetSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID);
    FShot*       GetShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID);
    FTake*       GetTake(const FString& ProjectCode, const FString& DayID, const FString& ShotID, const FString& TakeID);
    TArray<FTake*> GetTakesByLabel(ETakeLabel Label);
    TArray<FTake*> GetUnprocessedQueuedTakes();
};
