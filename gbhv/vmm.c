#include "vmm.h"

#include <wdm.h>
#include "vmx.h"

/**
 * Call HvInitialize on all processors using an Inter-Process Interrupt (IPI).
 * 
 * All processors will stop executing until all processors have entered VMX root-mode.
 */
BOOL HvInitializeAllProcessors()
{
	SIZE_T FeatureMSR;

	// Check if VMX support is enabled on the processor.
	if (!ArchIsCPUFeaturePresent(CPUID_VMX_ENABLED_FUNCTION, 
			CPUID_VMX_ENABLED_SUBFUNCTION, 
			CPUID_REGISTER_ECX, 
			CPUID_VMX_ENABLED_BIT)
		)
	{
		HvLogError("VMX feature is not present on this processor.");
		return FALSE;
	}


	// Enable bits in MSR to enable VMXON instruction.
	FeatureMSR = ArchGetHostMSR(MSR_IA32_FEATURE_CONTROL_ADDRESS);

	// The BIOS will lock the VMX bit on startup.
	if(!HvUtilBitIsSet(FeatureMSR, FEATURE_LOCK_BIT))
	{
		HvLogError("VMX support was not locked by BIOS.");
		return FALSE;
	}

	// VMX support can be configured to be disabled outside SMX.
	// Check to ensure this isn't the case.
	if (!HvUtilBitIsSet(FeatureMSR, FEATURE_ALLOW_OUTSIDE_SMX_OPERATION))
	{
		HvLogError("VMX support was disabled outside of SMX operation.");
		return FALSE;
	}
	return TRUE;

	// Generates an IPI that signals all processors to execute the broadcast function.
	// KeIpiGenericCall(HvpIPIBroadcastFunction, 0);
}

/**
 * Called by HvInitializeAllProcessors to initialize VMX on all processors.
 */
ULONG_PTR HvpIPIBroadcastFunction(_In_ ULONG_PTR Argument)
{
	// Initialize VMX
	HvInitialize();

	return 0;
}

/**
 * Initialize VMCS and enter VMX root-mode.
 */
VOID HvInitialize()
{
}
