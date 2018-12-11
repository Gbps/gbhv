#include "arch.h"
#include "util.h"
#include "debugaux.h"
#include "vmm.h"
#include "exit.h"

/**
 * Checks to ensure that the processor supports all EPT features that we want to use.
 */
BOOL HvEptCheckFeatures()
{
	IA32_VMX_EPT_VPID_CAP_REGISTER VpidRegister;
	IA32_MTRR_DEF_TYPE_REGISTER MTRRDefType;

	VpidRegister.Flags = ArchGetHostMSR(IA32_VMX_EPT_VPID_CAP);
	MTRRDefType.Flags = ArchGetHostMSR(IA32_MTRR_DEF_TYPE);

	if (!VpidRegister.PageWalkLength4 || !VpidRegister.MemoryTypeWriteBack || !VpidRegister.Pde2MbPages || !VpidRegister.AdvancedVmexitEptViolationsInformation)
	{
		return FALSE;
	}

	if (!MTRRDefType.MtrrEnable)
	{
		HvUtilLogError("MTRR Dynamic Ranges not supported");
		return FALSE;
	}

	HvUtilLogSuccess("HvEptCheckFeatures: All EPT features present.");
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
	EPT_PML2_ENTRY PML2EntryTemplate;
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

	/* Initialize the dynamic split list which holds all dynamic page splits */
	InitializeListHead(&PageTable->DynamicSplitList);

	/* Initialize the page hook list which holds information on currently hooked pages */
	InitializeListHead(&PageTable->PageHookList);

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
	__stosq((SIZE_T*)&PageTable->PML3[0], RWXTemplate.Flags, VMM_EPT_PML3E_COUNT);

	/* For each of the 512 PML3 entries */
	for(EntryIndex = 0; EntryIndex < VMM_EPT_PML3E_COUNT; EntryIndex++)
	{
		/*
		 * Map the 1GB PML3 entry to 512 PML2 (2MB) entries to describe each large page.
		 * NOTE: We do *not* manage any PML1 (4096 byte) entries and do not allocate them.
		 */
		PageTable->PML3[EntryIndex].PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML2[EntryIndex][0]) / PAGE_SIZE;
	}

	PML2EntryTemplate.Flags = 0;

	/* All PML2 entries will be RWX and 'present' */
	PML2EntryTemplate.WriteAccess = 1;
	PML2EntryTemplate.ReadAccess = 1;
	PML2EntryTemplate.ExecuteAccess = 1;

	/* We are using 2MB large pages, so we must mark this 1 here. */
	PML2EntryTemplate.LargePage = 1;

	/* For each collection of 512 PML2 entries (512 collections * 512 entries per collection), mark it RWX using the same template above.
	 * This marks the entries as "Present" regardless of if the actual system has memory at this region or not. We will cause a fault in our
	 * EPT handler if the guest access a page outside a usable range, despite the EPT frame being present here.
	 */
	__stosq((SIZE_T*)&PageTable->PML2[0], PML2EntryTemplate.Flags, VMM_EPT_PML3E_COUNT * VMM_EPT_PML2E_COUNT);

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
 * Get the PML2 entry for this physical address.
 */
PEPT_PML2_ENTRY HvEptGetPml2Entry(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	SIZE_T Directory, DirectoryPointer, PML4Entry;
	PEPT_PML2_ENTRY PML2;

	Directory  = ADDRMASK_EPT_PML2_INDEX(PhysicalAddress);
	DirectoryPointer = ADDRMASK_EPT_PML3_INDEX(PhysicalAddress);
	PML4Entry = ADDRMASK_EPT_PML4_INDEX(PhysicalAddress);

	/* Addresses above 512GB are invalid because it is > physical address bus width */
	if(PML4Entry > 0)
	{
		return NULL;
	}

	PML2 = &ProcessorContext->EptPageTable->PML2[DirectoryPointer][Directory];
	return PML2;
}

/**
 * Get the PML1 entry for this physical address if the page is split. Return NULL if the address is invalid
 * or the page wasn't already split.
 */
