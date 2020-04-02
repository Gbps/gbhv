#include "os.h"
#include "util.h"
#include "arch.h"

/*
 * Pool tag for memory allocations.
 */
#define HV_POOL_TAG (ULONG)'vhbG'

/*
 * Get the number of CPUs on the system
 */
SIZE_T OsGetCPUCount()
{
	return (SIZE_T)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
}

/*
 * Get the current CPU number of the processor executing this function.
 */
SIZE_T OsGetCurrentProcessorNumber()
{
	return (SIZE_T)KeGetCurrentProcessorNumberEx(NULL);
}

/*
 * Allocate a number of page-aligned, contiguous pages of memory and return a pointer to the region.
 * 
 * Returns NULL if the pages could not be allocated.
 */
PVOID OsAllocateContiguousAlignedPages(SIZE_T NumberOfPages)
{
	PHYSICAL_ADDRESS MaxSize;
	PVOID Output;

	// Allocate address anywhere in the OS's memory space
	MaxSize.QuadPart = MAXULONG64;

	Output = MmAllocateContiguousMemory(NumberOfPages * PAGE_SIZE, MaxSize);

	if(Output == NULL)
	{
		HvUtilLogError("OsAllocateContiguousAlignedPages: Out of memory!\n");
	}

	return Output;
}

/*
 * Free a region of pages allocated by OsAllocateContiguousAlignedPages.
 */
VOID OsFreeContiguousAlignedPages(PVOID PageRegionAddress)
{
	MmFreeContiguousMemory(PageRegionAddress);
}

/*
 * Allocate generic, nonpaged r/w memory.
 * 
 * Returns NULL if the bytes could not be allocated.
 */
PVOID OsAllocateNonpagedMemory(SIZE_T NumberOfBytes)
{
	PVOID Output;
	
	Output = ExAllocatePoolWithTag(NonPagedPoolNx, NumberOfBytes, HV_POOL_TAG);

	if (Output == NULL)
	{
		HvUtilLogError("OsAllocateNonpagedMemory: Out of memory!\n");
	}

	return Output;
}

/*
 * Allocate generic, nonpaged, executable r/w memory.
 *
 * Returns NULL if the bytes could not be allocated.
 */
PVOID OsAllocateExecutableNonpagedMemory(SIZE_T NumberOfBytes)
{
	PVOID Output;

	Output = ExAllocatePoolWithTag(NonPagedPool, NumberOfBytes, HV_POOL_TAG);

	if (Output == NULL)
	{
		HvUtilLogError("OsAllocateExecutableNonpagedMemory: Out of memory!\n");
	}

	return Output;
}


/*
 * Free memory allocated with OsAllocateNonpagedMemory.
 */
VOID OsFreeNonpagedMemory(PVOID MemoryPointer)
{
	ExFreePoolWithTag(MemoryPointer, HV_POOL_TAG);
}

/*
 * Convert a virtual address to a physical address.
 */
PPHYSVOID OsVirtualToPhysical(PVOID VirtualAddress)
{
	return (PPHYSVOID)MmGetPhysicalAddress(VirtualAddress).QuadPart;
}

/*
 * Convert a physical address to a virtual address.
 */
PVOID OsPhysicalToVirtual(PPHYSVOID PhysicalAddressIn)
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PhysicalAddress.QuadPart = (ULONG64)PhysicalAddressIn;

	return (PVOID)MmGetVirtualForPhysical(PhysicalAddress);
}

/*
 * Zero out Length bytes of a region of memory.
 */
VOID OsZeroMemory(PVOID VirtualAddress, SIZE_T Length)
{
	RtlZeroMemory(VirtualAddress, Length);
}

/*
 * OS-dependent version of RtlCaptureContext from NT.
 */
VOID OsCaptureContext(PREGISTER_CONTEXT ContextRecord)
{
	ArchCaptureContext(ContextRecord);
}

/*
 * OS-dependent version of RtlRestoreContext from NT.
 */
VOID OsRestoreContext(PREGISTER_CONTEXT ContextRecord)
{
	RtlRestoreContext((PCONTEXT)ContextRecord, NULL);
}
