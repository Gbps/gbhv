#pragma once
#include "arch.h"

typedef struct _VMX_VMM_CONTEXT VMX_VMM_CONTEXT, *PVMM_CONTEXT;

BOOL HvEptInitialize(PVMM_CONTEXT GlobalContext);

typedef struct _MTRR_RANGE_DESCRIPTOR
{
	SIZE_T PhysicalBaseAddress;
	SIZE_T PhysicalEndAddress;
	UCHAR MemoryType;
} MTRR_RANGE_DESCRIPTOR, *PMTRR_RANGE_DESCRIPTOR;

/**
 * Okay, you can totally shoot me here but I *hate* the naming scheme of PDE PTE PDPTE and PML4 that Intel uses.
 * It just makes way more sense to me to simply annotate each level of the table by its number.
 * 
 * PML4 == The 4th level of page table translation.
 * PML3 == The 3rd level of page table translation... etc.
 * 
 */

/**
 * The number of 512GB PML4 entries in the page table
 */
#define VMM_EPT_PML4E_COUNT 512

/**
 * The number of 1GB PDPT entries in the page table per 512GB PML4 entry.
 */
#define VMM_EPT_PML3E_COUNT 512

/**
 * Then number of 2MB Page Directory entries in the page table per 1GB PML3 entry.
 */
#define VMM_EPT_PML2E_COUNT 512

/**
 * Integer 2MB
 */
#define SIZE_2_MB ((SIZE_T)(512 * PAGE_SIZE))

typedef EPT_PML4 EPT_PML4_POINTER, *PEPT_PML4_POINTER;
typedef EPDPTE_1GB EPT_PML3_POINTER, *PEPT_PML3_POINTER;
typedef EPDE_2MB EPT_PML2_ENTRY, *PEPT_PML2_ENTRY;

typedef struct _VMM_EPT_PAGE_TABLE
{
	/**
	 * 28.2.2 Describes 512 contiguous 512GB memory regions each with 512 1GB regions.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPT_PML4_POINTER PML4[VMM_EPT_PML4E_COUNT];
	
	/**
	 * Describes exactly 512 contiguous 1GB memory regions within a our singular 512GB PML4 region.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPT_PML3_POINTER PML3[VMM_EPT_PML3E_COUNT];

	/**
	 * For each 1GB PML3 entry, create 512 2MB entries to map identity. 
	 * NOTE: We are using 2MB pages as the smallest paging size in our map, so we do not manage individiual 4096 byte pages.
	 * Therefore, we do not allocate any PML1 (4096 byte) paging structures.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPT_PML2_ENTRY PML2[VMM_EPT_PML3E_COUNT][VMM_EPT_PML2E_COUNT];

} VMM_EPT_PAGE_TABLE, *PVMM_EPT_PAGE_TABLE;