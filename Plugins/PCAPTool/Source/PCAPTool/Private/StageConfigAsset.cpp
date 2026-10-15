#include "StageConfigAsset.h"
#include "PCAPToolSettings.h"
#include "MocapDatabase.h"

TArray<FString> UStageConfigAsset::GetProjectOptions() const
{
    TArray<FString> Out;
    Out.Add(FString());   // "any project"
    const UPCAPToolSettings* S = UPCAPToolSettings::Get();
    if (const UMocapDatabase* DB = S ? S->GetDatabase() : nullptr)
        for (const FProduction& P : DB->Productions)
            if (!P.ProjectCode.IsEmpty()) Out.AddUnique(P.ProjectCode);
    return Out;
}

TArray<FString> UStageConfigAsset::GetMocapOptions() const
{
    const UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetMocapOptions() : TArray<FString>();
}

TArray<FString> UStageConfigAsset::GetFacialOptions() const
{
    const UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetFacialOptions() : TArray<FString>();
}

TArray<FString> UStageConfigAsset::GetAudioOptions() const
{
    const UPCAPToolSettings* S = UPCAPToolSettings::Get();
    return S ? S->GetAudioOptions(FacialSystem) : TArray<FString>();
}

bool UStageConfigAsset::MigrateLegacySystems()
{
    // Legacy enum display names carry over as-is (e.g. "Xsens", "Technoprops HMC"), so an
    // old stage keeps showing what it had even if that name isn't in today's lists.
    bool bChanged = false;
    if (MocapSystem.IsEmpty() && BodySystem != EBodySystem::None)
    {
        MocapSystem = StaticEnum<EBodySystem>()->GetDisplayNameTextByValue((int64)BodySystem).ToString();
        bChanged = true;
    }
    if (FacialSystem.IsEmpty() && FaceSystem != EFaceSystem::None)
    {
        FacialSystem = StaticEnum<EFaceSystem>()->GetDisplayNameTextByValue((int64)FaceSystem).ToString();
        bChanged = true;
    }
    if (AudioSystemName.IsEmpty() && AudioSystem != EAudioSystem::None)
    {
        AudioSystemName = StaticEnum<EAudioSystem>()->GetDisplayNameTextByValue((int64)AudioSystem).ToString();
        bChanged = true;
    }
    return bChanged;
}

void UStageConfigAsset::PostLoad()
{
    Super::PostLoad();
    MigrateLegacySystems();
}
