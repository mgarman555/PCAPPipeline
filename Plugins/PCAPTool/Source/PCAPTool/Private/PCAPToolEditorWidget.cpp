#include "PCAPToolEditorWidget.h"
#include "PCAPToolSettings.h"
#include "PCAPDatabase.h"
#include "FileHelpers.h"

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void UPCAPToolEditorWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // Fire both events so Blueprint can populate its lists on first open.
    OnDatabaseChanged();
    OnSelectionChanged();
}

// ─── Database ─────────────────────────────────────────────────────────────────

UPCAPDatabase* UPCAPToolEditorWidget::GetDatabase() const
{
    return UPCAPToolSettings::Get()->GetDatabase();
}

void UPCAPToolEditorWidget::SaveDatabase()
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;

    UPackage* Package = DB->GetOutermost();
    if (!Package) return;

    Package->MarkPackageDirty();

    TArray<UPackage*> Packages = { Package };
    FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
}

void UPCAPToolEditorWidget::MarkDirtyAndNotify()
{
    UPCAPDatabase* DB = GetDatabase();
    if (DB)
    {
        DB->MarkPackageDirty();
    }
    OnDatabaseChanged();
}

// ─── Productions ──────────────────────────────────────────────────────────────

TArray<FProduction> UPCAPToolEditorWidget::GetAllProductions() const
{
    UPCAPDatabase* DB = GetDatabase();
    return DB ? DB->Productions : TArray<FProduction>();
}

TArray<FString> UPCAPToolEditorWidget::GetProductionNames() const
{
    TArray<FString> Names;
    for (const FProduction& Prod : GetAllProductions())
    {
        Names.Add(Prod.ProductionName);
    }
    return Names;
}

bool UPCAPToolEditorWidget::GetProduction(const FString& ProjectCode, FProduction& OutProduction) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return false;
    for (const FProduction& Prod : DB->Productions)
    {
        if (Prod.ProjectCode == ProjectCode)
        {
            OutProduction = Prod;
            return true;
        }
    }
    return false;
}

void UPCAPToolEditorWidget::AddProduction(const FProduction& NewProduction)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    DB->Productions.Add(NewProduction);
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::SetActiveProduction(const FString& ProjectCode)
{
    ActiveProjectCode = ProjectCode;
    ActiveDayID.Empty();
    ActiveSessionID.Empty();
    ActiveShotID.Empty();
    OnSelectionChanged();
}

// ─── Shoot Days ───────────────────────────────────────────────────────────────

TArray<FShootDay> UPCAPToolEditorWidget::GetShootDays(const FString& ProjectCode) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return {};
    FProduction* Prod = DB->GetProductionByCode(ProjectCode);
    return Prod ? Prod->Days : TArray<FShootDay>();
}

bool UPCAPToolEditorWidget::GetShootDay(const FString& ProjectCode, const FString& DayID, FShootDay& OutDay) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return false;
    FShootDay* Day = DB->GetDay(ProjectCode, DayID);
    if (!Day) return false;
    OutDay = *Day;
    return true;
}

void UPCAPToolEditorWidget::AddShootDay(const FString& ProjectCode, const FShootDay& NewDay)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FProduction* Prod = DB->GetProductionByCode(ProjectCode);
    if (!Prod) return;
    Prod->Days.Add(NewDay);
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::SetActiveDay(const FString& DayID)
{
    ActiveDayID = DayID;
    ActiveSessionID.Empty();
    ActiveShotID.Empty();
    OnSelectionChanged();
}

// ─── Sessions ─────────────────────────────────────────────────────────────────

TArray<FSession> UPCAPToolEditorWidget::GetSessions(const FString& ProjectCode, const FString& DayID) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return {};
    FShootDay* Day = DB->GetDay(ProjectCode, DayID);
    return Day ? Day->Sessions : TArray<FSession>();
}

bool UPCAPToolEditorWidget::GetSession(const FString& ProjectCode, const FString& DayID,
                                        const FString& SessionID, FSession& OutSession) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return false;
    FSession* Session = DB->GetSession(ProjectCode, DayID, SessionID);
    if (!Session) return false;
    OutSession = *Session;
    return true;
}

