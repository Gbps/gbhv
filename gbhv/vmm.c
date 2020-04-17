#include "vmm.h"
#include "vmx.h"
#include "vmcs.h"
#include "exit.h"

/**
 * Initialize all logical processors on the system for hypervisor execution.
 * 
 * This function performs the following actions:
 *      - Uses the CPUID instruction to verify that VMX is supported on the system.
 *        (As in, check we're not loading the driver on an AMD chip)
 *      - Checks the Feature Control MSR to ensure that the user's BIOS has enabled VT-X
 *        (Some BIOS allows users to disable this feature)
 *      - Allocates all relevant hypervisor structures. During Windows execution, this will
 *        utilize kernel APIs to allocate some structures into the kernel pool and OS defined contiguous physical memory ranges.
 *      - On Windows, a DPC (Deferred Procedure Call) is queued on each processor to begin the next
 *        step of initialization, one for each logical processor for VT-X execution.
 *      - After each DPC is complete, this function returns TRUE if all processors successfully entered
 *        VT-x mode.
 */
PVMM_CONTEXT HvInitializeAllProcessors()
{
    SIZE_T FeatureMSR;
    PVMM_CONTEXT GlobalContext;

    HvUtilLog("HvInitializeAllProcessors: Starting.\n");

    // Check if VMX support is enabled on the processor.
    if (!ArchIsVMXAvailable())
    {
        HvUtilLogError("VMX is not a feture of this processor.\n");
        return NULL;
    }

    // Enable bits in MSR to enable VMXON instruction.
    FeatureMSR = ArchGetHostMSR(MSR_IA32_FEATURE_CONTROL_ADDRESS);

    // The BIOS will lock the VMX bit on startup.
    if (!HvUtilBitIsSet(FeatureMSR, FEATURE_BIT_VMX_LOCK))
    {
        HvUtilLogError("VMX support was not locked by BIOS.\n");
        return NULL;
    }

    // VMX support can be configured to be disabled outside SMX.
    // Check to ensure this isn't the case.
    if (!HvUtilBitIsSet(FeatureMSR, FEATURE_BIT_ALLOW_VMX_OUTSIDE_SMX))
    {
        HvUtilLogError("VMX support was disabled outside of SMX operation by BIOS.\n");
        return NULL;
    }

    HvUtilLog("Total Processor Count: %i\n", OsGetCPUCount());

    // Pre-allocate all logical processor contexts, VMXON regions, VMCS regions
    GlobalContext = HvAllocateVmmContext();

	if(!GlobalContext)
	{
		return NULL;
	}

	if (!HvEptGlobalInitialize(GlobalContext))
	{
		HvUtilLogError("Processor does not support all necessary EPT features.\n");
		HvFreeVmmContext(GlobalContext);
		return NULL;
	}

    // Generates a DPC that makes all processors execute the broadcast function.
    KeGenericCallDpc(HvpDPCBroadcastFunction, (PVOID)GlobalContext);

    if (GlobalContext->SuccessfulInitializationsCount != OsGetCPUCount())
    {
        // TODO: Move to driver uninitalization
        HvUtilLogError("HvInitializeAllProcessors: Not all processors initialized. [%i successful]\n", GlobalContext->SuccessfulInitializationsCount);
		HvFreeVmmContext(GlobalContext);
        return NULL;
    }

    HvUtilLogSuccess("HvInitializeAllProcessors: Success.\n");
    return GlobalContext;
}

/*
 * Allocate the global VMM context used by all processors.
 * 
 * - Allocates a VMM_GLOBAL_CONTEXT structure, containing information about hv operations
 *   that is independent of a single processor.
 * - Allocates a VMM_PROCESSOR_CONTEXT structure for each logical processor, containing
 *   information about hv operation for only that singular processor.
 */
