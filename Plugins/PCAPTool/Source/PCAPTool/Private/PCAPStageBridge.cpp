#include "PCAPStageBridge.h"

#include "StageConfigAsset.h"
#include "PCAPToolTypes.h"                        // ETimecodeSource

#include "UObject/UnrealType.h"                   // FBoolProperty / FStrProperty / FStructProperty
#include "UObject/EnumProperty.h"                 // FEnumProperty
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"
#include "EngineUtils.h"                          // TActorIterator
#include "FileHelpers.h"

// ───────────────────────────────────────────────────────────────────────────
// Reflection surface — Mocap Manager (Performance Capture Workflow, UE 5.8).
//
// The Workflow plugin's stage classes carry no API macro and live in a private
// module folder, so PCAPTool reaches them only by /Script path and FProperty
// name — never by #include. The names below were read off the real 5.8 headers
// (PCapSessionTemplate.h, PCapStageRoot.h, PCapDatabase.h); each is marked with
// what that read did and did not settle. Everything still open is listed in the
// Verify checklist of docs/specs/2026-08-09-pcap-stage-alignment-design.md, and
// every lookup below logs and no-ops rather than assuming.
// ───────────────────────────────────────────────────────────────────────────

// VERIFIED — UPCapSessionTemplate : UPCapDataAsset, in /Script/PerformanceCaptureWorkflow.
static const TCHAR* GPCapSessionTemplateClassPath = TEXT("/Script/PerformanceCaptureWorkflow.PCapSessionTemplate");

// Class name VERIFIED, module UNVERIFIED. The header is PCapStageRoot.h but the
// class inside it is APerformanceCaptureStageRoot — the two do not match, and
// the header alone did not settle which module exports it. Both candidates are
// tried and the one that resolves is logged once.
static const TCHAR* GStageRootClassPaths[] =
{
    TEXT("/Script/PerformanceCaptureWorkflowRuntime.PerformanceCaptureStageRoot"),
    TEXT("/Script/PerformanceCaptureWorkflow.PerformanceCaptureStageRoot"),
};

// VERIFIED — UPCapDataAsset's durable key, the same one the performer and prop
// extensions pair on. Private in C++ behind GetAssetUID(), but a UPROPERTY, so
// FindFProperty reaches it: reflection does not observe access specifiers.
static const TCHAR* GAssetUIDPropertyName = TEXT("AssetUID");

// VERIFIED — the edit lock. Protected + VisibleAnywhere, and every editable
// field on the template carries meta=(EditCondition="bIsEditable"). It goes
// false once a session has been created from the template, freezing its
// tokenised strings into that session's serialized data.
static const TCHAR* GIsEditablePropertyName = TEXT("bIsEditable");

// VERIFIED — identity of the session the template seeds (read-only here; these
// name a production/session, not a stage, so PCAPTool never writes them).
static const TCHAR* GProductionNamePropertyName = TEXT("ProductionName");
static const TCHAR* GSessionNamePropertyName    = TEXT("SessionName");

// VERIFIED — EUpdateClockSource (MovieScene), default EUpdateClockSource::Timecode.
// The one field on the template PCAPTool has an opinion about.
static const TCHAR* GRecordingClockSourcePropertyName = TEXT("RecordingClockSource");

// VERIFIED as a UFUNCTION(BlueprintCallable); its parameter list is UNVERIFIED,
// so the call is guarded on NumParms == 0. Re-evaluates every tokenised string
// and folder path on the template.
static const TCHAR* GUpdateAllFieldsFunctionName = TEXT("UpdateAllFields");

namespace
{
    IAssetRegistry* GetAssetRegistry()
    {
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        return &ARM.Get();
    }

    void SavePackageFor(UObject* Obj)
    {
        if (Obj)
        {
            if (UPackage* Pkg = Obj->GetPackage())
            {
                Pkg->MarkPackageDirty();
                FEditorFileUtils::PromptForCheckoutAndSave({ Pkg }, /*bCheckDirty*/ false, /*bPromptToSave*/ false);
            }
        }
    }

