// Copyright (c) 2021 Vicon Motion Systems Ltd. All Rights Reserved.

#include "LiveLinkViconDataStreamSourceEditor.h"
#include "LiveLinkViconDataStreamSourcePropEditor.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "LiveLinkViconDataStreamSourceEditor"

SLiveLinkViconDataStreamSourceEditor::~SLiveLinkViconDataStreamSourceEditor()
{
}

void SLiveLinkViconDataStreamSourceEditor::Construct(const FArguments& Args)
{
  //  CurrentPollRequest = FGuid::NewGuid();
  OnPropertiesSelected = Args._OnPropertiesSelected;

  PortNumber = 801;

  ChildSlot
    [SNew(SBox).HeightOverride(215).WidthOverride(250)
       [SNew(SVerticalBox) +
        SVerticalBox::Slot().AutoHeight().Padding(2.0f)
          [SNew(SHorizontalBox) +
           SHorizontalBox::Slot()
             .HAlign(HAlign_Left)
             .FillWidth(0.5f)[SNew(STextBlock).Text(LOCTEXT("ViconServerName", "Vicon Server Name"))] +
           SHorizontalBox::Slot()
             .HAlign(HAlign_Fill)
             .FillWidth(0.5f)[SAssignNew(ServerName, SEditableTextBox)
                                .Text(LOCTEXT("UndeterminedViconServerName", "localhost"))]] +
        SVerticalBox::Slot().AutoHeight().Padding(2.0f)
          [SNew(SHorizontalBox) +
           SHorizontalBox::Slot()
             .HAlign(HAlign_Left)
             .FillWidth(0.5f)[SNew(STextBlock).Text(LOCTEXT("ViconPortNumber", "Port Number"))] +
           SHorizontalBox::Slot()
             .HAlign(HAlign_Fill)
             .FillWidth(
               0.5f)[SNew(SNumericEntryBox<uint32>)
                       .Value(this, &SLiveLinkViconDataStreamSourceEditor::OnGet_PortNumber_EntryBoxValue)
                       .OnValueChanged(this, &SLiveLinkViconDataStreamSourceEditor::On_PortNumber_EntryBoxChanged)]] +
        SVerticalBox::Slot()
          .AutoHeight()
          .Padding(2.0)
            [SAssignNew(ConnectionProperties, SLiveLinkViconDataStreamSourcePropEditor)] +
        SVerticalBox::Slot().AutoHeight().Padding(
          2.0f)[SNew(SButton)
                  .Text(LOCTEXT("Create", "Create"))
                  .OnClicked(this, &SLiveLinkViconDataStreamSourceEditor::CreateSource)]]];
}

FReply SLiveLinkViconDataStreamSourceEditor::CreateSource() const
{
  ViconStreamProperties Properties;
  Properties.m_ServerName = GetServerName();
  Properties.m_PortNumber = GetPortNumber();

  Properties.m_SubjectFilter = ConnectionProperties->GetSubjectFilter();
  Properties.m_bRetimed = ConnectionProperties->GetIsRetimed();
  Properties.m_bLogOutput = ConnectionProperties->GetLogOutput();
  Properties.m_bUsePrefetch = ConnectionProperties->GetUsePrefetch();
  Properties.m_RetimeOffset = ConnectionProperties->GetOffset();

  Properties.m_bScaled = true;

  OnPropertiesSelected.ExecuteIfBound(Properties);

  return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
