#include "PCAPMocapBridge.h"

#include "PropRosterEntry.h"

// Performance Capture plugin (UE 5.8) — the official Mocap Manager actors.
#include "CapturePerformer.h"       // PerformanceCaptureCore — ACapturePerformer
#include "PCapPropComponent.h"      // PerformanceCaptureWorkflowRuntime — UPCapPropComponent

#include "LiveLinkTypes.h"          // FLiveLinkSubjectName
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

FName UPCAPMocapBridge::ResolvePerformerSubject(const FShotSubject& Subject)
{
    if (Subject.bHasBodyStream && !Subject.BodyStream.LiveLinkSubjectName.IsNone())
    {
        return Subject.BodyStream.LiveLinkSubjectName;
    }
    if (Subject.bHasFaceStream && !Subject.FaceStream.LiveLinkSubjectName.IsNone())
    {
        return Subject.FaceStream.LiveLinkSubjectName;
    }
    return NAME_None;
}

bool UPCAPMocapBridge::ConfigurePerformer(ACapturePerformer* Performer, const FShotSubject& Subject)
{
    if (!Performer)
    {
        return false;
    }

    const FName SubjectName = ResolvePerformerSubject(Subject);
    Performer->SetLiveLinkSubject(FLiveLinkSubjectName(SubjectName));
    Performer->SetEvaluateLiveLinkData(!SubjectName.IsNone());

    // DrivenTarget is what this performance drives in the scene. When it resolves
    // to a skeletal mesh asset, make it the performer's mocap mesh; anything else
    // (a placed actor / component) is left to be wired up in the Mocap Manager.
    if (!Subject.DrivenTarget.IsNull())
    {
        if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Subject.DrivenTarget.LoadSynchronous()))
        {
            Performer->SetMocapMesh(Mesh);
        }
    }

#if WITH_EDITOR
    const FString Label = !Subject.CharacterName.IsEmpty() ? Subject.CharacterName : Subject.ActorID;
    if (!Label.IsEmpty())
    {
        Performer->SetActorLabel(Label);
    }
#endif

    return true;
}

ACapturePerformer* UPCAPMocapBridge::SpawnPerformerForSubject(UWorld* World, const FShotSubject& Subject)
{
    if (!World)
    {
        return nullptr;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ACapturePerformer* Performer =
        World->SpawnActor<ACapturePerformer>(ACapturePerformer::StaticClass(),
                                             FVector::ZeroVector, FRotator::ZeroRotator, Params);
    ConfigurePerformer(Performer, Subject);
    return Performer;
}

AActor* UPCAPMocapBridge::SpawnPropActor(UWorld* World, const FPropEntry& Prop, const UPropRosterEntry* Roster)
{
    if (!World)
    {
        return nullptr;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // Resolve the prop's asset to decide which mesh actor to place.
    UObject* PropAsset = (Roster && !Roster->PropAsset.IsNull()) ? Roster->PropAsset.LoadSynchronous() : nullptr;

    AActor* Spawned = nullptr;
    USceneComponent* MeshComp = nullptr;

    if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(PropAsset))
    {
        ASkeletalMeshActor* SkelActor =
            World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(),
                                                  FVector::ZeroVector, FRotator::ZeroRotator, Params);
        if (SkelActor && SkelActor->GetSkeletalMeshComponent())
        {
            SkelActor->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(SkelMesh);
            MeshComp = SkelActor->GetSkeletalMeshComponent();
        }
        Spawned = SkelActor;
    }
    else
    {
        // Static mesh (or unresolved asset — still placed so the tracked
        // transform has something to drive).
        AStaticMeshActor* StaticActor =
            World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
                                                FVector::ZeroVector, FRotator::ZeroRotator, Params);
        if (StaticActor && StaticActor->GetStaticMeshComponent())
        {
            StaticActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(PropAsset))
            {
                StaticActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
            }
            MeshComp = StaticActor->GetStaticMeshComponent();
        }
        Spawned = StaticActor;
    }

    if (!Spawned)
    {
        return nullptr;
    }

    // Attach the prop component and bind it to the prop's Live Link subject.
    UPCapPropComponent* PropComp = NewObject<UPCapPropComponent>(Spawned);
    if (PropComp)
    {
        Spawned->AddInstanceComponent(PropComp);
        PropComp->RegisterComponent();

        const FName SubjectName = (Prop.bIsTracked && !Prop.LiveLinkSubjectName.IsNone())
            ? Prop.LiveLinkSubjectName
            : (Roster ? Roster->DefaultLiveLinkName : NAME_None);

        PropComp->SetLiveLinkSubject(FLiveLinkSubjectName(SubjectName));
        PropComp->SetEvaluateLiveLinkData(Prop.bIsTracked && !SubjectName.IsNone());
        if (MeshComp)
        {
            PropComp->SetControlledComponent(MeshComp);
        }
    }

#if WITH_EDITOR
    if (!Prop.PropID.IsEmpty())
    {
        Spawned->SetActorLabel(Prop.PropID);
    }
#endif

    return Spawned;
}

int32 UPCAPMocapBridge::SpawnShotToStage(UWorld* World, const FShot& Shot,
                                         const TArray<UPropRosterEntry*>& PropRoster,
                                         TArray<AActor*>& OutSpawned)
{
    if (!World)
    {
        return 0;
    }

    int32 Count = 0;

    for (const FShotSubject& Subject : Shot.Subjects)
    {
        if (ACapturePerformer* Performer = SpawnPerformerForSubject(World, Subject))
        {
            OutSpawned.Add(Performer);
            ++Count;
        }
    }

    for (const FPropEntry& Prop : Shot.Props)
    {
        // Match the called prop to its permanent roster record by PropID.
        const UPropRosterEntry* Roster = nullptr;
        for (const UPropRosterEntry* Entry : PropRoster)
        {
            if (Entry && Entry->PropID == Prop.PropID)
            {
                Roster = Entry;
                break;
            }
        }

        if (AActor* PropActor = SpawnPropActor(World, Prop, Roster))
        {
            OutSpawned.Add(PropActor);
            ++Count;
        }
    }

    return Count;
}
