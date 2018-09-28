#include "vmm.h"

#include <wdm.h>
#include "vmx.h"

/**
 * Call HvInitializeLogicalProcessor on all processors using an Inter-Process Interrupt (IPI).
 * 
 * All processors will stop executing until all processors have entered VMX root-mode.
 */
BOOL HvInitializeAllProcessors()
{
	SIZE_T FeatureMSR;

	HvUtilLog("HvInitializeAllProcessors: Starting.");

	// Check if VMX support is enabled on the processor.
	if (!ArchIsVMXAvailable())
	{
		HvUtilLogError("VMX is not a feture of this processor.");
		return FALSE;
	}

	// Enable bits in MSR to enable VMXON instruction.
	FeatureMSR = ArchGetHostMSR(MSR_IA32_FEATURE_CONTROL_ADDRESS);

	// The BIOS will lock the VMX bit on startup.
	if(!HvUtilBitIsSet(FeatureMSR, FEATURE_BIT_VMX_LOCK))
	{
		HvUtilLogError("VMX support was not locked by BIOS.");
		return FALSE;
	}

	// VMX support can be configured to be disabled outside SMX.
	// Check to ensure this isn't the case.
	if (!HvUtilBitIsSet(FeatureMSR, FEATURE_BIT_ALLOW_VMX_OUTSIDE_SMX))
	{
		HvUtilLogError("VMX support was disabled outside of SMX operation by BIOS.");
		return FALSE;
	}

	HvUtilLog("Total Processor Count: %i", OsGetCPUCount());

	// Allocate array of processor contexts
	PVMX_PROCESSOR_CONTEXT* ProcessorContexts = OsAllocateNonpagedMemory(OsGetCPUCount() * sizeof(PVMX_PROCESSOR_CONTEXT));

	for(SIZE_T ProcessorNumber = 0; ProcessorNumber < OsGetCPUCount(); ProcessorNumber++)
	{
		ProcessorContexts[ProcessorNumber] = HvAllocateLogicalProcessorContext();
		if (ProcessorContexts[ProcessorNumber] == NULL)
		{
			HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to setup processor context.", ProcessorNumber);
			return FALSE;
		}

		HvUtilLog("HvInitializeLogicalProcessor[#%i]: Allocated Context [Context = 0x%llx/0x%llx]", ProcessorNumber, ProcessorContexts[ProcessorNumber]);
	}

	// Generates an IPI that signals all processors to execute the broadcast function.
	KeIpiGenericCall(HvpIPIBroadcastFunction, (ULONG_PTR)ProcessorContexts);

	HvUtilLogSuccess("HvInitializeAllProcessors: Success.");
	return TRUE;
}

/*
 * Allocate and setup the VMXON region for the logical processor context.
 */
PVMXON_REGION HvAllocateVmxonRegion()
{
	PVMXON_REGION Region;
	IA32_VMX_BASIC_REGISTER VMXBasicCapabilities;

	// See VMX_VMXON_NUMBER_PAGES documentation.
	Region = (PVMXON_REGION)OsAllocateContiguousAlignedPages(VMX_VMXON_NUMBER_PAGES);

	// Zero VMXON region just to be sure...
	OsZeroMemory(Region, sizeof(VMX_PROCESSOR_CONTEXT));

	VMXBasicCapabilities = ArchGetBasicVmxCapabilities();

	/*
	 * Initialize the version identifier in the VMXON region (the first 31 bits) with the VMCS revision identifier
	 * reported by capability MSRs.
	 *
	 * Clear bit 31 of the first 4 bytes of the VMXON region. (Handled by OsZeroMemory above)
	 */
	Region->VmcsRevisionNumber = (UINT32)VMXBasicCapabilities.VmcsRevisionId;

	HvUtilLog("VmcsRevisionNumber: %x", VMXBasicCapabilities.VmcsRevisionId);

	return Region;
}

/**
 * Allocates a logical processor context.
 * 
 * Returns NULL on error.
 */
PVMX_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext()
{
	PVMX_PROCESSOR_CONTEXT Context;

	// Allocate some generic memory for our context
	Context = (PVMX_PROCESSOR_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMX_PROCESSOR_CONTEXT));
	if(!Context)
	{
		return NULL;
	}

	// Allocate and setup the VMXON region for this processor
	Context->VmxonRegion = HvAllocateVmxonRegion();
	if(!Context->VmxonRegion)
	{
		return NULL;
	}

	Context->VmxonRegionPhysical = OsVirtualToPhysical(Context->VmxonRegion);
	if (!Context->VmxonRegionPhysical)
	{
		return NULL;
	}

	return Context;
}

/*
 * Free a logical processor context allocated by HvAllocateLogicalProcessorContext
 */
VOID HvFreeLogicalProcessorContext(PVMX_PROCESSOR_CONTEXT Context)
{
	if(Context)
	{
		OsFreeContiguousAlignedPages(Context->VmxonRegion);
		OsFreeNonpagedMemory(Context);
	}
}

/**
 * Called by HvInitializeAllProcessors to initialize VMX on all processors.
 */
ULONG_PTR HvpIPIBroadcastFunction(_In_ ULONG_PTR Argument)
{
	SIZE_T CurrentProcessorNumber;
	PVMX_PROCESSOR_CONTEXT* ProcessorContexts;
	PVMX_PROCESSOR_CONTEXT CurrentContext;

	ProcessorContexts = (PVMX_PROCESSOR_CONTEXT*)Argument;

	// Get the current processor number we're executing this function on right now
	CurrentProcessorNumber = OsGetCurrentProcessorNumber();

	// Get the logical processor context that was allocated for this current processor
	CurrentContext = ProcessorContexts[CurrentProcessorNumber];

	// Initialize processor for VMX
	HvInitializeLogicalProcessor(CurrentContext);

	// TODO: Free at APC level. 
	// HvFreeLogicalProcessorContext(Context);

	return 0;
}

/**
 * Initialize VMCS and enter VMX root-mode.
 */
VOID HvInitializeLogicalProcessor(PVMX_PROCESSOR_CONTEXT Context)
{
	SIZE_T CurrentProcessorNumber;

	// Get the current processor we're executing this function on right now
	CurrentProcessorNumber = OsGetCurrentProcessorNumber();

	// Enable VMXe, execute VMXON and enter VMX root mode
	if (!VmxEnterRootMode(Context))
	{
		HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to execute VMXON.", CurrentProcessorNumber);
		return;
	}
	
	HvUtilLogSuccess("HvInitializeLogicalProcessor[#%i]: Successfully entered VMX Root Mode.", CurrentProcessorNumber);
	
	__vmx_off();
}