    // "(unset)" reads better than a blank column in a report the operator scans.
    FString DisplayOrUnset(const FString& Value)
    {
        return Value.IsEmpty() ? FString(TEXT("(unset)")) : Value;
    }

    FPCAPStageDivergence MakeDivergence(const TCHAR* Field, const FString& PCAPToolValue,
                                        const FString& MocapManagerValue, EPCAPStageDivergenceKind Kind)
    {
        FPCAPStageDivergence Row;
        Row.Field             = Field;
        Row.PCAPToolValue     = PCAPToolValue;
        Row.MocapManagerValue = MocapManagerValue;
        Row.Kind              = Kind;
        return Row;
    }

#if WITH_PCAP_WORKFLOW

    // ── Class resolution ────────────────────────────────────────────────────

    UClass* ResolveSessionTemplateClass()
    {
        if (UClass* Class = FindObject<UClass>(nullptr, GPCapSessionTemplateClassPath))
        {
            return Class;
        }

        static bool bLoggedMiss = false;
        if (!bLoggedMiss)
        {
            bLoggedMiss = true;
            UE_LOG(LogTemp, Warning,
                TEXT("[PCAP] Mocap Manager session template not found at '%s' — stage alignment is inert. Confirm the /Script path on Windows."),
                GPCapSessionTemplateClassPath);
        }
        return nullptr;
    }

    UClass* ResolveStageRootClass()
    {
        for (const TCHAR* Path : GStageRootClassPaths)
        {
            if (UClass* Class = FindObject<UClass>(nullptr, Path))
            {
                static bool bLoggedHit = false;
                if (!bLoggedHit)
                {
                    bLoggedHit = true;
                    UE_LOG(LogTemp, Display, TEXT("[PCAP] Mocap Manager stage root resolved at '%s'."), Path);
                }
                return Class;
            }
        }

        static bool bLoggedMiss = false;
        if (!bLoggedMiss)
        {
            bLoggedMiss = true;
            UE_LOG(LogTemp, Warning,
                TEXT("[PCAP] APerformanceCaptureStageRoot not found at either candidate /Script path — stage-root queries return empty. Confirm its module on Windows."));
        }
        return nullptr;
    }

    // ── Reflected reads (defaults when a field is absent) ────────────────────

