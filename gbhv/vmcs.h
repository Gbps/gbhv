#pragma once
#include "extern.h"
#include "vmm.h"
#include "vmx.h"

VMX_ERROR HvSetupVmcsControlFields(PVMM_CONTEXT GlobalContext, PVMX_PROCESSOR_CONTEXT Context);

IA32_VMX_PINBASED_CTLS_REGISTER HvSetupVmcsControlPinBased(PVMM_CONTEXT GlobalContext);

IA32_VMX_PROCBASED_CTLS_REGISTER HvSetupVmcsControlProcessor(PVMM_CONTEXT GlobalContext);

IA32_VMX_PROCBASED_CTLS2_REGISTER HvSetupVmcsControlSecondaryProcessor(PVMM_CONTEXT GlobalContext);

IA32_VMX_ENTRY_CTLS_REGISTER HvSetupVmcsControlVmEntry(PVMM_CONTEXT GlobalContext);

IA32_VMX_EXIT_CTLS_REGISTER HvSetupVmcsControlVmExit(PVMM_CONTEXT GlobalContext);