#pragma once

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
 * CPUID Register in CPUID result to check if VMX is enabled.
 *
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_REGISTER_ECX 2

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
