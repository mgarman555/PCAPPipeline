#include "PCAPCharacterBridge.h"

#include "ActorRosterEntry.h"

// Performance Capture plugin (UE 5.8) — header-verified against the install, so the
// character/retarget path is direct typed calls, not reflection. ACaptureCharacter is
// UCLASS(MinimalAPI) with PERFORMANCECAPTURECORE_API methods; both classes ship in
// PerformanceCaptureCore, already a PCAPTool.Build.cs dependency.
#include "CaptureCharacter.h"           // PerformanceCaptureCore — ACaptureCharacter
#include "CapturePerformer.h"           // PerformanceCaptureCore — ACapturePerformer
#include "RetargetComponent.h"          // PerformanceCaptureCore — URetargetComponent
#include "Retargeter/IKRetargeter.h"    // IKRig — UIKRetargeter (SetRetargetAsset argument)

#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"                    // FNameProperty / FSoftObjectProperty
#include "UObject/TopLevelAssetPath.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// ---------------------------------------------------------------------------
// Reflection targets — Epic's PRIVATE Performance Capture Workflow data model.
//
// UPCapCharacterDataAsset lives in the Workflow plugin's Private/ folder, so it
// can never be #included: it is resolved by /Script path and read by FProperty
// name, the same way UPCAPMocapData reads the other PCap assets. The names below
// were taken from the 5.8 headers, but they are still *runtime* lookups — a
// renamed field or a machine without the Workflow plugin must degrade to a log,
// never to a link error. CONFIRM ON THE WINDOWS INSTALL: these strings are the
// only unverified-at-compile-time thing in this file (see the Verify section of
// docs/specs/2026-08-09-pcap-retarget-character-design.md).
// ---------------------------------------------------------------------------
static const TCHAR* GPCapWorkflowModulePath      = TEXT("/Script/PerformanceCaptureWorkflow");
static const TCHAR* GPCapCharacterClassName      = TEXT("PCapCharacterDataAsset");
static const TCHAR* GPCapCharacterClassPath      = TEXT("/Script/PerformanceCaptureWorkflow.PCapCharacterDataAsset");

static const TCHAR* GPCapCharacterNameProp       = TEXT("CharacterName");           // FName
static const TCHAR* GPCapCharacterClassProp      = TEXT("CaptureCharacterClass");   // TSoftClassPtr<ASkeletalMeshActor>
static const TCHAR* GPCapCharacterMeshProp       = TEXT("SkeletalMesh");            // TSoftObjectPtr<USkeletalMesh>
static const TCHAR* GPCapCharacterRetargeterProp = TEXT("Retargeter");              // TSoftObjectPtr<UIKRetargeter>
static const TCHAR* GPCapCharacterIKRigProp      = TEXT("IKRig");                   // TSoftObjectPtr<UIKRigDefinition>

namespace
{
    // ── Reflected field readers (return defaults when the field is absent) ──