    FGuid ReadAssetUID(const UObject* Obj)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), GAssetUIDPropertyName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                if (const FGuid* Value = P->ContainerPtrToValuePtr<FGuid>(Obj))
                {
                    return *Value;
                }
            }
        }
        return FGuid();
    }

    // Returns the template's AssetUID, minting and assigning one if it was
    // invalid — the same find-or-create the performer/prop extensions do.
    FGuid EnsureAssetUID(UObject* Obj, bool& bOutAssigned)
    {
        bOutAssigned = false;
        if (FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), GAssetUIDPropertyName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                if (FGuid* Value = P->ContainerPtrToValuePtr<FGuid>(Obj))
                {
                    if (!Value->IsValid()) { *Value = FGuid::NewGuid(); bOutAssigned = true; }
                    return *Value;
                }
            }
        }
        return FGuid();
    }

    FString ReadString(const UObject* Obj, const TCHAR* PropName)
    {
        if (const FStrProperty* P = FindFProperty<FStrProperty>(Obj->GetClass(), PropName))
        {
            return P->GetPropertyValue_InContainer(Obj);
        }
        return FString();
    }

    // Reads the template's edit lock. False return = the flag itself could not
    // be read, in which case bOutEditable is left false: an unverifiable lock is
    // treated as locked, because the cost of writing a locked template (silently
    // re-pointing an in-flight session's folders and take names) is far worse
    // than the cost of refusing a write.
    bool ReadIsEditable(const UObject* Obj, bool& bOutEditable)
    {
        bOutEditable = false;
        if (const FBoolProperty* P = FindFProperty<FBoolProperty>(Obj->GetClass(), GIsEditablePropertyName))
        {
            bOutEditable = P->GetPropertyValue_InContainer(Obj);
            return true;
        }
        return false;
    }

    // "EUpdateClockSource::Timecode" -> "Timecode". UEnum reports enum-class
    // entries fully qualified; the short name is what we match on, so PCAPTool's
    // enum and Epic's line up by MEANING and never by ordinal — two unrelated
    // enums that happen to share an index would corrupt the value silently.
    FString ShortEnumeratorName(const FString& Qualified)
    {
        int32 Separator = INDEX_NONE;
        return Qualified.FindLastChar(TCHAR(':'), Separator) ? Qualified.RightChop(Separator + 1) : Qualified;
    }

    // Reads RecordingClockSource as a short enumerator name. Handles both shapes
    // an enum UPROPERTY can take: FEnumProperty (enum class) and FByteProperty
    // (TEnumAsByte). False = the property did not resolve at all.
    bool ReadClockSource(const UObject* Obj, FString& OutValue)
    {
        const UClass* Class = Obj->GetClass();

        if (const FEnumProperty* P = FindFProperty<FEnumProperty>(Class, GRecordingClockSourcePropertyName))
        {
            const UEnum* Enum = P->GetEnum();
            const FNumericProperty* Underlying = P->GetUnderlyingProperty();
            if (Enum && Underlying)
            {
                const void* ValuePtr = P->ContainerPtrToValuePtr<void>(Obj);
                OutValue = ShortEnumeratorName(Enum->GetNameStringByValue(Underlying->GetSignedIntPropertyValue(ValuePtr)));
                return true;
            }
        }

        if (const FByteProperty* P = FindFProperty<FByteProperty>(Class, GRecordingClockSourcePropertyName))
        {
            const uint8 Raw = P->GetPropertyValue_InContainer(Obj);
            OutValue = P->Enum ? ShortEnumeratorName(P->Enum->GetNameStringByValue((int64)Raw)) : FString::FromInt((int32)Raw);
            return true;
        }

        return false;
    }

    // Writes RecordingClockSource by enumerator name. OutFailure is set (and
    // false returned) when the property or the enumerator does not exist — the
    // value is never coerced to an ordinal.
    bool WriteClockSource(UObject* Obj, const TCHAR* EnumeratorName, FString& OutFailure)
    {
        UClass* Class = Obj->GetClass();

        const UEnum* Enum = nullptr;
        FNumericProperty* Underlying = nullptr;
        void* ValuePtr = nullptr;

        if (FEnumProperty* P = FindFProperty<FEnumProperty>(Class, GRecordingClockSourcePropertyName))
        {
            Enum       = P->GetEnum();
            Underlying = P->GetUnderlyingProperty();
            ValuePtr   = P->ContainerPtrToValuePtr<void>(Obj);
        }
        else if (FByteProperty* P = FindFProperty<FByteProperty>(Class, GRecordingClockSourcePropertyName))
        {
            Enum       = P->Enum;
            Underlying = P;
            ValuePtr   = P->ContainerPtrToValuePtr<void>(Obj);
        }

        if (!Enum || !Underlying || !ValuePtr)
        {
            OutFailure = FString::Printf(TEXT("'%s' did not resolve as an enum property"), GRecordingClockSourcePropertyName);
            return false;
        }

        for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)   // skip the implicit _MAX
        {
            if (ShortEnumeratorName(Enum->GetNameStringByIndex(Index)).Equals(EnumeratorName, ESearchCase::IgnoreCase))
            {
                Underlying->SetIntPropertyValue(ValuePtr, (int64)Enum->GetValueByIndex(Index));
                return true;
            }
        }

        OutFailure = FString::Printf(TEXT("'%s' has no enumerator named '%s'"), *Enum->GetName(), EnumeratorName);
        return false;
    }

    // Re-evaluates every tokenised string and folder path on the template. Those
    // fields keep an authored Template side and a computed Output side, and the
    // Output side is regenerated here — so it must never be written directly.
    // PCAPTool writes no tokenised field today; this call keeps the write
    // protocol correct and matches what the asset expects after any edit.
    void CallUpdateAllFields(UObject* Template)
    {
        UFunction* Function = Template->FindFunction(FName(GUpdateAllFieldsFunctionName));
        if (!Function)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[PCAP] Stage template '%s' has no %s() — tokenised fields were not re-evaluated."),
                *Template->GetName(), GUpdateAllFieldsFunctionName);
            return;
        }

        if (Function->NumParms != 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[PCAP] Stage template '%s': %s() takes parameters — not called. Confirm its signature on Windows."),
                *Template->GetName(), GUpdateAllFieldsFunctionName);
            return;
        }

        Template->ProcessEvent(Function, nullptr);
    }

    // The read-out for one session template. StageName falls back to the asset
    // name because UPCapSessionTemplate has no self-name field.
    FPCAPStageInfo MakeStageInfo(UObject* Asset)
    {
        FPCAPStageInfo Info;
        Info.Asset          = Asset;
        Info.StageName      = Asset->GetFName();
        Info.AssetUID       = ReadAssetUID(Asset);
        Info.ProductionName = ReadString(Asset, GProductionNamePropertyName);
        Info.SessionName    = ReadString(Asset, GSessionNamePropertyName);

        Info.bRecordingClockSourceRead = ReadClockSource(Asset, Info.RecordingClockSource);
        Info.bIsEditableRead           = ReadIsEditable(Asset, Info.bIsEditable);
        return Info;
    }

    // Every session template asset in the project, loaded. Blueprint subclasses
    // included — a duplicated-and-edited template is still a stage.
    void GatherSessionTemplates(TArray<UObject*>& OutAssets)
    {
        UClass* TemplateClass = ResolveSessionTemplateClass();
        IAssetRegistry* AssetRegistry = GetAssetRegistry();
        if (!TemplateClass || !AssetRegistry) { return; }

        TArray<FAssetData> Found;
        AssetRegistry->GetAssetsByClass(TemplateClass->GetClassPathName(), Found, /*bSearchSubClasses*/ true);

        OutAssets.Reserve(Found.Num());
        for (const FAssetData& AssetData : Found)
        {
            if (UObject* Asset = AssetData.GetAsset())
            {
                OutAssets.Add(Asset);
            }
        }
    }

    UObject* FindSessionTemplateByUID(const FGuid& StageUID)
    {
        if (!StageUID.IsValid()) { return nullptr; }

        TArray<UObject*> Templates;
        GatherSessionTemplates(Templates);
        for (UObject* Template : Templates)
        {
            if (ReadAssetUID(Template) == StageUID)
            {
                return Template;
            }
        }
        return nullptr;
    }

    // PCAPTool's ETimecodeSource -> the EUpdateClockSource enumerator to write,
    // or null when PCAPTool's value has no Mocap Manager equivalent.
    //
    // Resolved by enumerator NAME through the property's own UEnum rather than by
    // including MovieSceneSequence.h and naming the C++ enumerator: this module's
    // build gate is Windows, and a name that has moved between engine versions
    // then degrades to a logged no-op instead of a build break. It also keeps
    // MovieScene — a private dependency — out of a public header's reach.
    const TCHAR* ClockSourceNameFor(ETimecodeSource Source)
    {
        switch (Source)
        {
        // Hardware timecode is an external signal the recorder should ride, which
        // is exactly what EUpdateClockSource::Timecode means ("use the current
        // timecode provider"). This is also the template's own default.
        case ETimecodeSource::Hardware:
            return TEXT("Timecode");

        // Software timecode is generated in-engine, so the recording clock is the
        // machine's own continuous clock rather than an incoming signal. The real
        // hardware/software distinction lives in the project's Timecode Provider
        // setting, which the template does not carry; Platform vs Timecode is the
        // closest the template can express, and it keeps the two PCAPTool values
        // distinguishable on the Epic side instead of collapsing them.
        case ETimecodeSource::Software:
            return TEXT("Platform");

        // A clapper is manual sync recovered in post — there is no running
        // timecode at all. EUpdateClockSource has no enumerator meaning "no
        // timecode, sync later", so nothing is written and the divergence report
        // says so. Forcing Tick or Platform here would assert a clock PCAPTool
        // does not actually know anything about.
        case ETimecodeSource::Clapper:
            return nullptr;
        }
        return nullptr;
    }

    FString TimecodeSourceDisplay(ETimecodeSource Source)
    {
        const UEnum* Enum = StaticEnum<ETimecodeSource>();
        const FString Name = Enum ? ShortEnumeratorName(Enum->GetNameStringByValue((int64)Source)) : FString();
        const TCHAR* Mapped = ClockSourceNameFor(Source);
        return Mapped ? FString::Printf(TEXT("%s (→ %s)"), *Name, Mapped) : Name;
    }

