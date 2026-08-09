#include "PCAPTakeRecordWriter.h"

#include "Engine/DataTable.h"
#include "LevelSequence.h"
#include "UObject/UnrealType.h"      // FStructProperty / FStrProperty / FSoftObjectProperty / FEnumProperty
#include "UObject/StructOnScope.h"   // default-constructs a reflected row struct
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// ── Reflected names — Epic's PCap record model ───────────────────────────────
// The record structs live in the Performance Capture Workflow plugin's *private*
// module, so PCAPTool never includes a PCap header: each struct is resolved by
// /Script path and each field by FProperty name.
//
// The struct paths, field names and enumerator ordering below were read off the
// plugin's own PCapDatabase.h for UE 5.8, so they are VERIFIED — they are still
// resolved defensively (log once, then no-op) because a plugin update can move any of
// them and this module has no compile-time link to catch it.
//
// The remaining UNVERIFIED-PENDING-WINDOWS items are called out individually below and
// listed in docs/specs/2026-08-09-pcap-take-record-reconciliation-design.md § Verify.

// Row structs — VERIFIED (PCapDatabase.h).
static const TCHAR* GTakeRecordStructPath    = TEXT("/Script/PerformanceCaptureWorkflow.PCapTakeRecord");
static const TCHAR* GSlateRecordStructPath   = TEXT("/Script/PerformanceCaptureWorkflow.PCapSlateRecord");
static const TCHAR* GSessionRecordStructPath = TEXT("/Script/PerformanceCaptureWorkflow.PCapSessionRecord");

// Take status enum — VERIFIED: EPCapTakeStatus : uint8 { ThumbsUp = 0, ThumbsDown = 1, Neutral = 2 }.
static const TCHAR* GTakeStatusEnumPath = TEXT("/Script/PerformanceCaptureWorkflow.EPCapTakeStatus");

// FPCapRecordBase (base of every record) — VERIFIED.
static const TCHAR* GProp_UID        = TEXT("UID");
static const TCHAR* GProp_IsArchived = TEXT("bIsArchived");

// FPCapTakeRecord — VERIFIED.
static const TCHAR* GProp_DateTimeCreated        = TEXT("DateTimeCreated");
static const TCHAR* GProp_RecordedTake           = TEXT("RecordedTake");
static const TCHAR* GProp_TakeDurationSeconds    = TEXT("TakeDurationSeconds");
static const TCHAR* GProp_TakeStatus             = TEXT("TakeStatus");
static const TCHAR* GProp_ContainsLiveLinkSources= TEXT("bContainsLiveLinkSources");

// FPCapSlateRecord — VERIFIED.
static const TCHAR* GProp_Slate     = TEXT("Slate");
static const TCHAR* GProp_SlateNote = TEXT("SlateNote");

// FPCapSessionRecord — VERIFIED. (SessionUID is the same field name on the take and
// slate records, so one constant serves all three.)
static const TCHAR* GProp_SessionUID        = TEXT("SessionUID");
static const TCHAR* GProp_SessionName       = TEXT("SessionName");
static const TCHAR* GProp_TakesDataTable    = TEXT("TakesDataTable");
static const TCHAR* GProp_SessionSlateTable = TEXT("SessionSlateTable");

// EPCapTakeStatus enumerator names — VERIFIED. Looked up by name so a re-ordering of the
// enum cannot silently re-label takes; the literals below are the 5.8 ordering and are
// used only if the enum object itself does not resolve.
static const TCHAR*    GStatus_ThumbsUp           = TEXT("ThumbsUp");
static const TCHAR*    GStatus_ThumbsDown         = TEXT("ThumbsDown");
static const TCHAR*    GStatus_Neutral            = TEXT("Neutral");
static constexpr uint8 GStatusFallback_ThumbsUp   = 0;
static constexpr uint8 GStatusFallback_ThumbsDown = 1;
static constexpr uint8 GStatusFallback_Neutral    = 2;

// Asset Registry tag a UDataTable exports for its row struct. VERIFIED indirectly: the
// plugin's own FPCapSessionRecord::SessionSlateTable filters on
// meta=(RequiredAssetDataTags="RowStructure=/Script/PerformanceCaptureWorkflow.PCapSlateRecord").
// UNVERIFIED-PENDING-WINDOWS: whether 5.8 writes the tag as the struct's full path or its
// bare name — both are accepted below, and a table with no tag at all is loaded and
// compared against its RowStruct, so the scan is correct either way.
static const TCHAR* GDataTableRowStructTag = TEXT("RowStructure");

