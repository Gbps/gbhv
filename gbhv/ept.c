#include "arch.h"
#include "util.h"
#include "debugaux.h"
#include "vmm.h"

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

BOOL HvEptBuildMTRRMap(PVMM_CONTEXT GlobalContext)
{
	IA32_MTRR_CAPABILITIES_REGISTER MTRRCap;
	ULONG CurrentRegister;
	IA32_MTRR_PHYSBASE_REGISTER CurrentPhysBase;
	IA32_MTRR_PHYSMASK_REGISTER CurrentPhysMask;
	ULONG NumberOfBitsInMask;
	PMTRR_RANGE_DESCRIPTOR Descriptor;

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
			// Our processor context descriptor for this VP
			// We could read the MTRRs once and copy each of the ranges
			Descriptor = &GlobalContext->MemoryRanges[GlobalContext->NumberOfEnabledMemoryRanges++];

			// Calculate the base address in bytes
			Descriptor->PhysicalBaseAddress = CurrentPhysBase.PageFrameNumber * PAGE_SIZE;

			// Calculate the total size of the range
			// The lowest bit of the mask that is set to 1 specifies the size of the range
			_BitScanForward64(&NumberOfBitsInMask, CurrentPhysMask.PageFrameNumber * PAGE_SIZE);

			// Size of the range in bytes
			Descriptor->SizeOfRange = ((1ULL << NumberOfBitsInMask) - 1ULL);

			// Memory Type (cacheable attributes)
			Descriptor->MemoryType = (UCHAR) CurrentPhysBase.Type;

			HvUtilLogDebug("MTRR Range: Base=0x%llX Size=0x%llX Type=0x%X", Descriptor->PhysicalBaseAddress, Descriptor->SizeOfRange, Descriptor->MemoryType);
		}
	}

	return FALSE;
}


BOOL HvEptInitialize(PVMM_CONTEXT GlobalContext)
{
	if (!HvEptCheckFeatures())
	{
		HvUtilLogError("Processor does not support all necessary EPT features.");
		return FALSE;
	}

	if(!HvEptBuildMTRRMap(GlobalContext))
	{
		HvUtilLogError("Could not build MTRR memory map.");
		return FALSE;
	}

	return TRUE;
}