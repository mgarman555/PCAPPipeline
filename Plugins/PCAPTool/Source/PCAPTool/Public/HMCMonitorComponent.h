#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PCAPToolTypes.h"
#include "HMCMonitorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHMCStatusUpdated, const FHMCDeviceStatus&, UpdatedStatus);

// CameraIndex: 1 = Top, 2 = Bot. Frame is nullptr if decode failed or device unreachable.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHMCFrameReceived,
    const FString&, DeviceID, int32, CameraIndex, UTexture2D*, Frame);

UCLASS(ClassGroup = (PCAP), meta = (BlueprintSpawnableComponent))
class PCAPTOOL_API UHMCMonitorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHMCMonitorComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ─── Device Registration ─────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterDevice(const FString& DeviceID, const FString& IPAddress);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FHMCDeviceStatus GetDeviceStatus(const FString& DeviceID) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceStatus> GetAllDeviceStatuses() const;

    // ─── Commands ────────────────────────────────────────────────────────────

    // Sends GET /control?cmd=[Command]&param= to the device.
    // Use "startrecording" and "stoprecording" for test-only record triggers.
    // Take Recorder is master clock in production — these are for isolated HMC testing only.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SendCommand(const FString& DeviceID, const FString& Command);

    // ─── Video Frames ─────────────────────────────────────────────────────────

    // Fires a single GET /video?cam=[CameraIndex] request.
    // CameraIndex: 1 = Top cam, 2 = Bot cam.
    // On completion broadcasts OnFrameReceived. Bind before calling.
    // Call this from a Blueprint timer for live feed (e.g. every 100ms for ~10fps).
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RequestVideoFrame(const FString& DeviceID, int32 CameraIndex);

    // Returns the last successfully decoded frame texture, or nullptr if none yet.
    // Texture is owned by this component — do not call AddToRoot or RemoveFromRoot on it.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    UTexture2D* GetLastFrame(const FString& DeviceID, int32 CameraIndex) const;

    // ─── Delegates ───────────────────────────────────────────────────────────

    // Fires on game thread after each status poll (success or failure).
    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnHMCStatusUpdated OnStatusUpdated;

    // Fires on game thread after each video frame request completes.
    // Frame is nullptr if the device is unreachable or JPEG decode failed.
    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnHMCFrameReceived OnFrameReceived;

    // ─── Settings ────────────────────────────────────────────────────────────

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

    // Frame textures stored here to prevent GC. Key = DeviceID + "_" + CameraIndex.
    UPROPERTY()
    TMap<FString, TObjectPtr<UTexture2D>> FrameTextures;

    void PollAllDevices();
    void PollDevice(const FString& DeviceID, const FString& IPAddress);
    void OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                        bool bWasSuccessful, FString DeviceID, FString IPAddress);
    void OnVideoFrameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                              bool bWasSuccessful, FString DeviceID, int32 CameraIndex);
};
