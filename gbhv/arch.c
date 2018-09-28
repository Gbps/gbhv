#pragma once

#include "arch.h"
#include "util.h"

/**
 * Get an MSR by its address and convert it to the specified type.
 */
SIZE_T ArchGetHostMSR(ULONG MsrAddress)
{
	return __readmsr(MsrAddress);
}

/*
 * Returns TRUE if the CPU feature is present.
 */
BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit)
{
	INT32 CPUInfo[4];

	__cpuidex(CPUInfo, FunctionId, SubFunctionId);

	return HvUtilBitIsSet(CPUInfo[CPUIDRegister], FeatureBit);
}
