#include "vmx.h"
#include "vmm.h"

BOOL VmxEnterRootMode(PVMX_PROCESSOR_CONTEXT Context)
{
	// Enable VMXe in CR4 of the processor
	ArchEnableVmxe();

	HvUtilLogDebug("VmxOnRegion[#%i]: (V) 0x%llx / (P) 0x%llx", OsGetCurrentProcessorNumber(), Context->VmxonRegion, Context->VmxonRegionPhysical);

	// Execute VMXON to bring processor to VMXON mode
	// Check RFLAGS.CF == 0 to ensure successful execution
	return __vmx_on((ULONGLONG*)&Context->VmxonRegionPhysical) == 0;
}
