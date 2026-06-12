#include "SPCAPVCamPanel.h"

#include "VCamConfig.h"
#include "PCAPVCamSubsystem.h"   // also brings VCamInputLayer.h (FVCamControllerInput)
#include "PCAPToolTypes.h"       // EStreamStatus

#include "Engine/Engine.h"
#include "HAL/PlatformTime.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "PropertyCustomizationHelpers.h"
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
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
                        [ SNew(STextBlock).Text(LOCTEXT("CfgLbl", "VCam Config")).ColorAndOpacity(ColLabel) ]
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [ BuildConfigPicker() ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SAssignNew(StatusBox, SBox) ]
                ]
            ]
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
    RebuildInputMonitor();
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

// ── Live status line ─────────────────────────────────────────────────────────

void SPCAPVCamPanel::RebuildStatus()
{
    if (!StatusBox.IsValid()) { return; }

    UPCAPVCamSubsystem* V = GetVCam();
    UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr;

    const EStreamStatus St = V ? V->GetStreamStatus() : EStreamStatus::Disconnected;
    const FLinearColor Dot = (St == EStreamStatus::Connected) ? ColGreen
                           : (St == EStreamStatus::Degraded)  ? ColAmber : ColRed;
    const FString Subj = C ? C->LiveLinkSubjectName.ToString() : TEXT("(no config)");

    FString Foc = TEXT("--");
    FString Map = TEXT("--");
    if (V && C)
    {
        Foc = FString::Printf(TEXT("%.1f mm"), V->GetCurrentFocalLength());
        Map = V->GetActiveMapping();
    }

    StatusBox->SetContent(
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("●"))).ColorAndOpacity(Dot) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(Subj)) ]
        + SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
        [ SNew(STextBlock).Text(FText::FromString(Foc)) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
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
        InputMonitorBox.Reset();
        BodyBox->SetContent(
            SNew(SBox).Padding(20.f).HAlign(HAlign_Center)
            [ SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("NoCfg", "Pick a VCam Config asset above to drive the camera."))
                .ColorAndOpacity(ColText2) ]);
        return;
    }

    BodyBox->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ BuildOffsetsSection() ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.55f).Padding(0.f, 0.f, 8.f, 0.f)[ BuildModesSection() ]
            + SHorizontalBox::Slot().FillWidth(0.45f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ BuildLensSection() ]
                + SVerticalBox::Slot().AutoHeight()[ BuildScalingSection() ]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(0.5f).Padding(0.f, 0.f, 8.f, 0.f)[ BuildNavigationSection() ]
            + SHorizontalBox::Slot().FillWidth(0.5f)[ BuildControllerSection() ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[ SAssignNew(InputMonitorBox, SBox) ]
        + SVerticalBox::Slot().AutoHeight()[ BuildOutputSection() ]
    );

    RebuildInputMonitor();
}

// ── Live controller-input monitor (the Buttonology equivalent / input test) ──

