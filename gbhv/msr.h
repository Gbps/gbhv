#pragma once
#include <basetsd.h>

/*
 * Bit 0: Lock bit. Must be 0 from the BIOS.
 * Bit 1: VMXON in SMX operation.
 * Bit 2: VMXON outside SMX operation.
 */
#define MSR_IA32_FEATURE_CONTROL_ADDRESS 0x3A

 /* Lock bit. Must be 0 from the BIOS. */
#define FEATURE_LOCK_BIT 0

/* VMXON allowed in SMX operation. */
#define FEATURE_ALLOW_IN_SMX_OPERATION 1

/* VMXON allowed outside of SMX operation. */
#define FEATURE_ALLOW_OUTSIDE_SMX_OPERATION 2