PEPT_PML1_ENTRY HvEptGetPml1Entry(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	SIZE_T Directory, DirectoryPointer, PML4Entry;
	PEPT_PML2_ENTRY PML2;
	PEPT_PML1_ENTRY PML1;
	PEPT_PML2_POINTER PML2Pointer;

	Directory = ADDRMASK_EPT_PML2_INDEX(PhysicalAddress);
	DirectoryPointer = ADDRMASK_EPT_PML3_INDEX(PhysicalAddress);
	PML4Entry = ADDRMASK_EPT_PML4_INDEX(PhysicalAddress);

	/* Addresses above 512GB are invalid because it is > physical address bus width */
	if (PML4Entry > 0)
	{
		return NULL;
	}

	PML2 = &ProcessorContext->EptPageTable->PML2[DirectoryPointer][Directory];

	/* Check to ensure the page is split */
	if(PML2->LargePage)
	{
		return NULL;
	}

	/* Conversion to get the right PageFrameNumber. These pointers occupy the same place in the
	 * table and are directly convertable.
	 */
	PML2Pointer = (PEPT_PML2_POINTER) PML2;

	/* If it is, translate to the PML1 pointer */
	PML1 = (PEPT_PML1_ENTRY) OsPhysicalToVirtual((PPHYSVOID)(PML2Pointer->PageFrameNumber * PAGE_SIZE));

	if(!PML1)
	{
		return NULL;
	}

	/* Index into PML1 for that address */
	PML1 = &PML1[ADDRMASK_EPT_PML1_INDEX(PhysicalAddress)];

	return PML1;
}

/**
 * Split a large 2MB page into 512 smaller 4096 pages.
 * 
 * In order to set discrete EPT permissions on a singular 4096 byte page, we need to split our
 * default 2MB entries into 512 smaller 4096 byte entries. This function will replace the default
 * 2MB entry created for the page table at the specified PhysicalAddress and replace it with a 2MB
 * pointer entry. That pointer will point to a dynamically allocated set of 512 smaller 4096 byte
 * pages, which will become the new permission structures for that 2MB region.
 */
BOOL HvEptSplitLargePage(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	PVMM_EPT_DYNAMIC_SPLIT NewSplit;
	EPT_PML1_ENTRY EntryTemplate;
	SIZE_T EntryIndex;
	PEPT_PML2_ENTRY TargetEntry;
	EPT_PML2_POINTER NewPointer;

	/* Find the PML2 entry that's currently used*/
	TargetEntry = HvEptGetPml2Entry(ProcessorContext, PhysicalAddress);
	if(!TargetEntry)
	{
		HvUtilLogError("HvEptSplitLargePage: Invalid physical address.");
		return FALSE;
	}

	/* If this large page is not marked a large page, that means it's a pointer already.
	 * That page is therefore already split.
	 */
	if(!TargetEntry->LargePage)
	{
		return TRUE;
	}

	/* Allocate the PML1 entries */
	NewSplit = (PVMM_EPT_DYNAMIC_SPLIT) OsAllocateNonpagedMemory(sizeof(VMM_EPT_DYNAMIC_SPLIT));
	if(!NewSplit)
	{
		HvUtilLogError("HvEptSplitLargePage: Failed to allocate dynamic split memory.");
		return FALSE;
	}

	/*
	 * Point back to the entry in the dynamic split for easy reference for which entry that
	 * dynamic split is for.
	 */
	NewSplit->Entry = TargetEntry;

	/* Make a template for RWX */
	EntryTemplate.Flags = 0;
	EntryTemplate.ReadAccess = 1;
	EntryTemplate.WriteAccess = 1;
	EntryTemplate.ExecuteAccess = 1;

	/* Copy the template into all the PML1 entries */
	__stosq((SIZE_T*)&NewSplit->PML1[0], EntryTemplate.Flags, VMM_EPT_PML1E_COUNT);

	/**
	 * Set the page frame numbers for identity mapping.
	 */
	for(EntryIndex = 0; EntryIndex < VMM_EPT_PML1E_COUNT; EntryIndex++)
	{
		/* Convert the 2MB page frame number to the 4096 page entry number plus the offset into the frame. */
		NewSplit->PML1[EntryIndex].PageFrameNumber = ( (TargetEntry->PageFrameNumber * SIZE_2_MB) / PAGE_SIZE ) + EntryIndex;
	}

	/* Allocate a new pointer which will replace the 2MB entry with a pointer to 512 4096 byte entries. */
	NewPointer.Flags = 0;
	NewPointer.WriteAccess = 1;
	NewPointer.ReadAccess = 1;
	NewPointer.ExecuteAccess = 1;
	NewPointer.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&NewSplit->PML1[0]) / PAGE_SIZE;

	/* Add our allocation to the linked list of dynamic splits for later deallocation */
	InsertHeadList(&ProcessorContext->EptPageTable->DynamicSplitList, &NewSplit->DynamicSplitList);

	/**
	 * Now, replace the entry in the page table with our new split pointer.
	 */
	RtlCopyMemory(TargetEntry, &NewPointer, sizeof(NewPointer));

	return TRUE;
}

