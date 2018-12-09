#include "arch.h"
#include "util.h"
#include "debugaux.h"
#include "vmm.h"

/**
 * Checks to ensure that the processor supports all EPT features that we want to use.
 */
BOOL HvEptCheckFeatures()
{
	IA32_VMX_EPT_VPID_CAP_REGISTER VpidRegister;
	IA32_MTRR_DEF_TYPE_REGISTER MTRRDefType;

	VpidRegister.Flags = ArchGetHostMSR(IA32_VMX_EPT_VPID_CAP);
	MTRRDefType.Flags = ArchGetHostMSR(IA32_MTRR_DEF_TYPE);

	if (!VpidRegister.PageWalkLength4 || !VpidRegister.MemoryTypeWriteBack || !VpidRegister.Pde2MbPages)
	{
		return FALSE;
	}

	if (!MTRRDefType.MtrrEnable)
	{
		HvUtilLogError("MTRR Dynamic Ranges not supported");
		return FALSE;
	}

	HvUtilLogDebug("All EPT features present.");
	return TRUE;
}

/**
 * Uses the Memory Type Range Register MSRs to build a map of the physical memory on the system.
 * This will be used to build an identity mapped EPT PML4 table which directly maps physical memory
 * of the guest to the system physical memory map containing the same memory type ranges.
 */
BOOL HvEptBuildMTRRMap(PVMM_CONTEXT GlobalContext)
{
	IA32_MTRR_CAPABILITIES_REGISTER MTRRCap;
	IA32_MTRR_PHYSBASE_REGISTER CurrentPhysBase;
	IA32_MTRR_PHYSMASK_REGISTER CurrentPhysMask;
	PMTRR_RANGE_DESCRIPTOR Descriptor;
	ULONG CurrentRegister;
	ULONG NumberOfBitsInMask;


	MTRRCap.Flags = ArchGetHostMSR(IA32_MTRR_CAPABILITIES);

	HvUtilLogDebug("EPT: Number of dynamic ranges: %d", MTRRCap.VariableRangeCount);

	for(CurrentRegister = 0; CurrentRegister < MTRRCap.VariableRangeCount; CurrentRegister++)
	{
		// For each dynamic register pair
		CurrentPhysBase.Flags = ArchGetHostMSR(IA32_MTRR_PHYSBASE0 + (CurrentRegister * 2));
		CurrentPhysMask.Flags = ArchGetHostMSR(IA32_MTRR_PHYSMASK0 + (CurrentRegister * 2));
	
		// Is the range enabled?
		if(CurrentPhysMask.Valid)
		{
			// We only need to read these once because the ISA dictates that MTRRs are to be synchronized between all processors
			// during BIOS initialization.
			Descriptor = &GlobalContext->MemoryRanges[GlobalContext->NumberOfEnabledMemoryRanges++];

			// Calculate the base address in bytes
			Descriptor->PhysicalBaseAddress = CurrentPhysBase.PageFrameNumber * PAGE_SIZE;

			// Calculate the total size of the range
			// The lowest bit of the mask that is set to 1 specifies the size of the range
			_BitScanForward64(&NumberOfBitsInMask, CurrentPhysMask.PageFrameNumber * PAGE_SIZE);

			// Size of the range in bytes + Base Address
			Descriptor->PhysicalEndAddress = Descriptor->PhysicalBaseAddress + ((1ULL << NumberOfBitsInMask) - 1ULL);

			// Memory Type (cacheability attributes)
			Descriptor->MemoryType = (UCHAR) CurrentPhysBase.Type;

			if(Descriptor->MemoryType == MEMORY_TYPE_WRITE_BACK)
			{
				/* This is already our default, so no need to store this range. 
				 * Simply 'free' the range we just wrote. */
				GlobalContext->NumberOfEnabledMemoryRanges--;
			}
			HvUtilLogDebug("MTRR Range: Base=0x%llX End=0x%llX Type=0x%X", Descriptor->PhysicalBaseAddress, Descriptor->PhysicalEndAddress, Descriptor->MemoryType);
		}
	}

	HvUtilLogDebug("Total MTRR Ranges Committed: %d", GlobalContext->NumberOfEnabledMemoryRanges);

	return TRUE;
}


/*
 * Creates a 2MB identity mapped PML2 entry with a cacheability type specified by system MTRRs.
 * 
 * We must ensure that we map each 2MB entry with the correct cacheability type for performance. 
 * Unfortunately, the smallest paging structure is 4096 bytes so we have to mark the whole 2MB region as the least prohibitive cache type. 
 * In real systems, this not much of a problem, as there are just never single pages marked with certain cacheability attributes except for the first 1MB.
 */