#endif // WITH_PCAP_WORKFLOW
}

// ── Read side ───────────────────────────────────────────────────────────────

bool UPCAPStageBridge::IsStageModelAvailable()
{
#if WITH_PCAP_WORKFLOW
    return ResolveSessionTemplateClass() != nullptr;
#else
    return false;
#endif
}

TArray<FPCAPStageInfo> UPCAPStageBridge::GetAllStages()
{
    TArray<FPCAPStageInfo> Result;

#if WITH_PCAP_WORKFLOW
    TArray<UObject*> Templates;
    GatherSessionTemplates(Templates);

    Result.Reserve(Templates.Num());
    for (UObject* Template : Templates)
    {
        Result.Add(MakeStageInfo(Template));
    }
#endif

    return Result;
}

bool UPCAPStageBridge::FindStageByUID(const FGuid& StageUID, FPCAPStageInfo& OutStage)
{
#if WITH_PCAP_WORKFLOW
    if (UObject* Template = FindSessionTemplateByUID(StageUID))
    {
        OutStage = MakeStageInfo(Template);
        return true;
    }
#endif
    return false;
}

TArray<AActor*> UPCAPStageBridge::FindPlacedStageRoots(UWorld* World)
{
    TArray<AActor*> Result;

#if WITH_PCAP_WORKFLOW
    if (!World) { return Result; }

    UClass* StageRootClass = ResolveStageRootClass();
    if (!StageRootClass) { return Result; }

    // APerformanceCaptureStageRoot is Abstract, so there is never an instance of
    // the base class to find — only Blueprint subclasses (BP_DemoStage and
    // duplicates of it) placed in the level. Nothing here spawns one.
    for (TActorIterator<AActor> It(World, StageRootClass); It; ++It)
    {
        Result.Add(*It);
    }
#else
    (void)World;
#endif

    return Result;
}