// Operator override for session resolution (see SetPinnedSessionUID). Editor-session
// scoped on purpose: it is a shoot-day correction, not a saved setting.
static FGuid GPinnedSessionUID;

namespace
{
    // ── Logging ──────────────────────────────────────────────────────────────
    // A missing struct/field is a permanent condition for the editor session, and a
    // shoot writes a record per take — warn once per key, then stay quiet.

    void WarnOnce(const FString& Key, const FString& Message)
    {
        static TSet<FString> Warned;
        if (Warned.Contains(Key)) { return; }
        Warned.Add(Key);
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] %s"), *Message);
    }

    void WarnMissingField(const UScriptStruct* Struct, const TCHAR* PropName)
    {
        const FString StructName = Struct ? Struct->GetName() : TEXT("<null>");
        WarnOnce(StructName + TEXT(".") + PropName,
            FString::Printf(TEXT("%s has no '%s' property — field skipped. Confirm against the Workflow plugin's PCapDatabase.h."),
                *StructName, PropName));
    }

    // ── Reflection: resolve a PCap record struct ─────────────────────────────

    const UScriptStruct* FindRecordStruct(const TCHAR* StructPath)
    {
        const UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, StructPath);
        if (!Struct)
        {
            WarnOnce(StructPath,
                FString::Printf(TEXT("PCap record struct '%s' did not resolve — is the Performance Capture Workflow plugin enabled? Take write-back is disabled."),
                    StructPath));
        }
        return Struct;
    }

    IAssetRegistry* GetAssetRegistry()
    {
        FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        return &ARM.Get();
    }

    // Every DataTable in the project whose row struct is RowStruct. Used ONLY to find the
    // project's session table(s) — take and slate tables are never found this way, they are
    // read off the owning session record (see ResolveSessionBinding).
    void GatherTablesWithRowStruct(const UScriptStruct* RowStruct, TArray<UDataTable*>& OutTables)
    {
        if (!RowStruct) { return; }

        IAssetRegistry* AR = GetAssetRegistry();
        if (!AR) { return; }

        TArray<FAssetData> Found;
        // bSearchSubClasses picks up UPCapDataTable, which is a plain UDataTable subclass.
        AR->GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ true);

        const FString WantedPath = RowStruct->GetPathName();   // "/Script/…​.PCapSessionRecord"
        const FString WantedName = RowStruct->GetName();       // "PCapSessionRecord"

        for (const FAssetData& AssetData : Found)
        {
            // Cheap pass first: skip tables the registry already tells us are a different
            // row struct, so we never load unrelated project DataTables.
            FString TagValue;
            if (AssetData.GetTagValue(FName(GDataTableRowStructTag), TagValue))
            {
                if (TagValue != WantedPath && TagValue != WantedName) { continue; }
            }

            if (UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset()))
            {
                if (Table->GetRowStruct() == RowStruct)
                {
                    OutTables.Add(Table);
                }
            }
        }
    }

    // ── Reflected field readers (row memory + its row struct) ────────────────

    FName ReadNameField(const void* Row, const UScriptStruct* Struct, const TCHAR* PropName)
    {
        if (const FNameProperty* P = FindFProperty<FNameProperty>(Struct, PropName))
        {
            return P->GetPropertyValue_InContainer(Row);
        }
        if (const FStrProperty* P = FindFProperty<FStrProperty>(Struct, PropName))
        {
            return FName(*P->GetPropertyValue_InContainer(Row));
        }
        WarnMissingField(Struct, PropName);
        return NAME_None;
    }

    FString ReadStringField(const void* Row, const UScriptStruct* Struct, const TCHAR* PropName)
    {
        if (const FStrProperty* P = FindFProperty<FStrProperty>(Struct, PropName))
        {
            return P->GetPropertyValue_InContainer(Row);
        }
        WarnMissingField(Struct, PropName);
        return FString();
    }

    FGuid ReadGuidField(const void* Row, const UScriptStruct* Struct, const TCHAR* PropName)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(Struct, PropName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                return *P->ContainerPtrToValuePtr<FGuid>(Row);
            }
        }
        WarnMissingField(Struct, PropName);
        return FGuid();
    }

    bool ReadBoolField(const void* Row, const UScriptStruct* Struct, const TCHAR* PropName)
    {
        if (const FBoolProperty* P = FindFProperty<FBoolProperty>(Struct, PropName))
        {
            return P->GetPropertyValue_InContainer(Row);
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    // A TSoftObjectPtr<UPCapDataTable> field, resolved to the plain engine UDataTable it is.
    // UPCapDataTable lives in the editor Workflow module (not linked here), so the load goes
    // through UObject and casts to the base class — never to the concrete type.
    UDataTable* ReadTableField(const void* Row, const UScriptStruct* Struct, const TCHAR* PropName)
    {
        if (const FSoftObjectProperty* P = FindFProperty<FSoftObjectProperty>(Struct, PropName))
        {
            const FSoftObjectPath Path = P->GetPropertyValue_InContainer(Row).ToSoftObjectPath();
            return Path.IsNull() ? nullptr : Cast<UDataTable>(Path.TryLoad());
        }
        if (const FObjectProperty* P = FindFProperty<FObjectProperty>(Struct, PropName))
        {
            return Cast<UDataTable>(P->GetObjectPropertyValue_InContainer(Row));
        }
        WarnMissingField(Struct, PropName);
        return nullptr;
    }

    // ── Reflected field writers ──────────────────────────────────────────────

    bool WriteGuidField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, const FGuid& Value)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(Struct, PropName))
        {
            if (P->Struct == TBaseStructure<FGuid>::Get())
            {
                *P->ContainerPtrToValuePtr<FGuid>(Row) = Value;
                return true;
            }
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    bool WriteDateTimeField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, const FDateTime& Value)
    {
        if (const FStructProperty* P = FindFProperty<FStructProperty>(Struct, PropName))
        {
            if (P->Struct == TBaseStructure<FDateTime>::Get())
            {
                *P->ContainerPtrToValuePtr<FDateTime>(Row) = Value;
                return true;
            }
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    bool WriteFloatField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, float Value)
    {
        if (const FFloatProperty* P = FindFProperty<FFloatProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, Value);
            return true;
        }
        if (const FDoubleProperty* P = FindFProperty<FDoubleProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, (double)Value);
            return true;
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    bool WriteStringField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, const FString& Value)
    {
        if (const FStrProperty* P = FindFProperty<FStrProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, Value);
            return true;
        }
        if (const FNameProperty* P = FindFProperty<FNameProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, FName(*Value));
            return true;
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    bool WriteBoolField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, bool Value)
    {
        if (const FBoolProperty* P = FindFProperty<FBoolProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, Value);
            return true;
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    // Handles both shapes of an asset reference so a soft→hard change in the plugin does
    // not silently drop the reference.
    bool WriteObjectRefField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, const FSoftObjectPath& Value)
    {
        if (const FSoftObjectProperty* P = FindFProperty<FSoftObjectProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, FSoftObjectPtr(Value));
            return true;
        }
        if (const FObjectProperty* P = FindFProperty<FObjectProperty>(Struct, PropName))
        {
            P->SetObjectPropertyValue_InContainer(Row, Value.IsNull() ? nullptr : Value.TryLoad());
            return true;
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    // EPCapTakeStatus is `enum class : uint8`, so UHT emits an FEnumProperty; FByteProperty
    // is accepted too in case the field is ever re-declared as TEnumAsByte.
    bool WriteEnumByteField(void* Row, const UScriptStruct* Struct, const TCHAR* PropName, uint8 Value)
    {
        if (const FEnumProperty* P = FindFProperty<FEnumProperty>(Struct, PropName))
        {
            void* ValuePtr = P->ContainerPtrToValuePtr<void>(Row);
            P->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)Value);
            return true;
        }
        if (const FByteProperty* P = FindFProperty<FByteProperty>(Struct, PropName))
        {
            P->SetPropertyValue_InContainer(Row, Value);
            return true;
        }
        WarnMissingField(Struct, PropName);
        return false;
    }

    // ── Rows ─────────────────────────────────────────────────────────────────

    // Find-or-add RowName in Table and return its row memory. Refuses outright if the
    // table's row struct is not the one we intend to write — a mismatched write would
    // reinterpret one record as another and corrupt the operator's session data.
    uint8* EnsureRow(UDataTable* Table, const UScriptStruct* RowStruct, FName RowName, bool& bOutCreated)
    {
        bOutCreated = false;
        if (!Table || !RowStruct || RowName.IsNone()) { return nullptr; }

        if (Table->GetRowStruct() != RowStruct)
        {
            const UScriptStruct* Actual = Table->GetRowStruct();
            UE_LOG(LogTemp, Warning,
                TEXT("[PCAP] '%s' holds %s rows, not %s — refusing to write (a mismatched row write corrupts the table)."),
                *Table->GetPathName(),
                Actual ? *Actual->GetName() : TEXT("<none>"),
                *RowStruct->GetName());
            return nullptr;
        }

        if (uint8* Existing = Table->FindRowUnchecked(RowName))
        {
            return Existing;
        }

#if WITH_EDITOR
        // A default-constructed instance of the (private) row struct, built by reflection.
        // UDataTable::AddRow copies it through the table's own RowStruct, so the concrete
        // C++ type never has to appear here.
        FStructOnScope Scratch(RowStruct);
        Table->Modify();
        Table->AddRow(RowName, *reinterpret_cast<const FTableRowBase*>(Scratch.GetStructMemory()));
        bOutCreated = true;
        return Table->FindRowUnchecked(RowName);
#else
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] DataTable rows can only be added in the editor — record '%s' not written."), *RowName.ToString());
        return nullptr;