VOID HvEptSetupPML2Entry(PVMM_CONTEXT GlobalContext, PEPT_PML2_ENTRY NewEntry, SIZE_T PageFrameNumber)
{
	SIZE_T AddressOfPage;
	SIZE_T CurrentMtrrRange;
	SIZE_T TargetMemoryType;

	/*
	 * Each of the 512 collections of 512 PML2 entries is setup here.
	 * This will, in total, identity map every physical address from 0x0 to physical address 0x8000000000 (512GB of memory)
	 *
	 * ((EntryGroupIndex * VMM_EPT_PML2E_COUNT) + EntryIndex) * 2MB is the actual physical address we're mapping
	 */
	NewEntry->PageFrameNumber = PageFrameNumber;

	/* Size of 2MB page * PageFrameNumber == AddressOfPage (physical memory). */
	AddressOfPage = PageFrameNumber * SIZE_2_MB;

    /* To be safe, we will map the first page as UC as to not bring up any kind of undefined behavior from the 
     * fixed MTRR section which we are not formally recognizing (typically there is MMIO memory in the first MB).
	 *
	 * I suggest reading up on the fixed MTRR section of the manual to see why the first entry is likely going to need to be UC.
	 */
	if(PageFrameNumber == 0)
	{
		NewEntry->MemoryType = MEMORY_TYPE_UNCACHEABLE;
		return;
	}

	/* Default memory type is always WB for performance. */
	TargetMemoryType = MEMORY_TYPE_WRITE_BACK;

	/* For each MTRR range */
	for(CurrentMtrrRange = 0; CurrentMtrrRange < GlobalContext->NumberOfEnabledMemoryRanges; CurrentMtrrRange++)
	{
		/* If this page's address is below or equal to the max physical address of the range */
		if(AddressOfPage <= GlobalContext->MemoryRanges[CurrentMtrrRange].PhysicalEndAddress)
		{
			/* And this page's last address is above or equal to the base physical address of the range */
			if( (AddressOfPage + SIZE_2_MB - 1) >= GlobalContext->MemoryRanges[CurrentMtrrRange].PhysicalBaseAddress )
			{
				/* If we're here, this page fell within one of the ranges specified by the variable MTRRs
				 * Therefore, we must mark this page as the same cache type exposed by the MTRR 
				 */
				TargetMemoryType = GlobalContext->MemoryRanges[CurrentMtrrRange].MemoryType;
				//HvUtilLogDebug("0x%X> Range=%llX -> %llX | Begin=%llX End=%llX", PageFrameNumber, AddressOfPage, AddressOfPage + SIZE_2_MB - 1, GlobalContext->MemoryRanges[CurrentMtrrRange].PhysicalBaseAddress, GlobalContext->MemoryRanges[CurrentMtrrRange].PhysicalEndAddress);

				/* 11.11.4.1 MTRR Precedences */
				if(TargetMemoryType == MEMORY_TYPE_UNCACHEABLE)
				{
					/* If this is going to be marked uncacheable, then we stop the search as UC always takes precedent. */
					break;
				}
			}
		}
	}

	/* Finally, commit the memory type to the entry. */
	NewEntry->MemoryType = TargetMemoryType;
}

