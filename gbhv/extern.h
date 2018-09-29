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

#pragma warning(push, 0)

/*
 * PHNT is the library of reversed structures used by Process Hacker.
 */
#include "phnt/phnt.h"

/*
 * IA32-doc has structures for the entire intel SDM... pretty insane tbh.
 */
#include <ia32.h>

#pragma warning(pop)

/*
 * Some additional types used by everything.
 */
typedef UINT32 BOOL;

/*
 * A physical address pointer
 */
typedef PVOID PPHYSVOID;

/*
 * Pointer values that don't follow NT code style.
 */
typedef VMCS* PVMCS;