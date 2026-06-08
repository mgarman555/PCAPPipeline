#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Engine/TimerHandle.h"
#include "PCAPToolTypes.h"
#include "HMCMonitorComponent.generated.h"

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

    // How often each connected device is polled over HTTP, in seconds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PCAP|HMC")
    float PollIntervalSeconds = 2.0f;

    // ─── Registration ────────────────────────────────────────────────────────

    // Add or update a device entry. Does NOT start polling — call ConnectDevice after.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterDevice(const FHMCDeviceConfig& Config);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void UnregisterDevice(const FString& DeviceName);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceConfig> GetRegisteredDevices() const;

    // Reassigns a registered device to a different actor. Updates the config, the
    // status entry, and migrates the device's camera feeds to the new actor's group
    // (CameraFeeds is keyed by ActorName). No-op if the device is unknown or the
    // actor is unchanged. Backs the OperatorPanel Combo_Actor dropdown.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void AssignActor(const FString& DeviceName, const FString& NewActorName);

    // ─── Connection ───────────────────────────────────────────────────────────

    // Starts the HTTP poll timer for one device. Idempotent — safe if already polling.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void ConnectDevice(const FString& DeviceName);

    // Starts polling for all registered devices.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void ConnectAll();

    // Polls every currently-connected device once, immediately. Driven by the
    // OperatorPanel EUW timer because the component's world timer does not fire
    // on an editor-placed actor outside PIE.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void PollAllDevicesNow();

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

    // Fires GET /control?cmd=[Command]&param= on the device.
    // For startrecording / stoprecording — TEST ONLY, not used in production pipeline.
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
    TMap<FString, FTimerHandle>              PollTimers;          // keyed by DeviceName
    TMap<FString, int32>                     ManualIssueFlags;    // keyed by DeviceName

    // Continuous video pump state — see PCAPToolSubsystem for the rationale. Frames
    // run a self-sustaining chain off HTTP completions (no world timer), so they
    // stream even on an editor-placed actor where the poll timer can't tick.
    TSet<FString> FrameStreamDevices;   // devices with active frame chains
    TSet<FString> FrameInFlight;        // "DeviceName_Cam" with a request in flight
    TSet<FString> FrameNoFace;          // "DeviceName_Cam" with no subject in frame

    // GC root for frame textures — keeps transient textures alive between broadcasts.
    UPROPERTY()
    TMap<FString, TObjectPtr<UTexture2D>> FrameTextureCache; // key = "DeviceName_CamIndex"

    // Requests one frame for the given camera if the device is streaming, connected,
    // and has no request already in flight. The chain re-arms from OnVideoFrameResponse
    // (alternating cameras); kicked once on connect and re-kicked each successful poll.
    void PumpFrameCam(const FString& DeviceName, int32 CameraIndex);

    // HTTP status poll (control.json)
    void PollDevice(FString DeviceName);
    void OnPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                        bool bWasSuccessful, FString DeviceName);

    // HTTP frame response
    void OnVideoFrameResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                              bool bWasSuccessful, FString DeviceName, int32 CameraIndex);

    // Reuses one persistent texture per "DeviceName_Cam", updating its pixels in
    // place via UpdateTextureRegions — no per-frame allocation or UpdateResource.
    UTexture2D* UpdateFrameTexture(const FString& Key, const TArray<uint8>& BGRA, int32 Width, int32 Height);

    void SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState);
    void MarkFeedsDisconnected(const FString& DeviceName);

    static FString GenerateStatusQuip(const FHMCDeviceStatus& Status);
    static FString ConfigFilePath();
};
