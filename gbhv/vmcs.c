#include "vmcs.h"
#include "arch.h"
#include "vmm.h"
#include "util.h"
#include "vmx.h"

BOOL HvSetupVmcsDefaults(PVMM_PROCESSOR_CONTEXT Context, SIZE_T HostRIP, SIZE_T HostRSP, SIZE_T GuestRIP, SIZE_T GuestRSP)
{
	VMX_ERROR VmError;

	VmError = 0;

	/*
	 * Capture the current state of gp, float, and xmm registers of the processor.
	 * Some of these values are used to help setup the VMCS.
	 */
	OsCaptureContext(&Context->InitialRegisters);

	/*
	 * Capture the current state of special registers of the processor.
	 * These values will be used to correctly setup initial values in the VMCS to match the current
	 * running host.
	 */
	ArchCaptureSpecialRegisters(&Context->InitialSpecialRegisters);

	// Setup all of the control fields of the VMCS
	VmError |= HvSetupVmcsControlFields(Context);
	HvUtilLogDebug("HvSetupVmcsControlFields: VmError = %i", VmError);

	if(VmError != 0)
	{
		HvUtilLogError("HvSetupVmcsControlFields: VmError = %i", VmError);
		return FALSE;
	}
		
	// Setup the guest area of the VMCS
	VmError |= HvSetupVmcsGuestArea(Context, GuestRIP, GuestRSP);

	if (VmError != 0)
	{
		HvUtilLogError("HvSetupVmcsGuestArea: VmError = %i", VmError);
		return FALSE;
	}

	// Setup the host area of the VMCS
	VmError |= HvSetupVmcsHostArea(Context, HostRIP, HostRSP);

	if (VmError != 0)
	{
		HvUtilLogError("HvSetupVmcsHostArea: VmError = %i", VmError);
		return FALSE;
	}

	return VmError == 0;
}

/*
 * Calls VmxGetSegmentDescriptorFromSelector to decode the segment descriptor into VMX-ready values.
 * 
 * Sets the Selector and Base fields of the Host VMCS given the segment
 */
