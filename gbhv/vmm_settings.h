#pragma once

#include "extern.h"

/*
 * VMM General Compiler Settings
 */

/*
 * Stack space allocated for the host during vmexit.
 */
#define VMM_SETTING_STACK_SPACE (PAGE_SIZE * 8)