VOID HvEptTest(PVMM_PROCESSOR_CONTEXT ProcessorContext, int set)
{
	UNREFERENCED_PARAMETER(ProcessorContext);

	SIZE_T PhysPage = 0xDEADBEEF;

	SIZE_T Offset = ADDRMASK_EPT_PML1_OFFSET(PhysPage);
	SIZE_T Table = ADDRMASK_EPT_PML1_INDEX(PhysPage);
	SIZE_T Directory = ADDRMASK_EPT_PML2_INDEX(PhysPage);
	SIZE_T DirectoryPointer = ADDRMASK_EPT_PML3_INDEX(PhysPage);
	SIZE_T PML4Entry = ADDRMASK_EPT_PML4_INDEX(PhysPage);

	HvUtilLogDebug("Physical Address: 0x%llX", PhysPage);
	HvUtilLogDebug("PML1E = 0x%llX", Offset);
	HvUtilLogDebug("PML1 = 0x%llX", Table);
	HvUtilLogDebug("PML2 = 0x%llX", Directory);
	HvUtilLogDebug("PML3 = 0x%llX", DirectoryPointer);
	HvUtilLogDebug("PML4 = 0x%llX", PML4Entry);

	PEPT_PML2_ENTRY PML2 = &ProcessorContext->EptPageTable->PML2[DirectoryPointer][Directory];
	HvUtilLogDebug("PageFrameNumber: 0x%llX", PML2->PageFrameNumber);
	PML2->ReadAccess = set;
	PML2->WriteAccess = set;
	PML2->ExecuteAccess = set;

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

	/* 
	 * Bits 5:3 (1 less than the EPT page-walk length) must be 3, indicating an EPT page-walk length of 4; 
	 * see Section 28.2.2 
	 */
	EPTP.PageWalkLength = 3;

	/* The physical page number of the page table we will be using */
	EPTP.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&PageTable->PML4) / PAGE_SIZE;

	/* We will write the EPTP to the VMCS later */
	ProcessorContext->EptPointer.Flags = EPTP.Flags;

	HvEptAddPageHook(ProcessorContext, 0xDEADBEEF);

	return TRUE;
}

/*
 * Free memory allocated by EPT functions.
 */
VOID HvEptFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT ProcessorContext)
{
	if (ProcessorContext->EptPageTable)
	{
		/* No races because we are above DPC IRQL */

		/* Free each split */
		FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, DynamicSplitList, VMM_EPT_DYNAMIC_SPLIT, Split)
			OsFreeNonpagedMemory(Split);
		}

		/* Free each page hook */
		FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, PageHookList, VMM_EPT_PAGE_HOOK, Hook)
			OsFreeNonpagedMemory(Hook);
		}

		/* Free the actual page table */
		OsFreeContiguousAlignedPages(ProcessorContext->EptPageTable);
	}
}

/**
 * Handle VM exits for EPT violations. Violations are thrown whenever an operation is performed
 * on an EPT entry that does not provide permissions to access that page.
 */
VOID HvExitHandleEptViolation(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	VMX_EXIT_QUALIFICATION_EPT_VIOLATION ViolationQualification;
	PVMM_EPT_PAGE_HOOK PageHook;

	PageHook = NULL;

	UNREFERENCED_PARAMETER(ProcessorContext);

	ViolationQualification.Flags = ExitContext->ExitQualification;

	HvUtilLogDebug("EPT Violation => 0x%llX", ExitContext->GuestPhysicalAddress);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, ReadAccess);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, WriteAccess);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, ExecuteAccess);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, EptReadable);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, EptWriteable);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, EptExecutable);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, EptExecutableForUserMode);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, ValidGuestLinearAddress);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, CausedByTranslation);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, UserModeLinearAddress);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, ReadableWritablePage);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, ExecuteDisablePage);
	DEBUG_PRINT_STRUCT_MEMBER(ViolationQualification, NmiUnblocking);

	/* 
	 * The only kind of EPT violations we should expect are ones related to address translation.
	 * If this is not one of those, something went terribly wrong with EPT and we need to try
	 * to get out of VMX immediately.
	 */
	if (!ViolationQualification.CausedByTranslation)
	{
		HvUtilLogError("EPT Violation not caused by translation! Fatal error.");
		ExitContext->ShouldStopExecution = FALSE;
		return;
	}

	/* Resolve the hook if there is one */
	FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, PageHookList, VMM_EPT_PAGE_HOOK, Hook)
	{
		/* Check if our access happened inside a page we are currently hooking. */
		if (Hook->PhysicalBaseAddress <= ExitContext->GuestPhysicalAddress
			&& (Hook->PhysicalBaseAddress + (PAGE_SIZE - 1)) >= ExitContext->GuestPhysicalAddress)
		{
			PageHook = Hook;
			break;
		}
	}}

	/* If a violation happened outside of one of our hooked pages */
	if (!PageHook)
	{
		HvUtilLogError("EPT Violation outside of a hooked section!");
		ExitContext->ShouldStopExecution = TRUE;
		return;
	}

	/* Otherwise, resolve the page hook */
	*PageHook->TargetPage = PageHook->FakeEntry;

	/* Redo the instruction that caused the exception. */
	ExitContext->ShouldIncrementRIP = FALSE;
}

