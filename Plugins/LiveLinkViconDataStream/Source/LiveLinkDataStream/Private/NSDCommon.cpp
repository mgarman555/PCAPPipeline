///////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) OMG Plc 2015.
// All rights reserved.  This software is protected by copyright
// law and international treaties.  No part of this software / document
// may be reproduced or distributed in any form or by any means,
// whether transiently or incidentally to some other use of this software,
// without the written permission of the copyright owner.
//
///////////////////////////////////////////////////////////////////////////////

#include "NSDCommon.h"


FNSDService::FNSDService(
  const FString& i_rName, const FString& i_rType, const FString& i_rDomain)
  : m_Name(i_rName)
  , m_Type(i_rType)
  , m_Domain(i_rDomain)
{
}

FString FNSDService::ToString()
{
  return FString::Printf(TEXT("%s.%s%s"), *m_Name, *m_Type, *m_Domain);
}

#if defined(__APPLE__)

  #include "dns_sd.h"

FString BonjourErrorMessage(int32_t i_rError)
{
  switch (i_rError)
  {
    case kDNSServiceErr_NoError:
      // Probably should not be here.
      return "No Error";
    case kDNSServiceErr_Unknown:
      return "Unknown Error";
    case kDNSServiceErr_NoSuchName:
      return "No Such Name";
    case kDNSServiceErr_NoMemory:
      return "No Memory";
    case kDNSServiceErr_BadParam:
      return "Bad Param";
    case kDNSServiceErr_BadReference:
      return "Bad Reference";
    case kDNSServiceErr_BadState:
      return "Bad State";
    case kDNSServiceErr_BadFlags:
      return "Bad Flags";
    case kDNSServiceErr_Unsupported:
      return "Unsupported";
    case kDNSServiceErr_NotInitialized:
      return "Not Initialized";
    case kDNSServiceErr_AlreadyRegistered:
      return "Already Registered";
    case kDNSServiceErr_NameConflict:
      return "Name Conflict";
    case kDNSServiceErr_Invalid:
      return "Invalid";
    case kDNSServiceErr_Firewall:
      return "Firewall";
    case kDNSServiceErr_Incompatible:
      return "Incompatible - Client library incompatible with daemon";
    case kDNSServiceErr_BadInterfaceIndex:
      return "Bad Interface Index";
    case kDNSServiceErr_Refused:
      return "Refused";
    case kDNSServiceErr_NoSuchRecord:
      return "No Such Record";
    case kDNSServiceErr_NoAuth:
      return "No Auth";
    case kDNSServiceErr_NoSuchKey:
      return "No Such Key";
    case kDNSServiceErr_NATTraversal:
      return "NAT Traversal";
    case kDNSServiceErr_DoubleNAT:
      return "Double NAT";
    case kDNSServiceErr_BadSig:
      return "Bad Sig";
    case kDNSServiceErr_BadKey:
      return "Bad Key";
    case kDNSServiceErr_Transient:
      return "Transient";
    case kDNSServiceErr_ServiceNotRunning:
      return "Service Not Running - Background daemon not running";
    case kDNSServiceErr_NATPortMappingUnsupported:
      return "NAT Port Mapping Unsupported - NAT doesn't support PCP, NAT-PMP or UPnP";
    case kDNSServiceErr_NATPortMappingDisabled:
      return "NAT Port Mapping Disabled - NAT supports PCP, NAT-PMP or UPnP, but it's disabled by the administrator";
    case kDNSServiceErr_NoRouter:
      return "No Router - No router currently configured (probably no network connectivity)";
    case kDNSServiceErr_PollingMode:
      return "Polling Mode";
    case kDNSServiceErr_Timeout:
      return "Timeout";
    default:
      return "Unrecognised Error";
  }
}

#else

FString BonjourErrorMessage(int32_t i_Error)
{
  return FString::Printf(TEXT("DNS-SD Error: %d"), i_Error);
}

#endif
