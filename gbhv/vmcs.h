#pragma once
#include "extern.h"
#include "vmm.h"
#include "vmx.h"

VMX_ERROR HvSetupVmcsGuestArea(PVMM_GLOBAL_CONTEXT GlobalContext, PVMM_PROCESSOR_CONTEXT Context, SIZE_T GuestRIP, SIZE_T GuestRSP);

VMX_ERROR HvSetupVmcsControlFields(PVMM_GLOBAL_CONTEXT GlobalContext, PVMM_PROCESSOR_CONTEXT Context);

IA32_VMX_PINBASED_CTLS_REGISTER HvSetupVmcsControlPinBased(PVMM_GLOBAL_CONTEXT GlobalContext);

IA32_VMX_PROCBASED_CTLS_REGISTER HvSetupVmcsControlProcessor(PVMM_GLOBAL_CONTEXT GlobalContext);

IA32_VMX_PROCBASED_CTLS2_REGISTER HvSetupVmcsControlSecondaryProcessor(PVMM_GLOBAL_CONTEXT GlobalContext);

IA32_VMX_ENTRY_CTLS_REGISTER HvSetupVmcsControlVmEntry(PVMM_GLOBAL_CONTEXT GlobalContext);

IA32_VMX_EXIT_CTLS_REGISTER HvSetupVmcsControlVmExit(PVMM_GLOBAL_CONTEXT GlobalContext);