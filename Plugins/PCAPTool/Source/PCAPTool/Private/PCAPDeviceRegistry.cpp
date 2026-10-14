#include "PCAPDeviceRegistry.h"
#include "PCAPToolSubsystem.h"
#include "Devices/TechnopropsFacialDevice.h"
#include "Engine/Engine.h"

namespace
{
    struct FPCAPDeviceCatalog
    {
        TMap<FName, UPCAPDeviceRegistry::FFacialFactory> Facial;
        TMap<FName, UPCAPDeviceRegistry::FBodyFactory>   Body;
        TMap<FName, UPCAPDeviceRegistry::FAudioFactory>  Audio;
        bool bBuiltInsRegistered = false;
    };

    FPCAPDeviceCatalog& Catalog()
    {
        static FPCAPDeviceCatalog Instance;
        return Instance;
    }

    template <typename MapType>
    TArray<FName> SortedKeys(const MapType& Map)
    {
        TArray<FName> Keys;
        Map.GetKeys(Keys);
        Keys.Sort(FNameLexicalLess());
        return Keys;
    }

    UPCAPToolSubsystem* GetHMCSubsystem()
    {
        return GEngine ? GEngine->GetEngineSubsystem<UPCAPToolSubsystem>() : nullptr;
    }
}

void UPCAPDeviceRegistry::EnsureBuiltIns()
{
    FPCAPDeviceCatalog& C = Catalog();
    if (C.bBuiltInsRegistered) return;
    C.bBuiltInsRegistered = true;

    // Only adapters whose behavior is verified ship here. OneHMC / markerless camera /
    // USB audio register in Phase 2, each after its device agent's PASS log.
    C.Facial.Add(FTechnopropsFacialDevice::StaticVendorId(),
        []() -> TSharedRef<IPCAPFacialDevice> { return MakeShared<FTechnopropsFacialDevice>(); });
}

void UPCAPDeviceRegistry::RegisterFacialVendor(FName VendorId, FFacialFactory Factory)
{
    EnsureBuiltIns();
    Catalog().Facial.Add(VendorId, MoveTemp(Factory));
}

void UPCAPDeviceRegistry::RegisterBodyVendor(FName VendorId, FBodyFactory Factory)
{
    EnsureBuiltIns();
    Catalog().Body.Add(VendorId, MoveTemp(Factory));
}

void UPCAPDeviceRegistry::RegisterAudioVendor(FName VendorId, FAudioFactory Factory)
{
    EnsureBuiltIns();
    Catalog().Audio.Add(VendorId, MoveTemp(Factory));
}

void UPCAPDeviceRegistry::UnregisterVendor(FName VendorId)
{
    EnsureBuiltIns();
    FPCAPDeviceCatalog& C = Catalog();
    C.Facial.Remove(VendorId);
    C.Body.Remove(VendorId);
    C.Audio.Remove(VendorId);
}

TArray<FName> UPCAPDeviceRegistry::GetFacialVendors() { EnsureBuiltIns(); return SortedKeys(Catalog().Facial); }
TArray<FName> UPCAPDeviceRegistry::GetBodyVendors()   { EnsureBuiltIns(); return SortedKeys(Catalog().Body); }
TArray<FName> UPCAPDeviceRegistry::GetAudioVendors()  { EnsureBuiltIns(); return SortedKeys(Catalog().Audio); }

TSharedPtr<IPCAPFacialDevice> UPCAPDeviceRegistry::CreateFacialDevice(FName VendorId)
{
    EnsureBuiltIns();
    const FFacialFactory* F = Catalog().Facial.Find(VendorId);
    return F ? TSharedPtr<IPCAPFacialDevice>((*F)()) : nullptr;
}

TSharedPtr<IPCAPBodySource> UPCAPDeviceRegistry::CreateBodySource(FName VendorId)
{
    EnsureBuiltIns();
    const FBodyFactory* F = Catalog().Body.Find(VendorId);
    return F ? TSharedPtr<IPCAPBodySource>((*F)()) : nullptr;
}

TSharedPtr<IPCAPAudioSource> UPCAPDeviceRegistry::CreateAudioSource(FName VendorId)
{
    EnsureBuiltIns();
    const FAudioFactory* F = Catalog().Audio.Find(VendorId);
    return F ? TSharedPtr<IPCAPAudioSource>((*F)()) : nullptr;
}

bool UPCAPDeviceRegistry::IsVendorInstalled(FName VendorId)
{
    EnsureBuiltIns();
    const FPCAPDeviceCatalog& C = Catalog();
    return C.Facial.Contains(VendorId) || C.Body.Contains(VendorId) || C.Audio.Contains(VendorId);
}

FPCAPDeviceHealth UPCAPDeviceRegistry::GetFacialHealth(const FString& DeviceName) const
{
    if (const UPCAPToolSubsystem* Sub = GetHMCSubsystem())
    {
        if (const TSharedPtr<IPCAPFacialDevice> Device = Sub->GetFacialDevice(DeviceName))
        {
            return Device->GetHealth();
        }
    }
    return FPCAPDeviceHealth();
}

TArray<FPCAPDeviceHealth> UPCAPDeviceRegistry::GetAllFacialHealth() const
{
    TArray<FPCAPDeviceHealth> Out;
    if (const UPCAPToolSubsystem* Sub = GetHMCSubsystem())
    {
        for (const FHMCDeviceConfig& Config : Sub->GetRegisteredDevices())
        {
            if (const TSharedPtr<IPCAPFacialDevice> Device = Sub->GetFacialDevice(Config.DeviceName))
            {
                Out.Add(Device->GetHealth());
            }
        }
    }
    return Out;
}
