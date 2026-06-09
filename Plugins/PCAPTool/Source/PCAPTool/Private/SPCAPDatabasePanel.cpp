#include "SPCAPDatabasePanel.h"

#include "PCAPToolSettings.h"
#include "MocapDatabase.h"
#include "ActorRosterEntry.h"
#include "PropRosterEntry.h"
#include "StageConfigAsset.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PCAPDatabasePanel"

namespace
{
    TSharedPtr<FPCAPDBNode> MakeNode(FPCAPDBNode::EKind Kind, const FString& Label, const FString& Detail = FString())
    {
        TSharedPtr<FPCAPDBNode> N = MakeShared<FPCAPDBNode>();
        N->Kind   = Kind;
        N->Label  = Label;
        N->Detail = Detail;
        return N;
    }
}

void SPCAPDatabasePanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder)
        .BorderImage(FAppStyle::GetBrush("NoBorder"))
        .Padding(0)
        [
            SNew(SVerticalBox)

            // ── Header bar ─────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(8.f, 6.f))
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("Title", "MOCAP DATABASE"))
                        .ColorAndOpacity(FSlateColor(ColGreen))
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(1.f)
                    .VAlign(VAlign_Center)
                    .Padding(12.f, 0.f)
                    [
                        SNew(STextBlock)
                        .Text(this, &SPCAPDatabasePanel::HeaderStatusText)
                        .ColorAndOpacity(FSlateColor(ColText2))
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Refresh", "Refresh"))
                        .OnClicked(this, &SPCAPDatabasePanel::OnRefreshClicked)
                    ]
                ]
            ]

            // ── Tree ───────────────────────────────────────────────────────
            + SVerticalBox::Slot()
            .FillHeight(1.f)
            [
                SAssignNew(TreeView, STreeView<TSharedPtr<FPCAPDBNode>>)
                .TreeItemsSource(&RootNodes)
                .OnGenerateRow(this, &SPCAPDatabasePanel::OnGenerateRow)
                .OnGetChildren(this, &SPCAPDatabasePanel::OnGetChildren)
                .SelectionMode(ESelectionMode::Single)
            ]
        ]
    ];

    RebuildTree();
}

void SPCAPDatabasePanel::RebuildTree()
{
    RootNodes.Reset();

    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    UMocapDatabase*    DB       = Settings ? Settings->GetDatabase() : nullptr;

    if (!DB)
    {
        RootNodes.Add(MakeNode(FPCAPDBNode::EKind::Info,
            TEXT("No database assigned"),
            TEXT("Project Settings → PCAP → Database Asset")));
        if (TreeView.IsValid()) TreeView->RequestTreeRefresh();
        return;
    }

    // ── Roster ────────────────────────────────────────────────────────────
    {
        TSharedPtr<FPCAPDBNode> Roster = MakeNode(FPCAPDBNode::EKind::Section, TEXT("Roster"));

        TSharedPtr<FPCAPDBNode> Actors = MakeNode(FPCAPDBNode::EKind::Section, TEXT("Actors"),
            FString::FromInt(DB->ActorRoster.Num()));
        for (const TSoftObjectPtr<UActorRosterEntry>& Ref : DB->ActorRoster)
        {
            UActorRosterEntry* A = Ref.LoadSynchronous();
            Actors->Children.Add(MakeNode(FPCAPDBNode::EKind::Actor,
                A ? A->ActorID : Ref.GetAssetName(),
                A ? FString::Printf(TEXT("%s %s"), *A->FirstName, *A->LastName).TrimStartAndEnd() : TEXT("(unloaded)")));
        }
        Roster->Children.Add(Actors);

        TSharedPtr<FPCAPDBNode> Props = MakeNode(FPCAPDBNode::EKind::Section, TEXT("Props"),
            FString::FromInt(DB->PropRoster.Num()));
        for (const TSoftObjectPtr<UPropRosterEntry>& Ref : DB->PropRoster)
        {
            UPropRosterEntry* P = Ref.LoadSynchronous();
            Props->Children.Add(MakeNode(FPCAPDBNode::EKind::Prop,
                P ? P->PropID : Ref.GetAssetName(),
                P ? (P->bIsTracked ? TEXT("tracked") : TEXT("untracked")) : TEXT("(unloaded)")));
        }
        Roster->Children.Add(Props);

        TSharedPtr<FPCAPDBNode> Configs = MakeNode(FPCAPDBNode::EKind::Section, TEXT("Stage Configs"),
            FString::FromInt(DB->StageConfigs.Num()));
        for (const TSoftObjectPtr<UStageConfigAsset>& Ref : DB->StageConfigs)
        {
            UStageConfigAsset* C = Ref.LoadSynchronous();
            Configs->Children.Add(MakeNode(FPCAPDBNode::EKind::StageConfig,
                C ? C->ConfigName : Ref.GetAssetName(),
                C ? StaticEnum<EBodySystem>()->GetDisplayNameTextByValue((int64)C->BodySystem).ToString() : TEXT("(unloaded)")));
        }
        Roster->Children.Add(Configs);

        RootNodes.Add(Roster);
    }

    // ── Productions → Days → Sessions → Shots → Takes ───────────────────────
    {
        TSharedPtr<FPCAPDBNode> Prods = MakeNode(FPCAPDBNode::EKind::Section, TEXT("Productions"),
            FString::FromInt(DB->Productions.Num()));

        for (const FProduction& P : DB->Productions)
        {
            TSharedPtr<FPCAPDBNode> PN = MakeNode(FPCAPDBNode::EKind::Production,
                FString::Printf(TEXT("%s  [%s]"), *P.ProductionName, *P.ProjectCode),
                FString::Printf(TEXT("%d days"), P.Days.Num()));

            for (const FShootDay& D : P.Days)
            {
                TSharedPtr<FPCAPDBNode> DN = MakeNode(FPCAPDBNode::EKind::Day,
                    FString::Printf(TEXT("Day_%s"), *D.DayID),
                    FString::Printf(TEXT("%d sessions"), D.Sessions.Num()));

                for (const FSession& S : D.Sessions)
                {
                    TSharedPtr<FPCAPDBNode> SN = MakeNode(FPCAPDBNode::EKind::Session,
                        FString::Printf(TEXT("Session_%s  %s"), *S.SessionID, *S.Label).TrimStartAndEnd(),
                        FString::Printf(TEXT("%d shots"), S.Shots.Num()));

                    for (const FShot& Sh : S.Shots)
                    {
                        TSharedPtr<FPCAPDBNode> ShN = MakeNode(FPCAPDBNode::EKind::Shot,
                            FString::Printf(TEXT("Shot_%s  %s"), *Sh.ShotID, *Sh.Description).TrimStartAndEnd(),
                            FString::Printf(TEXT("%d takes"), Sh.Takes.Num()));

                        for (const FTake& T : Sh.Takes)
                        {
                            ShN->Children.Add(MakeNode(FPCAPDBNode::EKind::Take,
                                T.TakeID,
                                StaticEnum<ETakeLabel>()->GetDisplayNameTextByValue((int64)T.Label).ToString()));
                        }
                        SN->Children.Add(ShN);
                    }
                    DN->Children.Add(SN);
                }
                PN->Children.Add(DN);
            }
            Prods->Children.Add(PN);
        }
        RootNodes.Add(Prods);
    }

    if (TreeView.IsValid())
    {
        TreeView->RequestTreeRefresh();
        ExpandAll(RootNodes, true);
    }
}

