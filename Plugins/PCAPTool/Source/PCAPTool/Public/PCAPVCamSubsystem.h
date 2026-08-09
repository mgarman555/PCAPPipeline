#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "TickableEditorObject.h"
#include "HAL/CriticalSection.h"
#include "Common/UdpSocketReceiver.h"   // FArrayReaderPtr + FUdpSocketReceiver + FIPv4Endpoint
#include "VCamProcessor.h"      // FPCAPVCamRuntimeState
#include "VCamInputLayer.h"     // FVCamInputLayer / FVCamControllerInput / FVCamInputIntents
#include "PCAPToolTypes.h"      // EStreamStatus
#include "PCAPVCamSubsystem.generated.h"

class UPCAPVCamConfig;
class APCAPVCamActor;
class FSocket;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPCAPVCamStreamStatusChanged, EStreamStatus, NewStatus);

/**
 * Editor-lifetime subsystem owning the live virtual camera.
 * Ticks every editor frame (FTickableEditorObject) while a config + live subject exist:
 * reads TPVCam from Live Link, runs FPCAPVCamProcessor, and drives APCAPVCamActor.
 * Recording is owned by UPCAPTakeRecorderSubsystem, not here.
 *
 * Access: GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>().
 */
UCLASS()
class PCAPTOOL_API UPCAPVCamSubsystem : public UEngineSubsystem, public FTickableEditorObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // FTickableEditorObject
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPCAPVCamSubsystem, STATGROUP_Tickables); }

    // ── Activation ─────────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetActiveConfig(UPCAPVCamConfig* Config);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") UPCAPVCamConfig* GetActiveConfig() const { return ActiveConfig; }

    // The config is a saved DataAsset, so an operator-intent edit (rig alignment, setup, saved
    // positions, scale, layout) has to dirty the package or a whole shoot day of calibration is
    // gone on restart with no prompt. Called by the operator panel after its direct field writes.
    // NOT called from the per-frame integration (Navigate/focal length/Sony XY) — that is runtime
    // state living on the asset, and dirtying it every tick would bake it in.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void MarkConfigDirty();

    // The single camera actor (found-or-spawned in the editor world). Used by the record controller.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") APCAPVCamActor* GetOrCreateVCamActor();

    // ── Transform controls ─────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void ZeroSpace();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetHold(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetFlightMode(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetLockPosition(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetLockRotation(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetLockRoll(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetKillRoll(bool bEnabled);

    // ── Navigation / saved positions ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SaveCurrentPosition();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void GotoSavedPosition(int32 Index);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void DeleteSavedPosition(int32 Index);

    // ── Lens ───────────────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetFocalLength(float Millimeters);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void CycleFocalLengthUp();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void CycleFocalLengthDown();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void AddFocalLengthDelta(float DeltaMm);

    // ── Gains / scale (joystick seam — stored now, fed by the input layer later) ──
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetTranslationGain(float Gain);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetZoomGain(float Gain);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetWorldSpaceScale(FVector Scale);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SelectNextWorldScale();
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SelectPreviousWorldScale();

    // Joystick/d-pad translation rate (units/sec); the tick accumulates it into Navigate.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetNavigateRate(FVector Rate);

    // Active controller layout (0=Default, 1=Sony) — stored on the config. Leaving Sony folds
    // its platform offset into Navigate first (see FoldPlatformXYIntoNavigate).
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetActiveButtonLayout(int32 Layout);

    // Clears the Sony-driven platform offset (Config.Platform X/Y). The camera moves back by the
    // offset — this is the explicit operator reset, unlike the layout switch which preserves the
    // pose. While the Sony layout is live the accumulator re-writes it next tick; the controller's
    // right_x is the clear that also zeroes the accumulator.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void ResetPlatformOffset();

    // ── Readouts ───────────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") EStreamStatus GetStreamStatus() const { return StreamStatus; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") FTransform GetCurrentTransform() const;
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") float GetCurrentFocalLength() const;
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") FString GetActiveMapping() const { return RuntimeState.ActiveMapping; }

    // Mode/toggle readouts — so the panel (and future controller HUD) reflects live state,
    // not just sets it. Backed by the runtime state the processor mutates.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool  IsFlightMode() const   { return RuntimeState.bFlightMode; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool  IsHeld() const         { return RuntimeState.bHold; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool  IsLockPosition() const { return RuntimeState.bLockPosition; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool  IsLockRotation() const { return RuntimeState.bLockRotation; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool  IsLockRoll() const     { return RuntimeState.bLockRoll; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool  IsKillRoll() const     { return RuntimeState.bKillRoll; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") float GetTranslationGain() const { return RuntimeState.TranslationGain; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") float GetZoomGain() const        { return RuntimeState.ZoomGain; }

    // Live controller-input readout for the panel's input monitor (plain C++ — FVCamControllerInput
    // isn't BP-exposed). GetInputPacketCount rising = packets are arriving from WVCAM.
    FVCamControllerInput GetLatestInput() const;
    int32 GetInputPacketCount() const;

    // ── Controller feed (the UDP listener) ─────────────────────────────────────
    // True while packets are actually arriving. This is the same gate the tick uses to decide
    // whether the last packet may still drive the camera, so the panel's readout and the camera's
    // behaviour can never disagree.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool IsReceivingControllerInput() const;

    // Listener state, so "nobody is sending" reads differently from "I could not open the socket".
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") bool    IsControllerFeedBound() const { return InputSocket != nullptr; }
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") FString GetControllerFeedEndpoint() const;
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") FString GetControllerFeedError() const { return InputListenerError; }

    // Re-open the listener on the active config's current IP/port. The way back from a bind
    // failure (port in use) without de-picking and re-picking the config.
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void RestartControllerFeed();

    UPROPERTY(BlueprintAssignable, Category="PCAP|VCam") FOnPCAPVCamStreamStatusChanged OnStreamStatusChanged;

