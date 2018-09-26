#pragma once
#include "msr.h"
#include "arch.h"
#include "util.h"
#include <ntdef.h>


BOOL HvInitializeAllProcessors();

ULONG_PTR HvpIPIBroadcastFunction(_In_ ULONG_PTR Argument);

VOID HvInitialize();