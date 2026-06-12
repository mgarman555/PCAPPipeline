#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "TickableEditorObject.h"
#include "VCamProcessor.h"      // FPCAPVCamRuntimeState
#include "PCAPToolTypes.h"      // EStreamStatus
#include "PCAPVCamSubsystem.generated.h"

class UPCAPVCamConfig;
class APCAPVCamActor;

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

    // ── Gains / scale (joystick seam — stored now, fed by the input layer later) ──
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetTranslationGain(float Gain);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetZoomGain(float Gain);
    UFUNCTION(BlueprintCallable, Category="PCAP|VCam") void SetWorldSpaceScale(FVector Scale);

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

    UPROPERTY(BlueprintAssignable, Category="PCAP|VCam") FOnPCAPVCamStreamStatusChanged OnStreamStatusChanged;

private:
    UPROPERTY() TObjectPtr<UPCAPVCamConfig> ActiveConfig = nullptr;
    UPROPERTY() TWeakObjectPtr<APCAPVCamActor> VCamActor;

    FPCAPVCamRuntimeState RuntimeState;
    EStreamStatus StreamStatus = EStreamStatus::Disconnected;

    // Reads the active config's Live Link subject transform. Returns false if unavailable.
    bool ReadLiveLinkTransform(FTransform& OutTransform) const;
    void SetStreamStatus(EStreamStatus NewStatus);
};