BOOL HvEptAddPageHook(PVMM_PROCESSOR_CONTEXT ProcessorContext, SIZE_T PhysicalAddress)
{
	PVMM_EPT_PAGE_HOOK NewHook;
	EPT_PML1_ENTRY FakeEntry;
	EPT_PML1_ENTRY OriginalEntry;
	INVEPT_DESCRIPTOR Descriptor;

	/* Create a hook object*/
	NewHook = (PVMM_EPT_PAGE_HOOK) OsAllocateNonpagedMemory(sizeof(VMM_EPT_PAGE_HOOK));

	if (!NewHook)
	{
		HvUtilLogError("HvEptAddPageHook: Could not allocate memory for new hook.");
		return FALSE;
	}

	/* 
	 * Ensure the page is split into 512 4096 byte page entries. We can only hook a 4096 byte page, not a 2MB page.
	 * This is due to performance hit we would get from hooking a 2MB page.
	 */
	if (!HvEptSplitLargePage(ProcessorContext, PhysicalAddress))
	{
		HvUtilLogError("HvEptAddPageHook: Could not split page for address 0x%llX.", PhysicalAddress);
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	/* Zero our newly allocated memory */
	OsZeroMemory(NewHook, sizeof(VMM_EPT_PAGE_HOOK));

	/* Debug page */
	__stosq((SIZE_T*) &NewHook->FakePage, 0xDEADBEEF, PAGE_SIZE / 8);

	/* Base address of the 4096 page. */
	NewHook->PhysicalBaseAddress = PhysicalAddress & ~0xFFF;

	/* Pointer to the page entry in the page table. */
	NewHook->TargetPage = HvEptGetPml1Entry(ProcessorContext, PhysicalAddress);

	/* Ensure the target is valid. */
	if (!NewHook->TargetPage)
	{
		HvUtilLogError("HvEptAddPageHook: Failed to get PML1 entry for target address.");
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	/* Save the original permissions of the page */
	NewHook->OriginalEntry = *NewHook->TargetPage;
	OriginalEntry = *NewHook->TargetPage;

	/* Setup the new fake page table entry */
	FakeEntry.Flags = 0;

	/* We want this page to raise an EPT violation so we can handle by swapping in the fake page. */
	FakeEntry.ReadAccess = 1;
	FakeEntry.WriteAccess = 1;
	FakeEntry.ExecuteAccess = 1;

	/* Point to our fake page we just made */
	FakeEntry.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&NewHook->FakePage) / PAGE_SIZE;

	/* Save a copy of the fake entry. */
	NewHook->FakeEntry = FakeEntry;

	/* Keep a record of the page hook */
	InsertHeadList(&ProcessorContext->EptPageTable->PageHookList, &NewHook->PageHookList);

	/* 
	 * Lastly, mark the entry in the table as unused. This will cause the next time that it's accessed
	 * to cause an EPT violation exit. This will allow us to swap in the fake page or the real page depending
	 * on the type of access.
	 */
	OriginalEntry.ReadAccess = 0;
	OriginalEntry.WriteAccess = 0;
	OriginalEntry.ExecuteAccess = 0;
	*NewHook->TargetPage = OriginalEntry;

	/*
	 * Invalidate the entry in the TLB caches so it will not conflict with the actual paging structure.
	 */
	if (ProcessorContext->HasLaunched)
	{
		Descriptor.EptPointer = ProcessorContext->EptPointer.Flags;
		Descriptor.Reserved = 0;
		__invept(1, &Descriptor);
	}
	
	return TRUE;
}