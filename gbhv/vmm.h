#pragma once
#include "extern.h"
#include "vmm_settings.h"
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


typedef struct _VMX_VMM_CONTEXT VMX_VMM_CONTEXT, *PVMM_GLOBAL_CONTEXT;

typedef struct _VMM_HOST_STACK_REGION
{
	/*
	 * Above the context pointer is the actual host stack which will be used by the exit handler
	 * for general operation.
	 */
	CHAR HostStack[VMM_SETTING_STACK_SPACE];

	/*
	 * Top of the host stack must always have a pointer to the global context.
	 * This allows the exit handler to access the global context after the host area is loaded.
	 */
	PVMM_GLOBAL_CONTEXT GlobalContext;

} VMM_HOST_STACK_REGION, *PVMM_HOST_STACK_REGION;

typedef struct _VMM_PROCESSOR_CONTEXT
{
	/*
	 * Pointer back to the global context structure.
	 */
	PVMM_GLOBAL_CONTEXT GlobalContext;

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
	PVMX_MSR_BITMAP MsrBitmap;

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

	/*
	 * Stack space allocated for host operation.
	 * 
	 * When the processor enters host mode from the guest, RSP = HostStack.
	 * 
	 * At the top of the host stack is the pointer to the global context, used by vmxdefs.asm to
	 * grab find the logical processor context in host operation.
	 */
	VMM_HOST_STACK_REGION HostStack;
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

	/*
	 * The SYSTEM process DirectoryTableBase, or CR3 at the moment of kernel execution.
	 * 
	 * Saved here due to the fact that DPCs might execute with a usermode process address space.
	 * 
	 * We want to make sure the VMM restores host context in the SYSTEM process.
	 */
	SIZE_T SystemDirectoryTableBase;

} VMM_CONTEXT, *PVMM_GLOBAL_CONTEXT;

PVMCS HvAllocateVmcsRegion(PVMM_GLOBAL_CONTEXT GlobalContext);

VOID HvFreeVmmContext(PVMM_GLOBAL_CONTEXT Context);

PVMM_GLOBAL_CONTEXT HvAllocateVmmContext();

PVMM_PROCESSOR_CONTEXT HvGetCurrentCPUContext(PVMM_GLOBAL_CONTEXT GlobalContext);

BOOL HvInitializeAllProcessors();

VOID HvpDPCBroadcastFunction(_In_ struct _KDPC *Dpc,
	_In_opt_ PVOID DeferredContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2);

/*
 * Defined in vmxdefs.asm.
 * 
 * Saves register contexts and calls HvInitialzeLogicalProcessor
 */
BOOL HvBeginInitializeLogicalProcessor(PVMM_PROCESSOR_CONTEXT Context);

/*
 * Defined in vmxdefs.asm.
 *
 * Saves register contexts and calls HvHandleVmExit
 */
VOID HvEnterFromGuest();

VOID HvInitializeLogicalProcessor(PVMM_PROCESSOR_CONTEXT Context, SIZE_T GuestRSP, SIZE_T GuestRIP);

PVMM_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext(PVMM_GLOBAL_CONTEXT GlobalContext);

VOID HvFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT Context);