void UPCAPToolEditorWidget::AddSession(const FString& ProjectCode, const FString& DayID, const FSession& NewSession)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShootDay* Day = DB->GetDay(ProjectCode, DayID);
    if (!Day) return;
    Day->Sessions.Add(NewSession);
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::StartSession(const FString& ProjectCode, const FString& DayID, const FString& SessionID)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FSession* Session = DB->GetSession(ProjectCode, DayID, SessionID);
    if (!Session) return;
    Session->StartedAt = FDateTime::UtcNow();
    Session->bHasStarted = true;
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::SetActiveSession(const FString& SessionID)
{
    ActiveSessionID = SessionID;
    ActiveShotID.Empty();
    OnSelectionChanged();
}

// ─── Shots ────────────────────────────────────────────────────────────────────

TArray<FShot> UPCAPToolEditorWidget::GetShots(const FString& ProjectCode, const FString& DayID,
                                               const FString& SessionID) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return {};
    FSession* Session = DB->GetSession(ProjectCode, DayID, SessionID);
    return Session ? Session->Shots : TArray<FShot>();
}

bool UPCAPToolEditorWidget::GetShot(const FString& ProjectCode, const FString& DayID,
                                     const FString& SessionID, const FString& ShotID, FShot& OutShot) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return false;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return false;
    OutShot = *Shot;
    return true;
}

void UPCAPToolEditorWidget::AddShot(const FString& ProjectCode, const FString& DayID,
                                     const FString& SessionID, const FShot& NewShot)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FSession* Session = DB->GetSession(ProjectCode, DayID, SessionID);
    if (!Session) return;
    Session->Shots.Add(NewShot);
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::RemoveShot(const FString& ProjectCode, const FString& DayID,
                                        const FString& SessionID, const FString& ShotID)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FSession* Session = DB->GetSession(ProjectCode, DayID, SessionID);
    if (!Session) return;
    Session->Shots.RemoveAll([&ShotID](const FShot& S) { return S.ShotID == ShotID; });
    if (ActiveShotID == ShotID) ActiveShotID.Empty();
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::UpdateShotDescription(const FString& ProjectCode, const FString& DayID,
                                                    const FString& SessionID, const FString& ShotID,
                                                    const FString& Description)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    Shot->Description = Description;
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::UpdateShotNotes(const FString& ProjectCode, const FString& DayID,
                                             const FString& SessionID, const FString& ShotID,
                                             const FString& Notes)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    Shot->Notes = Notes;
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::SetActiveShot(const FString& ShotID)
{
    ActiveShotID = ShotID;
    OnSelectionChanged();
}

// ─── Shot Subjects ────────────────────────────────────────────────────────────

void UPCAPToolEditorWidget::SetSubjectActive(const FString& ProjectCode, const FString& DayID,
                                              const FString& SessionID, const FString& ShotID,
                                              const FString& ActorName, bool bActive)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    for (FShotSubject& Subject : Shot->Subjects)
    {
        if (Subject.ActorName == ActorName)
        {
            Subject.IsActive = bActive;
            MarkDirtyAndNotify();
            return;
        }
    }
}

void UPCAPToolEditorWidget::AddSubjectToShot(const FString& ProjectCode, const FString& DayID,
                                              const FString& SessionID, const FString& ShotID,
                                              const FShotSubject& NewSubject)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    Shot->Subjects.Add(NewSubject);
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::RemoveSubjectFromShot(const FString& ProjectCode, const FString& DayID,
                                                   const FString& SessionID, const FString& ShotID,
                                                   const FString& ActorName)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    Shot->Subjects.RemoveAll([&ActorName](const FShotSubject& S) { return S.ActorName == ActorName; });
    MarkDirtyAndNotify();
}

// ─── Props ────────────────────────────────────────────────────────────────────

void UPCAPToolEditorWidget::AddPropToShot(const FString& ProjectCode, const FString& DayID,
                                           const FString& SessionID, const FString& ShotID,
                                           const FPropEntry& NewProp)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    Shot->Props.Add(NewProp);
    MarkDirtyAndNotify();
}

void UPCAPToolEditorWidget::RemovePropFromShot(const FString& ProjectCode, const FString& DayID,
                                                const FString& SessionID, const FString& ShotID,
                                                const FString& PropName)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    Shot->Props.RemoveAll([&PropName](const FPropEntry& P) { return P.PropName == PropName; });
    MarkDirtyAndNotify();
}

// ─── Takes ────────────────────────────────────────────────────────────────────

