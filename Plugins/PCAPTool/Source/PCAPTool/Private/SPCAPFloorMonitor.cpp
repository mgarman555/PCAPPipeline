#include "SPCAPFloorMonitor.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkRole.h"
#include "LiveLinkTypes.h"

#include "SPCAPPanelStyle.h"   // ColLabel

#define LOCTEXT_NAMESPACE "PCAPFloorMonitor"

void SPCAPFloorMonitor::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [ SNew(STextBlock).Text(LOCTEXT("Hdr", "Live Link — what's streaming on the floor")).ColorAndOpacity(ColLabel) ]
            ]
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(FMargin(8.f))
                [ SAssignNew(ListBox, SBox) ]
            ]
        ]
    ];

    RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SPCAPFloorMonitor::Poll));
    RebuildList();
}

EActiveTimerReturnType SPCAPFloorMonitor::Poll(double, float)
{
    RebuildList();
    return EActiveTimerReturnType::Continue;
}

void SPCAPFloorMonitor::RebuildList()
{
    if (!ListBox.IsValid()) { return; }

    IModularFeatures& MF = IModularFeatures::Get();
    if (!MF.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
    {
        ListBox->SetContent(SNew(SBox).Padding(16.f).HAlign(HAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("NoLL", "Live Link isn't available in this editor build."))
                .ColorAndOpacity(ColText2) ]);
        return;
    }

    ILiveLinkClient& Client = MF.GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
    TArray<FLiveLinkSubjectKey> Subjects = Client.GetSubjects(/*bIncludeDisabled*/true, /*bIncludeVirtual*/true);

    if (Subjects.Num() == 0)
    {
        ListBox->SetContent(SNew(SBox).Padding(16.f).HAlign(HAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("None", "No Live Link subjects. Start Shogun streaming and load the stage's Live Link preset."))
                .ColorAndOpacity(ColText2) ]);
        return;
    }

    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

    List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.42f)[ SNew(STextBlock).Text(LOCTEXT("cN", "Subject")).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().FillWidth(0.28f)[ SNew(STextBlock).Text(LOCTEXT("cR", "Role")).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().FillWidth(0.30f)[ SNew(STextBlock).Text(LOCTEXT("cS", "Source")).ColorAndOpacity(ColText2) ]
    ];

    for (const FLiveLinkSubjectKey& Key : Subjects)
    {
        const FName SubjName = Key.SubjectName.Name;

        TSubclassOf<ULiveLinkRole> Role = Client.GetSubjectRole_AnyThread(Key);
        FString RoleStr = Role ? Role->GetName() : TEXT("Unknown");
        RoleStr.RemoveFromStart(TEXT("LiveLink"));
        RoleStr.RemoveFromEnd(TEXT("Role"));

        const FText SrcText = Client.GetSourceType(Key.Source);

        bool bLive = false;
        if (Role)
        {
            FLiveLinkSubjectFrameData Frame;
            bLive = Client.EvaluateFrame_AnyThread(SubjName, Role, Frame);
        }
        const FLinearColor Dot = bLive ? ColGreen : ColAmber;

        List->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 2.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.42f).VAlign(VAlign_Center)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(Dot) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromName(SubjName)) ]
            ]
            + SHorizontalBox::Slot().FillWidth(0.28f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(RoleStr)).ColorAndOpacity(ColText2) ]
            + SHorizontalBox::Slot().FillWidth(0.30f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(SrcText).ColorAndOpacity(ColText2) ]
        ];
    }

    ListBox->SetContent(List);
}

#undef LOCTEXT_NAMESPACE
