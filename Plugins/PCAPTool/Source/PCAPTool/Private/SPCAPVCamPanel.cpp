#include "SPCAPVCamPanel.h"

#include "VCamConfig.h"
#include "PCAPVCamSubsystem.h"
#include "PCAPToolTypes.h"   // EStreamStatus

#include "Engine/Engine.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "PropertyCustomizationHelpers.h"   // SObjectPropertyEntryBox
#include "AssetRegistry/AssetData.h"

#include "SPCAPPanelStyle.h"   // ColLabel

#define LOCTEXT_NAMESPACE "PCAPVCamPanel"

UPCAPVCamSubsystem* SPCAPVCamPanel::GetVCam() const
{
    return GEngine ? GEngine->GetEngineSubsystem<UPCAPVCamSubsystem>() : nullptr;
}

void SPCAPVCamPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
    [
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("NoBorder")).Padding(0)
        [
            SNew(SVerticalBox)

            // Fixed header — config picker + live status line
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                        [ SNew(STextBlock).Text(LOCTEXT("CfgLbl", "VCam Config")).ColorAndOpacity(ColLabel) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [ BuildConfigPicker() ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SAssignNew(StatusBox, SBox) ]
                ]
            ]

            // Body — control sections
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(FMargin(6.f))
                [ SAssignNew(BodyBox, SBox) ]
            ]
        ]
    ];

    RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SPCAPVCamPanel::PollStatus));

    RebuildStatus();
    RebuildBody();
}

EActiveTimerReturnType SPCAPVCamPanel::PollStatus(double, float)
{
    RebuildStatus();
    return EActiveTimerReturnType::Continue;
}

// ── Config picker ────────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildConfigPicker()
{
    return SNew(SObjectPropertyEntryBox)
        .AllowedClass(UPCAPVCamConfig::StaticClass())
        .DisplayThumbnail(false)
        .ObjectPath_Lambda([this]() -> FString
        {
            UPCAPVCamSubsystem* V = GetVCam();
            UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr;
            return C ? C->GetPathName() : FString();
        })
        .OnObjectChanged(this, &SPCAPVCamPanel::OnConfigPicked);
}

void SPCAPVCamPanel::OnConfigPicked(const FAssetData& Asset)
{
    if (UPCAPVCamSubsystem* V = GetVCam())
    {
        V->SetActiveConfig(Cast<UPCAPVCamConfig>(Asset.GetAsset()));
    }
    RebuildStatus();
    RebuildBody();
}

// ── Live status ──────────────────────────────────────────────────────────────

void SPCAPVCamPanel::RebuildStatus()
{
    if (!StatusBox.IsValid()) { return; }

    UPCAPVCamSubsystem* V = GetVCam();
    UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr;

    const EStreamStatus St = V ? V->GetStreamStatus() : EStreamStatus::Disconnected;
    const FLinearColor Dot = (St == EStreamStatus::Connected) ? ColGreen
                           : (St == EStreamStatus::Degraded)  ? ColAmber : ColRed;
    const FString Subj = C ? C->LiveLinkSubjectName.ToString() : TEXT("(no config)");

    FString Xform = TEXT("--");
    FString Foc   = TEXT("--");
    FString Map   = TEXT("--");
    if (V && C)
    {
        const FTransform T = V->GetCurrentTransform();
        const FVector P = T.GetLocation();
        const FRotator R = T.Rotator();
        Xform = FString::Printf(TEXT("T %.1f %.1f %.1f   R %.1f %.1f %.1f"),
            P.X, P.Y, P.Z, R.Pitch, R.Yaw, R.Roll);
        Foc = FString::Printf(TEXT("%.1f mm"), V->GetCurrentFocalLength());
        Map = V->GetActiveMapping();
    }

    StatusBox->SetContent(
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(Dot) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [ SNew(STextBlock).Text(FText::FromString(Subj)) ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Xform)).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
        [ SNew(STextBlock).Text(FText::FromString(Foc)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
        [ SNew(STextBlock).Text(FText::FromString(Map)).ColorAndOpacity(ColLabel) ]
    );
}

// ── Body ─────────────────────────────────────────────────────────────────────

void SPCAPVCamPanel::RebuildBody()
{
    if (!BodyBox.IsValid()) { return; }

    UPCAPVCamSubsystem* V = GetVCam();
    UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr;

    if (!C)
    {
        BodyBox->SetContent(
            SNew(SBox).Padding(20.f).HAlign(HAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("NoCfg", "Pick a VCam Config asset above to drive the camera."))
                .ColorAndOpacity(ColText2) ]
        );
        return;
    }

    BodyBox->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ BuildTransformSection() ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ BuildLensSection() ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ BuildNavigationSection() ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ BuildControllerSection() ]
    );
}

