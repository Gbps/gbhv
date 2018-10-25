#include "exit.h"

/**
 * Dispatch to the correct handler function given the exit code.
 */
BOOL HvExitDispatchFunction(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	VMX_ERROR VmError;
	SIZE_T GuestInstructionLength;

	VmError = 0;

	/*
	 * Choose an appropriate function to handle our exit.
	 */
	switch(ExitContext->ExitReason.BasicExitReason)
	{
		/*
		 * The following instructions cause VM exits when they are executed in VMX non-root operation: CPUID, GETSEC,1
		 * INVD, and XSETBV. This is also true of instructions introduced with VMX, which include: INVEPT, INVVPID,
		 * VMCALL,2 VMCLEAR, VMLAUNCH, VMPTRLD, VMPTRST, VMRESUME, VMXOFF, and VMXON.
		 * 
		 * GETSEC will never exit because we will never run in SMX mode.
		 */
	case VMX_EXIT_REASON_EXECUTE_CPUID:
		HvExitHandleCpuid(ProcessorContext, ExitContext);
		break;
	case VMX_EXIT_REASON_EXECUTE_INVD:
		__wbinvd();
		break;
	case VMX_EXIT_REASON_EXECUTE_XSETBV:
		_xsetbv((UINT32)ExitContext->GuestContext->GuestRCX,
			ExitContext->GuestContext->GuestRDX << 32 |
			ExitContext->GuestContext->GuestRAX);
		break;
	}


	VmxVmreadFieldToImmediate(VMCS_VMEXIT_INSTRUCTION_LENGTH, &GuestInstructionLength);

	ExitContext->GuestRIP += GuestInstructionLength;

	VmxVmwriteFieldFromImmediate(VMCS_GUEST_RIP, ExitContext->GuestRIP);

	return TRUE;
}

BOOL HvExitHandleCpuid(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	INT32 CPUInfo[4];

	UNREFERENCED_PARAMETER(ProcessorContext);

	// Perform actual CPUID
	__cpuidex(CPUInfo, (int)ExitContext->GuestContext->GuestRAX, (int)ExitContext->GuestContext->GuestRCX);

	// If guest is requesting version information
	if(ExitContext->GuestContext->GuestRAX == CPUID_VERSION_INFORMATION)
	{
		// Tell them that VMX is not a supported feature of our virtual processor.
		CPUInfo[2] = (INT32)HvUtilBitClearBit(CPUInfo[2], CPUID_VMX_ENABLED_BIT);
	}

	/*
	 * Give guest the results of the CPUID call.
	 */
	ExitContext->GuestContext->GuestRAX = CPUInfo[0];
	ExitContext->GuestContext->GuestRBX = CPUInfo[1];
	ExitContext->GuestContext->GuestRCX = CPUInfo[2];
	ExitContext->GuestContext->GuestRDX = CPUInfo[3];

	return TRUE;
}