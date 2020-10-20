#include "arch.h"
#include "util.h"
#include "debugaux.h"
#include "vmm.h"
#include "exit.h"
#include "lde64.h"

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

	if (!VpidRegister.AdvancedVmexitEptViolationsInformation)
	{
		HvUtilLogDebug("Processor does not support AdvancedVmexitEptViolationsInformation!\n");
	}

	if (!MTRRDefType.MtrrEnable)
	{
		HvUtilLogError("MTRR Dynamic Ranges not supported.\n");
		return FALSE;
	}

	HvUtilLogSuccess("HvEptCheckFeatures: All EPT features present.\n");
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

	HvUtilLogDebug("EPT: Number of dynamic ranges: %d\n", MTRRCap.VariableRangeCount);

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
			HvUtilLogDebug("MTRR Range: Base=0x%llX End=0x%llX Type=0x%X\n", Descriptor->PhysicalBaseAddress, Descriptor->PhysicalEndAddress, Descriptor->MemoryType);
		}
	}

	HvUtilLogDebug("Total MTRR Ranges Committed: %d\n", GlobalContext->NumberOfEnabledMemoryRanges);

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
		HvUtilLogError("HvEptCreatePageTable: Failed to allocate memory for PageTable.\n");
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
		HvUtilLogError("Processor does not support all necessary EPT features.\n");
		return FALSE;
	}

	/* Build a map of the system memory as exposed by the BIOS */
	if(!HvEptBuildMTRRMap(GlobalContext))
	{
		HvUtilLogError("Could not build MTRR memory map.\n");
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
		HvUtilLogError("Failed to get PML1 entry: Translating PA:%p to VA returned NULL.", PML2Pointer->PageFrameNumber * PAGE_SIZE);
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

	HvUtilLog("Splitting large page @ PA:%p", PhysicalAddress);

	/* Find the PML2 entry that's currently used*/
	TargetEntry = HvEptGetPml2Entry(ProcessorContext, PhysicalAddress);
	if(!TargetEntry)
	{
		HvUtilLogError("HvEptSplitLargePage: Invalid physical address.\n");
		return FALSE;
	}

	/* If this large page is not marked a large page, that means it's a pointer already.
	 * That page is therefore already split.
	 */
	if(!TargetEntry->LargePage)
	{
		return TRUE;
	}

	/*
	* Allocate the PML1 entries for the split.
	* NOTE: This would *not* need to use contiguous aligned pages normally, except for a bug which is experienced
	* in Windows 10 v2004 where changes to the nonpaged pool allocator resulted in some page aligned allocations
	* being mapped as 4MB large pages rather than the expected 4KB pages. This causes the following VtoP and PtoV
	* functions to fail, because the Mm APIs are not able to properly translate physical addresses within a large page
	* back to its virtual address due to a null PTE pointer inside the PFN database entry for the large page.
	* 
	* From my testing, I was unable to find a way to coerce Mm to split a nonpaged pool large page for me, so the best
	* alternative was to use the contiguous aligned pages allocator because, in my testing, it resulted in only 4KB virtual
	* allocations. This allocator also utilizes nonpaged pool frames, so it is more-or-less the same as the other allocator.
	*/
	NewSplit = (PVMM_EPT_DYNAMIC_SPLIT) OsAllocateContiguousAlignedPages(sizeof(VMM_EPT_DYNAMIC_SPLIT)/PAGE_SIZE);
	if(!NewSplit)
	{
		HvUtilLogError("HvEptSplitLargePage: Failed to allocate dynamic split memory.\n");
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
	EntryTemplate.MemoryType = TargetEntry->MemoryType;
	EntryTemplate.IgnorePat = TargetEntry->IgnorePat;
	EntryTemplate.SuppressVe = TargetEntry->SuppressVe;

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

	/*
	* Create an EPT pointer to the new PML2 entry we just created
	*/
	NewPointer.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&NewSplit->PML1[0]) / PAGE_SIZE;

	/* Add our allocation to the linked list of dynamic splits for later deallocation */
	InsertHeadList(&ProcessorContext->EptPageTable->DynamicSplitList, &NewSplit->DynamicSplitList);

	/**
	 * Now, replace the entry in the page table with our new split pointer.
	 */
	RtlCopyMemory(TargetEntry, &NewPointer, sizeof(NewPointer));

	return TRUE;
}

NTSTATUS (*NtCreateFileOrig)(
	PHANDLE            FileHandle,
	ACCESS_MASK        DesiredAccess,
	POBJECT_ATTRIBUTES ObjectAttributes,
	PIO_STATUS_BLOCK   IoStatusBlock,
	PLARGE_INTEGER     AllocationSize,
	ULONG              FileAttributes,
	ULONG              ShareAccess,
	ULONG              CreateDisposition,
	ULONG              CreateOptions,
	PVOID              EaBuffer,
	ULONG              EaLength
	);

