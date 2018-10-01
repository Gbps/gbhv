#pragma once
#include "extern.h"
#include "vmm.h"

/*
 * CPUID Function identifier to check if VMX is enabled.
 * 
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_FUNCTION 1

/* 
 * CPUID Subfunction identifier to check if VMX is enabled.
 * 
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_SUBFUNCTION 0

 /*
  * CPUID Register EAX.
  */
#define CPUID_REGISTER_EAX 0

 /*
  * CPUID Register EBX.
  */
#define CPUID_REGISTER_EBX 1

/*
 * CPUID Register ECX.
 */
#define CPUID_REGISTER_ECX 2

 /*
  * CPUID Register EDX.
  */
#define CPUID_REGISTER_EDX 3

/*
 * CPUID VMX support enabled bit.
 *
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_BIT 5

 /*
  * The VMCS and VMXON region can be, at max, 4096 bytes, or one page. But the manual straight up contradicts itself, so
  * I'm not sure. Best be safe and allocate two pages.
  *
  * Vol 3D A-1 Basic VMX Information
  *
  * Bits 44:32 report the number of bytes that software should allocate for the VMXON region and any VMCS region.
  * It is a value greater than 0 and at most 4096 (bit 44 is set if and only if bits 43:32 are clear).
*/
#define VMX_VMXON_NUMBER_PAGES 2
#define VMX_VMCS_NUMBER_PAGES 2

BOOL VmxEnterRootMode(PVMX_PROCESSOR_CONTEXT Context);
BOOL VmxExitRootMode(PVMX_PROCESSOR_CONTEXT Context);

/*
 * Used to write a vmcs field using vmwrite and an ia32-doc register type.
 */
#define VmxVmwriteFieldFromRegister(_FIELD_DEFINE_, _REGISTER_VAR_) \
	VmError |= __vmx_vmwrite(_FIELD_DEFINE_, _REGISTER_VAR_.Flags);

/*
 * Used to write a vmcs field using vmwrite and an immediate value.
 */
#define VmxVmwriteFieldFromImmediate(_FIELD_DEFINE_, _IMMEDIATE_) \
	VmError |= __vmx_vmwrite(_FIELD_DEFINE_, _IMMEDIATE_);