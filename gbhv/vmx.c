#include "vmx.h"
#include "vmm.h"

/*
 * Execute VMLAUNCH and launch the processor.
 * 
 * Should never return, except on error.
 */
BOOL VmxLaunchProcessor(PVMM_PROCESSOR_CONTEXT Context)
{
    HvUtilLogDebug("VmxLaunchProcessor: VMLAUNCH....\n");

    // Launch the VMCS! If this returns, there was an error.
    // Otherwise, execution continues in guest_resumes_here from vmxdefs.asm
    __vmx_vmlaunch();

    VmxPrintErrorState(Context);

    VmxExitRootMode(Context);

    return FALSE;
}

/*
 * Give a printout when VMX instructions or vmexits fail.
 * 
 * Reads the instruction error from the VMCS.
 */
VOID VmxPrintErrorState(PVMM_PROCESSOR_CONTEXT Context)
{
    UINT64 FailureCode;

    UNREFERENCED_PARAMETER(Context);

    // TODO: Add register context

    // Read the failure code
    if (__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &FailureCode) != 0)
    {
        HvUtilLogError("VmxPrintErrorState: Failed to read error code.\n");
        return;
    }

    HvUtilLogError("VmxPrintErrorState: VMLAUNCH Error = 0x%llx\n", FailureCode);
}

/*
 * In VMX operation, processors may fix certain bits in CR0 and CR4 to specific values and not support other
 * values.
 * 
 * Ensures these CR0 and CR4 values are set for old processors.
 */
VOID VmxSetFixedBits()
{
    CR0 ControlRegister0;
    CR4 ControlRegister4;

    ControlRegister0.Flags = __readcr0();
    ControlRegister4.Flags = __readcr4();

    // Set required fixed bits for cr0
    ControlRegister0.Flags |= __readmsr(IA32_VMX_CR0_FIXED0);
    ControlRegister0.Flags &= __readmsr(IA32_VMX_CR0_FIXED1);

    // Set required fixed bits for cr4
    ControlRegister4.Flags |= __readmsr(IA32_VMX_CR4_FIXED0);
    ControlRegister4.Flags &= __readmsr(IA32_VMX_CR4_FIXED1);


    // Apply to the processor
    __writecr0(ControlRegister0.Flags);
    __writecr4(ControlRegister4.Flags);
}

/*
 * Enter VMX Root Mode on the processor.
 * 
 * This function will:
 *	- Enable VMX-enabled bit in CR4
 *	- Ensure the VMX fixed bits are set in CR0 and CR4
 *	- Turn on VMX with VMXON instruction
 *	- Clear the VMCS with VMXCLEAR instruction
 *	- Load the VMCS pointer with VMXPTRLD
 */
