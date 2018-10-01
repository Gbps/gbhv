#include "vmx.h"
#include "vmm.h"

BOOL VmxEnterRootMode(PVMX_PROCESSOR_CONTEXT Context)
{
	// Enable VMXe in CR4 of the processor
	ArchEnableVmxe();

	HvUtilLogDebug("VmxOnRegion[#%i]: (V) 0x%llx / (P) 0x%llx [%i]", OsGetCurrentProcessorNumber(), Context->VmxonRegion, Context->VmxonRegionPhysical, (PUINT32)Context->VmxonRegion->VmcsRevisionNumber);

	// Execute VMXON to bring processor to VMX mode
	// Check RFLAGS.CF == 0 to ensure successful execution
	if(__vmx_on((ULONGLONG*)&Context->VmxonRegionPhysical) != 0)
	{
		HvUtilLogError("VMXON failed.");
		return FALSE;
	}

	// And clear the VMCS before writing the configuration entries to it
	if (__vmx_vmclear((ULONGLONG*)&Context->VmcsRegionPhysical) != 0)
	{
		HvUtilLogError("VMCLEAR failed.");
		return FALSE;
	}

	// Now load the blank VMCS
	if(__vmx_vmptrld((ULONGLONG*)&Context->VmcsRegionPhysical) != 0)
	{
		HvUtilLogError("VMPTRLD failed.");
		return FALSE;
	}

	return TRUE;
}

BOOL VmxExitRootMode(PVMX_PROCESSOR_CONTEXT Context)
{
	// And clear the VMCS before writing the configuration entries to it
	if (__vmx_vmclear((ULONGLONG*)&Context->VmcsRegionPhysical) != 0)
	{
		HvUtilLogError("VMCLEAR failed.");
		return FALSE;
	}

	// Turn off VMX
	__vmx_off();

	// Turn off VMXe in CR4
	ArchDisableVmxe();

	return TRUE;
}