NTSTATUS NtCreateFileHook(
	PHANDLE            FileHandle,
	ACCESS_MASK        DesiredAccess,
	POBJECT_ATTRIBUTES ObjectAttributes,
	PIO_STATUS_BLOCK   IoStatusBlock,
	PLARGE_INTEGER     AllocationSize,
	ULONG              FileAttributes,
	ULONG              ShareAccess,
	ULONG              CreateDisposition,
	ULONG              CreateOptions,
	PVOID              EaBuffer,
	ULONG              EaLength
)
{
	static WCHAR BlockedFileName[] = L"test.txt";
	static SIZE_T BlockedFileNameLength = (sizeof(BlockedFileName) / sizeof(BlockedFileName[0])) - 1;

	PWCH NameBuffer;
	USHORT NameLength;

	__try
	{

		ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), 1);
		ProbeForRead(ObjectAttributes->ObjectName, sizeof(UNICODE_STRING), 1);

		NameBuffer = ObjectAttributes->ObjectName->Buffer;
		NameLength = ObjectAttributes->ObjectName->Length;

		ProbeForRead(NameBuffer, NameLength, 1);

		/* Convert to length in WCHARs */
		NameLength /= sizeof(WCHAR);

		/* Does the file path (ignoring case and null terminator) end with our blocked file name? */
		if (NameLength >= BlockedFileNameLength && 
			_wcsnicmp(&NameBuffer[NameLength - BlockedFileNameLength], BlockedFileName, BlockedFileNameLength) == 0)
		{
			HvUtilLogSuccess("Blocked access to %ws\n", BlockedFileName);
			return STATUS_ACCESS_DENIED;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		NOTHING;
	}

	return NtCreateFileOrig(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
		ShareAccess, CreateDisposition, CreateOptions, EaBuffer, EaLength);
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
		HvUtilLogError("Unable to allocate memory for EPT!\n");
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

	/*
	 * On each logical processor, create an EPT hook on NtCreateFile to intercept the system call.
	 */
	if (!HvEptAddPageHook(ProcessorContext, (PVOID)NtCreateFile, (PVOID)NtCreateFileHook, (PVOID*)&NtCreateFileOrig))
	{
		HvUtilLogError("Failed to build page hook for NtCreateFile");
		HvEptFreeLogicalProcessorContext(ProcessorContext);
		return FALSE;
	}

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
		FOR_EACH_LIST_ENTRY_END();

		/* Free each page hook */
		FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, PageHookList, VMM_EPT_PAGE_HOOK, Hook)
			OsFreeNonpagedMemory(Hook->Trampoline);
			OsFreeNonpagedMemory(Hook);
		FOR_EACH_LIST_ENTRY_END();

		/* Free the actual page table */
		OsFreeContiguousAlignedPages(ProcessorContext->EptPageTable);
	}
}

/* Write an absolute x64 jump to an arbitrary address to a buffer. */
VOID HvEptHookWriteAbsoluteJump(PCHAR TargetBuffer, SIZE_T TargetAddress)
{
	/* mov r15, Target */
	TargetBuffer[0] = 0x49;
	TargetBuffer[1] = 0xBB;

	/* Target */
	*((PSIZE_T)&TargetBuffer[2]) = TargetAddress;

	/* push r15 */
	TargetBuffer[10] = 0x41;
	TargetBuffer[11] = 0x53;

	/* ret */
	TargetBuffer[12] = 0xC3;
}


