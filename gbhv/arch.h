#pragma once

#include "extern.h"

SIZE_T ArchGetHostMSR(ULONG MsrAddress);

UINT32 ArchGetCPUID(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister);

BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit);

BOOL ArchIsVMXAvailable();

IA32_VMX_BASIC_REGISTER ArchGetMSR_BasicVmxCapabilities();