private:
    UPROPERTY() TObjectPtr<UPCAPVCamConfig> ActiveConfig = nullptr;
    UPROPERTY() TWeakObjectPtr<APCAPVCamActor> VCamActor;

    FPCAPVCamRuntimeState RuntimeState;
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;

    // Reads the active config's Live Link subject transform. Returns false if unavailable.
    bool ReadLiveLinkTransform(FTransform& OutTransform) const;
    void SetStreamStatus(EStreamStatus NewStatus);

    // Folds the Sony-driven platform offset (Config.Platform X/Y) into Navigate and clears it,
    // leaving the output transform unchanged. Used when the platform stops being driven but the
    // camera must not jump (reconciliation §3).
    void FoldPlatformXYIntoNavigate();

    // ── Controller input (WVCAM raw-broadcast UDP listener → input layer) ──────
    FVCamInputLayer InputLayer;
    FVCamControllerInput LatestInput;       // written on the receiver thread, read on tick
    mutable FCriticalSection InputMutex;
    bool bHasInput = false;
    double LastInputPacketTime = 0.0;       // FPlatformTime::Seconds() of the last packet
    int32 InputPacketCount = 0;
    bool bPrevInputHold = false;            // so SetHold only fires on change (re-solves Setup)
    FSocket* InputSocket = nullptr;
    FUdpSocketReceiver* InputReceiver = nullptr;
    int32 ActiveInputPort = 0;
    FString ActiveInputIP;                  // bound interface; with the port, gates re-binding
    int32 RequestedInputPort = 0;           // last endpoint we *tried* to bind (success or not) —
    FString RequestedInputIP;               // the tick compares it to the config to spot an edit
    FString InputListenerError;             // empty while the socket is open

    void StartInputListener(const FString& BindIP, int32 Port);
    void StopInputListener();
    void OnInputPacket(const FArrayReaderPtr& Data, const FIPv4Endpoint& From);  // receiver thread
    void ApplyInputIntents(const FVCamInputIntents& Intents);                    // game thread
};
