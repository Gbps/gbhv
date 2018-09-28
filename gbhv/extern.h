#pragma once

// PHNT_MODE_KERNEL
#define PHNT_MODE 0

// PHNT_REDSTONE4
#define PHNT_VERSION 105

/*
 * WDK includes
 */
#include <intrin.h>
#include <ntifs.h>

/*
 * PHNT is the library of reversed structures used by Process Hacker.
 */
#pragma warning(push, 0)      
#include "phnt/phnt.h"


/*
 * IA32-doc has structures for the entire intel SDM... pretty insane tbh.
 */
#include <ia32.h>

#pragma warning(pop)

typedef UINT32 BOOL;