#endif
    }

    // ── Notes ────────────────────────────────────────────────────────────────

    // One line of SlateNote for a take: "<notes> | Director: … | Commentator: …".
    FString ComposeTakeNote(const FTake& Take)
    {
        TArray<FString> Parts;
        if (!Take.Notes.IsEmpty())            { Parts.Add(Take.Notes); }
        if (!Take.DirectorNotes.IsEmpty())    { Parts.Add(TEXT("Director: ") + Take.DirectorNotes); }
        if (!Take.CommentatorNotes.IsEmpty()) { Parts.Add(TEXT("Commentator: ") + Take.CommentatorNotes); }
        return FString::Join(Parts, TEXT(" | "));
    }

    // A slate row is shared by every take on that shot, so the note is a set of lines keyed
    // by TakeID. Rewriting a take replaces only its own line; clearing a take's notes drops
    // the line. Lines sort by TakeID (zero-padded), so the block reads in take order.
    FString MergeNoteLine(const FString& ExistingNote, const FString& TakeID, const FString& NoteBody)
    {
        const FString Prefix = FString::Printf(TEXT("[%s]"), *TakeID);

        TArray<FString> Lines;
        ExistingNote.ParseIntoArrayLines(Lines, /*InCullEmpty*/ true);
        Lines.RemoveAll([&Prefix](const FString& Line)
        {
            const FString Trimmed = Line.TrimStartAndEnd();
            return Trimmed.IsEmpty() || Trimmed.StartsWith(Prefix);
        });

        if (!NoteBody.IsEmpty())
        {
            Lines.Add(FString::Printf(TEXT("%s %s"), *Prefix, *NoteBody));
        }
        Lines.Sort();
        return FString::Join(Lines, LINE_TERMINATOR);
    }

    // True when the take's manifest says the shot streamed body or face — the sequence
    // therefore carries Live Link tracks. Only ever raises the flag (Epic's plotting
    // workflow owns clearing it).
    bool TakeHasLiveLinkSources(const FTake& Take)
    {
        for (const FTakeSubjectSnapshot& Subject : Take.SubjectManifest)
        {
            if (Subject.bHadBodyStream || Subject.bHadFaceStream) { return true; }
        }
        return false;
    }
}