PVMM_CONTEXT HvAllocateVmmContext()
{
    PVMM_CONTEXT Context;

    // Allocate the global context structure
    Context = (PVMM_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMM_CONTEXT));
    if (!Context)
    {
        return NULL;
    }

	OsZeroMemory(Context, sizeof(VMM_CONTEXT));

    // Get count of all logical processors on the system
    Context->ProcessorCount = OsGetCPUCount();

    // Number of successful processor initializations
    Context->SuccessfulInitializationsCount = 0;

    // Save SYSTEM process DTB
    Context->SystemDirectoryTableBase = __readcr3();

    /*
	 * Get capability MSRs and add them to the global context.
	 */
    Context->VmxCapabilities = ArchGetBasicVmxCapabilities();

    PVMM_PROCESSOR_CONTEXT *ProcessorContexts = OsAllocateNonpagedMemory(Context->ProcessorCount * sizeof(PVMM_PROCESSOR_CONTEXT));
    if (!ProcessorContexts)
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
            HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to setup processor context.\n", ProcessorNumber);
            return NULL;
        }

        HvUtilLog("HvInitializeLogicalProcessor[#%i]: Allocated Context [Context = 0x%llx]\n", ProcessorNumber, ProcessorContexts[ProcessorNumber]);
    }

    Context->AllProcessorContexts = ProcessorContexts;
    HvUtilLog("VmcsRevisionNumber: %x\n", Context->VmxCapabilities.VmcsRevisionId);

    return Context;
}

/*
 * Free global VMM context and all logical processor contexts.
 */
VOID HvFreeVmmContext(PVMM_CONTEXT Context)
{
    if (Context)
    {
        // Free each logical processor context
        for (SIZE_T ProcessorNumber = 0; ProcessorNumber < Context->ProcessorCount; ProcessorNumber++)
        {
            HvFreeLogicalProcessorContext(Context->AllProcessorContexts[ProcessorNumber]);
        }

        // Free the collection of pointers to processor contexts
        OsFreeNonpagedMemory(Context->AllProcessorContexts);

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
    OsZeroMemory(Region, VMX_VMXON_NUMBER_PAGES * PAGE_SIZE);

    /*
	 * Initialize the version identifier in the VMXON region (the first 31 bits) with the VMCS revision identifier
	 * reported by capability MSRs.
	 *
	 * Clear bit 31 of the first 4 bytes of the VMXON region. (Handled by OsZeroMemory above)
	 */

    Region->VmcsRevisionNumber = (UINT32)GlobalContext->VmxCapabilities.VmcsRevisionId;

    return Region;
}

/**
 * Allocates a logical processor context.
 * 
 * Contains:
 *      - VMXON region
 *      - The Host stack, the stack used during exit handlers
 *      - The default VMCS, configuring all aspects of hv operation
 *      - MSR bitmap, containing a bitmap of MSRs to exit on
 * 
 * Returns NULL on error.
 */
PVMM_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext(PVMM_CONTEXT GlobalContext)
{
    PVMM_PROCESSOR_CONTEXT Context;

    // Allocate some generic memory for our context
    Context = (PVMM_PROCESSOR_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMM_PROCESSOR_CONTEXT));
    if (!Context)
    {
        return NULL;
    }

    // Inititalize all fields to 0, including the stack
    OsZeroMemory(Context, sizeof(VMM_PROCESSOR_CONTEXT));

    // Entry to refer back to the global context for simplicity
    Context->GlobalContext = GlobalContext;

    // Top of the host stack is the global context pointer.
    // See: vmxdefs.h and the structure definition.
    Context->HostStack.GlobalContext = GlobalContext;

    // Allocate and setup the VMXON region for this processor
    Context->VmxonRegion = HvAllocateVmxonRegion(GlobalContext);
    if (!Context->VmxonRegion)
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
    if (!Context->VmcsRegion)
    {
        return NULL;
    }

    Context->VmcsRegionPhysical = OsVirtualToPhysical(Context->VmcsRegion);
    if (!Context->VmcsRegionPhysical)
    {
        return NULL;
    }

    /*
	 * Allocate one page for MSR bitmap, all zeroes because we are not exiting on any MSRs.
	 */
    Context->MsrBitmap = OsAllocateContiguousAlignedPages(1);
    OsZeroMemory(Context->MsrBitmap, PAGE_SIZE);

    // Record the physical address of the MSR bitmap
    Context->MsrBitmapPhysical = OsVirtualToPhysical(Context->MsrBitmap);

	/*
	 * Initialize EPT paging structures and the EPTP that we will apply to the VMCS.
	 */
	if(!HvEptLogicalProcessorInitialize(Context))
	{
		OsFreeContiguousAlignedPages(Context);
		return NULL;
	}

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
    VmcsRegion->RevisionId = (UINT32)GlobalContext->VmxCapabilities.VmcsRevisionId;

    return VmcsRegion;
}

/*
 * Free a logical processor context allocated by HvAllocateLogicalProcessorContext
 */
VOID HvFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT Context)
{
    if (Context)
    {
        OsFreeContiguousAlignedPages(Context->VmxonRegion);
		OsFreeContiguousAlignedPages(Context->MsrBitmap);
		HvEptFreeLogicalProcessorContext(Context);
        OsFreeNonpagedMemory(Context);
    }
}

