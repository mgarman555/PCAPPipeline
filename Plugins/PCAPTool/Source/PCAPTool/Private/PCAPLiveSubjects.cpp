#include "PCAPLiveSubjects.h"
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"

#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Features/IModularFeatures.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

namespace PCAPLiveSubjects
{

TArray<FString> GetLive()
{
    TArray<FString> Out;
    IModularFeatures& MF = IModularFeatures::Get();
    if (MF.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        ILiveLinkClient& Client = MF.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
        for (const FLiveLinkSubjectKey& Key : Client.GetSubjects(/*bIncludeDisabled*/ false, /*bIncludeVirtual*/ false))
        {
            const FString Name = Key.SubjectName.ToString();
            if (!Name.IsEmpty()) Out.AddUnique(Name);
        }
    }
    return Out;
}

TSet<FString> GetAllBound()
{
    TSet<FString> Bound;
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    TArray<FAssetData> Actors;
    ARM.Get().GetAssetsByClass(UActorRosterEntry::StaticClass()->GetClassPathName(), Actors, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Actors)
        if (UActorRosterEntry* E = Cast<UActorRosterEntry>(AD.GetAsset()))
        {
            if (!E->DefaultBodyStream.LiveLinkSubjectName.IsNone()) Bound.Add(E->DefaultBodyStream.LiveLinkSubjectName.ToString());
            if (!E->DefaultFaceStream.LiveLinkSubjectName.IsNone()) Bound.Add(E->DefaultFaceStream.LiveLinkSubjectName.ToString());
        }

    TArray<FAssetData> Props;
    ARM.Get().GetAssetsByClass(UPropRosterEntry::StaticClass()->GetClassPathName(), Props, /*bSearchSubClasses*/ false);
    for (const FAssetData& AD : Props)
        if (UPropRosterEntry* E = Cast<UPropRosterEntry>(AD.GetAsset()))
            if (!E->DefaultLiveLinkName.IsNone()) Bound.Add(E->DefaultLiveLinkName.ToString());

    return Bound;
}

TArray<FString> GetUntracked()
{
    const TSet<FString> Bound = GetAllBound();
    TArray<FString> Out;
    for (const FString& Name : GetLive())
        if (!Bound.Contains(Name)) Out.Add(Name);
    Out.Sort();
    return Out;
}

} // namespace PCAPLiveSubjects
