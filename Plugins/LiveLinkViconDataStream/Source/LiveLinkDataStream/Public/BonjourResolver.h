#pragma once

#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Misc/Optional.h"
#include "NSDCommon.h"
#include "Templates/Tuple.h"


DECLARE_LOG_CATEGORY_CLASS(LogBonjourResolver, Display, All)

using THostNameAndPort = TTuple<FString, uint32_t>;

// Return service hostname and portnumber. Dssdk will resolve the ip address for the hostname.
TOptional<THostNameAndPort> ResolveServerAddress(const FNSDService& i_rService);
