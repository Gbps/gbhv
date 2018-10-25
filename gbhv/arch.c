#pragma once

#include "arch.h"
#include "util.h"
#include "vmx.h"
#include "vmm.h"
#include "intrin.h"

/**
 * Get an MSR by its address and convert it to the specified type.
 */
SIZE_T ArchGetHostMSR(ULONG MsrAddress)
{
    return __readmsr(MsrAddress);
}

/*
 * Get a CPUID register.
 */
UINT32 ArchGetCPUID(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister)
{
    INT32 CPUInfo[4];

    __cpuidex(CPUInfo, FunctionId, SubFunctionId);

    return (UINT32)CPUInfo[CPUIDRegister];
}

/*
 * Returns TRUE if the CPU feature is present.
 */
BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit)
{
    UINT32 Register;

    Register = ArchGetCPUID(FunctionId, SubFunctionId, CPUIDRegister);

    return HvUtilBitIsSet(Register, FeatureBit);
}

/*
 * Check if VMX support is enabled on the processor.
 */
BOOL ArchIsVMXAvailable()
{
    return ArchIsCPUFeaturePresent(CPUID_VMX_ENABLED_FUNCTION,
                                   CPUID_VMX_ENABLED_SUBFUNCTION,
                                   CPUID_REGISTER_ECX,
                                   CPUID_VMX_ENABLED_BIT);
}

/*
 * Get the IA32_VMX_BASIC MSR.
 * 
 * Reporting Register of Basic VMX Capabilities.
 */
IA32_VMX_BASIC_REGISTER ArchGetBasicVmxCapabilities()
{
    IA32_VMX_BASIC_REGISTER Register;

    Register.Flags = ArchGetHostMSR(IA32_VMX_BASIC);

    return Register;
}

/*
 * Enables "Virtual Machine Extensions Enable" bit in CR4 (bit 13)
 */
VOID ArchEnableVmxe()
{
    CR4 Register;

    // Get CR4
    Register.Flags = __readcr4();

    // Enable the bit
    Register.VmxEnable = 1;

    // Write it back to cr4
    __writecr4(Register.Flags);

    // Read back to verify
    Register.Flags = __readcr4();
}

/*
 * Disable "Virtual Machine Extensions Enable" bit in CR4 (bit 13)
 */
VOID ArchDisableVmxe()
{
    CR4 Register;

    // Get CR4
    Register.Flags = __readcr4();

    // Enable the bit
    Register.VmxEnable = 0;

    // Write it back to cr4
    __writecr4(Register.Flags);
}

VOID ArchCaptureSpecialRegisters(PIA32_SPECIAL_REGISTERS Registers)
{
    /*
	 * Control registers
	 */
    Registers->ControlRegister0.Flags = __readcr0();
    Registers->ControlRegister3.Flags = __readcr3();
    Registers->ControlRegister4.Flags = __readcr4();


	// Enable XSETBV on host, or else we'll #GP on XSETBV exits.
	Registers->ControlRegister4.OsXsave = 1;

    /*
	 * Global Descriptor Table and Interrupt Descriptor Table
	 */
    _sgdt(&Registers->GlobalDescriptorTableRegister.Limit);
    __sidt(&Registers->InterruptDescriptorTableRegister.Limit);

    /*
	 * Task register
	 */
    Registers->TaskRegister = ArchReadTaskRegister();

    /*
	 * LDT selector
	 */
    Registers->LocalDescriptorTableRegister = ArchReadLocalDescriptorTableRegister();

    /*
	 * Debug register DR7
	 */
    Registers->DebugRegister7.Flags = __readdr(7);

    /*
	 * EFLAGS (RFLAGS) register
	 */
    Registers->RflagsRegister.Flags = (UINT32)__readeflags();

    /*
	 * Required MSRs that will be loaded to the guest
	 */
    Registers->DebugControlMsr.Flags = __readmsr(IA32_DEBUGCTL);
    Registers->SysenterCsMsr.Flags = __readmsr(IA32_SYSENTER_CS);
    Registers->SysenterEspMsr = __readmsr(IA32_SYSENTER_ESP);
    Registers->SysenterEipMsr = __readmsr(IA32_SYSENTER_EIP);
    Registers->GlobalPerfControlMsr = __readmsr(IA32_PERF_GLOBAL_CTRL);
    Registers->PatMsr.Flags = __readmsr(IA32_PAT);
    Registers->EferMsr.Flags = __readmsr(IA32_EFER);
    // Not including yet:
    // Registers->BindConfigMsr.Flags = __readmsr(IA32_BNDCFGS);
}