// ── Availability ────────────────────────────────────────────────────────────

bool UPCAPTakeRecordWriter::IsAvailable()
{
#if WITH_PCAP_WORKFLOW
    return FindRecordStruct(GTakeRecordStructPath) != nullptr;
#else
    return false;
#endif
}

// ── Session resolution ──────────────────────────────────────────────────────
// There is no project-wide takes table: FPCapSessionRecord owns TakesDataTable and
// SessionSlateTable, so a take must be routed through its session or not written at all.
// Picking the wrong session would silently file takes under someone else's shoot day,
// which is why an ambiguous match fails loudly instead of choosing.

bool UPCAPTakeRecordWriter::ResolveSessionBinding(const FString& SessionID, FPCAPSessionBinding& OutBinding)
{
    OutBinding = FPCAPSessionBinding();

#if WITH_PCAP_WORKFLOW
    const UScriptStruct* SessionStruct = FindRecordStruct(GSessionRecordStructPath);
    if (!SessionStruct) { return false; }

    TArray<UDataTable*> SessionTables;
    GatherTablesWithRowStruct(SessionStruct, SessionTables);
    if (SessionTables.Num() == 0)
    {
        WarnOnce(TEXT("NoSessionTable"),
            TEXT("No FPCapSessionRecord DataTable found in the project — create a Mocap Manager session before recording, or takes cannot be published."));
        return false;
    }

    // Gather every live (non-archived) session row across the project's session tables.
    TArray<FPCAPSessionBinding> Candidates;
    for (UDataTable* Table : SessionTables)
    {
        for (const TPair<FName, uint8*>& Row : Table->GetRowMap())
        {
            if (!Row.Value) { continue; }
            if (ReadBoolField(Row.Value, SessionStruct, GProp_IsArchived)) { continue; }

            FPCAPSessionBinding Candidate;
            Candidate.SessionUID  = ReadGuidField(Row.Value, SessionStruct, GProp_UID);
            Candidate.SessionName = ReadNameField(Row.Value, SessionStruct, GProp_SessionName);
            Candidate.TakesTable  = ReadTableField(Row.Value, SessionStruct, GProp_TakesDataTable);
            Candidate.SlateTable  = ReadTableField(Row.Value, SessionStruct, GProp_SessionSlateTable);
            if (Candidate.SessionUID.IsValid())
            {
                Candidates.Add(MoveTemp(Candidate));
            }
        }
    }

    if (Candidates.Num() == 0)
    {
        WarnOnce(TEXT("NoSessionRows"),
            TEXT("Every FPCapSessionRecord row is archived or has no UID — nothing to publish takes into."));
        return false;
    }

    // 1) An operator-pinned session always wins.
    if (GPinnedSessionUID.IsValid())
    {
        for (const FPCAPSessionBinding& Candidate : Candidates)
        {
            if (Candidate.SessionUID == GPinnedSessionUID)
            {
                OutBinding = Candidate;
                return true;
            }
        }
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Pinned session %s is not among the project's live sessions — falling back to name matching."),
            *GPinnedSessionUID.ToString(EGuidFormats::DigitsWithHyphens));
    }

    // 2) Name match: Epic's SessionName against PCAPTool's SessionID, and against the
    //    "Session_<id>" form PCAPTool uses for its own folders.
    if (!SessionID.IsEmpty())
    {
        const FString Prefixed = TEXT("Session_") + SessionID;
        for (const FPCAPSessionBinding& Candidate : Candidates)
        {
            const FString Name = Candidate.SessionName.ToString();
            if (Name.Equals(SessionID, ESearchCase::IgnoreCase) || Name.Equals(Prefixed, ESearchCase::IgnoreCase))
            {
                OutBinding = Candidate;
                return true;
            }
        }
    }

    // 3) A single live session is unambiguous — use it.
    if (Candidates.Num() == 1)
    {
        OutBinding = Candidates[0];
        return true;
    }

    // 4) Several candidates and no match: refuse rather than file the take into a guess.
    FString Names;
    for (const FPCAPSessionBinding& Candidate : Candidates)
    {
        Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Candidate.SessionName.ToString();
    }
    UE_LOG(LogTemp, Warning,
        TEXT("[PCAP] Cannot tell which Mocap Manager session '%s' belongs to (candidates: %s). Pin one with UPCAPTakeRecordWriter::SetPinnedSessionUID, or rename the session to match."),
        *SessionID, *Names);
    return false;