    FName ReadName(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FNameProperty* P = FindFProperty<FNameProperty>(Obj->GetClass(), PropName))
        {
            return P->GetPropertyValue_InContainer(Obj);
        }
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Character bridge: %s has no FName property '%s' — confirm against the 5.8 Workflow headers."),
            *Obj->GetClass()->GetName(), PropName);
        return NAME_None;
    }

    // Covers TSoftObjectPtr *and* TSoftClassPtr — FSoftClassProperty derives from
    // FSoftObjectProperty, so one reader serves both.
    FSoftObjectPath ReadSoftPath(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FSoftObjectProperty* P = FindFProperty<FSoftObjectProperty>(Obj->GetClass(), PropName))
        {
            return P->GetPropertyValue_InContainer(Obj).ToSoftObjectPath();
        }
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Character bridge: %s has no soft-object property '%s' — confirm against the 5.8 Workflow headers."),
            *Obj->GetClass()->GetName(), PropName);
        return FSoftObjectPath();
    }

    // ── Small shared helpers ──

    // An asset reference is only a spawnable character when it names an AActor class.
    // Content-browser picks land on the UBlueprint, Epic's TSoftClassPtr on the
    // generated UClass — accept both, reject a mesh (that path sets the mesh instead).
    UClass* ResolveActorClass(UObject* Object)
    {
        if (UClass* AsClass = Cast<UClass>(Object))
        {
            return AsClass->IsChildOf(AActor::StaticClass()) ? AsClass : nullptr;
        }
        if (const UBlueprint* AsBlueprint = Cast<UBlueprint>(Object))
        {
            UClass* Generated = AsBlueprint->GeneratedClass;
            return (Generated && Generated->IsChildOf(AActor::StaticClass())) ? Generated : nullptr;
        }
        return nullptr;
    }

    // The mesh component the retarget drives. ACaptureCharacter (and any other
    // ASkeletalMeshActor) names it directly; a MetaHuman Blueprint just has one.
    USkeletalMeshComponent* GetCharacterMeshComponent(AActor* Actor)
    {
        if (ASkeletalMeshActor* SkelActor = Cast<ASkeletalMeshActor>(Actor))
        {
            return SkelActor->GetSkeletalMeshComponent();
        }
        return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
    }

    // Character name when the shot names one, else the performer's ActorID.
    FString SubjectLabel(const FShotSubject& Subject)
    {
        return !Subject.CharacterName.IsEmpty() ? Subject.CharacterName : Subject.ActorID;
    }

    // Everything that degrades goes through here: recorded for the UI and logged once.
    void Warn(FPCAPCharacterBindResult& Result, FString&& Message)
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Character bridge: %s"), *Message);
        Result.Warnings.Add(MoveTemp(Message));
    }
}

// ── Availability ────────────────────────────────────────────────────────────

bool UPCAPCharacterBridge::IsPCapCharacterDataAvailable()
{
#if WITH_PCAP_WORKFLOW
    return FindObject<UClass>(nullptr, GPCapCharacterClassPath) != nullptr;
#else
    return false;
#endif
}

bool UPCAPCharacterBridge::CanBuildCharacter(const UActorRosterEntry* Roster, const FRetargetConfig& Retarget,
                                             FString& OutReason)
{
    OutReason.Reset();

    if (!Roster)
    {
        OutReason = TEXT("No roster entry — this subject isn't in the Actor Database.");
        return false;
    }
    if (Roster->MetaHuman.IsNull())
    {
        OutReason = FString::Printf(TEXT("%s has no digital double — set Digital Double → MetaHuman in the Actor Database."),
                                    *Roster->ActorID);
        return false;
    }
    if (Retarget.IKRetargeter.IsNull())
    {
        OutReason = TEXT("The stage's retarget chain has no IK Retargeter — set one on the Stage Config.");
        return false;
    }
    return true;
}

// ── Epic's canonical character record (reflection over the private module) ──

