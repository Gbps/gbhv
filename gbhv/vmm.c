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
	PVMM_CONTEXT GlobalContext;

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

	// Pre-allocate all logical processor contexts, VMXON regions, VMCS regions
	GlobalContext = HvAllocateVmmContext();

	// Generates an IPI that signals all processors to execute the broadcast function.
	KeIpiGenericCall(HvpIPIBroadcastFunction, (ULONG_PTR)GlobalContext);

	HvUtilLogSuccess("HvInitializeAllProcessors: Success.");
	return TRUE;
}

/*
 * Allocate the global VMM context used by all processors.
 * 
 * Allocates a logical processor context structure for each logical processor on the system.
 * 
 * Accesses capability MSRs to get information about the VMX execution environment.
 */
PVMM_CONTEXT HvAllocateVmmContext()
{
	PVMM_CONTEXT Context;

	// Allocate the global context structure
	Context = (PVMM_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMM_CONTEXT));
	if(!Context)
	{
		return NULL;
	}

	// Get count of all logical processors on the system
	Context->ProcessorCount = OsGetCPUCount();

	PVMX_PROCESSOR_CONTEXT* ProcessorContexts = OsAllocateNonpagedMemory(Context->ProcessorCount * sizeof(PVMX_PROCESSOR_CONTEXT));
	if(!ProcessorContexts)
	{
		return NULL;
	}

	/*
	 * Allocate a logical processor context structure for each processor on the system.
	 */
	for (SIZE_T ProcessorNumber = 0; ProcessorNumber < Context->ProcessorCount; ProcessorNumber++)
	{
		ProcessorContexts[ProcessorNumber] = HvAllocateLogicalProcessorContext(Context);
		if (ProcessorContexts[ProcessorNumber] == NULL)
		{
			HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to setup processor context.", ProcessorNumber);
			return NULL;
		}

		HvUtilLog("HvInitializeLogicalProcessor[#%i]: Allocated Context [Context = 0x%llx]", ProcessorNumber, ProcessorContexts[ProcessorNumber]);
	}

	Context->AllProcessorContexts = ProcessorContexts;

	/*
	 * Get capability MSRs and add them to the global context.
	 */
	Context->VmxCapabilities = ArchGetBasicVmxCapabilities();

	HvUtilLog("VmcsRevisionNumber: %x", Context->VmxCapabilities.VmcsRevisionId);

	return Context;
}

/*
 * Free global VMM context and all logical processor contexts.
 */
VOID HvFreeVmmContext(PVMM_CONTEXT Context)
{
	if(Context)
	{
		// Free each logical processor context
		for(SIZE_T ProcessorNumber = 0; ProcessorNumber < Context->ProcessorCount; ProcessorNumber++)
		{
			HvFreeLogicalProcessorContext(Context->AllProcessorContexts[ProcessorNumber]);
		}

		// Free the actual context struct
		OsFreeNonpagedMemory(Context);
	}
}

/*
 * Allocate and setup the VMXON region for the logical processor context.
 */
PVMXON_REGION HvAllocateVmxonRegion(PVMM_CONTEXT GlobalContext)
{
	PVMXON_REGION Region;

	// See VMX_VMXON_NUMBER_PAGES documentation.
	Region = (PVMXON_REGION)OsAllocateContiguousAlignedPages(VMX_VMXON_NUMBER_PAGES);

	// Zero VMXON region just to be sure...
	OsZeroMemory(Region, sizeof(VMX_PROCESSOR_CONTEXT));

	/*
	 * Initialize the version identifier in the VMXON region (the first 31 bits) with the VMCS revision identifier
	 * reported by capability MSRs.
	 *
	 * Clear bit 31 of the first 4 bytes of the VMXON region. (Handled by OsZeroMemory above)
	 */
	Region->VmcsRevisionNumber = GlobalContext->VmxCapabilities.VmcsRevisionId;

	return Region;
}

/**
 * Allocates a logical processor context.
 * 
 * Returns NULL on error.
 */
PVMX_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext(PVMM_CONTEXT GlobalContext)
{
	PVMX_PROCESSOR_CONTEXT Context;

	// Allocate some generic memory for our context
	Context = (PVMX_PROCESSOR_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMX_PROCESSOR_CONTEXT));
	if(!Context)
	{
		return NULL;
	}

	// Inititalize all fields to 0
	OsZeroMemory(Context, sizeof(VMX_PROCESSOR_CONTEXT));

	// Allocate and setup the VMXON region for this processor
	Context->VmxonRegion = HvAllocateVmxonRegion(GlobalContext);
	if(!Context->VmxonRegion)
	{
		return NULL;
	}

	Context->VmxonRegionPhysical = OsVirtualToPhysical(Context->VmxonRegion);
	if (!Context->VmxonRegionPhysical)
	{
		return NULL;
	}

	// Allocate and setup a blank VMCS region
	Context->VmcsRegion = HvAllocateVmcsRegion(GlobalContext);
	if(!Context->VmcsRegion)
	{
		return NULL;
	}

	Context->VmcsRegionPhysical = OsVirtualToPhysical(Context->VmcsRegion);
	if(!Context->VmcsRegionPhysical)
	{
		return NULL;
	}

	// Record the physical address of the MSR bitmap
	Context->MsrBitmapPhysical = OsVirtualToPhysical(&Context->MsrBitmap);

	return Context;
}

/*
 * Allocate a VMCS memory region and write the revision identifier.
 */
PVMCS HvAllocateVmcsRegion(PVMM_CONTEXT GlobalContext)
{
	PVMCS VmcsRegion;

	// Allocate contiguous physical pages for the VMCS. See VMX_VMCS_NUMBER_PAGES.
	VmcsRegion = (PVMCS)OsAllocateContiguousAlignedPages(VMX_VMCS_NUMBER_PAGES);

	// Initialize all fields to zero.
	OsZeroMemory(VmcsRegion, VMX_VMCS_NUMBER_PAGES * PAGE_SIZE);

	/*
	 * Software should write the VMCS revision identifier to the VMCS region before using that region for a VMCS.
	 */
	VmcsRegion->RevisionId = GlobalContext->VmxCapabilities.VmcsRevisionId;

	return VmcsRegion;
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
	PVMM_CONTEXT GlobalContext;
	PVMX_PROCESSOR_CONTEXT CurrentContext;

	GlobalContext = (PVMM_CONTEXT)Argument;

	// Get the current processor number we're executing this function on right now
	CurrentProcessorNumber = OsGetCurrentProcessorNumber();

	// Get the logical processor context that was allocated for this current processor
	CurrentContext = GlobalContext->AllProcessorContexts[CurrentProcessorNumber];

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
