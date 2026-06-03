#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PCAPToolTypes.h"
#include "HMCMonitorComponent.generated.h"

class IWebSocket;

// DeviceName + full status struct (value copy — acceptable for a monitoring tool)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHMCStatusUpdated,
    FString, DeviceName, FHMCDeviceStatus, Status);

// DeviceName, CameraIndex (0=first, 1=second), decoded texture (nullptr = failed)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHMCFrameReceived,
    FString, DeviceName, int32, CameraIndex, UTexture2D*, Frame);

// DeviceName + new connection state
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHMCConnectionChanged,
    FString, DeviceName, EHMCConnectionState, ConnectionState);

UCLASS(ClassGroup = (PCAP), meta = (BlueprintSpawnableComponent))
class PCAPTOOL_API UHMCMonitorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UHMCMonitorComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ─── Registration ────────────────────────────────────────────────────────

    // Add or update a device entry. Does NOT open the WebSocket — call ConnectDevice after.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterDevice(const FHMCDeviceConfig& Config);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void UnregisterDevice(const FString& DeviceName);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceConfig> GetRegisteredDevices() const;

    // ─── Connection ───────────────────────────────────────────────────────────

    // Opens WebSocket for one device. Idempotent — safe to call if already connected.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void ConnectDevice(const FString& DeviceName);

    // Opens WebSockets for all registered devices.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void ConnectAll();

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void DisconnectDevice(const FString& DeviceName);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void DisconnectAll();

    // ─── Status ───────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FHMCDeviceStatus GetDeviceStatus(const FString& DeviceName) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceStatus> GetAllDeviceStatuses() const;

    // ─── Camera Feeds ─────────────────────────────────────────────────────────

    // Returns feeds for a specific actor — used by Preview to build actor groups.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCCameraFeed> GetFeedsForActor(const FString& ActorName) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCCameraFeed> GetAllFeeds() const;

    // Manually toggle Clear/NeedsFix in Setup. Cannot set Disconnected manually.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetFeedState(const FString& DeviceName, EHMCCameraRole Role, EHMCFeedState State);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetCameraRole(const FString& DeviceName, int32 CameraIndex, EHMCCameraRole NewRole);

    // ─── Commands ─────────────────────────────────────────────────────────────

    // Sends JSON {"cmd": "[Command]"} over the device's WebSocket.
    // For startrecording / stoprecording — TEST ONLY, not used in production pipeline.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SendCommand(const FString& DeviceName, const FString& Command);

    // ─── Video Frames (HTTP pull) ─────────────────────────────────────────────

    // Fires GET /video?cam=[CameraIndex+1] for an on-demand snapshot.
    // Result arrives via OnFrameReceived. For live feed, call on a timer.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RequestVideoFrame(const FString& DeviceName, int32 CameraIndex);

    // Returns the last successfully decoded frame for this device/camera. May be null.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    UTexture2D* GetLastFrame(const FString& DeviceName, int32 CameraIndex) const;

    // ─── Persistence ──────────────────────────────────────────────────────────

    // Saved/PCAPTool/HMCConfig.json
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SaveConfig() const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void LoadConfig();

    // ─── Delegates ───────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnHMCStatusUpdated OnStatusUpdated;

    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnHMCFrameReceived OnFrameReceived;

    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnHMCConnectionChanged OnConnectionChanged;

private:
    TMap<FString, FHMCDeviceConfig>          RegisteredConfigs;   // keyed by DeviceName
    TMap<FString, FHMCDeviceStatus>          DeviceStatuses;      // keyed by DeviceName
    TMap<FString, TArray<FHMCCameraFeed>>    CameraFeeds;         // keyed by ActorName
    TMap<FString, TSharedPtr<IWebSocket>>    ActiveSockets;       // keyed by DeviceName

    // GC root for frame textures — keeps transient textures alive between broadcasts.
    UPROPERTY()
    TMap<FString, TObjectPtr<UTexture2D>> FrameTextureCache; // key = "DeviceName_CamIndex"

    // WebSocket event handlers (may fire on any thread — all marshal to game thread)
    void HandleConnected(FString DeviceName);
    void HandleConnectionError(const FString& Error, FString DeviceName);
    void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean, FString DeviceName);
    void HandleMessage(const FString& Message, FString DeviceName);
    void HandleRawMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining, FString DeviceName);

    // HTTP frame response
    void OnVideoFrameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                              bool bWasSuccessful, FString DeviceName, int32 CameraIndex);

    // Creates UTexture2D from raw BGRA data. Must be called on game thread.
    UTexture2D* CreateTextureFromRaw(const TArray<uint8>& RawBGRA, int32 Width, int32 Height);

    // Per-device binary frame counter — alternates between cam 0 and cam 1
    // for devices that push frames over WebSocket without a camera header.
    TMap<FString, int32> BinaryFrameCounter; // keyed by DeviceName

    void SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState);
    void MarkFeedsDisconnected(const FString& DeviceName);

    static FString GenerateStatusQuip(const FHMCDeviceStatus& Status);
    static FString ConfigFilePath();
};
