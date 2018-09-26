#pragma once

#include <ntdef.h>
#include <intrin.h>
#include "bitfield.h"

SIZE_T ArchGetHostMSR(ULONG MsrAddress);

BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit);