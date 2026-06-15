#include "Misc/AutomationTest.h"
#include "PCAPMarkerSource.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVizColorDeterministic, "PCAP.Viz.ColorDeterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVizColorDeterministic::RunTest(const FString&)
{
    const FLinearColor A = PCAPViz::SubjectColor(FName("kevinDorman"));
    const FLinearColor B = PCAPViz::SubjectColor(FName("kevinDorman"));
    TestTrue("same name -> same color", A.Equals(B, 0.0001f));
    const FLinearColor C = PCAPViz::SubjectColor(FName("Lightsaber01"));
    TestTrue("different names -> different color", !A.Equals(C, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCAPVizViconConvert, "PCAP.Viz.ViconConvert",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPCAPVizViconConvert::RunTest(const FString&)
{
    const FVector V = PCAPViz::ViconMMToUE(1000.0, 2000.0, 3000.0);
    TestEqual("mm->cm X", V.X, 100.0);
    TestEqual("flip + mm->cm Y", V.Y, -200.0);
    TestEqual("mm->cm Z", V.Z, 300.0);
    return true;
}
