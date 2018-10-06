#pragma once

// PHNT_MODE_KERNEL
#define PHNT_MODE 0

// PHNT_REDSTONE4
#define PHNT_VERSION 105

/*
 * WDK includes
 */
#include <intrin.h>
#include <ntifs.h>

#pragma warning(push, 0)

/*
 * PHNT is the library of reversed structures used by Process Hacker.
 */
#include "phnt/phnt.h"

/*
 * IA32-doc has structures for the entire intel SDM... pretty insane tbh.
 */
#include <ia32.h>

#pragma warning(pop)

/*
 * Some additional types used by everything.
 */
typedef UINT32 BOOL;

/*
 * A physical address pointer
 */
typedef PVOID PPHYSVOID;

/*
 * Pointer values that don't follow NT code style.
 */
typedef VMCS* PVMCS;

/*
 * GDT/IDT Segment Descriptors
 */
typedef SEGMENT_DESCRIPTOR_64* PSEGMENT_DESCRIPTOR_64;

typedef CR0* PCR0;
typedef CR4* PCR4;

/*
 * NT APIs for DPCs Generic Calls (that for some reason aren't in the WDK)
 */
NTKERNELAPI
_IRQL_requires_max_(APC_LEVEL)
_IRQL_requires_min_(PASSIVE_LEVEL)
_IRQL_requires_same_
VOID
KeGenericCallDpc(
	_In_ PKDEFERRED_ROUTINE Routine,
	_In_opt_ PVOID Context
);


NTKERNELAPI
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
VOID
KeSignalCallDpcDone(
	_In_ PVOID SystemArgument1
);

NTKERNELAPI
_IRQL_requires_(DISPATCH_LEVEL)
_IRQL_requires_same_
LOGICAL
KeSignalCallDpcSynchronize(
	_In_ PVOID SystemArgument2
);

DECLSPEC_NORETURN
NTSYSAPI
VOID
RtlRestoreContext(
	_In_ PCONTEXT ContextRecord,
	_In_opt_ struct _EXCEPTION_RECORD * ExceptionRecord
);