BOOL HvEptHookInstructionMemory(PVMM_EPT_PAGE_HOOK Hook, PVOID TargetFunction, PVOID HookFunction, PVOID* OrigFunction)
{
	SIZE_T SizeOfHookedInstructions;
	SIZE_T OffsetIntoPage;

	OffsetIntoPage = ADDRMASK_EPT_PML1_OFFSET((SIZE_T)TargetFunction);
	HvUtilLogDebug("OffsetIntoPage: 0x%llx\n", OffsetIntoPage);

	if ((OffsetIntoPage + 13) > PAGE_SIZE-1)
	{
		HvUtilLogError("Function extends past a page boundary. We just don't have the technology to solve this.....\n");
		return FALSE;
	}

	/* Determine the number of instructions necessary to overwrite using Length Disassembler Engine */
	for(SizeOfHookedInstructions = 0; 
		SizeOfHookedInstructions < 13; 
		SizeOfHookedInstructions += LDE(TargetFunction, 64))
	{
		// Get the full size of instructions necessary to copy
	}

	HvUtilLogDebug("Number of bytes of instruction mem: %d\n", SizeOfHookedInstructions);

	/* Build a trampoline */
	
	/* Allocate some executable memory for the trampoline */
	Hook->Trampoline = OsAllocateExecutableNonpagedMemory(SizeOfHookedInstructions + 13);

	if (!Hook->Trampoline)
	{
		HvUtilLogError("Could not allocate trampoline function buffer.\n");
		return FALSE;
	}

	/* Copy the trampoline instructions in. */
	RtlCopyMemory(Hook->Trampoline, TargetFunction, SizeOfHookedInstructions);

	/* Add the absolute jump back to the original function. */
	HvEptHookWriteAbsoluteJump(&Hook->Trampoline[SizeOfHookedInstructions], (SIZE_T)TargetFunction + SizeOfHookedInstructions);

	HvUtilLogDebug("Trampoline: 0x%llx\n", Hook->Trampoline);
	HvUtilLogDebug("HookFunction: 0x%llx\n", HookFunction);

	/* Let the hook function call the original function */
	*OrigFunction = Hook->Trampoline;

	/* Write the absolute jump to our shadow page memory to jump to our hook. */
	HvEptHookWriteAbsoluteJump(&Hook->FakePage[OffsetIntoPage], (SIZE_T)HookFunction);

	return TRUE;
}


