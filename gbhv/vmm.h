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

	/*
	 * Virtual pointer to memory allocated for the VMCS.
	 * 
	 * Carries all state information about the current VMX context.
	 * 
	 * The VMCS contains:
	 *	* Guest-state area
	 *	* Host-state area
	 *	* VM-execution control fields
	 *	* VM-exit control fields
	 *	* VM-entry control fields
	 *	* VM-exit information fields
	 */
	PVMCS VmcsRegion;

	/*
	 * Physical poiner to VmcsRegion.
	 */
	PPHYSVOID VmcsRegionPhysical;

	/*
	 * Contains a bitmap of MSR addresses that will cause exits.
	 * If the bit is 1, that MSR address will cause an exit.
	 */
	VMX_MSR_BITMAP MsrBitmap;

	/*
	 * Physical address of the MsrBitmap.
	 */
	PPHYSVOID MsrBitmapPhysical;

	/*
	 * A structure of captured general purpose, floating point, and xmm registers at the time of VMX initialization.
	 */
	REGISTER_CONTEXT InitialRegisters;

	/*
	 * A structure of captured "special registers" at the time of VMX initialization used to setup the guest VMCS.
	 */
	IA32_SPECIAL_REGISTERS InitialSpecialRegisters;


} VMX_PROCESSOR_CONTEXT, *PVMM_PROCESSOR_CONTEXT;

typedef struct _VMX_VMM_CONTEXT
{
	/*
	 * Number of processor contexts. Equal to the number of logical processors on the host.
	 */
	SIZE_T ProcessorCount;

	/*
	 * Number of processors that have successfully entered VMX mode. If this is less than the number
	 * of logical processors on the system, then there was a critical failure.
	 */
	SIZE_T SuccessfulInitializationsCount;
	/*
	 * List of all processor contexts, indexed by the number processors.
	 */
	PVMM_PROCESSOR_CONTEXT* AllProcessorContexts;

	/*
	 * MSR reports various capabilities of the current running version of VMX.
	 */
	IA32_VMX_BASIC_REGISTER VmxCapabilities;

} VMM_CONTEXT, *PVMM_GLOBAL_CONTEXT;

PVMCS HvAllocateVmcsRegion(PVMM_GLOBAL_CONTEXT GlobalContext);

VOID HvFreeVmmContext(PVMM_GLOBAL_CONTEXT Context);

PVMM_GLOBAL_CONTEXT HvAllocateVmmContext();

BOOL HvInitializeAllProcessors();

VOID HvpDPCBroadcastFunction(_In_ struct _KDPC *Dpc,
	_In_opt_ PVOID DeferredContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2);

BOOL HvInitializeLogicalProcessor(PVMM_PROCESSOR_CONTEXT Context);

PVMM_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext(PVMM_GLOBAL_CONTEXT GlobalContext);

VOID HvFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT Context);
