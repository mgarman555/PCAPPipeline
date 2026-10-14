#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "PCAPToolTypes.h"
#include "PCAPDeviceInterfaces.h"
#include "PCAPDeviceRegistry.generated.h"

// Catalog of device adapters (HOME_REBUILD_HANDOFF §4.2). The Capture Setup dropdowns list
// what is registered here; a vendor shown in the UI but not registered is "adapter not
// installed". Factories live in a static catalog so they are usable before this editor
// subsystem exists (UPCAPToolSubsystem is an engine subsystem and initializes first);
// built-in adapters register themselves on first use.
//
// Live devices: the HMC devices are owned by UPCAPToolSubsystem (the frame pump); this
// subsystem reads their health through it so the UI has one place to ask.
UCLASS()
class PCAPTOOL_API UPCAPDeviceRegistry : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    using FFacialFactory = TFunction<TSharedRef<IPCAPFacialDevice>()>;
    using FBodyFactory   = TFunction<TSharedRef<IPCAPBodySource>()>;
    using FAudioFactory  = TFunction<TSharedRef<IPCAPAudioSource>()>;

    // ─── Catalog ───────────────────────────────────────────────────────────────
    static void RegisterFacialVendor(FName VendorId, FFacialFactory Factory);
    static void RegisterBodyVendor(FName VendorId, FBodyFactory Factory);
    static void RegisterAudioVendor(FName VendorId, FAudioFactory Factory);
    static void UnregisterVendor(FName VendorId);

    static TArray<FName> GetFacialVendors();
    static TArray<FName> GetBodyVendors();
    static TArray<FName> GetAudioVendors();

    // Null when no adapter is installed for VendorId.
    static TSharedPtr<IPCAPFacialDevice> CreateFacialDevice(FName VendorId);
    static TSharedPtr<IPCAPBodySource>   CreateBodySource(FName VendorId);
    static TSharedPtr<IPCAPAudioSource>  CreateAudioSource(FName VendorId);

    // ─── Live health ───────────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "PCAP|Devices")
    FPCAPDeviceHealth GetFacialHealth(const FString& DeviceName) const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Devices")
    TArray<FPCAPDeviceHealth> GetAllFacialHealth() const;

    UFUNCTION(BlueprintCallable, Category = "PCAP|Devices")
    static bool IsVendorInstalled(FName VendorId);

private:
    static void EnsureBuiltIns();
};