// ── Pairing ─────────────────────────────────────────────────────────────────

UObject* UPCAPStageBridge::ResolvePairedStage(const UStageConfigAsset* StageConfig)
{
    if (!StageConfig || !StageConfig->PCapStageUID.IsValid())
    {
        return nullptr;
    }

#if WITH_PCAP_WORKFLOW
    // Fast path: the cached soft ref, but only when it still carries the paired
    // UID. The UID is the durable key; the soft ref is a convenience that can go
    // stale when a template is renamed, moved, or duplicated over.
    if (!StageConfig->PCapStageAsset.IsNull())
    {
        if (UObject* Cached = StageConfig->PCapStageAsset.LoadSynchronous())
        {
            if (ReadAssetUID(Cached) == StageConfig->PCapStageUID)
            {
                return Cached;
            }
        }
    }

    // Slow path: the template moved. Re-find it by UID. The stale soft ref is
    // left alone — this query is const, and re-pairing is what refreshes it.
    return FindSessionTemplateByUID(StageConfig->PCapStageUID);
#else
    UE_LOG(LogTemp, Verbose,
        TEXT("[PCAP] Stage config '%s' is paired, but this build has no Performance Capture Workflow plugin (WITH_PCAP_WORKFLOW=0)."),
        *StageConfig->ConfigName);
    return nullptr;
#endif
}