void SPCAPVCamPanel::RebuildInputMonitor()
{
    if (!InputMonitorBox.IsValid()) { return; }
    UPCAPVCamSubsystem* V = GetVCam();
    if (!V) { InputMonitorBox->SetContent(SNullWidget::NullWidget); return; }

    const int32 Count = V->GetInputPacketCount();
    const double Now = FPlatformTime::Seconds();
    if (Count != MonPrevPacketCount) { MonPrevPacketCount = Count; MonLastChangeTime = Now; }
    const bool bReceiving = (Count > 0) && ((Now - MonLastChangeTime) < 0.5);

    const FVCamControllerInput In = V->GetLatestInput();

    const FString Status = bReceiving
        ? FString::Printf(TEXT("receiving · seq %d"), Count)
        : (Count > 0 ? TEXT("stalled (no new packets)") : TEXT("waiting for WVCAM…"));

    auto Pill = [this](const FString& L, bool bOn) -> TSharedRef<SWidget>
    {
        return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(5.f, 2.f))
            [ SNew(STextBlock).Text(FText::FromString(L)).ColorAndOpacity(bOn ? ColGreen : ColText2) ];
    };

    TSharedRef<SHorizontalBox> Btns = SNew(SHorizontalBox);
    const TArray<TPair<FString, bool>> ButtonStates = {
        {TEXT("LT"), In.LeftTrigger}, {TEXT("RT"), In.RightTrigger},
        {TEXT("L↑"), In.LeftUp}, {TEXT("L↓"), In.LeftDown}, {TEXT("L←"), In.LeftLeft}, {TEXT("L→"), In.LeftRight},
        {TEXT("R↑"), In.RightUp}, {TEXT("R↓"), In.RightDown}, {TEXT("R←"), In.RightLeft}, {TEXT("R→"), In.RightRight},
        {TEXT("LA"), In.LeftA}, {TEXT("LB"), In.LeftB}, {TEXT("RA"), In.RightA}, {TEXT("RB"), In.RightB},
    };
    for (const TPair<FString, bool>& B : ButtonStates)
    {
        Btns->AddSlot().AutoWidth().Padding(2.f, 0.f)[ Pill(B.Key, B.Value) ];
    }

    const FString Axes = FString::Printf(
        TEXT("Lx %5.0f   Ly %5.0f   Rx %5.0f   Ry %5.0f      L-enc %6.0f   R-enc %6.0f"),
        In.LeftJoyX, In.LeftJoyY, In.RightJoyX, In.RightJoyY, In.LeftEnc, In.RightEnc);

    InputMonitorBox->SetContent(
        SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("InHdr", "Controller input")).ColorAndOpacity(ColLabel) ]
                + SHorizontalBox::Slot().FillWidth(1.f)[ SNew(SSpacer) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Status)).ColorAndOpacity(bReceiving ? ColGreen : ColText2) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
            [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 11)).Text(FText::FromString(Axes)) ]
            + SVerticalBox::Slot().AutoHeight()[ Btns ]
        ]
    );
}

// ── Section / toggle helpers ─────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::MakeSection(const FText& Title, const TSharedRef<SWidget>& Content)
{
    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
        [ SNew(STextBlock).Text(Title).ColorAndOpacity(ColLabel) ]
        + SVerticalBox::Slot().AutoHeight()[ Content ]
    ];
}

TSharedRef<SWidget> SPCAPVCamPanel::MakeToggle(const FString& Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
{
    return SNew(SCheckBox)
        .IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
        .OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
        [ SNew(STextBlock).Margin(FMargin(4.f, 0.f, 0.f, 0.f)).Text(FText::FromString(Label)) ];
}

// ── Offsets: Align / Setup / Navigate ────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::OffsetField(int32 OffsetId, bool bRotation, int32 Comp)
{
    auto Resolve = [this, OffsetId]() -> FPCAPVCamAlignOffset*
    {
        UPCAPVCamSubsystem* V = GetVCam();
        UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr;
        if (!C) { return nullptr; }
        return (OffsetId == 0) ? &C->AlignRigidBody : (OffsetId == 1) ? &C->Setup : &C->Navigate;
    };

    return SNew(SSpinBox<float>).MinDesiredWidth(44.f)
        .Value_Lambda([Resolve, bRotation, Comp]()
        {
            FPCAPVCamAlignOffset* O = Resolve();
            if (!O) { return 0.f; }
            if (bRotation) { return (Comp == 0) ? O->Rotation.Pitch : (Comp == 1) ? O->Rotation.Yaw : O->Rotation.Roll; }
            return (Comp == 0) ? O->Translation.X : (Comp == 1) ? O->Translation.Y : O->Translation.Z;
        })
        .OnValueChanged_Lambda([Resolve, bRotation, Comp](float NewVal)
        {
            FPCAPVCamAlignOffset* O = Resolve();
            if (!O) { return; }
            if (bRotation) { if (Comp == 0) O->Rotation.Pitch = NewVal; else if (Comp == 1) O->Rotation.Yaw = NewVal; else O->Rotation.Roll = NewVal; }
            else           { if (Comp == 0) O->Translation.X = NewVal; else if (Comp == 1) O->Translation.Y = NewVal; else O->Translation.Z = NewVal; }
        });
}

TSharedRef<SWidget> SPCAPVCamPanel::BuildOffsetBlock(const FText& Title, int32 OffsetId)
{
    return MakeSection(Title,
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
            [ SNew(STextBlock).Text(LOCTEXT("Rot", "Rot")).ColorAndOpacity(ColText2) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ OffsetField(OffsetId, true, 0) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ OffsetField(OffsetId, true, 1) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ OffsetField(OffsetId, true, 2) ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
            [ SNew(STextBlock).Text(LOCTEXT("Tra", "Tra")).ColorAndOpacity(ColText2) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ OffsetField(OffsetId, false, 0) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ OffsetField(OffsetId, false, 1) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ OffsetField(OffsetId, false, 2) ]
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("ResetOffset", "Reset"))
            .OnClicked_Lambda([this, OffsetId]()
            {
                UPCAPVCamSubsystem* V = GetVCam();
                UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr;
                if (C)
                {
                    const FPCAPVCamAlignOffset Def;
                    if (OffsetId == 0) C->AlignRigidBody = Def; else if (OffsetId == 1) C->Setup = Def; else C->Navigate = Def;
                }
                return FReply::Handled();
            })
        ]
    );
}

