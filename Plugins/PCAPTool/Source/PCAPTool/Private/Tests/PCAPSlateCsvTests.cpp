#include "Misc/AutomationTest.h"
#include "PCAPSlateCsv.h"

#if WITH_DEV_AUTOMATION_TESTS

// Round-trip: rows -> CSV -> rows preserves slot/type/desc/actors/props/notes,
// including a comma inside a quoted description and a ';'-separated actor list.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPSlateCsvRoundTripTest,
    "PCAP.SlateCsv.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPSlateCsvRoundTripTest::RunTest(const FString&)
{
    TArray<FSlateCsvRow> In;
    {
        FSlateCsvRow R;
        R.Slot = TEXT("003"); R.Type = TEXT("Production");
        R.Description = TEXT("office hallway, push-in");   // comma must survive
        R.Actors = { TEXT("sarahKov"), TEXT("kevinDorman") };
        R.Props  = { TEXT("lightsaberHiltA") };
        R.Notes  = TEXT("hero beat");
        In.Add(R);
    }
    { FSlateCsvRow R; R.Slot = TEXT("901"); R.Type = TEXT("Calibration"); In.Add(R); }

    const FString Csv = FPCAPSlateCsv::Format(In);

    TArray<FSlateCsvRow> Out; FString Err;
    TestTrue(TEXT("parse ok"), FPCAPSlateCsv::Parse(Csv, Out, Err));
    TestEqual(TEXT("row count"), Out.Num(), 2);
    if (Out.Num() == 2)
    {
        TestEqual(TEXT("slot"),               Out[0].Slot, FString(TEXT("003")));
        TestEqual(TEXT("type"),               Out[0].Type, FString(TEXT("Production")));
        TestEqual(TEXT("comma preserved"),    Out[0].Description, FString(TEXT("office hallway, push-in")));
        TestEqual(TEXT("actor count"),        Out[0].Actors.Num(), 2);
        TestEqual(TEXT("actor 2"),            Out[0].Actors[1], FString(TEXT("kevinDorman")));
        TestEqual(TEXT("prop"),               Out[0].Props.Num(), 1);
        TestEqual(TEXT("notes"),              Out[0].Notes, FString(TEXT("hero beat")));
        TestEqual(TEXT("second slot"),        Out[1].Slot, FString(TEXT("901")));
        TestEqual(TEXT("second has no actors"), Out[1].Actors.Num(), 0);
    }
    return true;
}

// Header row is skipped, blank lines ignored, empty list-fields yield empty arrays.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPSlateCsvHeaderTest,
    "PCAP.SlateCsv.HeaderAndBlanks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPSlateCsvHeaderTest::RunTest(const FString&)
{
    const FString Csv = TEXT("slot,type,description,actors,props,notes\n\n003,Production,hi,sarahKov,,\n");
    TArray<FSlateCsvRow> Out; FString Err;
    TestTrue(TEXT("parse ok"), FPCAPSlateCsv::Parse(Csv, Out, Err));
    TestEqual(TEXT("one data row (header + blank skipped)"), Out.Num(), 1);
    if (Out.Num() == 1)
    {
        TestEqual(TEXT("slot"), Out[0].Slot, FString(TEXT("003")));
        TestEqual(TEXT("one actor"), Out[0].Actors.Num(), 1);
        TestEqual(TEXT("no props"), Out[0].Props.Num(), 0);
    }
    return true;
}

// A data row with no slot is a hard error (every shot must have a slot).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPSlateCsvMissingSlotTest,
    "PCAP.SlateCsv.MissingSlotFails",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPSlateCsvMissingSlotTest::RunTest(const FString&)
{
    TArray<FSlateCsvRow> Out; FString Err;
    TestFalse(TEXT("missing slot fails"), FPCAPSlateCsv::Parse(TEXT(",Production,desc\n"), Out, Err));
    TestTrue(TEXT("error message set"), !Err.IsEmpty());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
