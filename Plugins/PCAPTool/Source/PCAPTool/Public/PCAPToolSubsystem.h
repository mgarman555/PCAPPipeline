#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/TimerHandle.h"
#include "PCAPToolTypes.h"
#include "PCAPToolSubsystem.generated.h"

// Same delegate signatures as UHMCMonitorComponent — widget bindings are identical.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPCAPHMCStatusUpdated,
    FString, DeviceName, FHMCDeviceStatus, Status);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPCAPHMCFrameReceived,
    FString, DeviceName, int32, CameraIndex, UTexture2D*, Frame);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPCAPHMCConnectionChanged,
    FString, DeviceName, EHMCConnectionState, ConnectionState);

/**
 * Editor-lifetime subsystem owning all HMC state.
 * Lives from editor open to editor close — no actor, no PIE required.
 *
 * Blueprint access:
 *   Get Engine Subsystem → PCAPToolSubsystem → call any function below.
 */
UCLASS()
class PCAPTOOL_API UPCAPToolSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Registration ────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterDevice(const FHMCDeviceConfig& Config);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void UnregisterDevice(const FString& DeviceName);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceConfig> GetRegisteredDevices() const;

    // ─── Connection ───────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void ConnectDevice(const FString& DeviceName);

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

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCCameraFeed> GetFeedsForActor(const FString& ActorName) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCCameraFeed> GetAllFeeds() const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetFeedState(const FString& DeviceName, EHMCCameraRole Role, EHMCFeedState State);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetCameraRole(const FString& DeviceName, int32 CameraIndex, EHMCCameraRole NewRole);

    // ─── Commands ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SendCommand(const FString& DeviceName, const FString& Command);

    // Generic device write: GET /control?cmd=[Cmd]&param=[Param][&ExtraKey=ExtraVal].
    // Pass empty ExtraKey/ExtraVal when unused. Examples:
    //   exposure cam0: SendDeviceCommand(Dev,"setexposure","4550","cam","0")
    //   gain cam1:     SendDeviceCommand(Dev,"setgain","2","cam","1")
    //   lights top:    SendDeviceCommand(Dev,"setlights","80","which","top")
    // NOTE: command tokens are best-guess from the MugShot web UI — verify against
    // the device JS. Fire-and-forget; the value confirms on the next poll.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SendDeviceCommand(const FString& DeviceName, const FString& Cmd,
                           const FString& Param, const FString& ExtraKey, const FString& ExtraVal);

    // ─── Issue flags ───────────────────────────────────────────────────────────

    // Operator-reported flags the device cannot sense (face off-axis, lip seal…).
    // Re-broadcasts status so UI repaints immediately.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetManualIssue(const FString& DeviceName, EHMCManualIssue Issue, bool bSet);

    // Current on/off state of one manual flag — drives the Setup-mode toggle.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    bool GetManualIssue(const FString& DeviceName, EHMCManualIssue Issue) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    int32 GetManualIssueFlags(const FString& DeviceName) const;

    // Hardware issues for the camera OR'd with the device's manual flags — the
    // value the widget feeds to GetIssueSeverity / GetIssueBannerText.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    int32 GetEffectiveIssueFlags(const FString& DeviceName, int32 CameraIndex) const;

    // ─── Video Frames ─────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RequestVideoFrame(const FString& DeviceName, int32 CameraIndex);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    UTexture2D* GetLastFrame(const FString& DeviceName, int32 CameraIndex) const;

    // ─── Persistence ──────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SaveConfig() const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void LoadConfig();

    // ─── Delegates ───────────────────────────────────────────────────────────

    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnPCAPHMCStatusUpdated OnStatusUpdated;

    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnPCAPHMCFrameReceived OnFrameReceived;

    UPROPERTY(BlueprintAssignable, Category = "PCAP|HMC")
    FOnPCAPHMCConnectionChanged OnConnectionChanged;

private:
    TMap<FString, FHMCDeviceConfig>       RegisteredConfigs;
    TMap<FString, FHMCDeviceStatus>       DeviceStatuses;
    TMap<FString, TArray<FHMCCameraFeed>> CameraFeeds;   // keyed by ActorName
    TMap<FString, FTimerHandle>           PollTimers;     // keyed by DeviceName
    TMap<FString, int32>                  ManualIssueFlags; // keyed by DeviceName

    // Continuous video pump state. Frames are NOT tied to the 2s status poll —
    // each device runs a self-sustaining chain that alternates cameras as fast as
    // the device can serve (one request in flight per device → kind to firmware,
    // and both cameras get served on single-stream hardware).
    TSet<FString> FrameStreamDevices;   // devices with an active frame chain
    TSet<FString> FrameInFlight;        // devices with a video request in flight
    TSet<FString> FrameNoFace;          // "DeviceName_Cam" with no subject in frame

    // No world context on an EngineSubsystem — polled via GEditor's timer manager.
    float PollIntervalSeconds = 2.0f;

    UPROPERTY()
    TMap<FString, TObjectPtr<UTexture2D>> FrameTextureCache;  // key = "DeviceName_CamIndex"

    void PollDevice(FString DeviceName);
    void OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                        bool bWasSuccessful, FString DeviceName);

    // Requests one frame for the given camera if the device is streaming, connected,
    // and has no request already in flight. The chain re-arms itself from
    // OnVideoFrameResponse (alternating cameras), so this only needs kicking once
    // per device (on connect) and re-kicking after a stall (each successful poll).
    void PumpFrameCam(const FString& DeviceName, int32 CameraIndex);

    void OnVideoFrameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                              bool bWasSuccessful, FString DeviceName, int32 CameraIndex);

    UTexture2D* CreateTextureFromRaw(const TArray<uint8>& RawBGRA, int32 Width, int32 Height);
    void SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState);
    void MarkFeedsDisconnected(const FString& DeviceName);

    static FString GenerateStatusQuip(const FHMCDeviceStatus& Status);
    static FString ConfigFilePath();
};
