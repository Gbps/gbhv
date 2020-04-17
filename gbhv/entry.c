#include "vmm.h"
#include "vmx.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
);

PVMM_CONTEXT GlobalContext;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	
	DriverObject->DriverUnload = DriverUnload;

	HvUtilLog("--------------------------------------------------------------\n");

	GlobalContext = HvInitializeAllProcessors();

	// Initialize Hypervisor
	if(!GlobalContext)
	{
		// TODO: Fix this!!!!
		return STATUS_SUCCESS;
	}

	return STATUS_SUCCESS;
}


/**
 * DPC to exit VMX on all processors.
 */
VOID NTAPI ExitRootModeOnAllProcessors(_In_ struct _KDPC *Dpc,
	_In_opt_ PVOID DeferredContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2)
{
	SIZE_T CurrentProcessorNumber;
	PVMM_PROCESSOR_CONTEXT CurrentContext;

	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(DeferredContext);

	// Get the current processor number we're executing this function on right now
	CurrentProcessorNumber = OsGetCurrentProcessorNumber();

	// Get the logical processor context that was allocated for this current processor
	CurrentContext = HvGetCurrentCPUContext(GlobalContext);

	// Initialize processor for VMX
	if (VmxExitRootMode(CurrentContext))
	{
		HvUtilLogDebug("ExitRootModeOnAllProcessors[#%i]: Exiting VMX mode.\n", CurrentProcessorNumber);
	}
	else
	{
		HvUtilLogError("ExitRootModeOnAllProcessors[#%i]: Failed to exit VMX mode.\n", CurrentProcessorNumber);
	}

	// These must be called for GenericDpcCall to signal other processors
	// SimpleVisor code shows how to do this

	// Wait for all DPCs to synchronize at this point
	KeSignalCallDpcSynchronize(SystemArgument2);

	// Mark the DPC as being complete
	KeSignalCallDpcDone(SystemArgument1);
}

VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
)
{
	UNREFERENCED_PARAMETER(DriverObject);

	if(GlobalContext)
	{
		KeGenericCallDpc(ExitRootModeOnAllProcessors, (PVOID)GlobalContext);
	}

}