PVMM_EPT_PAGE_TABLE HvEptAllocateAndCreateIdentityPageTable(PVMM_CONTEXT GlobalContext)
{
	PVMM_EPT_PAGE_TABLE PageTable;
	EPT_PML3_POINTER RWXTemplate;
	SIZE_T EntryGroupIndex;
	SIZE_T EntryIndex;
	
	/* Allocate all paging structures as 4KB aligned pages */
	PageTable = OsAllocateContiguousAlignedPages(sizeof(VMM_EPT_PAGE_TABLE) / PAGE_SIZE);

	if(PageTable == NULL)
	{
		HvUtilLogError("HvEptCreatePageTable: Failed to allocate memory for PageTable.");
		return NULL;
	}

	/* Zero out all entries to ensure all unused entries are marked Not Present */
	OsZeroMemory(PageTable, sizeof(VMM_EPT_PAGE_TABLE));

	/* Mark the first 512GB PML4 entry as present, which allows us to manage up to 512GB of discrete paging structures. */
	PageTable->PML4[0].PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML3[0]) / PAGE_SIZE;
	PageTable->PML4[0].ReadAccess = 1;
	PageTable->PML4[0].WriteAccess = 1;
	PageTable->PML4[0].ExecuteAccess = 1;

	/* Now mark each 1GB PML3 entry as RWX and map each to their PML2 entry */

	/* Ensure stack memory is cleared*/
	RWXTemplate.Flags = 0;

	/* Set up one 'template' RWX PML3 entry and copy it into each of the 512 PML3 entries */
	/* Using the same method as SimpleVisor for copying each entry using intrinsics. */
	RWXTemplate.ReadAccess = 1;
	RWXTemplate.WriteAccess = 1;
	RWXTemplate.ExecuteAccess = 1;

	/* Copy the template into each of the 512 PML3 entry slots */
	__stosq((SIZE_T*)&PageTable->PML3, RWXTemplate.Flags, VMM_EPT_PML3E_COUNT);

	/* For each of the 512 PML3 entries */
	for(EntryIndex = 0; EntryIndex < VMM_EPT_PML3E_COUNT; EntryIndex++)
	{
		/*
		 * Map the 1GB PML3 entry to 512 PML2 (2MB) entries to describe each large page.
		 * NOTE: We do *not* manage any PML1 (4096 byte) entries and do not allocate them.
		 */
		PageTable->PML3[EntryIndex].PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML2[EntryIndex][0]) / PAGE_SIZE;
	}

	/* For each collection of 512 PML2 entries (512 collections * 512 entries per collection), mark it RWX using the same template above.
	 * This marks the entries as "Present" regardless of if the actual system has memory at this region or not. We will cause a fault in our
	 * EPT handler if the guest access a page outside a usable range, despite the EPT frame being present here.
	 * NOTE: We can reuse the template because the entries use the same structure as EPT_PML3_POINTER for the first 3 bits we are setting
	 */
	__stosq((SIZE_T*)&PageTable->PML2, RWXTemplate.Flags, VMM_EPT_PML3E_COUNT * VMM_EPT_PML2E_COUNT);

	/* For each of the 512 collections of 512 2MB PML2 entries */
	for(EntryGroupIndex = 0; EntryGroupIndex < VMM_EPT_PML3E_COUNT; EntryGroupIndex++)
	{
		/* For each 2MB PML2 entry in the collection */
		for(EntryIndex = 0; EntryIndex < VMM_EPT_PML2E_COUNT; EntryIndex++)
		{
			/* Setup the memory type and frame number of the PML2 entry. */
			HvEptSetupPML2Entry(GlobalContext, &PageTable->PML2[EntryGroupIndex][EntryIndex], (EntryGroupIndex * VMM_EPT_PML2E_COUNT) + EntryIndex);
		}
	}

	return PageTable;
}

/**
 * Initializes any EPT components that are not local to a particular processor.
 * 
 * Checks to ensure EPT is supported by the processor and builds a map of system memory from
 * the MTRR registers.
 */
BOOL HvEptGlobalInitialize(PVMM_CONTEXT GlobalContext)
{
	/* Ensure our processor supports all the EPT features we want to use */
	if (!HvEptCheckFeatures())
	{
		HvUtilLogError("Processor does not support all necessary EPT features.");
		return FALSE;
	}

	/* Build a map of the system memory as exposed by the BIOS */
	if(!HvEptBuildMTRRMap(GlobalContext))
	{
		HvUtilLogError("Could not build MTRR memory map.");
		return FALSE;
	}

	return TRUE;
}

/**
 * Initialize EPT for an individual logical processor.
 * 
 * Creates an identity mapped page table and sets up an EPTP to be applied to the VMCS later.
 */
BOOL HvEptLogicalProcessorInitialize(PVMM_PROCESSOR_CONTEXT ProcessorContext)
{
	PVMM_EPT_PAGE_TABLE PageTable;
	EPT_POINTER EPTP;

	/* Allocate the identity mapped page table*/
	PageTable = HvEptAllocateAndCreateIdentityPageTable(ProcessorContext->GlobalContext);
	if (PageTable == NULL)
	{
		HvUtilLogError("Unable to allocate memory for EPT!");
		return FALSE;
	}

	/* Virtual address to the page table to keep track of it for later freeing */
	ProcessorContext->EptPageTable = PageTable;

	EPTP.Flags = 0;

	/* For performance, we let the processor know it can cache the EPT. */
	EPTP.MemoryType = MEMORY_TYPE_WRITE_BACK;

	/* We are not utilizing the 'access' and 'dirty' flag features. */
	EPTP.EnableAccessAndDirtyFlags = FALSE;

	/* We are always allocating exactly 3 levels of paging structures. */
	/* PML4 -> PML3 -> PML2 */
	EPTP.PageWalkLength = 3;

	/* The physical page number of the page table we will be using */
	EPTP.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(PageTable) / PAGE_SIZE;

	/* We will write the EPTP to the VMCS later */
	ProcessorContext->EptPointer.Flags = EPTP.Flags;

	return TRUE;
}

/*
 * Free memory allocated by EPT functions.
 */
VOID HvEptFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT ProcessorContext)
{
	if(ProcessorContext->EptPageTable)
	{
		OsFreeContiguousAlignedPages(ProcessorContext->EptPageTable);
	}
}