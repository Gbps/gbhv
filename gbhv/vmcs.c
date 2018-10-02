#include "vmcs.h"
#include "arch.h"
#include "vmm.h"
#include "util.h"
#include "vmx.h"

VMX_ERROR HvSetupVmcsDefaults(PVMM_CONTEXT GlobalContext, PVMX_PROCESSOR_CONTEXT Context)
{
	//VMX_ERROR VmError;
	UNREFERENCED_PARAMETER(GlobalContext);

	/*
	 * Capture the current state of gp, float, and xmm registers of the processor.
	 */
	ArchCaptureContext(&Context->InitialRegisters);

	/*
	 * Capture the current state of special registers of the processor.
	 * These values will be used to correctly setup initial values in the VMCS.
	 */
	ArchCaptureSpecialRegisters(&Context->InitialSpecialRegisters);
	
	return 0;
}
VMX_ERROR HvSetupVmcsGuestArea(PVMM_CONTEXT GlobalContext, PVMX_PROCESSOR_CONTEXT Context)
{
	PREGISTER_CONTEXT Registers;
	VMX_ERROR VmError;

	Registers = &Context->InitialRegisters;

	//VMX_ERROR VmError;
	UNREFERENCED_PARAMETER(GlobalContext);
	UNREFERENCED_PARAMETER(Context);

	/* ES Segment */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_ES_SELECTOR, Registers->SegEs.Flags);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_ES_LIMIT, Registers->SegEs.);
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_ES_ACCESS_RIGHTS, __accessright(Registers->SegEs));

	return VmError;
}

VMX_ERROR HvSetupVmcsControlFields(PVMM_CONTEXT GlobalContext, PVMX_PROCESSOR_CONTEXT Context)
{
	VMX_ERROR VmError;

	/*
	 * VMCS Link Pointer should always be 0xFFFFFFFFFFFFFFFF (=== ~0ULL)
	 */
	VmxVmwriteFieldFromImmediate(VMCS_GUEST_VMCS_LINK_POINTER, ~0ULL);

	/////////////////////////////// Pin-based Control ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_PIN_BASED_VM_EXECUTION_CONTROLS, HvSetupVmcsControlPinBased(GlobalContext));

	/////////////////////////////// Processor-Based VM-Execution Controls ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, HvSetupVmcsControlProcessor(GlobalContext));

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
	VmxVmwriteFieldFromRegister(VMCS_CTRL_VMEXIT_CONTROLS, HvSetupVmcsControlVmExit(GlobalContext));

	/*
	 * Default the MSR store/load fields to 0, as we are not storing or loading any MSRs on exit.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMEXIT_MSR_STORE_COUNT, 0);
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_VMEXIT_MSR_LOAD_COUNT, 0);


	/////////////////////////////// VM-Entry Controls ///////////////////////////////
	VmxVmwriteFieldFromRegister(VMCS_CTRL_VMENTRY_CONTROLS, HvSetupVmcsControlVmEntry(GlobalContext));

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
	VmxVmwriteFieldFromRegister(VMCS_CTRL_SECONDARY_PROCESSOR_BASED_VM_EXECUTION_CONTROLS, HvSetupVmcsControlSecondaryProcessor(GlobalContext));

	/*
	 * MSR bitmap defines which MSRs in a certain usable range will cause exits.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_MSR_BITMAP_ADDRESS, (SIZE_T)Context->MsrBitmapPhysical);

	/*
	 * Setup Cr0/Cr4 shadowing so values of those registers as read by the guest will equate to the values of the system at setup time.
	 */
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_CR0_GUEST_HOST_MASK, 0);
	VmxVmwriteFieldFromImmediate(VMCS_CTRL_CR4_GUEST_HOST_MASK, 0);

	VmxVmwriteFieldFromRegister(VMCS_CTRL_CR0_READ_SHADOW, Context->InitialSpecialRegisters.RegisterCr0);
	VmxVmwriteFieldFromRegister(VMCS_CTRL_CR4_READ_SHADOW, Context->InitialSpecialRegisters.RegisterCr4);

	return VmError;
}

/*
 * Configure the Pin-based Control settings of the VMCS.
 */
IA32_VMX_PINBASED_CTLS_REGISTER HvSetupVmcsControlPinBased(PVMM_CONTEXT GlobalContext)
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
	if (GlobalContext->VmxCapabilities.VmxControls == 1)
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
IA32_VMX_PROCBASED_CTLS_REGISTER HvSetupVmcsControlProcessor(PVMM_CONTEXT GlobalContext)
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
	if (GlobalContext->VmxCapabilities.VmxControls == 1)
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
IA32_VMX_PROCBASED_CTLS2_REGISTER HvSetupVmcsControlSecondaryProcessor(PVMM_CONTEXT GlobalContext)
{
	IA32_VMX_PROCBASED_CTLS2_REGISTER Register;
	SIZE_T ConfigMSR;

	UNREFERENCED_PARAMETER(GlobalContext);

	// Start with default 0 in all bits.
	Register.Flags = 0;

	/*
	 *	TODO: Do not enable EPT quite yet.
	 *  
	 *  ------------------------------------------------------------------------------------------------------------
	 *  
	 *  If this control is 1, extended page tables (EPT) are enabled. See Section 28.2.
	 */
	Register.EnableEpt = 0;

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
IA32_VMX_ENTRY_CTLS_REGISTER HvSetupVmcsControlVmEntry(PVMM_CONTEXT GlobalContext)
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
	if (GlobalContext->VmxCapabilities.VmxControls == 1)
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
IA32_VMX_EXIT_CTLS_REGISTER HvSetupVmcsControlVmExit(PVMM_CONTEXT GlobalContext)
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
	if (GlobalContext->VmxCapabilities.VmxControls == 1)
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