BOOL HvEptAddPageHook(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVOID TargetFunction, PVOID HookFunction, PVOID* OrigFunction)
{
	PVMM_EPT_PAGE_HOOK NewHook;
	EPT_PML1_ENTRY FakeEntry;
	EPT_PML1_ENTRY OriginalEntry;
	INVEPT_DESCRIPTOR Descriptor;
	SIZE_T PhysicalAddress;
	PVOID VirtualTarget;

	/* Translate the page from a physical address to virtual so we can read its memory. 
	 * This function will return NULL if the physical address was not already mapped in
	 * virtual memory.
	 */
	VirtualTarget = PAGE_ALIGN(TargetFunction);

	PhysicalAddress = (SIZE_T) OsVirtualToPhysical(VirtualTarget);

	if(!PhysicalAddress)
	{
		HvUtilLogError("HvEptAddPageHook: Target address could not be mapped to physical memory!\n");
		return FALSE;
	}

	/* Create a hook object*/
	NewHook = (PVMM_EPT_PAGE_HOOK) OsAllocateNonpagedMemory(sizeof(VMM_EPT_PAGE_HOOK));

	if (!NewHook)
	{
		HvUtilLogError("HvEptAddPageHook: Could not allocate memory for new hook.\n");
		return FALSE;
	}

	/* 
	 * Ensure the page is split into 512 4096 byte page entries. We can only hook a 4096 byte page, not a 2MB page.
	 * This is due to performance hit we would get from hooking a 2MB page.
	 */
	if (!HvEptSplitLargePage(ProcessorContext, PhysicalAddress))
	{
		HvUtilLogError("HvEptAddPageHook: Could not split page for address 0x%llX.\n", PhysicalAddress);
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	/* Zero our newly allocated memory */
	OsZeroMemory(NewHook, sizeof(VMM_EPT_PAGE_HOOK));

	RtlCopyMemory(&NewHook->FakePage[0], VirtualTarget, PAGE_SIZE);

	/* Base address of the 4096 page. */
	NewHook->PhysicalBaseAddress = (SIZE_T) PAGE_ALIGN(PhysicalAddress);

	/* Pointer to the page entry in the page table. */
	NewHook->TargetPage = HvEptGetPml1Entry(ProcessorContext, PhysicalAddress);

	/* Ensure the target is valid. */
	if (!NewHook->TargetPage)
	{
		HvUtilLogError("HvEptAddPageHook: Failed to get PML1 entry for target address.\n");
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	/* Save the original permissions of the page */
	NewHook->OriginalEntry = *NewHook->TargetPage;
	OriginalEntry = *NewHook->TargetPage;

	/* Setup the new fake page table entry */
	FakeEntry.Flags = 0;

	/* We want this page to raise an EPT violation on RW so we can handle by swapping in the original page. */
	FakeEntry.ReadAccess = 0;
	FakeEntry.WriteAccess = 0;
	FakeEntry.ExecuteAccess = 1;

	/* Point to our fake page we just made */
	FakeEntry.PageFrameNumber = (SIZE_T)OsVirtualToPhysical(&NewHook->FakePage) / PAGE_SIZE;

	/* Save a copy of the fake entry. */
	NewHook->ShadowEntry.Flags = FakeEntry.Flags;

	/* Keep a record of the page hook */
	InsertHeadList(&ProcessorContext->EptPageTable->PageHookList, &NewHook->PageHookList);

	/* 
	 * Lastly, mark the entry in the table as no execute. This will cause the next time that an instruction is
	 * fetched from this page to cause an EPT violation exit. This will allow us to swap in the fake page with our
	 * hook.
	 */
	OriginalEntry.ReadAccess = 1;
	OriginalEntry.WriteAccess = 1;
	OriginalEntry.ExecuteAccess = 0;

	/* The hooked entry will be swapped in first. */
	NewHook->HookedEntry.Flags = OriginalEntry.Flags;

	if(!HvEptHookInstructionMemory(NewHook, TargetFunction, HookFunction, OrigFunction))
	{
		HvUtilLogError("HvEptAddPageHook: Could not build hook.\n");
		OsFreeNonpagedMemory(NewHook);
		return FALSE;
	}

	/* Apply the hook to EPT */
	NewHook->TargetPage->Flags = OriginalEntry.Flags;

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

/* Check if this exit is due to a violation caused by a currently hooked page. Returns FALSE
 * if the violation was not due to a page hook.
 * 
 * If the memory access attempt was RW and the page was marked executable, the page is swapped with
 * the original page.
 * 
 * If the memory access attempt was execute and the page was marked not executable, the page is swapped with
 * the hooked page.
 */
BOOL HvExitHandlePageHookExit(
	PVMM_PROCESSOR_CONTEXT ProcessorContext,
	PVMEXIT_CONTEXT ExitContext,
	VMX_EXIT_QUALIFICATION_EPT_VIOLATION ViolationQualification)
{
	PVMM_EPT_PAGE_HOOK PageHook;

	PageHook = NULL;

	/*
	 * The only kind of EPT violations we should expect are ones related to address translation.
	 * If this is not one of those, something went terribly wrong with EPT and we need to try
	 * to get out of VMX immediately.
	 */
	if (!ViolationQualification.CausedByTranslation)
	{
		return FALSE;
	}

	/* Resolve the hook if there is one */
	FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, PageHookList, VMM_EPT_PAGE_HOOK, Hook)
	{
		/* Check if our access happened inside a page we are currently hooking. */
		if ( Hook->PhysicalBaseAddress == (SIZE_T)PAGE_ALIGN(ExitContext->GuestPhysicalAddress) )
		{
			PageHook = Hook;
			break;
		}
	}
	FOR_EACH_LIST_ENTRY_END();


	/* If a violation happened outside of one of our hooked pages we don't
	 * want to try to handle it.
	 */
	if (!PageHook)
	{
		return FALSE;
	}

	/* If the violation was due to trying to execute a non-executable page, that means that the currently
	 * swapped in page is our original RW page. We need to swap in the hooked executable page (fake page)
	 */

	if(!ViolationQualification.EptExecutable && ViolationQualification.ExecuteAccess)
	{
		/* Swap out the non-executable page and swap in the executable page */
		PageHook->TargetPage->Flags = PageHook->ShadowEntry.Flags;

		/* Redo the instruction */
		ExitContext->ShouldIncrementRIP = FALSE;

		HvUtilLogSuccess("Made Exec\n");

		return TRUE;
	}

	/* If the current page is executable but the memory access was a R or W operation, we want to
	 * swap back in the original page.
	 */
	if(ViolationQualification.EptExecutable 
		&& (ViolationQualification.ReadAccess | ViolationQualification.WriteAccess) )
	{
		/* Otherwise, the executable page is swapped */
		PageHook->TargetPage->Flags = PageHook->HookedEntry.Flags;

		/* Redo the instruction */
		ExitContext->ShouldIncrementRIP = FALSE;

		HvUtilLogSuccess("Made RW\n");

		return TRUE;
	}

	HvUtilLogError("Hooked page had invalid page swapping logic?!\n");

	return FALSE;
}
/**
 * Handle VM exits for EPT violations. Violations are thrown whenever an operation is performed
 * on an EPT entry that does not provide permissions to access that page.
 */
VOID HvExitHandleEptViolation(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext)
{
	VMX_EXIT_QUALIFICATION_EPT_VIOLATION ViolationQualification;

	UNREFERENCED_PARAMETER(ProcessorContext);

	ViolationQualification.Flags = ExitContext->ExitQualification;

	HvUtilLogDebug("EPT Violation => 0x%llX\n", ExitContext->GuestPhysicalAddress);

	if(HvExitHandlePageHookExit(ProcessorContext, ExitContext, ViolationQualification))
	{
		// Handled by page hook code.
		return;
	}

	HvUtilLogError("Unexpected EPT violation!\n");

	/* Redo the instruction that caused the exception. */
	ExitContext->ShouldStopExecution = TRUE;
}
