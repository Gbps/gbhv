#pragma once

#include "extern.h"

SIZE_T ArchGetHostMSR(ULONG MsrAddress);

UINT32 ArchGetCPUID(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister);

BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit);

BOOL ArchIsVMXAvailable();

IA32_VMX_BASIC_REGISTER ArchGetBasicVmxCapabilities();

VOID ArchEnableVmxe();
VOID ArchDisableVmxe();

#define DEBUG_PRINT_STRUCT_NAME(_STRUCT_NAME_) HvUtilLogDebug(#_STRUCT_NAME_ ": ")
#define DEBUG_PRINT_STRUCT_MEMBER(_STRUCT_MEMBER_) HvUtilLogDebug("    " #_STRUCT_MEMBER_ ": %i [0x%X]", Register._STRUCT_MEMBER_, Register._STRUCT_MEMBER_)
