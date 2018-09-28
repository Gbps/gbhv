#pragma once

#include "arch.h"
#include "util.h"
#include "vmx.h"

/**
 * Get an MSR by its address and convert it to the specified type.
 */
SIZE_T ArchGetHostMSR(ULONG MsrAddress)
{
	return __readmsr(MsrAddress);
}

/*
 * Get a CPUID register.
 */
UINT32 ArchGetCPUID(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister)
{
	INT32 CPUInfo[4];

	__cpuidex(CPUInfo, FunctionId, SubFunctionId);

	return (UINT32)CPUInfo[CPUIDRegister];
}

/*
 * Returns TRUE if the CPU feature is present.
 */
BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit)
{
	UINT32 Register;

	Register = ArchGetCPUID(FunctionId, SubFunctionId, CPUIDRegister);

	return HvUtilBitIsSet(Register, FeatureBit);
}

/*
 * Check if VMX support is enabled on the processor.
 */
BOOL ArchIsVMXAvailable()
{
	return ArchIsCPUFeaturePresent(CPUID_VMX_ENABLED_FUNCTION,
									CPUID_VMX_ENABLED_SUBFUNCTION,
									CPUID_REGISTER_ECX,
									CPUID_VMX_ENABLED_BIT);
}

/*
 * Get the IA32_VMX_BASIC MSR.
 * 
 * Reporting Register of Basic VMX Capabilities.
 */
IA32_VMX_BASIC_REGISTER ArchGetMSR_BasicVmxCapabilities()
{
	IA32_VMX_BASIC_REGISTER Register;

	Register.Flags = ArchGetHostMSR(IA32_VMX_BASIC);

#define DEBUG_PRINT_STRUCT_NAME(_STRUCT_NAME_) HvUtilLogDebug(#_STRUCT_NAME_ ": ")
#define DEBUG_PRINT_STRUCT_MEMBER(_STRUCT_MEMBER_) HvUtilLogDebug("    " #_STRUCT_MEMBER_ ": %i [0x%X]", Register._STRUCT_MEMBER_, Register._STRUCT_MEMBER_)

	DEBUG_PRINT_STRUCT_NAME(IA32_VMX_BASIC_REGISTER);
	DEBUG_PRINT_STRUCT_MEMBER(VmcsRevisionId);
	DEBUG_PRINT_STRUCT_MEMBER(MustBeZero);
	DEBUG_PRINT_STRUCT_MEMBER(VmcsSizeInBytes);
	DEBUG_PRINT_STRUCT_MEMBER(Reserved1);
	DEBUG_PRINT_STRUCT_MEMBER(VmcsPhysicalAddressWidth);
	DEBUG_PRINT_STRUCT_MEMBER(DualMonitorSupport);
	DEBUG_PRINT_STRUCT_MEMBER(MemoryType);
	DEBUG_PRINT_STRUCT_MEMBER(InsOutsReporting);
	DEBUG_PRINT_STRUCT_MEMBER(VmxControls);
	DEBUG_PRINT_STRUCT_MEMBER(Reserved2);

	return Register;
}