TArray<FTake> UPCAPToolEditorWidget::GetTakesForShot(const FString& ProjectCode, const FString& DayID,
                                                       const FString& SessionID, const FString& ShotID) const
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return {};
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    return Shot ? Shot->Takes : TArray<FTake>();
}

void UPCAPToolEditorWidget::SetTakeLabel(const FString& ProjectCode, const FString& DayID,
                                          const FString& SessionID, const FString& ShotID,
                                          const FString& TakeID, ETakeLabel NewLabel)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    for (FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == TakeID)
        {
            Take.Label = NewLabel;
            MarkDirtyAndNotify();
            return;
        }
    }
}

void UPCAPToolEditorWidget::UpdateTakeNotes(const FString& ProjectCode, const FString& DayID,
                                             const FString& SessionID, const FString& ShotID,
                                             const FString& TakeID, const FString& Notes)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    for (FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == TakeID)
        {
            Take.Notes = Notes;
            MarkDirtyAndNotify();
            return;
        }
    }
}

void UPCAPToolEditorWidget::UpdateTakeDirectorNotes(const FString& ProjectCode, const FString& DayID,
                                                     const FString& SessionID, const FString& ShotID,
                                                     const FString& TakeID, const FString& DirectorNotes)
{
    UPCAPDatabase* DB = GetDatabase();
    if (!DB) return;
    FShot* Shot = DB->GetShot(ProjectCode, DayID, SessionID, ShotID);
    if (!Shot) return;
    for (FTake& Take : Shot->Takes)
    {
        if (Take.TakeID == TakeID)
        {
            Take.DirectorNotes = DirectorNotes;
            MarkDirtyAndNotify();
            return;
        }
    }
}

// ─── Date / Time Helpers ──────────────────────────────────────────────────────

FDateTime UPCAPToolEditorWidget::MakeDateTimeFromParts(int32 Year, int32 Month, int32 Day,
                                                        int32 Hour, int32 Minute)
{
    if (!FDateTime::Validate(Year, Month, Day, Hour, Minute, 0, 0))
    {
        UE_LOG(LogTemp, Warning, TEXT("[PCAPTool] MakeDateTimeFromParts: invalid date %d-%d-%d"), Year, Month, Day);
        return FDateTime(0);
    }
    return FDateTime(Year, Month, Day, Hour, Minute, 0);
}

void UPCAPToolEditorWidget::BreakDateTime(const FDateTime& InDateTime, int32& Year, int32& Month,
                                           int32& Day, int32& Hour, int32& Minute)
{
    Year   = InDateTime.GetYear();
    Month  = InDateTime.GetMonth();
    Day    = InDateTime.GetDay();
    Hour   = InDateTime.GetHour();
    Minute = InDateTime.GetMinute();
}

FDateTime UPCAPToolEditorWidget::Today()
{
    const FDateTime Now = FDateTime::Now();
    return FDateTime(Now.GetYear(), Now.GetMonth(), Now.GetDay());
}

FString UPCAPToolEditorWidget::FormatDateDisplay(const FDateTime& InDateTime)
{
    if (InDateTime.GetTicks() == 0)
    {
        return TEXT("—");
    }
    return FString::Printf(TEXT("%s %s %02d %04d"),
        *FText::FromString(InDateTime.GetDayOfWeek() == EDayOfWeek::Monday    ? "Mon" :
                           InDateTime.GetDayOfWeek() == EDayOfWeek::Tuesday   ? "Tue" :
                           InDateTime.GetDayOfWeek() == EDayOfWeek::Wednesday ? "Wed" :
                           InDateTime.GetDayOfWeek() == EDayOfWeek::Thursday  ? "Thu" :
                           InDateTime.GetDayOfWeek() == EDayOfWeek::Friday    ? "Fri" :
                           InDateTime.GetDayOfWeek() == EDayOfWeek::Saturday  ? "Sat" : "Sun").ToString(),
        *FText::FromString(InDateTime.GetMonthOfYear() == EMonthOfYear::January   ? "Jan" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::February  ? "Feb" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::March     ? "Mar" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::April     ? "Apr" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::May       ? "May" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::June      ? "Jun" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::July      ? "Jul" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::August    ? "Aug" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::September ? "Sep" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::October   ? "Oct" :
                           InDateTime.GetMonthOfYear() == EMonthOfYear::November  ? "Nov" : "Dec").ToString(),
        InDateTime.GetDay(),
        InDateTime.GetYear()
    );
}
