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

    // Reassigns a registered device to a different actor: updates the config + status
    // and migrates the device's camera feeds to the new actor's group (CameraFeeds is
    // keyed by ActorID). No-op if the device is unknown or the actor is unchanged.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void AssignActor(const FString& DeviceName, const FString& NewActorID);

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
    TArray<FHMCCameraFeed> GetFeedsForActor(const FString& ActorID) const;

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

    // ─── Capture Monitor (pipeline + framing reference + auto image metrics) ───

    // Active capture pipeline for a device (default MetaHumanHMC). Drives which
    // checks/thresholds the monitor applies. Set during shoot-day setup; persisted.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    ECapturePipeline GetDevicePipeline(const FString& DeviceName) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetDevicePipeline(const FString& DeviceName, ECapturePipeline Pipeline);

    // Per-camera framing reference ("where the face should be"), captured at setup.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FHMCFramingRef GetFramingRef(const FString& DeviceName, int32 CameraIndex) const;

    // Snapshots the camera's current subject centroid/size as the framing reference
    // and persists it. Returns true if the captured framing is within the pipeline's
    // target tolerance (false = saved, but the operator should reframe).
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    bool SetFramingReferenceFromCurrent(const FString& DeviceName, int32 CameraIndex);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void ClearFramingReference(const FString& DeviceName, int32 CameraIndex);

    // Latest automatic image-analysis metrics for a camera (for the Setup read-outs).
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FHMCImageMetrics GetImageMetrics(const FString& DeviceName, int32 CameraIndex) const;

    // Direction the subject has drifted from its framing reference ("low" / "high" /
    // "off-axis left" / ...) — empty if no reference set or no significant drift.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FString GetFramingHint(const FString& DeviceName, int32 CameraIndex) const;

    // ─── Prepped for Preview ────────────────────────────────────────────────────
    // Only prepped devices appear in HMC Preview. Set by the "Prepped for Preview"
    // action in Setup; persisted on the device config.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    bool IsPreppedForPreview(const FString& DeviceName) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void SetPreppedForPreview(const FString& DeviceName, bool bPrepped);

    // Marks every registered device prepped (the bottom "Prepped for Preview" button).
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void MarkAllPreppedForPreview();

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
    TMap<FString, TArray<FHMCCameraFeed>> CameraFeeds;   // keyed by ActorID
    TMap<FString, FTimerHandle>           PollTimers;     // keyed by DeviceName
    TMap<FString, int32>                  ManualIssueFlags; // keyed by DeviceName

    // Capture Monitor: latest per-camera image metrics + debounced auto flags
    // (keyed "DeviceName_Cam"). Analysis is throttled (~5Hz) off the decode path.
    TMap<FString, FHMCImageMetrics> ImageMetrics;
    TMap<FString, double>           LastAnalyzeTime;   // last analysis timestamp per cam
    TMap<FString, int32>            StableAutoFlags;   // hysteresis-stable auto flags per cam
    TMap<FString, int32>            AutoFlagHold;      // per "Cam|bit" integrator counter

    // Mount-stability detection: subject-centroid history per camera (~last 2s at the
    // ~5Hz rate) for instability variance, plus a per-camera "bumped until" timestamp
    // so a one-frame jump stays visible ~1.2s.
    TMap<FString, TArray<FVector2D>> CentroidHistory;
    TMap<FString, double>            BumpUntil;

    // Continuous video pump state. Frames are NOT tied to the 2s status poll —
    // each device runs a self-sustaining chain that alternates cameras as fast as
    // the device can serve (one request in flight per device → kind to firmware,
    // and both cameras get served on single-stream hardware).
    TSet<FString> FrameStreamDevices;   // devices with active frame chains
    TSet<FString> FrameInFlight;        // "DeviceName_Cam" with a request in flight
    TSet<FString> FrameNoFace;          // "DeviceName_Cam" with no subject in frame
    TMap<FString, int32> PollFailCount; // consecutive status-poll failures per device

    // Throughput instrumentation — logs fps / decode-time / frame-size per camera.
    TMap<FString, int32>  FrameRateCount;
    TMap<FString, double> FrameRateWindowStart;
    TMap<FString, double> FrameRateDecodeAccum;
    void LogFrameRate(const FString& Key, double DecodeMs, int32 FrameBytes);

    // No world context on an EngineSubsystem — polled via GEditor's timer manager.
    // 3s (was 2s) so the status poll contends less with the high-rate video pulls.
    float PollIntervalSeconds = 3.0f;

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

    // Reuses one persistent texture per "DeviceName_Cam", updating its pixels in
    // place via UpdateTextureRegions — no per-frame allocation or UpdateResource.
    UTexture2D* UpdateFrameTexture(const FString& Key, const TArray<uint8>& BGRA, int32 Width, int32 Height);

    // Integrator hysteresis: folds this frame's raw auto-flags into the per-camera
    // StableAutoFlags so a flag must persist ~1s before it sets/clears (no blinking).
    int32 UpdateAutoFlagHysteresis(const FString& CamKey, int32 RawFlags);

    void SetConnectionState(const FString& DeviceName, EHMCConnectionState NewState);
    void MarkFeedsDisconnected(const FString& DeviceName);

    static FString GenerateStatusQuip(const FHMCDeviceStatus& Status);
    static FString ConfigFilePath();
};