#else
    WarnOnce(TEXT("WorkflowDisabled"),
        TEXT("WITH_PCAP_WORKFLOW=0 — take records are not published to the Mocap Manager."));
    return false;
#endif
}

void UPCAPTakeRecordWriter::SetPinnedSessionUID(const FGuid& SessionUID)
{
    GPinnedSessionUID = SessionUID;
    UE_LOG(LogTemp, Log, TEXT("[PCAP] Take records pinned to session %s."),
        SessionUID.IsValid() ? *SessionUID.ToString(EGuidFormats::DigitsWithHyphens) : TEXT("(auto)"));
}

FGuid UPCAPTakeRecordWriter::GetPinnedSessionUID()
{
    return GPinnedSessionUID;
}

// ── Label → Epic take status ────────────────────────────────────────────────

FString UPCAPTakeRecordWriter::TakeStatusNameForLabel(ETakeLabel Label)
{
    switch (Label)
    {
    case ETakeLabel::Best:  return GStatus_ThumbsUp;
    case ETakeLabel::Burn:  return GStatus_ThumbsDown;
    case ETakeLabel::Captured:
    case ETakeLabel::Alt:
    default:                return GStatus_Neutral;   // anything unmapped lands Neutral
    }
}

uint8 UPCAPTakeRecordWriter::TakeStatusValueForLabel(ETakeLabel Label)
{
    const FString EnumeratorName = TakeStatusNameForLabel(Label);

    if (const UEnum* StatusEnum = FindObject<UEnum>(nullptr, GTakeStatusEnumPath))
    {
        const int64 Value = StatusEnum->GetValueByNameString(EnumeratorName);
        if (Value != INDEX_NONE)
        {
            return (uint8)Value;
        }

        // The enumerator was renamed — take Neutral by name before falling back to literals.
        const int64 NeutralValue = StatusEnum->GetValueByNameString(GStatus_Neutral);
        if (NeutralValue != INDEX_NONE)
        {
            WarnOnce(TEXT("TakeStatus:") + EnumeratorName,
                FString::Printf(TEXT("EPCapTakeStatus has no '%s' enumerator — take status falls back to Neutral."), *EnumeratorName));
            return (uint8)NeutralValue;
        }
    }

    // Enum absent (plugin not loaded) or unreadable: the verified 5.8 ordering. Note that
    // Neutral is 2 — a zero default here would mark every take ThumbsUp.
    if (EnumeratorName == GStatus_ThumbsUp)   { return GStatusFallback_ThumbsUp; }
    if (EnumeratorName == GStatus_ThumbsDown) { return GStatusFallback_ThumbsDown; }
    return GStatusFallback_Neutral;
}