FPCAPCharacterRecord UPCAPCharacterBridge::FindCharacterRecordForSubject(const FShotSubject& Subject)
{
    FPCAPCharacterRecord Record;

#if WITH_PCAP_WORKFLOW
    if (!IsPCapCharacterDataAvailable())
    {
        return Record;
    }

    const FString CharacterName = Subject.CharacterName;
    const FString ActorID       = Subject.ActorID;
    if (CharacterName.IsEmpty() && ActorID.IsEmpty())
    {
        return Record;
    }

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

    TArray<FAssetData> Found;
    const FTopLevelAssetPath ClassPath(GPCapWorkflowModulePath, GPCapCharacterClassName);
    ARM.Get().GetAssetsByClass(ClassPath, Found, /*bSearchSubClasses*/ true);

    // CharacterName is the intended key; ActorID is the fallback for rosters that
    // haven't split person from role yet. One pass over the assets (GetAsset() loads
    // the package, so it is not worth walking twice), keeping the best-ranked hit.
    UObject* BestAsset = nullptr;
    FName     BestName;
    int32     BestRank = MAX_int32;   // 0 = CharacterName hit, 1 = ActorID hit

    for (const FAssetData& AssetData : Found)
    {
        UObject* Asset = AssetData.GetAsset();
        if (!Asset)
        {
            continue;
        }

        const FName Name = ReadName(Asset, GPCapCharacterNameProp);
        if (Name.IsNone())
        {
            continue;
        }

        const FString NameString = Name.ToString();
        int32 Rank = MAX_int32;
        if (!CharacterName.IsEmpty() && NameString.Equals(CharacterName, ESearchCase::IgnoreCase))
        {
            Rank = 0;
        }
        else if (!ActorID.IsEmpty() && NameString.Equals(ActorID, ESearchCase::IgnoreCase))
        {
            Rank = 1;
        }

        if (Rank < BestRank)
        {
            BestRank  = Rank;
            BestAsset = Asset;
            BestName  = Name;
            if (Rank == 0)
            {
                break;   // an exact character-name hit can't be beaten
            }
        }
    }

    if (BestAsset)
    {
        const FSoftObjectPath CharacterClassPath = ReadSoftPath(BestAsset, GPCapCharacterClassProp);
        UClass* CharacterClass = CharacterClassPath.IsNull() ? nullptr : ResolveActorClass(CharacterClassPath.TryLoad());

        Record.bFound                = true;
        Record.CharacterName         = BestName;
        Record.Asset                 = BestAsset;
        Record.CaptureCharacterClass = CharacterClass;
        Record.SkeletalMesh          = TSoftObjectPtr<USkeletalMesh>(ReadSoftPath(BestAsset, GPCapCharacterMeshProp));
        Record.Retargeter            = TSoftObjectPtr<UIKRetargeter>(ReadSoftPath(BestAsset, GPCapCharacterRetargeterProp));
        Record.IKRig                 = TSoftObjectPtr<UIKRigDefinition>(ReadSoftPath(BestAsset, GPCapCharacterIKRigProp));
    }
#endif

    return Record;
}

// ── Digital-double resolution off the roster ────────────────────────────────

USkeletalMesh* UPCAPCharacterBridge::ResolveDigitalDoubleMesh(const UActorRosterEntry* Roster)
{
    if (!Roster || Roster->MetaHuman.IsNull())
    {
        return nullptr;
    }
    return Cast<USkeletalMesh>(Roster->MetaHuman.LoadSynchronous());
}

TSubclassOf<AActor> UPCAPCharacterBridge::ResolveDigitalDoubleClass(const UActorRosterEntry* Roster)
{
    if (!Roster || Roster->MetaHuman.IsNull())
    {
        return nullptr;
    }
    return ResolveActorClass(Roster->MetaHuman.LoadSynchronous());
}

// ── Bind ────────────────────────────────────────────────────────────────────

