// Copyright (c) 2021 Vicon Motion Systems Ltd. All Rights Reserved.

// =========================================================================
// UI for LiveLink connection to a Vicon server
//
// =========================================================================

#pragma once

#include "Misc/Guid.h"
#include "ViconStreamFrameReader.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FOnDataStreamPropertiesSelected, ViconStreamProperties);

class SLiveLinkViconDataStreamSourcePropEditor;
class SLiveLinkViconDataStreamSourceEditor : public SCompoundWidget
{
  SLATE_BEGIN_ARGS(SLiveLinkViconDataStreamSourceEditor)
  {
  }

  SLATE_EVENT(FOnDataStreamPropertiesSelected, OnPropertiesSelected)

  SLATE_END_ARGS()

  ~SLiveLinkViconDataStreamSourceEditor();

  void Construct(const FArguments& Args);

  FText GetServerName() const { return ServerName.Get()->GetText(); }
  uint32 GetPortNumber() const { return PortNumber.IsSet() ? PortNumber.GetValue() : 801; }

private:
  TSharedPtr<SEditableTextBox> ServerName;
  TOptional<uint32> PortNumber;
  void On_PortNumber_EntryBoxChanged(uint32 NewValue)
  {
    PortNumber = NewValue;
  }

  TOptional<uint32> OnGet_PortNumber_EntryBoxValue() const
  {
    return PortNumber;
  }

  FReply CreateSource() const;

  FOnDataStreamPropertiesSelected OnPropertiesSelected;

  TSharedPtr<SLiveLinkViconDataStreamSourcePropEditor> ConnectionProperties;
};
