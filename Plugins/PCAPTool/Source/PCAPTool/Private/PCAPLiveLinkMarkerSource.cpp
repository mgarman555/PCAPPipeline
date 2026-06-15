#include "PCAPLiveLinkMarkerSource.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "LiveLinkTypes.h"

static ILiveLinkClient* GetLLClient()
{
    IModularFeatures& MF = IModularFeatures::Get();
    if (MF.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        return &MF.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    }
    return nullptr;
}

bool FLiveLinkMarkerSource::IsAvailable() const
{
    return GetLLClient() != nullptr;
}

void FLiveLinkMarkerSource::Poll(FVizFrame& OutFrame)
{
    OutFrame.Reset();
    ILiveLinkClient* Client = GetLLClient();
    if (!Client) { return; }

    for (const FLiveLinkSubjectKey& Key : Client->GetSubjects(/*disabled*/false, /*virtual*/false))
    {
        TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(Key);
        if (!Role || !Role->IsChildOf(ULiveLinkAnimationRole::StaticClass())) { continue; }

        FLiveLinkSubjectFrameData Data;
        if (!Client->EvaluateFrame_AnyThread(Key.SubjectName, ULiveLinkAnimationRole::StaticClass(), Data)) { continue; }

        const FLiveLinkSkeletonStaticData* Skel = Data.StaticData.Cast<FLiveLinkSkeletonStaticData>();
        const FLiveLinkAnimationFrameData* Anim = Data.FrameData.Cast<FLiveLinkAnimationFrameData>();
        if (!Skel || !Anim) { continue; }

        const TArray<int32>& Parents = Skel->GetBoneParents();
        const TArray<FTransform>& Locals = Anim->Transforms;
        if (Parents.Num() != Locals.Num() || Locals.Num() == 0) { continue; }

        // Compose parent-relative bone transforms down the hierarchy.
        // Live Link bones are ordered parent-before-child, so a single forward pass works.
        TArray<FTransform> World;
        World.SetNum(Locals.Num());
        for (int32 i = 0; i < Locals.Num(); ++i)
        {
            const int32 P = Parents[i];
            World[i] = (P >= 0 && P < i) ? (Locals[i] * World[P]) : Locals[i];
        }

        FVizMarkerGroup Group;
        Group.SubjectName = Key.SubjectName.Name;
        Group.Points.Reserve(World.Num());
        for (const FTransform& T : World) { Group.Points.Add(T.GetLocation()); }
        OutFrame.Labeled.Add(MoveTemp(Group));
    }
}