bool UPCAPCharacterBridge::ConfigureRetargetComponent(AActor* CharacterActor, const FRetargetConfig& Retarget,
                                                      ACapturePerformer* SourcePerformer,
                                                      USkeletalMesh* TargetMesh,
                                                      FPCAPCharacterBindResult& OutResult)
{
    if (!CharacterActor)
    {
        Warn(OutResult, TEXT("ConfigureRetargetComponent called with a null actor."));
        return false;
    }

    const FString ActorName = CharacterActor->GetName();

    // 1. Target mesh — applied only when the actor doesn't already carry one. A double
    //    spawned from its own Blueprint (a MetaHuman BP) ships with the correct body
    //    mesh, materials and LODs; overwriting that with the roster's reference would
    //    quietly strip them. Set it before the retarget binds, so the component
    //    initialises against the mesh it will actually drive.
    USkeletalMeshComponent* MeshComp = GetCharacterMeshComponent(CharacterActor);
    if (TargetMesh)
    {
        if (!MeshComp)
        {
            Warn(OutResult, FString::Printf(TEXT("'%s' has no USkeletalMeshComponent — the digital-double mesh was not applied."), *ActorName));
        }
        else if (USkeletalMesh* Existing = MeshComp->GetSkeletalMeshAsset())
        {
            OutResult.bTargetMeshBound = (Existing == TargetMesh);
            if (Existing != TargetMesh)
            {
                UE_LOG(LogTemp, Log, TEXT("[PCAP] Character bridge: '%s' already carries mesh '%s' — kept it instead of '%s'."),
                    *ActorName, *Existing->GetName(), *TargetMesh->GetName());
            }
        }
        else
        {
            MeshComp->SetSkeletalMeshAsset(TargetMesh);
            OutResult.bTargetMeshBound = true;
        }
    }

    // 2. Retarget asset. The target IK rig is baked into it — see step 5.
    UIKRetargeter* Retargeter = nullptr;
    if (!Retarget.IKRetargeter.IsNull())
    {
        Retargeter = Retarget.IKRetargeter.LoadSynchronous();
        if (!Retargeter)
        {
            Warn(OutResult, FString::Printf(TEXT("IK Retargeter '%s' failed to load."),
                *Retarget.IKRetargeter.ToSoftObjectPath().ToString()));
        }
    }
    else
    {
        Warn(OutResult, TEXT("No IK Retargeter configured — the character will not follow the performer."));
    }

    if (!SourcePerformer)
    {
        Warn(OutResult, TEXT("No source performer supplied — the character has nothing to retarget from."));
    }

    // 3. Bind. ACaptureCharacter's own setters forward to its (private) retarget
    //    component and re-initiate the animation, so they are preferred. Anything else
    //    — a MetaHuman Blueprint, or a CaptureCharacterClass that isn't an
    //    ACaptureCharacter (the PCap asset types that field as ASkeletalMeshActor) —
    //    gets a URetargetComponent driven directly.
    if (ACaptureCharacter* Character = Cast<ACaptureCharacter>(CharacterActor))
    {
        if (Retargeter)
        {
            Character->SetRetargetAsset(Retargeter);
            OutResult.bRetargetAssetBound = true;
        }
        if (SourcePerformer)
        {
            Character->SetSourcePerformer(SourcePerformer);
            OutResult.bSourcePerformerBound = true;
        }

        OutResult.bRetargetComponentBound = (Character->GetRetargetComponent() != nullptr);
        if (!OutResult.bRetargetComponentBound)
        {
            Warn(OutResult, FString::Printf(TEXT("'%s' is an ACaptureCharacter but GetRetargetComponent() returned null — the setters had nothing to forward to."), *ActorName));
        }
    }
    else
    {
        URetargetComponent* RetargetComp = CharacterActor->FindComponentByClass<URetargetComponent>();
        if (!RetargetComp)
        {
            RetargetComp = NewObject<URetargetComponent>(CharacterActor);
            if (RetargetComp)
            {
                CharacterActor->AddInstanceComponent(RetargetComp);
                RetargetComp->RegisterComponent();
            }
        }

        if (!RetargetComp)
        {
            Warn(OutResult, FString::Printf(TEXT("Could not add a URetargetComponent to '%s'."), *ActorName));
            return false;
        }
        OutResult.bRetargetComponentBound = true;

        // ACaptureCharacter wires its controlled mesh itself; a plain actor can't, so
        // point the component at the mesh before handing it the performer.
        if (MeshComp)
        {
            RetargetComp->SetControlledMesh(MeshComp);
        }
        else
        {
            Warn(OutResult, FString::Printf(TEXT("'%s' has no USkeletalMeshComponent for its retarget component to control."), *ActorName));
        }

        if (Retargeter)
        {
            RetargetComp->SetRetargetAsset(Retargeter);
            OutResult.bRetargetAssetBound = true;
        }
        if (SourcePerformer)
        {
            RetargetComp->SetSourcePerformer(SourcePerformer);
            OutResult.bSourcePerformerBound = true;
        }

        // The setters above land on a component we just built by hand — rebuild the
        // retarget anim instance once, rather than relying on each setter's own call.
        RetargetComp->InitiateAnimation();
    }

    // 4. Finger data. FRetargetConfig carries HasFingerData + a FallbackHandPose, but
    //    URetargetComponent has no pose-injection hook (only RetargetAsset and a
    //    FRetargetProfile of chain overrides), so there is nowhere honest to put the
    //    pose here. Report the state instead of writing dead code — see the design
    //    spec's "Finger data" decision for the escalation path.
    OutResult.bHasFingerData           = Retarget.HasFingerData;
    OutResult.FallbackHandPose         = Retarget.FallbackHandPose;
    OutResult.bFallbackHandPosePending = !Retarget.HasFingerData && !Retarget.FallbackHandPose.IsNull();

    if (OutResult.bFallbackHandPosePending)
    {
        Warn(OutResult, FString::Printf(
            TEXT("No finger data, and fallback hand pose '%s' is configured — the retarget path cannot apply it. Apply it downstream (Sequencer additive track / anim BP)."),
            *Retarget.FallbackHandPose.ToSoftObjectPath().ToString()));
    }
    else if (!Retarget.HasFingerData)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[PCAP] Character bridge: '%s' has no finger data and no fallback hand pose — its hands will show whatever the retargeter produces."),
            *ActorName);
    }

    // 5. Target IK rig. URetargetComponent has no IK-rig slot: the target rig is baked
    //    into the UIKRetargeter asset. FRetargetConfig.IKRigTarget is authoring data
    //    (it maps onto UPCapCharacterDataAsset.IKRig), so it is reported, not applied.
    OutResult.bTargetIKRigUnapplied = !Retarget.IKRigTarget.IsNull();
    if (OutResult.bTargetIKRigUnapplied)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[PCAP] Character bridge: target IK rig '%s' is carried by the retargeter asset, not bound separately."),
            *Retarget.IKRigTarget.ToSoftObjectPath().ToString());
    }

    return OutResult.bRetargetComponentBound;
}

