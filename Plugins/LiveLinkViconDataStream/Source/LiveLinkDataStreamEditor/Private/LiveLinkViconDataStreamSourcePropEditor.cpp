// Copyright (c) 2024 Vicon Motion Systems Ltd. All Rights Reserved.

#include "LiveLinkViconDataStreamSourcePropEditor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "LiveLinkViconDataStreamSourcePropEditor"

SLiveLinkViconDataStreamSourcePropEditor::~SLiveLinkViconDataStreamSourcePropEditor()
{
}

void SLiveLinkViconDataStreamSourcePropEditor::Construct(const FArguments& Args)
{
  Offset = 0.0f;

  // clang-format off
  ChildSlot
  [
    SNew(SBox)
    [
      SNew(SVerticalBox)
      +SVerticalBox::Slot()
      .AutoHeight()
      .Padding(8.0f, 4.0f, 8.0f, 4.0f)
      [
        SNew(SSeparator)
      ] 
      +SVerticalBox::Slot()
      .AutoHeight()
      .Padding(2.0f)
      [
        SNew(SHorizontalBox)
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
          SNew(STextBlock)
          .Text(LOCTEXT("UsePreFetch", "Use PreFetch"))
        ]
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
           SAssignNew(UsePreFetch, SCheckBox)
           .IsChecked(ECheckBoxState::Unchecked)
        ]
      ]
      +SVerticalBox::Slot()
      .AutoHeight()
      .Padding(2.0f)
      [
        SNew(SHorizontalBox)
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
          SNew(STextBlock)
          .Text(LOCTEXT("IsRetimed", "Is Retimed"))
        ]
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
          SAssignNew(IsRetimed, SCheckBox)
          .IsChecked(ECheckBoxState::Unchecked)
        ]
      ]
      +SVerticalBox::Slot()
      .AutoHeight()
      .Padding(2.0f)
      [
        SNew(SHorizontalBox)
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.1f)
        [
           SNew(SBox)
        ]
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.4f)
        [
          SNew(STextBlock)
          .Text(LOCTEXT("Offset", "Offset"))
        ]
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Fill)
        .FillWidth(0.5f)
        [
          SNew(SNumericEntryBox<float>)
          .Value(this, &SLiveLinkViconDataStreamSourcePropEditor::OnGetOffsetEntryBoxValue)
          .OnValueChanged(this, &SLiveLinkViconDataStreamSourcePropEditor::OnOffsetEntryBoxChanged)
        ]
      ] 
      +SVerticalBox::Slot()
      .AutoHeight()
      .Padding(2.0f)
      [
        SNew(SHorizontalBox)
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
          SNew(STextBlock)
          .Text(LOCTEXT("LogOutput", "Log Output"))
        ]
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
          SAssignNew(LogOutput, SCheckBox)
          .IsChecked(ECheckBoxState::Unchecked)
        ]
      ]
      +SVerticalBox::Slot()
      .AutoHeight()
      .Padding(2.0f)
      [
        SNew(SHorizontalBox)
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Left)
        .FillWidth(0.5f)
        [
          SNew(STextBlock)
          .Text(LOCTEXT("Subject Filter", "Subject Filter"))
        ]
        +SHorizontalBox::Slot()
        .HAlign(HAlign_Fill)
        .FillWidth(0.5f)
        [
          SAssignNew(SubjectFilter, SEditableTextBox).Text(LOCTEXT("EmptyFilter", ""))
        ]
      ]
    ]
  ];
  // clang-format on
}

FText SLiveLinkViconDataStreamSourcePropEditor::GetSubjectFilter() const
{
  return SubjectFilter.Get()->GetText();
}

bool SLiveLinkViconDataStreamSourcePropEditor::GetIsRetimed() const
{
  return IsRetimed->IsChecked();
}

float SLiveLinkViconDataStreamSourcePropEditor::GetOffset() const
{
  return Offset.IsSet() ? Offset.GetValue() : 0.0f;
}

bool SLiveLinkViconDataStreamSourcePropEditor::GetLogOutput() const
{
  return LogOutput->IsChecked();
}

bool SLiveLinkViconDataStreamSourcePropEditor::GetUsePrefetch() const
{
  return UsePreFetch->IsChecked();
}

void SLiveLinkViconDataStreamSourcePropEditor::OnOffsetEntryBoxChanged(float NewValue)
{
  Offset = NewValue;
}

TOptional<float> SLiveLinkViconDataStreamSourcePropEditor::OnGetOffsetEntryBoxValue() const
{
  return Offset;
}

#undef LOCTEXT_NAMESPACE
