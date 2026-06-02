#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCAPToolTypes.h"
#include "HMCMonitorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHMCStatusUpdated, const FHMCDeviceStatus&, UpdatedStatus);

UCLASS(ClassGroup = (PCAP), meta = (BlueprintSpawnableComponent))
class PCAPTOOL_API UHMCMonitorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHMCMonitorComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // Register a device for polling. Safe to call before or after BeginPlay.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterDevice(const FString& DeviceID, const FString& IPAddress);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FHMCDeviceStatus GetDeviceStatus(const FString& DeviceID) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceStatus> GetAllDeviceStatuses() const;

    // Fires on game thread after each device poll completes (success or timeout).
    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnHMCStatusUpdated OnStatusUpdated;

    // Seconds between poll cycles. Default 2.0.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCAP|HMC")
    float PollIntervalSeconds = 2.0f;

private:
    struct FDeviceRecord
    {
        FString IPAddress;
        FHMCDeviceStatus Status;
    };

    TMap<FString, FDeviceRecord> DeviceRegistry;
    FTimerHandle PollTimerHandle;

    void PollAllDevices();
    void PollDevice(const FString& DeviceID, const FString& IPAddress);
    void OnPollResponse(FHttpResponsePtr Response, bool bWasSuccessful,
                        FString DeviceID, FString IPAddress);
};