BOOL VmxEnterRootMode(PVMM_PROCESSOR_CONTEXT Context)
{
    // Enable VMXe in CR4 of the processor
    ArchEnableVmxe();

    // Ensure the required fixed bits are set in cr0 and cr4, as per the spec.
    VmxSetFixedBits();

    HvUtilLogDebug("VmxOnRegion[#%i]: (V) 0x%llx / (P) 0x%llx [%i]\n", OsGetCurrentProcessorNumber(), Context->VmxonRegion, Context->VmxonRegionPhysical, (PUINT32)Context->VmxonRegion->VmcsRevisionNumber);

    // Execute VMXON to bring processor to VMX mode
    // Check RFLAGS.CF == 0 to ensure successful execution
    if (__vmx_on((ULONGLONG *)&Context->VmxonRegionPhysical) != 0)
    {
        HvUtilLogError("VMXON failed.\n");
        return FALSE;
    }

    // And clear the VMCS before writing the configuration entries to it
    if (__vmx_vmclear((ULONGLONG *)&Context->VmcsRegionPhysical) != 0)
    {
        HvUtilLogError("VMCLEAR failed.\n");
        return FALSE;
    }

    // Now load the blank VMCS
    if (__vmx_vmptrld((ULONGLONG *)&Context->VmcsRegionPhysical) != 0)
    {
        HvUtilLogError("VMPTRLD failed.\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Exits VMX Root Mode on a processor currently in VMX operation mode.
 * 
 * This function will:
 *	- Clear the current VMCS
 *	- Execute VMXOFF
 *	- Unset the VMX enabled bit in CR4
 */
BOOL VmxExitRootMode(PVMM_PROCESSOR_CONTEXT Context)
{
	HvUtilLogError("Exiting VMX.\n");

    // Clear the VMCS before VMXOFF (Specification requires this)
    if (__vmx_vmclear((ULONGLONG *)&Context->VmcsRegionPhysical) != 0)
    {
        HvUtilLogError("VMCLEAR failed.\n");
    }

    // Turn off VMX
    __vmx_off();

    // Turn off VMXe in CR4
    ArchDisableVmxe();

    // TODO: Reset fixed cr bits.

    return TRUE;
}

/*
 * VMX has a very specific layout for its segment descriptor fields that needs to be populated from the GDT entries of the OS.
 * 
 * This function inputs a typical segment selector value (ss, es, cs, ds, etc.), accesses the OS's GDT, and populates the VMX structure.
 * 
 * This allows the guest to continue using the same segments it was using prior to entering VMX mode.
 */
VOID VmxGetSegmentDescriptorFromSelector(PVMX_SEGMENT_DESCRIPTOR VmxSegmentDescriptor, SEGMENT_DESCRIPTOR_REGISTER_64 GdtRegister, SEGMENT_SELECTOR SegmentSelector, BOOL ClearRPL)
{
    /*
	 * Segment descriptor given by the OS.
	 */
    PSEGMENT_DESCRIPTOR_64 OsSegmentDescriptor;

    OsZeroMemory(VmxSegmentDescriptor, sizeof(VMX_SEGMENT_DESCRIPTOR));

    /*
	 * If the selector is 0 or the segment selector is an IDT selector, return an unusable segment.
	 */
    if (SegmentSelector.Flags == 0 || SegmentSelector.Table != 0)
    {
        // Null or invalid GDT entry
        VmxSegmentDescriptor->AccessRights.Unusable = 1;
        return;
    }

    /*
	 * Index into the GDT to get a pointer to the segment descriptor for this segment.
	 */
    OsSegmentDescriptor = (PSEGMENT_DESCRIPTOR_64)(((UINT64)GdtRegister.BaseAddress) + (SegmentSelector.Index << 3));

    /*
	 * Populate the base address from the OS-defined base address.
	 *
	 * Populated from three address values stored in the GDT entry.
	 */
    VmxSegmentDescriptor->BaseAddress = (OsSegmentDescriptor->BaseAddressHigh << 24) |
                                        (OsSegmentDescriptor->BaseAddressMiddle << 16) |
                                        (OsSegmentDescriptor->BaseAddressLow);

    /*
	 * Ensure that the BaseAddress is a 32-bit value, even though it's defined as a 64-bit integer in the VMCS.
	 */
    VmxSegmentDescriptor->BaseAddress &= 0xFFFFFFFF;

    /*
	 * If it's a System segment, treat it as a 64-bit base address.
	 */
    if (OsSegmentDescriptor->DescriptorType == 0)
    {
        VmxSegmentDescriptor->BaseAddress |= ((UINT64)OsSegmentDescriptor->BaseAddressUpper << 32);
    }

    /*
	 * Populate the segment limit from the OS-defined base address.
	 *
	 * 20-bit value populated from the upper segment limit and lower segment limit fields.
	 */
    VmxSegmentDescriptor->SegmentLimit = __segmentlimit(SegmentSelector.Flags);

    /*
	 * Flag to clear the RPL of the selector.
	 * Used to ensure consistency of the VMX Host state.
	 */
    if (ClearRPL)
    {
        SegmentSelector.RequestPrivilegeLevel = 0;
    }

    /*
	 * Just copy straight from the selector we were given.
	 */
    VmxSegmentDescriptor->Selector = SegmentSelector.Flags;
    /*
	 * Copy over all of the access right values from the OS segment descriptor to the VMX descriptor.
	 */
    VmxSegmentDescriptor->AccessRights.Type = OsSegmentDescriptor->Type;
    VmxSegmentDescriptor->AccessRights.DescriptorType = OsSegmentDescriptor->DescriptorType;
    VmxSegmentDescriptor->AccessRights.DescriptorPrivilegeLevel = OsSegmentDescriptor->DescriptorPrivilegeLevel;
    VmxSegmentDescriptor->AccessRights.Present = OsSegmentDescriptor->Present;
    VmxSegmentDescriptor->AccessRights.AvailableBit = OsSegmentDescriptor->System;
    VmxSegmentDescriptor->AccessRights.LongMode = OsSegmentDescriptor->LongMode;
    VmxSegmentDescriptor->AccessRights.DefaultBig = OsSegmentDescriptor->DefaultBig;
    VmxSegmentDescriptor->AccessRights.Granularity = OsSegmentDescriptor->Granularity;

    /*
	 * Mark the segment usable for VMX.
	 */
    VmxSegmentDescriptor->AccessRights.Unusable = 0;
}
