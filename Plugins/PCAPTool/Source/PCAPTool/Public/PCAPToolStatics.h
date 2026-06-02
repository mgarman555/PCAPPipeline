#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPToolTypes.h"
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
};