bool UPCAPStageBridge::PairStageConfig(UStageConfigAsset* StageConfig, UObject* StageAsset)
{
    if (!StageConfig || !StageAsset)
    {
        return false;
    }

#if WITH_PCAP_WORKFLOW
    UClass* TemplateClass = ResolveSessionTemplateClass();
    if (!TemplateClass)
    {
        return false;   // ResolveSessionTemplateClass already logged why
    }

    if (!StageAsset->IsA(TemplateClass))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] '%s' is a %s, not a Mocap Manager session template — stage config '%s' left unpaired."),
            *StageAsset->GetName(), *StageAsset->GetClass()->GetName(), *StageConfig->ConfigName);
        return false;
    }

    bool bAssignedUID = false;
    const FGuid StageUID = EnsureAssetUID(StageAsset, bAssignedUID);
    if (!StageUID.IsValid())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Session template '%s' has no readable '%s' — stage config '%s' left unpaired."),
            *StageAsset->GetName(), GAssetUIDPropertyName, *StageConfig->ConfigName);
        return false;
    }
    if (bAssignedUID)
    {
        SavePackageFor(StageAsset);   // persist a freshly-minted UID
    }

    StageConfig->Modify();
    StageConfig->PCapStageUID   = StageUID;
    StageConfig->PCapStageAsset = StageAsset;
    SavePackageFor(StageConfig);

    UE_LOG(LogTemp, Display, TEXT("[PCAP] Stage config '%s' paired to Mocap Manager stage '%s' (%s)."),
        *StageConfig->ConfigName, *StageAsset->GetName(), *StageUID.ToString(EGuidFormats::DigitsWithHyphens));
    return true;
#else
    UE_LOG(LogTemp, Warning,
        TEXT("[PCAP] Cannot pair stage config '%s' — this build has no Performance Capture Workflow plugin (WITH_PCAP_WORKFLOW=0)."),
        *StageConfig->ConfigName);
    return false;
#endif
}

bool UPCAPStageBridge::UnpairStageConfig(UStageConfigAsset* StageConfig)
{
    if (!StageConfig)
    {
        return false;
    }

    if (!StageConfig->PCapStageUID.IsValid() && StageConfig->PCapStageAsset.IsNull())
    {
        return true;   // already unpaired — nothing to write
    }

    StageConfig->Modify();
    StageConfig->PCapStageUID = FGuid();
    StageConfig->PCapStageAsset.Reset();
    SavePackageFor(StageConfig);
    return true;
}

UStageConfigAsset* UPCAPStageBridge::FindStageConfigForStage(const FGuid& StageUID)
{
    if (!StageUID.IsValid()) { return nullptr; }

    IAssetRegistry* AssetRegistry = GetAssetRegistry();
    if (!AssetRegistry) { return nullptr; }

    TArray<FAssetData> Found;
    AssetRegistry->GetAssetsByClass(UStageConfigAsset::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ false);

    for (const FAssetData& AssetData : Found)
    {
        if (UStageConfigAsset* StageConfig = Cast<UStageConfigAsset>(AssetData.GetAsset()))
        {
            if (StageConfig->PCapStageUID == StageUID)
            {
                return StageConfig;
            }
        }
    }
    return nullptr;
}

// ── The edit lock ───────────────────────────────────────────────────────────