/*
 * Get the current CPU context object from the global context by reading the current processor number.
 */
PVMM_PROCESSOR_CONTEXT HvGetCurrentCPUContext(PVMM_CONTEXT GlobalContext)
{
    SIZE_T CurrentProcessorNumber;
    PVMM_PROCESSOR_CONTEXT CurrentContext;

    // Get the current processor we're executing this function on right now
    CurrentProcessorNumber = OsGetCurrentProcessorNumber();

    // Get the logical processor context that was allocated for this current processor
    CurrentContext = GlobalContext->AllProcessorContexts[CurrentProcessorNumber];

    return CurrentContext;
}

/**
 * Called by HvInitializeAllProcessors to initialize VMX on a specific logical processor.
 */
VOID NTAPI HvpDPCBroadcastFunction(_In_ struct _KDPC *Dpc,
                                   _In_opt_ PVOID DeferredContext,
                                   _In_opt_ PVOID SystemArgument1,
                                   _In_opt_ PVOID SystemArgument2)
{
    SIZE_T CurrentProcessorNumber;
    PVMM_CONTEXT GlobalContext;
    PVMM_PROCESSOR_CONTEXT CurrentContext;

    UNREFERENCED_PARAMETER(Dpc);
	
    GlobalContext = (PVMM_CONTEXT)DeferredContext;

    // Get the current processor number we're executing this function on right now
    CurrentProcessorNumber = OsGetCurrentProcessorNumber();

    // Get the logical processor context that was allocated for this current processor
    CurrentContext = HvGetCurrentCPUContext(GlobalContext);

    // Initialize processor for VMX
    if (HvBeginInitializeLogicalProcessor(CurrentContext))
    {
        // We were successful in initializing the processor
		InterlockedIncrement((volatile LONG*)&GlobalContext->SuccessfulInitializationsCount);

		// Mark this context as launched.
		CurrentContext->HasLaunched = TRUE;
    }
    else
    {
        HvUtilLogError("HvpDPCBroadcastFunction[#%i]: Failed to VMLAUNCH.\n", CurrentProcessorNumber);
    }

    // These must be called for GenericDpcCall to signal other processors
    // SimpleVisor code shows how to do this

    // Wait for all DPCs to synchronize at this point
    KeSignalCallDpcSynchronize(SystemArgument2);

    // Mark the DPC as being complete
    KeSignalCallDpcDone(SystemArgument1);
}

/**
 * Initialize VMCS and enter VMX root-mode.
 * 
 * This function should never return, except on error. Execution will continue on the guest on success.
 * 
 * See: HvBeginInitializeLogicalProcessor and vmxdefs.asm.
 */
VOID HvInitializeLogicalProcessor(PVMM_PROCESSOR_CONTEXT Context, SIZE_T GuestRSP, SIZE_T GuestRIP)
{
    SIZE_T CurrentProcessorNumber;

    // Get the current processor we're executing this function on right now
    CurrentProcessorNumber = OsGetCurrentProcessorNumber();

    // Enable VMXe, execute VMXON and enter VMX root mode
    if (!VmxEnterRootMode(Context))
    {
        HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to enter VMX Root Mode.\n", CurrentProcessorNumber);
        return;
    }

    // Setup VMCS with all values necessary to begin VMXLAUNCH
    // &Context->HostStack.GlobalContext is also the top of the host stack
    if (!HvSetupVmcsDefaults(Context, (SIZE_T)&HvEnterFromGuest, (SIZE_T)&Context->HostStack.GlobalContext, GuestRIP, GuestRSP))
    {
        HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to enter VMX Root Mode.\n", CurrentProcessorNumber);
        VmxExitRootMode(Context);
        return;
    }

    // Launch the hypervisor! This function should not return if it is successful, as we continue execution
    // on the guest.
    if (!VmxLaunchProcessor(Context))
    {
        HvUtilLogError("HvInitializeLogicalProcessor[#%i]: Failed to VmxLaunchProcessor.\n", CurrentProcessorNumber);
        return;
    }
}