// ── Row keys ────────────────────────────────────────────────────────────────

FName UPCAPTakeRecordWriter::MakeTakeRowName(const FTake& Take)
{
    // The canonical take id ("001003_004") — stable across re-writes, unique in a session.
    return Take.TakeID.IsEmpty() ? NAME_None : FName(*Take.TakeID);
}

FName UPCAPTakeRecordWriter::MakeSlateRowName(const FTake& Take)
{
    // The slate is the shot: PCAPTool records with UTakeMetaData::SetSlate(ShotID), so the
    // slate record keys on the same value.
    return Take.ShotID.IsEmpty() ? NAME_None : FName(*Take.ShotID);
}

// ── Writes ──────────────────────────────────────────────────────────────────

bool UPCAPTakeRecordWriter::WriteTakeRecord(const FTake& Take)
{
#if WITH_PCAP_WORKFLOW
    const FName RowName = MakeTakeRowName(Take);
    if (RowName.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Take has no TakeID — no take record written."));
        return false;
    }

    const UScriptStruct* TakeStruct = FindRecordStruct(GTakeRecordStructPath);
    if (!TakeStruct) { return false; }

    FPCAPSessionBinding Binding;
    if (!ResolveSessionBinding(Take.SessionID, Binding)) { return false; }
    if (!Binding.TakesTable)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Session '%s' has no TakesDataTable assigned — take '%s' not published."),
            *Binding.SessionName.ToString(), *Take.TakeID);
        return false;
    }

    bool bCreated = false;
    uint8* Row = EnsureRow(Binding.TakesTable, TakeStruct, RowName, bCreated);
    if (!Row) { return false; }

    Binding.TakesTable->Modify();

    // A new row gets its own record UID; an existing one keeps the UID the Mocap Manager
    // already knows it by. bIsArchived is left alone for the same reason.
    if (bCreated)
    {
        WriteGuidField(Row, TakeStruct, GProp_UID, FGuid::NewGuid());
    }

    WriteDateTimeField(Row, TakeStruct, GProp_DateTimeCreated, Take.RecordedAt);
    WriteObjectRefField(Row, TakeStruct, GProp_RecordedTake, Take.MasterSequence.ToSoftObjectPath());
    WriteFloatField(Row, TakeStruct, GProp_TakeDurationSeconds, Take.DurationSeconds);
    WriteEnumByteField(Row, TakeStruct, GProp_TakeStatus, TakeStatusValueForLabel(Take.Label));
    WriteGuidField(Row, TakeStruct, GProp_SessionUID, Binding.SessionUID);
    if (TakeHasLiveLinkSources(Take))
    {
        WriteBoolField(Row, TakeStruct, GProp_ContainsLiveLinkSources, true);
    }

    // Not mapped, deliberately (see the design spec): Framerate / StartTimecode /
    // EndTimecode / TakeDurationTimecode (FTake carries no timecode), Rating (no PCAPTool
    // equivalent — the operator rates in the Review tab), and MocapStageRootTransform,
    // which stays at identity because PCAPTool has no stage-root actor to read.

    Binding.TakesTable->HandleDataTableChanged(RowName);

    UE_LOG(LogTemp, Log, TEXT("[PCAP] %s take record '%s' in %s (session '%s')."),
        bCreated ? TEXT("Created") : TEXT("Updated"),
        *RowName.ToString(), *Binding.TakesTable->GetName(), *Binding.SessionName.ToString());
    return true;