TSharedRef<SWidget> SPCAPVCamPanel::BuildOffsetsSection()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)[ BuildOffsetBlock(LOCTEXT("Align", "Align rigid body"), 0) ]
        + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)[ BuildOffsetBlock(LOCTEXT("Setup", "Setup"), 1) ]
        + SHorizontalBox::Slot().FillWidth(1.f)[ BuildOffsetBlock(LOCTEXT("Navigate", "Navigate"), 2) ];
}

// ── Options / modes + Zero Space + smoothing ─────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildModesSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
    [ SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("Zero", "Zero space"))
        .OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S = GetVCam()) S->ZeroSpace(); return FReply::Handled(); }) ];

    Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
    [
        SNew(SUniformGridPanel).SlotPadding(FMargin(4.f))
        + SUniformGridPanel::Slot(0, 0)[ MakeToggle(TEXT("Flight mode"), [this](){ UPCAPVCamSubsystem* S=GetVCam(); return S && S->IsFlightMode(); }, [this](bool b){ if (UPCAPVCamSubsystem* S=GetVCam()) S->SetFlightMode(b); }) ]
        + SUniformGridPanel::Slot(1, 0)[ MakeToggle(TEXT("Hold"),        [this](){ UPCAPVCamSubsystem* S=GetVCam(); return S && S->IsHeld(); },       [this](bool b){ if (UPCAPVCamSubsystem* S=GetVCam()) S->SetHold(b); }) ]
        + SUniformGridPanel::Slot(0, 1)[ MakeToggle(TEXT("Lock position"), [this](){ UPCAPVCamSubsystem* S=GetVCam(); return S && S->IsLockPosition(); }, [this](bool b){ if (UPCAPVCamSubsystem* S=GetVCam()) S->SetLockPosition(b); }) ]
        + SUniformGridPanel::Slot(1, 1)[ MakeToggle(TEXT("Lock rotation"), [this](){ UPCAPVCamSubsystem* S=GetVCam(); return S && S->IsLockRotation(); }, [this](bool b){ if (UPCAPVCamSubsystem* S=GetVCam()) S->SetLockRotation(b); }) ]
        + SUniformGridPanel::Slot(0, 2)[ MakeToggle(TEXT("Lock roll"),   [this](){ UPCAPVCamSubsystem* S=GetVCam(); return S && S->IsLockRoll(); },   [this](bool b){ if (UPCAPVCamSubsystem* S=GetVCam()) S->SetLockRoll(b); }) ]
        + SUniformGridPanel::Slot(1, 2)[ MakeToggle(TEXT("Kill roll"),   [this](){ UPCAPVCamSubsystem* S=GetVCam(); return S && S->IsKillRoll(); },   [this](bool b){ if (UPCAPVCamSubsystem* S=GetVCam()) S->SetKillRoll(b); }) ]
    ];

    Box->AddSlot().AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("SmPos", "Smooth pos")).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
        [ SNew(SSpinBox<float>).MinValue(1.f).MaxValue(20.f).MinDesiredWidth(54.f)
            .Value_Lambda([this]() { UPCAPVCamSubsystem* S=GetVCam(); UPCAPVCamConfig* C=S?S->GetActiveConfig():nullptr; return C?C->Smoothing.PositionSmoothing:10.f; })
            .OnValueChanged_Lambda([this](float v) { UPCAPVCamSubsystem* S=GetVCam(); if (UPCAPVCamConfig* C=S?S->GetActiveConfig():nullptr) C->Smoothing.PositionSmoothing=v; }) ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("SmRot", "Smooth rot")).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SSpinBox<float>).MinValue(1.f).MaxValue(20.f).MinDesiredWidth(54.f)
            .Value_Lambda([this]() { UPCAPVCamSubsystem* S=GetVCam(); UPCAPVCamConfig* C=S?S->GetActiveConfig():nullptr; return C?C->Smoothing.RotationSmoothing:10.f; })
            .OnValueChanged_Lambda([this](float v) { UPCAPVCamSubsystem* S=GetVCam(); if (UPCAPVCamConfig* C=S?S->GetActiveConfig():nullptr) C->Smoothing.RotationSmoothing=v; }) ]
    ];

    return MakeSection(LOCTEXT("ModesSec", "Options / modes"), Box);
}