/*
 * This function is the main handler of all exits that take place during VMM execution.
 * 
 * This function is initially called from HvEnterFromGuest (defined in vmxdefs.asm). During HvEnterFromGuest,
 * the state of the guest's registers are stored to the stack and given as a stack pointer in GuestRegisters.
 * The value of most guest gp registers can be accessed using the PGPREGISTER_CONTEXT, but GuestRSP must
 * be read out of the VMCS due to the fact that, during the switch from guest to host, the HostRSP value
 * replaced RSP. During the switch, GuestRSP was saved back to the guest area of the VMCS so we can access
 * it with a VMREAD.
 * 
 * Defined in Section 27.2 RECORDING VM-EXIT INFORMATION AND UPDATING VM-ENTRY CONTROL FIELDS, exits have two main
 * exit fields, one which describes what kind of exit just took place (ExitReason) and why it took place (ExitQualification).
 * By reading these two values, the exit handler can know exactly what steps it should take to handle the exit properly.
 * 
 * When the exit handler is called by the CPU, interrupts are disabled. In order to call certain kernel api functions
 * in Type 2, we will need to enable interrupts. Therefore, the first action the handler must take is to ensure execution 
 * of the handler is not executing below DISPATCH_LEVEL. This is to prevent the dispatcher from context switching away 
 * from our exit handler if we enable interrupts, potentially causing serious memory synchronization problems.
 * 
 * Next, a VMEXIT_CONTEXT is initialized with the exit information, including certain guest registers (RSP, RIP, RFLAGS) 
 * from the VMCS.
 * 
 * This function is given two arguments from HvEnterFromGuest in vmxdefs.asm:
 *      - The GlobalContext, which was saved to the top of the HostStack
 *      - The guest register context, which was pushed onto the stack during HvEnterFromGuest.
 * 
 */
BOOL HvHandleVmExit(PVMM_CONTEXT GlobalContext, PGPREGISTER_CONTEXT GuestRegisters)
{
    VMEXIT_CONTEXT ExitContext;
    PVMM_PROCESSOR_CONTEXT ProcessorContext;
	BOOL Success;

	Success = FALSE;

    // Grab our logical processor context object for this processor
    ProcessorContext = HvGetCurrentCPUContext(GlobalContext);

    /*
	 * Initialize all fields of the exit context, including reading relevant fields from the VMCS.
	 */
    VmxInitializeExitContext(&ExitContext, GuestRegisters);

	/*
	 * If we tried to enter but failed, we return false here so HvHandleVmExitFailure is called.
	 */
	if (ExitContext.ExitReason.VmEntryFailure == 1)
	{
		return FALSE;
	}

    /*
	 * To prevent context switching while enabling interrupts, save IRQL here.
	 */
    ExitContext.SavedIRQL = KeGetCurrentIrql();
    if (ExitContext.SavedIRQL < DISPATCH_LEVEL)
    {
        KeRaiseIrqlToDpcLevel();
    }

	/*
	 * Handle our exit using the handler code inside of exit.c
	 */
	Success = HvExitDispatchFunction(ProcessorContext, &ExitContext);
    if(!Success)
    {
		// TODO: More information
		HvUtilLogError("Failed to handle exit.\n");
    }

    /*
	 * If we raised IRQL, lower it before returning to guest.
	 */
    if (ExitContext.SavedIRQL < DISPATCH_LEVEL)
    {
        KeLowerIrql(ExitContext.SavedIRQL);
    }

    return Success;
}

/*
 * If we're at this point, that means HvEnterFromGuest failed to enter back to the guest.
 * Print out the error informaiton and some state of the processor for debugging purposes.
 */
BOOL HvHandleVmExitFailure(PVMM_CONTEXT GlobalContext, PGPREGISTER_CONTEXT GuestRegisters)
{
    PVMM_PROCESSOR_CONTEXT ProcessorContext;

    UNREFERENCED_PARAMETER(GuestRegisters);
	UNREFERENCED_PARAMETER(GlobalContext);
	UNREFERENCED_PARAMETER(ProcessorContext);

	// TODO: Fix GlobalContext
	KeBugCheck(0xDEADBEEF);
	/*
    // Grab our logical processor context object for this processor
    ProcessorContext = HvGetCurrentCPUContext(GlobalContext);

    HvUtilLogError("HvHandleVmExitFailure: Encountered vmexit error.");

    // Print information about the error state
    VmxPrintErrorState(ProcessorContext);

    // Exit root mode, since we cannot recover here
    if (!VmxExitRootMode(ProcessorContext))
    {
        // We can't exit root mode? Ut oh. Things are real bad.
        // Returning false here will halt the processor.
        return FALSE;
    }

    // Continue execution with VMX disabled.
    return TRUE;
	*/
}
