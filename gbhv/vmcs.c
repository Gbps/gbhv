#include "vmcs.h"
#include "arch.h"
#include "vmm.h"
#include "util.h"

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