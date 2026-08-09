#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCAPToolTypes.h"   // FShotSubject / FRetargetConfig (+ fwd decls for UIKRetargeter / UIKRigDefinition)
#include "PCAPCharacterBridge.generated.h"

class UActorRosterEntry;
class USkeletalMesh;
class ACapturePerformer;   // PerformanceCaptureCore — CapturePerformer.h

// Epic's canonical character record (UPCapCharacterDataAsset), read out by
// reflection. The class lives in the Performance Capture *Workflow* plugin's
// private module, so it can never be #included — only resolved by /Script path
// and read by FProperty name, exactly as UPCAPMocapData reads the other PCap
// assets. bFound is false when the plugin is absent or nothing matched.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPCharacterRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bFound = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    FName CharacterName;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UObject> Asset;                  // the Epic character asset itself

    // UPCapCharacterDataAsset types this as TSoftClassPtr<ASkeletalMeshActor>, so the
    // class is NOT guaranteed to be an ACaptureCharacter — resolved here to whatever
    // AActor class it names (a MetaHuman Blueprint, typically) and cast at bind time.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSubclassOf<AActor> CaptureCharacterClass;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UIKRetargeter> Retargeter;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UIKRigDefinition> IKRig;
};

// What a bind actually achieved. Every field is reported rather than assumed, so
// the Operator Console can show a called subject as "double is live" vs "spawned
// but not driven" without re-deriving any of it, and so the finger-data gap below
// stays visible instead of being silently swallowed.
USTRUCT(BlueprintType)
struct PCAPTOOL_API FPCAPCharacterBindResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bRetargetComponentBound = false;   // the actor has a URetargetComponent we could reach

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bSourcePerformerBound = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bRetargetAssetBound = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bTargetMeshBound = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bUsedPCapCharacterRecord = false;  // an Epic UPCapCharacterDataAsset filled some gaps

    // ── Finger data — reported, never faked ──
    // URetargetComponent has no pose-injection hook, so a FallbackHandPose cannot be
    // applied on the retarget path. When bFallbackHandPosePending is true the pose is
    // configured and still unapplied; whoever owns the take (Sequencer additive track /
    // anim BP) has to apply it. See the design spec's "Finger data" decision.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bHasFingerData = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bFallbackHandPosePending = false;

    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TSoftObjectPtr<UAnimSequence> FallbackHandPose;

    // FRetargetConfig.IKRigTarget carried a target rig that the bridge did not apply:
    // URetargetComponent has no IK-rig slot (the target rig is baked into the
    // UIKRetargeter asset), so the field is authoring data, not a runtime binding.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    bool bTargetIKRigUnapplied = false;

    // Operator-readable notes for everything that degraded. Each entry is also logged.
    UPROPERTY(BlueprintReadOnly, Category="PCAP")
    TArray<FString> Warnings;
};

// ---------------------------------------------------------------------------
// PCAP ▸ digital double (UE 5.8)
//
// UPCAPMocapBridge gets a called performer streaming onto a raw mocap mesh.
// This bridge takes the next step: it puts the performer's *digital double* on
// the stage and retargets the performance onto it.
//
//   UActorRosterEntry.MetaHuman  -> the character actor's class / skeletal mesh
//   FRetargetConfig.IKRetargeter -> URetargetComponent.RetargetAsset
//   ACapturePerformer            -> URetargetComponent.SourcePerformer
//
// ACaptureCharacter and URetargetComponent are header-verified against the 5.8
// install (PerformanceCaptureCore, already a module dependency), so they are
// called directly. Only Epic's UPCapCharacterDataAsset — in the Workflow
// plugin's *private* module — is reached by reflection.
//
// Resolution rule everywhere below: the explicit call inputs win (Roster,
// FRetargetConfig), Epic's canonical character record fills the gaps, and the
// called subject's DrivenTarget is the last resort. All functions are no-ops on
// a null World/actor and return null/false rather than asserting.
// ---------------------------------------------------------------------------
UCLASS()
class PCAPTOOL_API UPCAPCharacterBridge : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // True when the Performance Capture Workflow plugin's character data-asset class
    // resolves — i.e. Epic's canonical character records can be read. Mirrors
    // UPCAPMocapData::IsWorkflowAvailable(). The Core side (ACaptureCharacter /
    // URetargetComponent) is a compile-time dependency and is always present, so
    // this gates the *record* lookup only, never the bind itself.
    UFUNCTION(BlueprintPure, Category="PCAP|Mocap Manager")
    static bool IsPCapCharacterDataAvailable();

    // Pre-flight for a "send digital double to stage" action: true when this subject
    // can actually be retargeted, else false with an operator-readable reason for the
    // disabled-button tooltip. Conservative — it only judges the explicit inputs, so a
    // subject covered solely by an Epic character record still reports false here.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static bool CanBuildCharacter(const UActorRosterEntry* Roster, const FRetargetConfig& Retarget,
                                  FString& OutReason);

    // Epic's canonical character record for a subject, matched on CharacterName and
    // then ActorID (case-insensitive). Empty (bFound=false) when the Workflow plugin
    // is absent or nothing matches.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static FPCAPCharacterRecord FindCharacterRecordForSubject(const FShotSubject& Subject);

    // The roster's digital double as a skeletal mesh, or null when MetaHuman is unset
    // or points at something else (a character Blueprint — see ResolveDigitalDoubleClass).
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static USkeletalMesh* ResolveDigitalDoubleMesh(const UActorRosterEntry* Roster);

    // The roster's digital double as a spawnable actor class — MetaHuman set to a
    // Blueprint (or a generated class) resolves here. Null when it is a plain mesh.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static TSubclassOf<AActor> ResolveDigitalDoubleClass(const UActorRosterEntry* Roster);

    // Spawn a called subject's digital double and retarget SourcePerformer onto it.
    // Spawn class: the roster's MetaHuman Blueprint, else the Epic record's
    // CaptureCharacterClass, else a plain ACaptureCharacter. Returns null if World is
    // null or the spawn failed; OutResult says what bound and what didn't.
    // Pair with UPCAPMocapBridge::SpawnPerformerForSubject — this bridge never spawns
    // the performer, it only consumes one.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static AActor* SpawnCharacterForSubject(UWorld* World, const FShotSubject& Subject,
                                            const UActorRosterEntry* Roster,
                                            ACapturePerformer* SourcePerformer,
                                            const FRetargetConfig& Retarget,
                                            FPCAPCharacterBindResult& OutResult);

    // Bind an already-placed character actor: its retarget component (found, or added
    // when the actor has none), the source performer, the IK Retargeter, and the target
    // mesh when the actor doesn't already carry one. Works on an ACaptureCharacter via
    // its own setters and on any other actor via a URetargetComponent directly.
    // Returns false only when there is no actor / no retarget component to bind.
    UFUNCTION(BlueprintCallable, Category="PCAP|Mocap Manager")
    static bool ConfigureRetargetComponent(AActor* CharacterActor, const FRetargetConfig& Retarget,
                                           ACapturePerformer* SourcePerformer,
                                           USkeletalMesh* TargetMesh,
                                           FPCAPCharacterBindResult& OutResult);
};
