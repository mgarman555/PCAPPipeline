#include "PCAPSlateCsv.h"

// Unique-named internal helpers (NOT generic names in an anonymous namespace — that
// collides under UE unity builds; see the ColLabel fix in SPCAPPanelStyle.h).
namespace
{
    // RFC-4180-ish tokenizer: records of fields. Handles "quoted, fields", "" escapes
    // inside quotes, and CR / LF / CRLF record separators.
    TArray<TArray<FString>> SlateCsvTokenize(const FString& Text)
    {
        TArray<TArray<FString>> Records;
        TArray<FString> Cur;
        FString Field;
        bool bInQuotes = false;
        bool bStarted  = false;   // any content seen on the current record

        auto PushField  = [&]() { Cur.Add(Field); Field.Reset(); };
        auto PushRecord = [&]() { PushField(); Records.Add(MoveTemp(Cur)); Cur.Reset(); bStarted = false; };

        const int32 N = Text.Len();
        for (int32 i = 0; i < N; ++i)
        {
            const TCHAR c = Text[i];
            if (bInQuotes)
            {
                if (c == TEXT('"'))
                {
                    if (i + 1 < N && Text[i + 1] == TEXT('"')) { Field.AppendChar(TEXT('"')); ++i; }
                    else                                        { bInQuotes = false; }
                }
                else { Field.AppendChar(c); bStarted = true; }
            }
            else
            {
                if      (c == TEXT('"'))  { bInQuotes = true; bStarted = true; }
                else if (c == TEXT(','))  { PushField();      bStarted = true; }
                else if (c == TEXT('\n')) { PushRecord(); }
                else if (c == TEXT('\r')) { /* ignore — '\n' ends the record */ }
                else                      { Field.AppendChar(c); bStarted = true; }
            }
        }
        if (bStarted || Field.Len() > 0 || Cur.Num() > 0) PushRecord();
        return Records;
    }

    FString SlateCsvQuoteField(const FString& In)
    {
        const bool bNeedsQuote = In.Contains(TEXT(",")) || In.Contains(TEXT("\""))
                              || In.Contains(TEXT("\n")) || In.Contains(TEXT("\r"));
        if (!bNeedsQuote) return In;
        return FString::Printf(TEXT("\"%s\""), *In.Replace(TEXT("\""), TEXT("\"\"")));
    }

    TArray<FString> SlateCsvSplitList(const FString& Field)
    {
        TArray<FString> Parts, Clean;
        Field.ParseIntoArray(Parts, TEXT(";"), /*InCullEmpty=*/true);
        for (FString S : Parts)
        {
            S = S.TrimStartAndEnd();
            if (!S.IsEmpty()) Clean.Add(S);
        }
        return Clean;
    }
}

FString FPCAPSlateCsv::Header()
{
    return TEXT("slot,type,description,actors,props,notes");
}

FString FPCAPSlateCsv::Format(const TArray<FSlateCsvRow>& Rows)
{
    FString Out = Header() + TEXT("\n");
    for (const FSlateCsvRow& R : Rows)
    {
        const FString Actors = FString::Join(R.Actors, TEXT(";"));
        const FString Props  = FString::Join(R.Props,  TEXT(";"));
        Out += FString::Printf(TEXT("%s,%s,%s,%s,%s,%s\n"),
            *SlateCsvQuoteField(R.Slot),
            *SlateCsvQuoteField(R.Type),
            *SlateCsvQuoteField(R.Description),
            *SlateCsvQuoteField(Actors),
            *SlateCsvQuoteField(Props),
            *SlateCsvQuoteField(R.Notes));
    }
    return Out;
}

bool FPCAPSlateCsv::Parse(const FString& Text, TArray<FSlateCsvRow>& OutRows, FString& OutError)
{
    OutRows.Reset();
    OutError.Reset();

    const TArray<TArray<FString>> Records = SlateCsvTokenize(Text);
    bool bFirst = true;

    for (const TArray<FString>& Rec : Records)
    {
        // Skip fully-empty records (blank lines).
        bool bEmpty = true;
        for (const FString& F : Rec) { if (!F.TrimStartAndEnd().IsEmpty()) { bEmpty = false; break; } }
        if (bEmpty) continue;

        // Skip a leading header row ("slot" in the first cell).
        if (bFirst)
        {
            bFirst = false;
            if (Rec.Num() > 0 && Rec[0].TrimStartAndEnd().ToLower() == TEXT("slot")) continue;
        }

        auto Get = [&Rec](int32 Idx) -> FString { return Rec.IsValidIndex(Idx) ? Rec[Idx].TrimStartAndEnd() : FString(); };

        FSlateCsvRow Row;
        Row.Slot        = Get(0);
        Row.Type        = Get(1);
        Row.Description = Get(2);
        Row.Actors      = SlateCsvSplitList(Get(3));
        Row.Props       = SlateCsvSplitList(Get(4));
        Row.Notes       = Get(5);

        if (Row.Slot.IsEmpty())
        {
            OutError = TEXT("A row is missing its slot (the first column). Every shot needs a slot, e.g. 003 or 901.");
            return false;
        }
        OutRows.Add(MoveTemp(Row));
    }
    return true;
}