bool UPCAPStageBridge::IsPairedStageEditable(const UStageConfigAsset* StageConfig, FString& OutReason)
{
    OutReason.Reset();

    if (!StageConfig)
    {
        OutReason = TEXT("no stage config supplied");
        return false;
    }

    UObject* Template = ResolvePairedStage(StageConfig);
    if (!Template)
    {
        OutReason = StageConfig->PCapStageUID.IsValid()
            ? TEXT("the paired Mocap Manager stage was not found in this project")
            : TEXT("this stage config is not paired to a Mocap Manager stage");
        return false;
    }

#if WITH_PCAP_WORKFLOW
    bool bEditable = false;
    if (!ReadIsEditable(Template, bEditable))
    {
        // Fail closed. Not being able to see the lock is not the same as the lock
        // being open, and the write we would be guessing at is one that rewrites
        // an in-flight session's output paths.
        OutReason = FString::Printf(
            TEXT("the session template's '%s' lock flag could not be read — treating it as locked"),
            GIsEditablePropertyName);
        return false;
    }

    if (!bEditable)
    {
        OutReason = TEXT("this session template is locked because a session has already been created from it");
        return false;
    }

    return true;
#else
    OutReason = TEXT("this build has no Performance Capture Workflow plugin");
    return false;
#endif
}

// ── Apply side ──────────────────────────────────────────────────────────────

int32 UPCAPStageBridge::ApplyStageConfigToSession(const UStageConfigAsset* StageConfig)
{
    if (!StageConfig)
    {
        return 0;
    }

#if WITH_PCAP_WORKFLOW
    UObject* Template = ResolvePairedStage(StageConfig);
    if (!Template)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Stage config '%s' has no resolvable Mocap Manager stage — nothing applied."),
            *StageConfig->ConfigName);
        return 0;
    }

    // Hard precondition, not a warning. A locked template is one a session has
    // already been created from; writing it retroactively re-points that
    // session's folder paths and take naming.
    FString LockReason;
    if (!IsPairedStageEditable(StageConfig, LockReason))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Stage config '%s' not applied — %s."),
            *StageConfig->ConfigName, *LockReason);
        return 0;
    }

    // Timecode source is the one thing PCAPTool owns that the session template
    // has a field for. Everything else on the template is production naming and
    // folder templating, which a stage config holds no opinion about — so it is
    // left exactly as the operator authored it rather than invented here.
    const TCHAR* ClockSourceName = ClockSourceNameFor(StageConfig->TimecodeSource);
    if (!ClockSourceName)
    {
        UE_LOG(LogTemp, Display,
            TEXT("[PCAP] Stage config '%s': timecode source '%s' has no Mocap Manager equivalent — '%s' left as authored."),
            *StageConfig->ConfigName, *TimecodeSourceDisplay(StageConfig->TimecodeSource), GRecordingClockSourcePropertyName);
        return 0;
    }

    Template->Modify();

    FString Failure;
    if (!WriteClockSource(Template, ClockSourceName, Failure))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Stage config '%s': could not set the recording clock source — %s. Confirm against the 5.8 header."),
            *StageConfig->ConfigName, *Failure);
        return 0;
    }

    // Re-evaluate the template's tokenised strings/folders after the edit. We
    // write none of them, but this is the protocol the asset expects and it keeps
    // the write path correct if a tokenised field is ever added here.
    CallUpdateAllFields(Template);
    SavePackageFor(Template);

    UE_LOG(LogTemp, Display,
        TEXT("[PCAP] Stage config '%s' applied to Mocap Manager stage '%s': %s = %s."),
        *StageConfig->ConfigName, *Template->GetName(), GRecordingClockSourcePropertyName, ClockSourceName);
    return 1;
#else
    UE_LOG(LogTemp, Warning,
        TEXT("[PCAP] Stage config '%s' not applied — this build has no Performance Capture Workflow plugin (WITH_PCAP_WORKFLOW=0)."),
        *StageConfig->ConfigName);
    return 0;
#endif
}

// ── Divergence ──────────────────────────────────────────────────────────────