#else
    WarnOnce(TEXT("WorkflowDisabled"),
        TEXT("WITH_PCAP_WORKFLOW=0 — take records are not published to the Mocap Manager."));
    return false;
#endif
}

bool UPCAPTakeRecordWriter::WriteSlateNote(const FTake& Take)
{
#if WITH_PCAP_WORKFLOW
    const FName RowName = MakeSlateRowName(Take);
    if (RowName.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAP] Take '%s' has no ShotID — no slate record written."), *Take.TakeID);
        return false;
    }

    const UScriptStruct* SlateStruct = FindRecordStruct(GSlateRecordStructPath);
    if (!SlateStruct) { return false; }

    FPCAPSessionBinding Binding;
    if (!ResolveSessionBinding(Take.SessionID, Binding)) { return false; }
    if (!Binding.SlateTable)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PCAP] Session '%s' has no SessionSlateTable assigned — slate note for '%s' not published."),
            *Binding.SessionName.ToString(), *Take.ShotID);
        return false;
    }

    bool bCreated = false;
    uint8* Row = EnsureRow(Binding.SlateTable, SlateStruct, RowName, bCreated);
    if (!Row) { return false; }

    Binding.SlateTable->Modify();

    if (bCreated)
    {
        WriteGuidField(Row, SlateStruct, GProp_UID, FGuid::NewGuid());
        WriteStringField(Row, SlateStruct, GProp_Slate, Take.ShotID);
    }

    // SlateStatus is left to the operator — the Mocap Manager owns marking a slate
    // Complete/Skip, and a take landing on it says nothing about whether the shot is done.
    const FString Merged = MergeNoteLine(
        ReadStringField(Row, SlateStruct, GProp_SlateNote), Take.TakeID, ComposeTakeNote(Take));

    WriteStringField(Row, SlateStruct, GProp_SlateNote, Merged);
    WriteGuidField(Row, SlateStruct, GProp_SessionUID, Binding.SessionUID);

    Binding.SlateTable->HandleDataTableChanged(RowName);

    UE_LOG(LogTemp, Verbose, TEXT("[PCAP] %s slate record '%s' in %s."),
        bCreated ? TEXT("Created") : TEXT("Updated"), *RowName.ToString(), *Binding.SlateTable->GetName());
    return true;
#else
    return false;
#endif
}

bool UPCAPTakeRecordWriter::WriteTake(const FTake& Take)
{
    const bool bTakeWritten = WriteTakeRecord(Take);
    WriteSlateNote(Take);   // advisory — a missing slate table never fails the take record
    return bTakeWritten;
}
