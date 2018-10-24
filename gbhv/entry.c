#include "vmm.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
);

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	
	DriverObject->DriverUnload = DriverUnload;

	HvUtilLog("--------------------------------------------------------------");

	// Initialize Hypervisor
	if(!HvInitializeAllProcessors())
	{
		// TODO: Fix this!!!!
		return STATUS_SUCCESS;
	}

	return STATUS_SUCCESS;
}

VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
)
{
	UNREFERENCED_PARAMETER(DriverObject);

	return;
}