// ── Lens ─────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildLensSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
            .Text_Lambda([this]() { UPCAPVCamSubsystem* S=GetVCam(); return FText::FromString(S ? FString::Printf(TEXT("%.1f mm"), S->GetCurrentFocalLength()) : TEXT("--")); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("FocDn", "Shorter")).OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->CycleFocalLengthDown(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SButton).Text(LOCTEXT("FocUp", "Longer")).OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->CycleFocalLengthUp(); return FReply::Handled(); }) ]
    ];

    TSharedRef<SHorizontalBox> Presets = SNew(SHorizontalBox);
    UPCAPVCamSubsystem* V = GetVCam();
    if (UPCAPVCamConfig* C = V ? V->GetActiveConfig() : nullptr)
    {
        for (float F : C->FocalLengthPresets)
        {
            Presets->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
            [ SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("%.0f"), F)))
                .OnClicked_Lambda([this, F]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->SetFocalLength(F); return FReply::Handled(); }) ];
        }
    }
    Box->AddSlot().AutoHeight()[ Presets ];

    return MakeSection(LOCTEXT("LensSec", "Lens"), Box);
}

// ── Scaling ──────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildScalingSection()
{
    auto ScaleField = [this](bool bWorld, int32 Comp) -> TSharedRef<SWidget>
    {
        return SNew(SSpinBox<float>).MinValue(0.01f).MaxValue(100.f).MinDesiredWidth(44.f)
            .Value_Lambda([this, bWorld, Comp]()
            {
                UPCAPVCamSubsystem* S = GetVCam(); UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr;
                if (!C) { return 1.f; }
                const FVector& Vec = bWorld ? C->Scaling.WorldSpaceScale : C->Scaling.CameraSpaceScale;
                return (Comp == 0) ? Vec.X : (Comp == 1) ? Vec.Y : Vec.Z;
            })
            .OnValueChanged_Lambda([this, bWorld, Comp](float NewVal)
            {
                UPCAPVCamSubsystem* S = GetVCam(); UPCAPVCamConfig* C = S ? S->GetActiveConfig() : nullptr;
                if (!C) { return; }
                FVector& Vec = bWorld ? C->Scaling.WorldSpaceScale : C->Scaling.CameraSpaceScale;
                if (Comp == 0) Vec.X = NewVal; else if (Comp == 1) Vec.Y = NewVal; else Vec.Z = NewVal;
            });
    };

    return MakeSection(LOCTEXT("ScaleSec", "Scaling"),
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[ SNew(STextBlock).Text(LOCTEXT("World", "World")).ColorAndOpacity(ColText2) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ ScaleField(true, 0) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ ScaleField(true, 1) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ ScaleField(true, 2) ]
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)[ SNew(STextBlock).Text(LOCTEXT("Cam", "Cam")).ColorAndOpacity(ColText2) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ ScaleField(false, 0) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ ScaleField(false, 1) ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f, 0.f)[ ScaleField(false, 2) ]
        ]
    );
}

