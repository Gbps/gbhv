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