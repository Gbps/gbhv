#include "vmm.h"

#include <wdm.h>

/**
 * Call HvInitialize on all processors using an Inter-Process Interrupt (IPI).
 * 
 * All processors will stop executing until all processors have entered VMX root-mode.
 */
BOOL HvInitializeAllProcessors()
{
	SIZE_T FeatureMSR;

	// Check that the CPU supports VMX
	FeatureMSR = ArchGetHostMSR(MSR_IA32_FEATURE_CONTROL_ADDRESS);

	if (!ArchIsCPUFeaturePresent(1, 0, 2, 5))
	{
		HvLogError("VMX feature is not present on this processor.");
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
