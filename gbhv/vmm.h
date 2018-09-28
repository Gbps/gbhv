#pragma once
#include "extern.h"
#include "msr.h"
#include "arch.h"
#include "util.h"
#include "os.h"

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

typedef struct _VMX_PROCESSOR_CONTEXT
{
	/*
	 * Virtual pointer to memory allocated for VMXON.
	 *
	 * Used internally by the processor for VMXON, should not be modified during normal operation.
	 */
	PVMXON_REGION VmxonRegion;

	/*
	 * Physical pointer to memory allocated for VMXON.
	 */
	PPHYSVOID VmxonRegionPhysical;


} VMX_PROCESSOR_CONTEXT, *PVMX_PROCESSOR_CONTEXT;

BOOL HvInitializeAllProcessors();

ULONG_PTR HvpIPIBroadcastFunction(_In_ ULONG_PTR Argument);

VOID HvInitializeLogicalProcessor(PVMX_PROCESSOR_CONTEXT Context);

PVMX_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext();

VOID HvFreeLogicalProcessorContext(PVMX_PROCESSOR_CONTEXT Context);