#define VMCS_SETUP_HOST_SEGMENTATION(_SEGMENT_NAME_UPPER_, _REGISTER_VALUE_) \
	VmxGetSegmentDescriptorFromSelector(&SegmentDescriptor, GdtRegister, _REGISTER_VALUE_, TRUE); \
	VmxVmwriteFieldFromImmediate(VMCS_HOST_##_SEGMENT_NAME_UPPER_##_SELECTOR, SegmentDescriptor.Selector); \
	VmxVmwriteFieldFromImmediate(VMCS_HOST_##_SEGMENT_NAME_UPPER_##_BASE, SegmentDescriptor.BaseAddress);

 /*
  * Calls VmxGetSegmentDescriptorFromSelector to decode the segment descriptor into VMX-ready values.
  *
  * Sets the Selector field the Host VMCS given the segment.
  */
#define VMCS_SETUP_HOST_SEGMENTATION_NOBASE(_SEGMENT_NAME_UPPER_, _REGISTER_VALUE_) \
	VmxGetSegmentDescriptorFromSelector(&SegmentDescriptor, GdtRegister, _REGISTER_VALUE_, TRUE); \
	VmxVmwriteFieldFromImmediate(VMCS_HOST_##_SEGMENT_NAME_UPPER_##_SELECTOR, SegmentDescriptor.Selector); \

/*
* Sets up all fields of the host area of the VMCS.
* 
* HostRIP: The RIP that vmexits will jump to initially, similar to how interrupts work.
* HostRSP: The RSP value to use when the vmexit is fired.
*/
VMX_ERROR HvSetupVmcsHostArea(PVMM_PROCESSOR_CONTEXT Context, SIZE_T HostRIP, SIZE_T HostRSP)
{
	VMX_ERROR VmError;
	PIA32_SPECIAL_REGISTERS SpecialRegisters;
	PREGISTER_CONTEXT Registers;
	SEGMENT_DESCRIPTOR_REGISTER_64 GdtRegister;
	VMX_SEGMENT_DESCRIPTOR SegmentDescriptor;

	VmError = 0;

	/*
	 * Registers as they were when we began setup. Used to get segment selector values.
	 */
	Registers = &Context->InitialRegisters;

	/*
	 * Special registers of the host, such as control registers (CR0, CR4)
	 */
	SpecialRegisters = &Context->InitialSpecialRegisters;

	/*
	 * Grab the GDTR for the current running system.
	 */
	GdtRegister = SpecialRegisters->GlobalDescriptorTableRegister;


	/* CR0, CR3, and CR4 (64 bits each; 32 bits on processors that do not support Intel 64 architecture) */
	VmxVmwriteFieldFromRegister(VMCS_HOST_CR0, SpecialRegisters->ControlRegister0);

	// Host CR3 is special because, due to the DPC, there might be a usermode process swapped into CR3. 
	// We always want host to enter with the kernel's CR3 value, for consistency.
	VmxVmwriteFieldFromImmediate(VMCS_HOST_CR3, Context->GlobalContext->SystemDirectoryTableBase);
	VmxVmwriteFieldFromRegister(VMCS_HOST_CR4, SpecialRegisters->ControlRegister4);

	/* RSP and RIP (64 bits each; 32 bits on processors that do not support Intel 64 architecture). */
	VmxVmwriteFieldFromImmediate(VMCS_HOST_RIP, HostRIP);
	VmxVmwriteFieldFromImmediate(VMCS_HOST_RSP, HostRSP);

	/* 
	 * Selector fields (16 bits each) for the segment registers CS, SS, DS, ES, FS, GS, and TR. There is no field in the
	 * host-state area for the LDTR selector
	 */

	// TODO: Totally refactor segmentation setup
	VMCS_SETUP_HOST_SEGMENTATION_NOBASE(CS, Registers->SegCS);
	VMCS_SETUP_HOST_SEGMENTATION_NOBASE(SS, Registers->SegSS);
	VMCS_SETUP_HOST_SEGMENTATION_NOBASE(DS, Registers->SegDS);
	VMCS_SETUP_HOST_SEGMENTATION_NOBASE(ES, Registers->SegES);
	VMCS_SETUP_HOST_SEGMENTATION(FS, Registers->SegFS);
	VMCS_SETUP_HOST_SEGMENTATION(GS, Registers->SegGS);
	VMCS_SETUP_HOST_SEGMENTATION(TR, SpecialRegisters->TaskRegister);

	/* Populate GS and FS base from MSRs */
	VmxVmwriteFieldFromImmediate(VMCS_HOST_GS_BASE, __readmsr(IA32_GS_BASE));
	VmxVmwriteFieldFromImmediate(VMCS_HOST_FS_BASE, __readmsr(IA32_FS_BASE));

	/*
	 * Copy GDT descriptor register
	 */
	VmxVmwriteFieldFromImmediate(VMCS_HOST_GDTR_BASE, SpecialRegisters->GlobalDescriptorTableRegister.BaseAddress);

	/*
	 * Copy IDT descriptor register
	 */
	VmxVmwriteFieldFromImmediate(VMCS_HOST_IDTR_BASE, SpecialRegisters->InterruptDescriptorTableRegister.BaseAddress);

	/*
	 * Copy required architecture MSRs to the guest.
	 * 
	 * The following MSRs:
	 *	— IA32_SYSENTER_CS (32 bits)
	 *	— IA32_SYSENTER_ESP and IA32_SYSENTER_EIP (64 bits; 32 bits on processors that do not support Intel 64
	 *	architecture).
	 *	— IA32_PERF_GLOBAL_CTRL (64 bits). This field is supported only on processors that support the 1-setting of
	 *	the “load IA32_PERF_GLOBAL_CTRL” VM-exit control.
	 *	— IA32_PAT (64 bits). This field is supported only on processors that support the 1-setting of the “load
	 *	IA32_PAT” VM-exit control.
	 *	— IA32_EFER (64 bits). This field is supported only on processors that support the 1-setting of the “load
	 *	IA32_EFER” VM-exit control.
	 */

	VmxVmwriteFieldFromRegister(VMCS_HOST_SYSENTER_CS, SpecialRegisters->SysenterCsMsr);
	VmxVmwriteFieldFromImmediate(VMCS_HOST_SYSENTER_ESP, SpecialRegisters->SysenterEspMsr);
	VmxVmwriteFieldFromImmediate(VMCS_HOST_SYSENTER_EIP, SpecialRegisters->SysenterEipMsr);

	return VmError;
}
/*
 * Very carefully populates the segmentation parts of one of the guest segmentation VMCS fields according to the values of the currently running system.
 */
VMX_ERROR HvSetupVmcsGuestSegment(SEGMENT_DESCRIPTOR_REGISTER_64 GdtRegister, SEGMENT_SELECTOR SegmentSelector, SIZE_T VmcsSelector, SIZE_T VmcsLimit, SIZE_T VmcsAccessRights, SIZE_T VmcsBase)
{
	VMX_SEGMENT_DESCRIPTOR SegmentDescriptor;
	VMX_ERROR VmError;

	VmError = 0;

	VmxGetSegmentDescriptorFromSelector(&SegmentDescriptor, GdtRegister, SegmentSelector, FALSE);

	/*
	 * The following fields for each of the registers CS, SS, DS, ES, FS, GS, LDTR, and TR:
	 *  — Selector (16 bits).
	 *  — Base address (64 bits; 32 bits on processors that do not support Intel 64 architecture). The base-address
	 *    fields for CS, SS, DS, and ES have only 32 architecturally-defined bits; nevertheless, the corresponding
	 *    VMCS fields have 64 bits on processors that support Intel 64 architecture.
	 *  — Segment limit (32 bits). The limit field is always a measure in bytes.
	 *  — Access rights (32 bits). The format of this field is given in Table 24-2 and detailed as follows
	 */
	VmxVmwriteFieldFromImmediate(VmcsSelector, SegmentDescriptor.Selector);
	VmxVmwriteFieldFromImmediate(VmcsBase, SegmentDescriptor.BaseAddress);

	VmxVmwriteFieldFromImmediate(VmcsLimit, SegmentDescriptor.SegmentLimit);
	VmxVmwriteFieldFromRegister(VmcsAccessRights, SegmentDescriptor.AccessRights);

	return VmError;
}

/*
 * Calls HvSetupVmcsGuestSegment with encoded VMCS values corresponding to that segment.
 */
#define VMCS_SETUP_GUEST_SEGMENTATION(_SEGMENT_NAME_UPPER_, _REGISTER_VALUE_) \
	VmxGetSegmentDescriptorFromSelector(&SegmentDescriptor, GdtRegister, _REGISTER_VALUE_, FALSE); \
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_##_SEGMENT_NAME_UPPER_##_SELECTOR, SegmentDescriptor.Selector); \
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_##_SEGMENT_NAME_UPPER_##_BASE, SegmentDescriptor.BaseAddress); \
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_##_SEGMENT_NAME_UPPER_##_LIMIT, SegmentDescriptor.SegmentLimit); \
	VmxVmwriteFieldFromRegister(VMCS_GUEST_##_SEGMENT_NAME_UPPER_##_ACCESS_RIGHTS, SegmentDescriptor.AccessRights); \

/*
 * Sets up all fields of the guest area of the VMCS.
 * 
 * GuestRIP: The RIP to set when swapping back to the guest.
 * GuestRSP: The RSP to set when swapping back to the guest. 
 */
VMX_ERROR HvSetupVmcsGuestArea(PVMM_PROCESSOR_CONTEXT Context, SIZE_T GuestRIP, SIZE_T GuestRSP)
{
	PREGISTER_CONTEXT Registers;
	VMX_ERROR VmError;
	SEGMENT_DESCRIPTOR_REGISTER_64 GdtRegister;
	PIA32_SPECIAL_REGISTERS SpecialRegisters;
	VMX_SEGMENT_DESCRIPTOR SegmentDescriptor;

	VmError = 0;

	/*
	 * Registers as they were when we began setup. Used to get segment selector values.
	 */
	Registers = &Context->InitialRegisters;

	/*
	 * Special registers of the host, such as control registers (CR0, CR4)
	 */
	SpecialRegisters = &Context->InitialSpecialRegisters;

	/*
	 * Grab the GDTR for the current running system.
	 */
	GdtRegister = SpecialRegisters->GlobalDescriptorTableRegister;

	/*
	 * Set guest cr0, cr3, cr4, dr7, rflags values to host values.
	 */
	VmxVmwriteFieldFromRegister(VMCS_GUEST_CR0, SpecialRegisters->ControlRegister0);
	VmxVmwriteFieldFromRegister(VMCS_GUEST_CR3, SpecialRegisters->ControlRegister3);
	VmxVmwriteFieldFromRegister(VMCS_GUEST_CR4, SpecialRegisters->ControlRegister4);
	VmxVmwriteFieldFromRegister(VMCS_GUEST_DR7, SpecialRegisters->DebugRegister7);
	VmxVmwriteFieldFromRegister(VMCS_GUEST_RFLAGS, SpecialRegisters->RflagsRegister);

	/*
	 * These are given as argument for configuring how the guest initially enters.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_RIP, GuestRIP);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_RSP, GuestRSP);

	/*
	 * Setup all VMCS fields for segmentation for the guest to match exactly with the current running OS.
	 * 
	 * Uses the segment selector from Registers and the GDT register from GdtRegister.
	 * 
	 * See #define directly above. Simply calls HvSetupVmcsGuestSegment.
	 */

	// TODO: Totally refactor segmentation setup

	HvUtilLogDebug("GdtRegister: 0x%llx, Base: 0x%llx, Limit: 0x%llx", GdtRegister, GdtRegister.BaseAddress, GdtRegister.Limit);

	VMCS_SETUP_GUEST_SEGMENTATION(ES, Registers->SegES);
	VMCS_SETUP_GUEST_SEGMENTATION(CS, Registers->SegCS);
	VMCS_SETUP_GUEST_SEGMENTATION(SS, Registers->SegSS);
	VMCS_SETUP_GUEST_SEGMENTATION(DS, Registers->SegDS);
	VMCS_SETUP_GUEST_SEGMENTATION(GS, Registers->SegGS);
	VMCS_SETUP_GUEST_SEGMENTATION(FS, Registers->SegFS);
	VMCS_SETUP_GUEST_SEGMENTATION(LDTR, SpecialRegisters->LocalDescriptorTableRegister);
	VMCS_SETUP_GUEST_SEGMENTATION(TR, SpecialRegisters->TaskRegister);

	/* Populate GS and FS base from MSRs */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_GS_BASE, __readmsr(IA32_GS_BASE));
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_FS_BASE, __readmsr(IA32_FS_BASE));

	/*
	 * Copy GDT descriptor register
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_GDTR_BASE, SpecialRegisters->GlobalDescriptorTableRegister.BaseAddress);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_GDTR_LIMIT, SpecialRegisters->GlobalDescriptorTableRegister.Limit);

	/*
	 * Copy IDT descriptor register
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_IDTR_BASE, SpecialRegisters->InterruptDescriptorTableRegister.BaseAddress);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_IDTR_LIMIT, SpecialRegisters->InterruptDescriptorTableRegister.Limit);

	/*
	 * Copy required architecture MSRs to the guest.
	 */
	// TODO: Make reserved bits of SpecialRegisters->DebugControlMsr zero
	VmxVmwriteFieldFromRegister(VMCS_GUEST_DEBUGCTL, SpecialRegisters->DebugControlMsr);
	VmxVmwriteFieldFromRegister(VMCS_GUEST_SYSENTER_CS, SpecialRegisters->SysenterCsMsr);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_SYSENTER_EIP, SpecialRegisters->SysenterEipMsr);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_SYSENTER_ESP, SpecialRegisters->SysenterEspMsr);

	/* Not required, can use regular MSR load/store vmexits: */
	//VmxVmwriteFieldFromImmediate(VMCS_GUEST_PERF_GLOBAL_CTRL, SpecialRegisters->GlobalPerfControlMsr);
	//VmxVmwriteFieldFromRegister(VMCS_GUEST_PAT, SpecialRegisters->PatMsr);
	//VmxVmwriteFieldFromRegister(VMCS_GUEST_EFER, SpecialRegisters->EferMsr);
	//VmxVmwriteFieldFromImmediate(VMCS_GUEST_SMBASE, SpecialRegisters->SmramBaseMsr);


	/*
	 * Setup default Guest Non-Register State
	 */

	/*
	 * Activity state (32 bits). This field identifies the logical processor’s activity state. When a logical processor is
	 * executing instructions normally, it is in the active state. Execution of certain instructions and the occurrence
	 * of certain events may cause a logical processor to transition to an inactive state in which it ceases to execute
	 * instructions.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_ACTIVITY_STATE, 0);

	/*
	 * Interruptibility state (32 bits). The IA-32 architecture includes features that permit certain events to be
	 * blocked for a period of time. This field contains information about such blocking. Details and the format of this
	 * field are given in Table 24-3.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);

	/*
	 * Pending debug exceptions (64 bits; 32 bits on processors that do not support Intel 64 architecture). IA-32
	 * processors may recognize one or more debug exceptions without immediately delivering them.2 This field
	 * contains information about such exceptions. This field is described in Table 24-4.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0);

	/*
	 *  If the “VMCS shadowing” VM-execution control is 1, the VMREAD and VMWRITE
	 *  instructions access the VMCS referenced by this pointer (see Section 24.10). Otherwise, software should set
	 *  this field to FFFFFFFF_FFFFFFFFH to avoid VM-entry failures (see Section 26.3.1.5).
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_VMCS_LINK_POINTER, ~0ULL);

	/*
	 *  The extended-page-table pointer (EPTP) contains the address of the base of EPT PML4 table (see Section
	 *  28.2.2), as well as other EPT configuration information. The format of this field is shown in Table 24-8.
	 */
	VmxVmwriteFieldFromRegister(VMCS_CTRL_EPT_POINTER, Context->EptPointer);

	return VmError;
}

