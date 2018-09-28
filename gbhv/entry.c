#include "vmm.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	HvUtilLog("--------------------------------------------------------------");

	// Initialize Hypervisor
	if(!HvInitializeAllProcessors())
	{
		return STATUS_DEVICE_BUSY;
	}

	// Do not keep driver loaded.
	return STATUS_DEVICE_PAPER_EMPTY;
}