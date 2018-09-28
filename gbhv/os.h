#pragma once

#include "extern.h"

SIZE_T OsGetCPUCount();

SIZE_T OsGetCurrentProcessorNumber();

PVOID OsAllocateContiguousAlignedPages(SIZE_T NumberOfPages);

VOID OsFreeContiguousAlignedPages(PVOID PageRegionAddress);

PVOID OsAllocateNonpagedMemory(SIZE_T NumberOfBytes);

VOID OsFreeNonpagedMemory(PVOID MemoryPointer);

PPHYSVOID OsVirtualToPhysical(PVOID VirtualAddress);

PVOID OsPhysicalToVirtual(PPHYSVOID PhysicalAddress);

VOID OsZeroMemory(PVOID VirtualAddress, SIZE_T Length);