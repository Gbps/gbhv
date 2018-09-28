#pragma once

#include "extern.h"

/*
 * Pool tag for memory allocations.
 */
#define HV_POOL_TAG (ULONG)'vhbG'

SIZE_T OsGetCPUCount();

SIZE_T OsGetCurrentProcessorNumber();

PVOID OsAllocateContiguousAlignedPages(SIZE_T NumberOfPages);

VOID OsFreeContiguousAlignedPages(PVOID PageRegionAddress);

PVOID OsAllocateNonpagedMemory(SIZE_T NumberOfBytes);

VOID OsFreeNonpagedMemory(PVOID MemoryPointer);