// ── Section / toggle helpers ─────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::MakeSection(const FText& Title, const TSharedRef<SWidget>& Content)
{
    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
        [ SNew(STextBlock).Text(Title).ColorAndOpacity(ColLabel) ]
        + SVerticalBox::Slot().AutoHeight()
        [ Content ]
    ];
}

TSharedRef<SWidget> SPCAPVCamPanel::MakeToggle(const FString& Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
{
    return SNew(SCheckBox)
        .IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
        [ SNew(STextBlock).Margin(FMargin(4, 0, 0, 0)).Text(FText::FromString(Label)) ];
}

// ── Transform section ────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildTransformSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
        [ SNew(SButton).Text(LOCTEXT("Zero", "Zero Space"))
            .OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->ZeroSpace(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ MakeToggle(TEXT("Hold"),
            [this]() { UPCAPVCamSubsystem* S = GetVCam(); return S && S->IsHeld(); },
            [this](bool b) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetHold(b); }) ]
    ];

    Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
    [
        SNew(SUniformGridPanel).SlotPadding(FMargin(4.f))
        + SUniformGridPanel::Slot(0, 0)
        [ MakeToggle(TEXT("Flight Mode"),
            [this]() { UPCAPVCamSubsystem* S = GetVCam(); return S && S->IsFlightMode(); },
            [this](bool b) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetFlightMode(b); }) ]
        + SUniformGridPanel::Slot(1, 0)
        [ MakeToggle(TEXT("Lock Position"),
            [this]() { UPCAPVCamSubsystem* S = GetVCam(); return S && S->IsLockPosition(); },
            [this](bool b) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetLockPosition(b); }) ]
        + SUniformGridPanel::Slot(2, 0)
        [ MakeToggle(TEXT("Lock Rotation"),
            [this]() { UPCAPVCamSubsystem* S = GetVCam(); return S && S->IsLockRotation(); },
            [this](bool b) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetLockRotation(b); }) ]
        + SUniformGridPanel::Slot(0, 1)
        [ MakeToggle(TEXT("Lock Roll"),
            [this]() { UPCAPVCamSubsystem* S = GetVCam(); return S && S->IsLockRoll(); },
            [this](bool b) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetLockRoll(b); }) ]
        + SUniformGridPanel::Slot(1, 1)
        [ MakeToggle(TEXT("Kill Roll"),
            [this]() { UPCAPVCamSubsystem* S = GetVCam(); return S && S->IsKillRoll(); },
            [this](bool b) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetKillRoll(b); }) ]
    ];

    Box->AddSlot().AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
        [ SNew(STextBlock).Text(LOCTEXT("SmPos", "Smooth Pos")).ColorAndOpacity(ColLabel) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
        [ SNew(SSpinBox<float>).MinValue(1.f).MaxValue(20.f).MinDesiredWidth(60.f)
            .Value_Lambda([this]() { UPCAPVCamSubsystem* S = GetVCam(); UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr; return C ? C->Smoothing.PositionSmoothing : 10.f; })
            .OnValueChanged_Lambda([this](float v) { UPCAPVCamSubsystem* S = GetVCam(); if (UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr) C->Smoothing.PositionSmoothing = v; }) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
        [ SNew(STextBlock).Text(LOCTEXT("SmRot", "Smooth Rot")).ColorAndOpacity(ColLabel) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SSpinBox<float>).MinValue(1.f).MaxValue(20.f).MinDesiredWidth(60.f)
            .Value_Lambda([this]() { UPCAPVCamSubsystem* S = GetVCam(); UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr; return C ? C->Smoothing.RotationSmoothing : 10.f; })
            .OnValueChanged_Lambda([this](float v) { UPCAPVCamSubsystem* S = GetVCam(); if (UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr) C->Smoothing.RotationSmoothing = v; }) ]
    ];

    return MakeSection(LOCTEXT("TransformSec", "Transform"), Box);
}

// ── Lens section ─────────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildLensSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
            .Text_Lambda([this]() { UPCAPVCamSubsystem* S = GetVCam(); return FText::FromString(S ? FString::Printf(TEXT("%.1f mm"), S->GetCurrentFocalLength()) : TEXT("--")); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
        [ SNew(SButton).Text(LOCTEXT("FocDn", "Shorter"))
            .OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->CycleFocalLengthDown(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SButton).Text(LOCTEXT("FocUp", "Longer"))
            .OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->CycleFocalLengthUp(); return FReply::Handled(); }) ]
    ];

    TSharedRef<SHorizontalBox> Presets = SNew(SHorizontalBox);
    UPCAPVCamSubsystem* V = GetVCam();
    if (UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr)
    {
        for (float F : C->FocalLengthPresets)
        {
            Presets->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
            [ SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("%.0f"), F)))
                .OnClicked_Lambda([this, F]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetFocalLength(F); return FReply::Handled(); }) ];
        }
    }
    Box->AddSlot().AutoHeight()[ Presets ];

    return MakeSection(LOCTEXT("LensSec", "Lens"), Box);
}

