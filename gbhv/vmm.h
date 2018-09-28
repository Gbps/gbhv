#pragma once
#include "extern.h"
#include "msr.h"
#include "arch.h"
#include "util.h"
#include "os.h"

/*
 * Represents a VMXON region allocated for the processor to do internal state management.
 */
typedef VOID VMXON_REGION, *PVMXON_REGION;

typedef struct _VMX_PROCESSOR_CONTEXT
{
	/*
	 * Virtual pointer to memory allocated for VMXON.
	 *
	 * Used internally by the processor for VMXON, should not be modified during normal operation.
	 */
	PVMXON_REGION VmxonRegion;

} VMX_PROCESSOR_CONTEXT, *PVMX_PROCESSOR_CONTEXT;

BOOL HvInitializeAllProcessors();

ULONG_PTR HvpIPIBroadcastFunction(_In_ ULONG_PTR Argument);

PVMX_PROCESSOR_CONTEXT HvInitializeLogicalProcessor();

PVMX_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext();

VOID HvFreeLogicalProcessorContext(PVMX_PROCESSOR_CONTEXT Context);
