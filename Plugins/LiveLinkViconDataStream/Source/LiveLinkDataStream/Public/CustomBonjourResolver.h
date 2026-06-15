#pragma once

#if defined(_WIN32)

  #include "Containers/UnrealString.h"
  #include "Misc/Optional.h"
  #include "NSDCommon.h"

using THostNameAndPort = TTuple<FString, uint32_t>;

namespace MDNS
{
  // Return service hostname and portnumber. Dssdk will resolve the ip address for the hostname.
  TOptional<THostNameAndPort> ResolveDottedServerAddress(const FNSDService& i_rService);
}

#endif
