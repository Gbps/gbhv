#pragma once

#include "extern.h"
#include "vmcs.h"


BOOL HvExitDispatchFunction(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);
BOOL HvExitHandleCpuid(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);