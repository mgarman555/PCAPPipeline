#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "PCAPToolTypes.h"
#include "PCAPToolSubsystem.h"
#include "PCAPToolEditorWidget.generated.h"

class UMocapDatabase;

/**
 * C++ base class for all PCAP Tool editor panels.
 * Create a Blueprint subclass of this (Editor Utility Widget) to build the UI.
 * All database read/write operations are exposed here as BlueprintCallable UFUNCTIONs.
 * The Blueprint subclass implements OnDatabaseChanged and OnSelectionChanged to refresh its lists.
 */
UCLASS(BlueprintType)
class PCAPTOOL_API UPCAPToolEditorWidget : public UEditorUtilityWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;

    // ─── Active Selection ────────────────────────────────────────────────────
    // These drive which Production/Day/Session/Shot the UI is showing.
    // Set them via the Set* functions below so OnSelectionChanged fires.

    UPROPERTY(BlueprintReadOnly, Category = "PCAP|Selection")
    FString ActiveProjectCode;

    UPROPERTY(BlueprintReadOnly, Category = "PCAP|Selection")
    FString ActiveDayID;

    UPROPERTY(BlueprintReadOnly, Category = "PCAP|Selection")
    FString ActiveSessionID;

    UPROPERTY(BlueprintReadOnly, Category = "PCAP|Selection")
    FString ActiveShotID;

    // ─── Subsystem access ────────────────────────────────────────────────────
    // Pre-wired in NativeConstruct. Use this in Blueprint instead of
    // manually calling Get Engine Subsystem.

    UPROPERTY(BlueprintReadOnly, Category = "PCAP|HMC")
    UPCAPToolSubsystem* HMCSubsystem = nullptr;

    // Quick helper for testing: registers a device by name + IP and connects.
    // WebSocketEndpoint auto-set to ws://[IP]/ws.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterTestDevice(const FString& DeviceName, const FString& IPAddress);

    // ─── Database ────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Database")
    UMocapDatabase* GetDatabase() const;

    // Saves the database asset to disk.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Database")
    void SaveDatabase();

    // ─── Productions ─────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Productions")
    TArray<FProduction> GetAllProductions() const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Productions")
    TArray<FString> GetProductionNames() const;

    // Returns false if the project code is not found.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Productions")
    bool GetProduction(const FString& ProjectCode, FProduction& OutProduction) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Productions")
    void AddProduction(const FProduction& NewProduction);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Selection")
    void SetActiveProduction(const FString& ProjectCode);

    // ─── Shoot Days ──────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Days")
    TArray<FShootDay> GetShootDays(const FString& ProjectCode) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Days")
    bool GetShootDay(const FString& ProjectCode, const FString& DayID, FShootDay& OutDay) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Days")
    void AddShootDay(const FString& ProjectCode, const FShootDay& NewDay);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Selection")
    void SetActiveDay(const FString& DayID);

    // ─── Sessions ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Sessions")
    TArray<FSession> GetSessions(const FString& ProjectCode, const FString& DayID) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Sessions")
    bool GetSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID, FSession& OutSession) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Sessions")
    void AddSession(const FString& ProjectCode, const FString& DayID, const FSession& NewSession);

    // Stamps StartedAt = UtcNow on the session and saves.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Sessions")
    void StartSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Selection")
    void SetActiveSession(const FString& SessionID);

    // ─── Shots ───────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Shots")
    TArray<FShot> GetShots(const FString& ProjectCode, const FString& DayID, const FString& SessionID) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Shots")
    bool GetShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID, FShot& OutShot) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Shots")
    void AddShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FShot& NewShot);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Shots")
    void RemoveShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Shots")
    void UpdateShotDescription(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID, const FString& Description);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Shots")
    void UpdateShotNotes(const FString& ProjectCode, const FString& DayID, const FString& SessionID, const FString& ShotID, const FString& Notes);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Selection")
    void SetActiveShot(const FString& ShotID);

    // ─── Shot Subjects ────────────────────────────────────────────────────────

    // Toggle whether a performer is called (active) for the current shot.
    UFUNCTION(BlueprintCallable, Category = "PCAP|Subjects")
    void SetSubjectActive(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                          const FString& ShotID, const FString& ActorID, bool bActive);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Subjects")
    void AddSubjectToShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                          const FString& ShotID, const FShotSubject& NewSubject);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Subjects")
    void RemoveSubjectFromShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                               const FString& ShotID, const FString& ActorID);

    // ─── Props ────────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Props")
    void AddPropToShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                       const FString& ShotID, const FPropEntry& NewProp);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Props")
    void RemovePropFromShot(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                            const FString& ShotID, const FString& PropID);

    // ─── Takes ────────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "PCAP|Takes")
    TArray<FTake> GetTakesForShot(const FString& ProjectCode, const FString& DayID,
                                   const FString& SessionID, const FString& ShotID) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Takes")
    void SetTakeLabel(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                      const FString& ShotID, const FString& TakeID, ETakeLabel NewLabel);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Takes")
    void UpdateTakeNotes(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                         const FString& ShotID, const FString& TakeID, const FString& Notes);

    UFUNCTION(BlueprintCallable, Category = "PCAP|Takes")
    void UpdateTakeDirectorNotes(const FString& ProjectCode, const FString& DayID, const FString& SessionID,
                                 const FString& ShotID, const FString& TakeID, const FString& DirectorNotes);

    // ─── Date / Time Helpers (for calendar picker widgets) ───────────────────

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCAP|DateTime")
    static FDateTime MakeDateTimeFromParts(int32 Year, int32 Month, int32 Day, int32 Hour = 0, int32 Minute = 0);

    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCAP|DateTime")
    static void BreakDateTime(const FDateTime& InDateTime, int32& Year, int32& Month, int32& Day, int32& Hour, int32& Minute);

    // Today's date at midnight UTC.
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCAP|DateTime")
    static FDateTime Today();

    // Formatted display string: "Mon Jun 02 2026"
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCAP|DateTime")
    static FString FormatDateDisplay(const FDateTime& InDateTime);

    // ─── HMC Device Monitoring ────────────────────────────────────────────────

    // Register a device for polling. Call once per device (e.g. in NativeConstruct or
    // from a "Add Device" button). DeviceID = "HMC_Unit_A", IPAddress = "192.168.50.x"
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void RegisterHMCDevice(const FString& DeviceID, const FString& IPAddress);

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void UnregisterHMCDevice(const FString& DeviceID);

    // Fire one HTTP poll cycle for all registered devices.
    // Call this from a Blueprint "Set Timer by Function Name" every 2 seconds.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    void PollHMCDevicesNow();

    // Returns the latest cached status for all registered devices.
    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    TArray<FHMCDeviceStatus> GetHMCStatuses() const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|HMC")
    FHMCDeviceStatus GetHMCStatus(const FString& DeviceID) const;

    // Fires on game thread after each device poll (success or failure).
    // Implement in Blueprint to update the device row widget for this DeviceID.
    UFUNCTION(BlueprintImplementableEvent, Category = "PCAP|HMC")
    void OnHMCStatusUpdated(const FHMCDeviceStatus& UpdatedStatus);

    // ─── Blueprint Callbacks ──────────────────────────────────────────────────

    // Fires after any database mutation or save. Implement in Blueprint to refresh lists.
    UFUNCTION(BlueprintImplementableEvent, Category = "PCAP|Events")
    void OnDatabaseChanged();

    // Fires when ActiveProjectCode / ActiveDayID / ActiveSessionID / ActiveShotID changes.
    UFUNCTION(BlueprintImplementableEvent, Category = "PCAP|Events")
    void OnSelectionChanged();

private:
    struct FHMCDeviceRecord
    {
        FString IPAddress;
        FHMCDeviceStatus Status;
    };

    TMap<FString, FHMCDeviceRecord> HMCDeviceRegistry;

    void PollSingleDevice(const FString& DeviceID, const FString& IPAddress);
    void OnHMCPollResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
                           bool bWasSuccessful, FString DeviceID, FString IPAddress);

    // Marks the database asset dirty and fires OnDatabaseChanged.
    void MarkDirtyAndNotify();
};