VMX_ERROR HvSetupVmcsControlFields(PVMM_PROCESSOR_CONTEXT Context)
{
	VMX_ERROR VmError;

	VmError = 0;

	/////////////////////////////// Pin-based Control ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, HvSetupVmcsControlPinBased(Context));

	/////////////////////////////// Processor-Based VM-Execution Controls ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, HvSetupVmcsControlProcessor(Context));

	/*
	 * No vmexits on any exceptions.
	 *
	 * The exception bitmap is a 32-bit field that contains one bit for each exception. When an exception occurs, its
	 * vector is used to select a bit in this field. If the bit is 1, the exception causes a VM exit. If the bit is 0, the exception
	 * is delivered normally through the IDT, using the descriptor corresponding to the exception’s vector
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_EXCEPTION_BITMAP, 0);

	/*
	 * Whether a page fault (exception with vector 14) causes a VM exit is determined by bit 14 in the exception bitmap
	 * as well as the error code produced by the page fault and two 32-bit fields in the VMCS (the page-fault error-code
	 * mask and page-fault error-code match). See Section 25.2 for details.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MASK, 0);
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_PAGEFAULT_ERROR_CODE_MATCH, 0);

	/*
	 * The VM-execution control fields include a set of 4 CR3-target values and a CR3-target count. The CR3-target
	 * values each have 64 bits on processors that support Intel 64 architecture and 32 bits on processors that do not.
	 * The CR3-target count has 32 bits on all processors.
	 *
	 * An execution of MOV to CR3 in VMX non-root operation does not cause a VM exit if its source operand matches one
	 * of these values. If the CR3-target count is n, only the first n CR3-target values are considered; if the CR3-target
	 * count is 0, MOV to CR3 always causes a VM exit
	 * There are no limitations on the values that can be written for the CR3-target values. VM entry fails (see Section
	 * 26.2) if the CR3-target count is greater than 4.
	 * Future processors may support a different number of CR3-target values. Software should read the VMX capability
	 * MSR IA32_VMX_MISC (see Appendix A.6) to determine the number of values supported.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_CR3_TARGET_COUNT, 0);

	/////////////////////////////// VM-Exit Controls ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_VMEXIT_CONTROLS, HvSetupVmcsControlVmExit(Context));

	/*
	 * Default the MSR store/load fields to 0, as we are not storing or loading any MSRs on exit.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT, 0);
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT, 0);


	/////////////////////////////// VM-Entry Controls ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_VMENTRY_CONTROLS, HvSetupVmcsControlVmEntry(Context));

	/*
	 * Default the MSR load fields to 0, as we are not loading any MSRs on entry.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMENTRY_MSR_LOAD_COUNT, 0);

	/*
	 * This field receives basic information associated with the event causing the VM exit.
	 * 
	 * Default to 0 for safety.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMENTRY_INTERRUPTION_INFORMATION_FIELD, 0);

	/*
	 * For VM exits caused by hardware exceptions that would have
	 * delivered an error code on the stack, this field receives that error code.
	 * 
	 * Default to 0 for safety.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMENTRY_EXCEPTION_ERROR_CODE, 0);

	/////////////////////////////// Secondary Processor-Based VM-Execution Controls ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, HvSetupVmcsControlSecondaryProcessor(Context));

	/*
	 * MSR bitmap defines which MSRs in a certain usable range will cause exits.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_MSR_BITMAP_ADDRESS, (SIZE_T)Context->MsrBitmapPhysical);

	/*
	 * Setup Cr0/Cr4 shadowing so values of those registers as read by the guest will equate to the values of the system at setup time.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0);
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0);

	VmxVmwriteFieldFromRegister(VMCS_CTRL_CR0_READ_SHADOW, Context->InitialSpecialRegisters.ControlRegister0);
	VmxVmwriteFieldFromRegister(VMCS_CTRL_CR4_READ_SHADOW, Context->InitialSpecialRegisters.ControlRegister4);

	/* VPID is set here. For all processors, we will use a VPID = 1. This allows the processor to separate caching
	 * of EPT structures away from the regular OS page translation tables in the TLB.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VIRTUAL_PROCESSOR_IDENTIFIER, 1);

	return VmError;
}

/*
 * Configure the Pin-based Control settings of the VMCS.
 */
IA32_VMX_PINBASED_CTLS_REGISTER HvSetupVmcsControlPinBased(PVMM_PROCESSOR_CONTEXT Context)
{
	IA32_VMX_PINBASED_CTLS_REGISTER Register;
	SIZE_T ConfigMSR;

	// Start with default 0 in all bits.
	Register.Flags = 0;

	/*
	 * There are two default states that the VMCS controls can use for setup.
	 * 
	 * The old one has required bits that differ from the new one.
	 * 
	 * If the processor supports the new, "true" MSR, then use that one. Otherwise, fallback on the old one.
	 */
	if (Context->GlobalContext->VmxCapabilities.VmxControls == 1)
	{
		// We can use the true MSR to set the default/reserved values.
		ConfigMSR = ArchGetHostMSR(IA32_VMX_TRUE_PINBASED_CTLS);
	}
	else
	{
		// Otherwise, use the defaults
		ConfigMSR = ArchGetHostMSR(IA32_VMX_PINBASED_CTLS);
	}

	// Encode "must be 1" and "must be 0" bits.
	Register.Flags = HvUtilEncodeMustBeBits(Register.Flags, ConfigMSR);

	return Register;
}

/*
 * Configure the Processor-Based VM-Execution Controls of the VMCS.
 */
IA32_VMX_PROCBASED_CTLS_REGISTER HvSetupVmcsControlProcessor(PVMM_PROCESSOR_CONTEXT Context)
{
	IA32_VMX_PROCBASED_CTLS_REGISTER Register;
	SIZE_T ConfigMSR;

	// Start with default 0 in all bits.
	Register.Flags = 0;

	/* 
	 * Activate secondary controls, since we might want to use some of them.
	 * 
	 * ------------------------------------------------------------------------------------------------------------
	 * 
	 * This control determines whether the secondary processor-based VM-execution controls are
	 * used. If this control is 0, the logical processor operates as if all the secondary processor-based
	 * VM-execution controls were also 0.
	 * 
	 * Bit 31 of the primary processor-based VM-execution controls determines whether the secondary processor-based
	 * VM-execution controls are used. If that bit is 0, VM entry and VMX non-root operation function as if all the
	 * secondary processor-based VM-execution controls were 0. Processors that support only the 0-setting of bit 31 of
	 * the primary processor-based VM-execution controls do not support the secondary processor-based VM-execution
	 * controls.
	 */
	Register.ActivateSecondaryControls = 1;

	/*
	 * Enable MSR bitmaps to determine which ranges of MSRs cause exits. 
	 * This much better (and faster) than all MSRs causing exits.
	 *
	 * ------------------------------------------------------------------------------------------------------------
	 * 
	 * This control determines whether MSR bitmaps are used to control execution of the RDMSR
	 * and WRMSR instructions (see Section 24.6.9 and Section 25.1.3).
	 * For this control, “0” means “do not use MSR bitmaps” and “1” means “use MSR bitmaps.” If the
	 * MSR bitmaps are not used, all executions of the RDMSR and WRMSR instructions cause
	 * VM exits.
	 */
	Register.UseMsrBitmaps = 1;

	/*
	 * There are two default states that the VMCS controls can use for setup.
	 *
	 * The old one has required bits that differ from the new one.
	 *
	 * If the processor supports the new, "true" MSR, then use that one. Otherwise, fallback on the old one.
	 */
	if (Context->GlobalContext->VmxCapabilities.VmxControls == 1)
	{
		// We can use the true MSR to set the default/reserved values.
		ConfigMSR = ArchGetHostMSR(IA32_VMX_TRUE_PROCBASED_CTLS);
	}
	else
	{
		// Otherwise, use the defaults.
		ConfigMSR = ArchGetHostMSR(IA32_VMX_PROCBASED_CTLS);
	}

	// Encode "must be 1" and "must be 0" bits.
	Register.Flags = HvUtilEncodeMustBeBits(Register.Flags, ConfigMSR);

	return Register;
}

/*
 * Configure the Secondary Processor-Based VM-Execution Controls settings of the VMCS.
 */
IA32_VMX_PROCBASED_CTLS2_REGISTER HvSetupVmcsControlSecondaryProcessor(PVMM_PROCESSOR_CONTEXT Context)
{
	IA32_VMX_PROCBASED_CTLS2_REGISTER Register;
	SIZE_T ConfigMSR;

	UNREFERENCED_PARAMETER(Context);

	// Start with default 0 in all bits.
	Register.Flags = 0;

	/*
	 *	Enable EPT feature of the processor. This allows us to virtualize accesses to physical memory.
	 *  
	 *  ------------------------------------------------------------------------------------------------------------
	 *  
	 *  If this control is 1, extended page tables (EPT) are enabled. See Section 28.2.
	 */
	Register.EnableEpt = 1;

	/*
	 *  Windows 10 will attempt to use RDTSCP if it is enabled in CPUID. If it isn't enabled here, it will cause a #UD.
	 *  That's bad, and will definitely crash the system.
     *
	 *  ------------------------------------------------------------------------------------------------------------
	 *  
	 *  If this control is 0, any execution of RDTSCP causes an invalid-opcode exception (#UD).
	 */
	Register.EnableRdtscp = 1;

	/*
	 *  Huge cache performance benefits if we enable VPID in the TLB. This allows the TLB to flush only certain required
	 *  VMX cache entries rather than flushing the entire TLB in the case of certain paging operations.
	 *  
	 *  ------------------------------------------------------------------------------------------------------------
	 *
	 *  If this control is 1, cached translations of linear addresses are associated with a virtualprocessor
     *  identifier (VPID). See Section 28.1.
	 */
	Register.EnableVpid = 1;

	/*
	 *  Windows 10 will attempt to use INVCPID if it is enabled in CPUID. If it isn't enabled here, it will cause a #UD.
	 *  That's bad, and will definitely crash the system.
	 *
	 *  ------------------------------------------------------------------------------------------------------------
	 *
	 *  If this control is 0, any execution of INVCPID causes a #UD.
	 */
	Register.EnableInvpcid = 1;

	/*
	 *  Windows 10 will attempt to use XSAVE/XRESTORE if it is enabled in CPUID. If it isn't enabled here, it will cause a #UD.
	 *  That's bad, and will definitely crash the system.
	 *
	 *  ------------------------------------------------------------------------------------------------------------
	 *
	 *  If this control is 0, any execution of XSAVES or XRSTORS causes a #UD.
	 */
	Register.EnableXsaves = 1;

	/*
	 * Why open another detection vector?
	 * 
	 * ------------------------------------------------------------------------------------------------------------
	 *
	 * If this control is 1, Intel Processor Trace suppresses data packets that indicate the use of
     * virtualization (see Chapter 36).
	 */
	Register.ConcealVmxFromPt = 1;

	/*
	 * There is no "true" CTLS2 register.
	 */
	ConfigMSR = ArchGetHostMSR(IA32_VMX_PROCBASED_CTLS2);

	// Encode "must be 1" and "must be 0" bits.
	Register.Flags = HvUtilEncodeMustBeBits(Register.Flags, ConfigMSR);


	return Register;
}


/*
 * Configure the VM-Entry Controls settings of the VMCS.
 */
IA32_VMX_ENTRY_CTLS_REGISTER HvSetupVmcsControlVmEntry(PVMM_PROCESSOR_CONTEXT Context)
{
	IA32_VMX_ENTRY_CTLS_REGISTER Register;
	SIZE_T ConfigMSR;

	// Start with default 0 in all bits.
	Register.Flags = 0;

	/*
	 *	Ensures the guest is always entering into 64-bit long mode.
	 *	
	 *  ------------------------------------------------------------------------------------------------------------
	 * 
	 *  On processors that support Intel 64 architecture, this control determines whether the logical
	 *  processor is in IA-32e mode after VM entry. Its value is loaded into IA32_EFER.LMA as part of
	 *  VM entry.1
	 *  This control must be 0 on processors that do not support Intel 64 architecture.
	 */
	Register.Ia32EModeGuest = 1;

	/*
	 * Why open another detection vector?
	 *
	 * ------------------------------------------------------------------------------------------------------------
	 *
	 * If this control is 1, Intel Processor Trace suppresses data packets that indicate the use of
	 * virtualization (see Chapter 36).
	 */
	Register.ConcealVmxFromPt = 1;

	/*
	 * There are two default states that the VMCS controls can use for setup.
	 *
	 * The old one has required bits that differ from the new one.
	 *
	 * If the processor supports the new, "true" MSR, then use that one. Otherwise, fallback on the old one.
	 */
	if (Context->GlobalContext->VmxCapabilities.VmxControls == 1)
	{
		// We can use the true MSR to set the default/reserved values.
		ConfigMSR = ArchGetHostMSR(IA32_VMX_TRUE_ENTRY_CTLS);
	}
	else
	{
		// Otherwise, use the defaults
		ConfigMSR = ArchGetHostMSR(IA32_VMX_ENTRY_CTLS);
	}

	// Encode "must be 1" and "must be 0" bits.
	Register.Flags = HvUtilEncodeMustBeBits(Register.Flags, ConfigMSR);

	return Register;
}


/*
 * Configure the VM-Exit Controls settings of the VMCS.
 */
IA32_VMX_EXIT_CTLS_REGISTER HvSetupVmcsControlVmExit(PVMM_PROCESSOR_CONTEXT Context)
{
	IA32_VMX_EXIT_CTLS_REGISTER Register;
	SIZE_T ConfigMSR;

	// Start with default 0 in all bits.
	Register.Flags = 0;


	/*
	 *	Ensures the host is always entering into 64-bit long mode.
	 *
	 *  ------------------------------------------------------------------------------------------------------------
	 *
	 *  On processors that support Intel 64 architecture, this control determines whether a logical
	 *  processor is in 64-bit mode after the next VM exit. Its value is loaded into CS.L,
	 *  IA32_EFER.LME, and IA32_EFER.LMA on every VM exit.1
	 *  This control must be 0 on processors that do not support Intel 64 architecture.
	 */
	Register.HostAddressSpaceSize = 1;


	/*
	 * Why open another detection vector?
	 *
	 * ------------------------------------------------------------------------------------------------------------
	 *
	 * If this control is 1, Intel Processor Trace suppresses data packets that indicate the use of
	 * virtualization (see Chapter 36).
	 */
	Register.ConcealVmxFromPt = 1;

	/*
	 * There are two default states that the VMCS controls can use for setup.
	 *
	 * The old one has required bits that differ from the new one.
	 *
	 * If the processor supports the new, "true" MSR, then use that one. Otherwise, fallback on the old one.
	 */
	if (Context->GlobalContext->VmxCapabilities.VmxControls == 1)
	{
		// We can use the true MSR to set the default/reserved values.
		ConfigMSR = ArchGetHostMSR(IA32_VMX_TRUE_EXIT_CTLS);
	}
	else
	{
		// Otherwise, use the defaults
		ConfigMSR = ArchGetHostMSR(IA32_VMX_EXIT_CTLS);
	}

	// Encode "must be 1" and "must be 0" bits.
	Register.Flags = HvUtilEncodeMustBeBits(Register.Flags, ConfigMSR);

	return Register;
}

