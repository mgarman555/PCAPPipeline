#pragma once

#include "Containers/UnrealString.h"

class LIVELINKDATASTREAM_API FNSDService
{
public:
  FNSDService(const FString& i_rName, const FString& i_rType, const FString& i_rDomain);
  FString ToString();
  FString m_Name;
  FString m_Type;
  FString m_Domain;

  bool operator==(const FNSDService& i_rOther) const
  {
    return m_Name == i_rOther.m_Name &&
           m_Type == i_rOther.m_Type &&
           m_Domain == i_rOther.m_Domain;
  }
};

LIVELINKDATASTREAM_API FString BonjourErrorMessage(int32_t i_rError);