// ── Saved positions ──────────────────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildNavigationSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("Save", "Save")).OnClicked_Lambda([this]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->SaveCurrentPosition(); RebuildBody(); return FReply::Handled(); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("Prev", "<")).OnClicked_Lambda([this]()
            {
                if (UPCAPVCamSubsystem* S=GetVCam()) { if (UPCAPVCamConfig* C=S->GetActiveConfig()) { if (C->SavedPositions.Num()>0) S->GotoSavedPosition(FMath::Max(0, C->ActiveSavedPositionIndex-1)); } }
                return FReply::Handled();
            }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(SButton).Text(LOCTEXT("Next", ">")).OnClicked_Lambda([this]()
            {
                if (UPCAPVCamSubsystem* S=GetVCam()) { if (UPCAPVCamConfig* C=S->GetActiveConfig()) { if (C->SavedPositions.Num()>0) S->GotoSavedPosition(FMath::Min(C->SavedPositions.Num()-1, C->ActiveSavedPositionIndex+1)); } }
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
            const FString Lbl = FString::Printf(TEXT("[%d]  %.0f %.0f %.0f"), i, Pos.Translation.X, Pos.Translation.Y, Pos.Translation.Z);
            List->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ SNew(STextBlock).Text(FText::FromString(Lbl)).ColorAndOpacity(ColText2) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)[ SNew(SButton).Text(LOCTEXT("Go", "Go")).OnClicked_Lambda([this, i]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->GotoSavedPosition(i); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)[ SNew(SButton).Text(LOCTEXT("Del", "X")).OnClicked_Lambda([this, i]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->DeleteSavedPosition(i); RebuildBody(); return FReply::Handled(); }) ]
            ];
        }
    }
    Box->AddSlot().AutoHeight()[ List ];

    return MakeSection(LOCTEXT("NavSec", "Saved positions"), Box);
}

// ── Controller (layout + gain) ───────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildControllerSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

    Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("Layout", "Layout")).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [
            SNew(SComboButton)
            .ButtonContent()
            [ SNew(STextBlock).Text_Lambda([this]()
                {
                    UPCAPVCamSubsystem* S=GetVCam(); UPCAPVCamConfig* C=S?S->GetActiveConfig():nullptr;
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
                        FUIAction(FExecuteAction::CreateLambda([this, Idx]() { if (UPCAPVCamSubsystem* S=GetVCam()) S->SetActiveButtonLayout(Idx); })));
                }
                return MB.MakeWidget();
            })
        ]
    ];

    Box->AddSlot().AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("TG", "Translation gain")).ColorAndOpacity(ColText2) ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [ SNew(SSpinBox<float>).MinValue(0.01f).MaxValue(1.f)
            .Value_Lambda([this]() { UPCAPVCamSubsystem* S=GetVCam(); return S ? S->GetTranslationGain() : 0.5f; })
            .OnValueChanged_Lambda([this](float v) { if (UPCAPVCamSubsystem* S=GetVCam()) S->SetTranslationGain(v); }) ]
    ];

    return MakeSection(LOCTEXT("CtlSec", "Controller"), Box);
}

// ── Transformer output readout ───────────────────────────────────────────────

TSharedRef<SWidget> SPCAPVCamPanel::BuildOutputSection()
{
    return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(8.f, 6.f))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
        [ SNew(STextBlock).Text(LOCTEXT("Out", "Output")).ColorAndOpacity(ColLabel) ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Mono", 11))
            .Text_Lambda([this]()
            {
                UPCAPVCamSubsystem* S = GetVCam();
                if (!S) { return FText::FromString(TEXT("--")); }
                const FTransform T = S->GetCurrentTransform();
                const FVector P = T.GetLocation();
                const FRotator R = T.Rotator();
                return FText::FromString(FString::Printf(
                    TEXT("T  %.1f  %.1f  %.1f       R  %.1f  %.1f  %.1f"),
                    P.X, P.Y, P.Z, R.Pitch, R.Yaw, R.Roll));
            }) ]
    ];
}

#undef LOCTEXT_NAMESPACE