// ── Navigation / saved positions ─────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildNavigationSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
        [ SNew(SButton).Text(LOCTEXT("Save", "Save Position"))
            .OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->SaveCurrentPosition(); RebuildBody(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
        [ SNew(SButton).Text(LOCTEXT("Prev", "< Prev"))
            .OnClicked_Lambda([this]()
            {
                if (UPCAPVCamSubsystem* S = GetVCam())
                {
                    if (UPCAPVCamConfig* C = S->GetActiveConfig())
                    {
                        if (C->SavedPositions.Num() > 0)
                        {
                            S->GotoSavedPosition(FMath::Max(0, C->ActiveSavedPositionIndex - 1));
                        }
                    }
                }
                return FReply::Handled();
            }) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SButton).Text(LOCTEXT("Next", "Next >"))
            .OnClicked_Lambda([this]()
            {
                if (UPCAPVCamSubsystem* S = GetVCam())
                {
                    if (UPCAPVCamConfig* C = S->GetActiveConfig())
                    {
                        if (C->SavedPositions.Num() > 0)
                        {
                            S->GotoSavedPosition(FMath::Min(C->SavedPositions.Num() - 1, C->ActiveSavedPositionIndex + 1));
                        }
                    }
                }
                return FReply::Handled();
            }) ]
    ];

    TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
    UPCAPVCamSubsystem* V = GetVCam();
    if (UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr)
    {
        for (int32 i = 0; i < C->SavedPositions.Num(); ++i)
        {
            const FPCAPVCamAlignOffset& Pos = C->SavedPositions[i];
            const FString Lbl = FString::Printf(TEXT("[%d]  T %.0f %.0f %.0f"),
                i, Pos.Translation.X, Pos.Translation.Y, Pos.Translation.Z);
            List->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Lbl)).ColorAndOpacity(ColText2) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [ SNew(SButton).Text(LOCTEXT("Go", "Go")).OnClicked_Lambda([this, i]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->GotoSavedPosition(i); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                [ SNew(SButton).Text(LOCTEXT("Del", "Delete")).OnClicked_Lambda([this, i]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->DeleteSavedPosition(i); RebuildBody(); return FReply::Handled(); }) ]
            ];
        }
    }
    Box->AddSlot().AutoHeight()[ List ];

    return MakeSection(LOCTEXT("NavSec", "Saved Positions"), Box);
}

// ── Controller section ───────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildControllerSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(STextBlock).Text(LOCTEXT("Layout", "Button Layout")).ColorAndOpacity(ColLabel) ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SComboButton)
            .ButtonContent()
            [ SNew(STextBlock).Text_Lambda([this]()
                {
                    UPCAPVCamSubsystem* S = GetVCam();
                    UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr;
                    const int32 L = C ? C->ActiveButtonLayout : 0;
                    static const TCHAR* Names[] = { TEXT("Default"), TEXT("Variation 1"), TEXT("Inverted Default") };
                    return FText::FromString((L >= 0 && L < 3) ? Names[L] : TEXT("Default"));
                }) ]
            .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
            {
                FMenuBuilder MB(true, nullptr);
                const TCHAR* Names[] = { TEXT("Default"), TEXT("Variation 1"), TEXT("Inverted Default") };
                for (int32 i = 0; i < 3; ++i)
                {
                    const int32 Idx = i;
                    MB.AddMenuEntry(FText::FromString(Names[i]), FText::GetEmpty(), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Idx]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetActiveButtonLayout(Idx); })));
                }
                return MB.MakeWidget();
            })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)
        [ SNew(STextBlock).Text_Lambda([this]() { UPCAPVCamSubsystem* S = GetVCam(); return FText::FromString(S ? S->GetActiveMapping() : TEXT("--")); }).ColorAndOpacity(ColText2) ]
    ];

    Box->AddSlot().AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
        [ SNew(STextBlock).Text(LOCTEXT("TG", "Translation Gain")).ColorAndOpacity(ColLabel) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SSpinBox<float>).MinValue(0.01f).MaxValue(1.f).MinDesiredWidth(70.f)
            .Value_Lambda([this]() { UPCAPVCamSubsystem* S = GetVCam(); return S ? S->GetTranslationGain() : 0.5f; })
            .OnValueChanged_Lambda([this](float v) { if (UPCAPVCamSubsystem* S = GetVCam()) S->SetTranslationGain(v); }) ]
    ];

    Box->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
    [ SNew(STextBlock).AutoWrapText(true)
        .Text(LOCTEXT("CtlNote", "Live controller input (joystick layouts, activation indicators) is wired in a later pass — the data source (WVCAM broadcast vs direct HID) is still open."))
        .ColorAndOpacity(ColText2) ]
    ;

    return MakeSection(LOCTEXT("CtlSec", "Controller"), Box);
}

#undef LOCTEXT_NAMESPACE