void SPCAPDatabasePanel::ExpandAll(const TArray<TSharedPtr<FPCAPDBNode>>& Nodes, bool bExpand)
{
    if (!TreeView.IsValid()) return;
    for (const TSharedPtr<FPCAPDBNode>& N : Nodes)
    {
        TreeView->SetItemExpansion(N, bExpand);
        if (N->Children.Num() > 0)
        {
            ExpandAll(N->Children, bExpand);
        }
    }
}

TSharedRef<ITableRow> SPCAPDatabasePanel::OnGenerateRow(TSharedPtr<FPCAPDBNode> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
    const bool bSection = Node.IsValid() && Node->Kind == FPCAPDBNode::EKind::Section;
    const bool bInfo    = Node.IsValid() && Node->Kind == FPCAPDBNode::EKind::Info;

    FSlateColor LabelColor = FSlateColor::UseForeground();
    if (bSection)   LabelColor = FSlateColor(ColGreen);
    else if (bInfo) LabelColor = FSlateColor(ColText2);

    return SNew(STableRow<TSharedPtr<FPCAPDBNode>>, OwnerTable)
    [
        SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .FillWidth(1.f)
        .VAlign(VAlign_Center)
        .Padding(2.f, 3.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Node.IsValid() ? Node->Label : FString()))
            .ColorAndOpacity(LabelColor)
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Center)
        .Padding(12.f, 3.f)
        [
            SNew(STextBlock)
            .Text(FText::FromString(Node.IsValid() ? Node->Detail : FString()))
            .ColorAndOpacity(FSlateColor(ColText3))
        ]
    ];
}

void SPCAPDatabasePanel::OnGetChildren(TSharedPtr<FPCAPDBNode> Node, TArray<TSharedPtr<FPCAPDBNode>>& OutChildren)
{
    if (Node.IsValid())
    {
        OutChildren = Node->Children;
    }
}

FReply SPCAPDatabasePanel::OnRefreshClicked()
{
    RebuildTree();
    return FReply::Handled();
}

FText SPCAPDatabasePanel::HeaderStatusText() const
{
    UPCAPToolSettings* Settings = UPCAPToolSettings::Get();
    UMocapDatabase*    DB       = Settings ? Settings->GetDatabase() : nullptr;
    if (!DB)
    {
        return LOCTEXT("NoDB", "no database assigned");
    }
    return FText::FromString(FString::Printf(TEXT("%s  ·  %d productions  ·  %d actors"),
        *DB->GetName(), DB->Productions.Num(), DB->ActorRoster.Num()));
}

#undef LOCTEXT_NAMESPACE
