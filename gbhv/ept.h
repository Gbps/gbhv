#pragma once
#include "arch.h"

typedef struct _VMX_VMM_CONTEXT VMX_VMM_CONTEXT, *PVMM_CONTEXT;

BOOL HvEptInitialize(PVMM_CONTEXT GlobalContext);

typedef struct _MTRR_RANGE_DESCRIPTOR
{
	SIZE_T PhysicalBaseAddress;
	SIZE_T SizeOfRange;
	UCHAR MemoryType;
} MTRR_RANGE_DESCRIPTOR, *PMTRR_RANGE_DESCRIPTOR;