TArray<FPCAPStageDivergence> UPCAPStageBridge::CompareStageConfigToSession(const UStageConfigAsset* StageConfig)
{
    TArray<FPCAPStageDivergence> Result;

    if (!StageConfig)
    {
        return Result;
    }

    if (!StageConfig->PCapStageUID.IsValid())
    {
        Result.Add(MakeDivergence(TEXT("Paired stage"), DisplayOrUnset(StageConfig->ConfigName),
            TEXT("(not paired to a Mocap Manager stage)"), EPCAPStageDivergenceKind::NotPaired));
        return Result;
    }

#if WITH_PCAP_WORKFLOW
    UObject* Template = ResolvePairedStage(StageConfig);
    if (!Template)
    {
        Result.Add(MakeDivergence(TEXT("Paired stage"), DisplayOrUnset(StageConfig->ConfigName),
            FString::Printf(TEXT("(UID %s not found in this project)"),
                *StageConfig->PCapStageUID.ToString(EGuidFormats::DigitsWithHyphens)),
            EPCAPStageDivergenceKind::NotPaired));
        return Result;
    }

    const FPCAPStageInfo Info = MakeStageInfo(Template);

    // The lock is not a value disagreement, but it is the single most important
    // thing to know before a shoot: a locked template will not take an Apply, so
    // whatever it currently says is what the session will run with.
    if (!Info.bIsEditableRead)
    {
        Result.Add(MakeDivergence(TEXT("Stage template lock"), DisplayOrUnset(StageConfig->ConfigName),
            FString::Printf(TEXT("(lock flag '%s' unreadable — treated as locked)"), GIsEditablePropertyName),
            EPCAPStageDivergenceKind::Locked));
    }
    else if (!Info.bIsEditable)
    {
        Result.Add(MakeDivergence(TEXT("Stage template lock"), DisplayOrUnset(StageConfig->ConfigName),
            TEXT("locked — a session has already been created from this template; Apply will refuse"),
            EPCAPStageDivergenceKind::Locked));
    }

    // Timecode source vs the template's recording clock source — the one field
    // the two models genuinely share. Everything else on either side is
    // PCAPTool-only or Mocap-Manager-only by design; the report stays high-signal
    // by not restating that boundary on every call.
    const FString PCAPToolTimecode = TimecodeSourceDisplay(StageConfig->TimecodeSource);
    const TCHAR* ExpectedClockSource = ClockSourceNameFor(StageConfig->TimecodeSource);

    if (!Info.bRecordingClockSourceRead)
    {
        Result.Add(MakeDivergence(TEXT("Timecode source"), PCAPToolTimecode,
            FString::Printf(TEXT("('%s' did not resolve)"), GRecordingClockSourcePropertyName),
            EPCAPStageDivergenceKind::Unreadable));
    }
    else if (!ExpectedClockSource)
    {
        Result.Add(MakeDivergence(TEXT("Timecode source"), PCAPToolTimecode,
            FString::Printf(TEXT("(no equivalent — clock source left as authored: %s)"),
                *DisplayOrUnset(Info.RecordingClockSource)),
            EPCAPStageDivergenceKind::Unmapped));
    }
    else if (!Info.RecordingClockSource.Equals(ExpectedClockSource, ESearchCase::IgnoreCase))
    {
        Result.Add(MakeDivergence(TEXT("Timecode source"), PCAPToolTimecode,
            DisplayOrUnset(Info.RecordingClockSource), EPCAPStageDivergenceKind::Mismatch));
    }
#else
    Result.Add(MakeDivergence(TEXT("Mocap Manager stage"), DisplayOrUnset(StageConfig->ConfigName),
        TEXT("(built without the Performance Capture Workflow plugin — cannot be read)"),
        EPCAPStageDivergenceKind::Unreadable));
#endif

    return Result;
}

TArray<FString> UPCAPStageBridge::DescribeStageDivergence(const UStageConfigAsset* StageConfig)
{
    TArray<FString> Lines;

    const TArray<FPCAPStageDivergence> Divergences = CompareStageConfigToSession(StageConfig);
    Lines.Reserve(Divergences.Num());
    for (const FPCAPStageDivergence& Divergence : Divergences)
    {
        Lines.Add(FString::Printf(TEXT("%s — PCAPTool: %s · Mocap Manager: %s"),
            *Divergence.Field, *Divergence.PCAPToolValue, *Divergence.MocapManagerValue));
    }

    return Lines;
}
