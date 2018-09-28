#pragma once
#include "extern.h"

/*
 * Represents a VMXON region allocated for the processor to do internal state management.
 */
typedef struct _VMXON_REGION
{
	/*
	 * Initialize the version identifier in the VMXON region (the first 31 bits) with the VMCS revision identifier 
	 * reported by capability MSRs. 
	 * 
	 * Clear bit 31 of the first 4 bytes of the VMXON region.
	 */
	UINT32 VmcsRevisionNumber;

	/*
	 * Unknown processor implemented data follows...
	 */
} VMXON_REGION, *PVMXON_REGION;


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