AActor* UPCAPCharacterBridge::SpawnCharacterForSubject(UWorld* World, const FShotSubject& Subject,
                                                       const UActorRosterEntry* Roster,
                                                       ACapturePerformer* SourcePerformer,
                                                       const FRetargetConfig& Retarget,
                                                       FPCAPCharacterBindResult& OutResult)
{
    if (!World)
    {
        return nullptr;
    }

    const FPCAPCharacterRecord Record = FindCharacterRecordForSubject(Subject);
    OutResult.bUsedPCapCharacterRecord = Record.bFound;

    // Explicit call inputs win; Epic's canonical record fills the gaps; the called
    // subject's DrivenTarget is the last resort.
    UClass* SpawnClass = ResolveDigitalDoubleClass(Roster);
    if (!SpawnClass)
    {
        SpawnClass = Record.CaptureCharacterClass.Get();
    }
    if (!SpawnClass)
    {
        SpawnClass = ACaptureCharacter::StaticClass();
    }

    USkeletalMesh* TargetMesh = ResolveDigitalDoubleMesh(Roster);
    if (!TargetMesh && !Record.SkeletalMesh.IsNull())
    {
        TargetMesh = Record.SkeletalMesh.LoadSynchronous();
    }
    if (!TargetMesh && !Subject.DrivenTarget.IsNull())
    {
        TargetMesh = Cast<USkeletalMesh>(Subject.DrivenTarget.LoadSynchronous());
    }

    FRetargetConfig Effective = Retarget;
    if (Effective.IKRetargeter.IsNull() && !Record.Retargeter.IsNull())
    {
        Effective.IKRetargeter = Record.Retargeter;
    }
    if (Effective.IKRigTarget.IsNull() && !Record.IKRig.IsNull())
    {
        Effective.IKRigTarget = Record.IKRig;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AActor* Character = World->SpawnActor<AActor>(SpawnClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (!Character)
    {
        Warn(OutResult, FString::Printf(TEXT("Failed to spawn character class '%s' for '%s'."),
            *SpawnClass->GetName(), *SubjectLabel(Subject)));
        return nullptr;
    }

    ConfigureRetargetComponent(Character, Effective, SourcePerformer, TargetMesh, OutResult);

#if WITH_EDITOR
    // The performer already claims the subject's name (UPCAPMocapBridge::ConfigurePerformer),
    // so suffix the double — otherwise the outliner shows two identically-named actors.
    const FString Label = SubjectLabel(Subject);
    if (!Label.IsEmpty())
    {
        Character->SetActorLabel(FString::Printf(TEXT("%s_Character"), *Label));
    }
#endif

    return Character;
}
