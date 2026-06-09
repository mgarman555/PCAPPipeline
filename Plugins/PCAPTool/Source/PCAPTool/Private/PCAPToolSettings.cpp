#include "PCAPToolSettings.h"

UPCAPToolSettings* UPCAPToolSettings::Get()
{
    return GetMutableDefault<UPCAPToolSettings>();
}

UMocapDatabase* UPCAPToolSettings::GetDatabase() const
{
    if (DatabaseAsset.IsNull())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] DatabaseAsset not set — go to Edit -> Project Settings -> PCAP Tool"));
        return nullptr;
    }
    UMocapDatabase* DB = DatabaseAsset.LoadSynchronous();
    if (!DB)
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] Failed to load DatabaseAsset: %s"), *DatabaseAsset.ToString());
    return DB;
}
