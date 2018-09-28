#pragma once
#include "extern.h"

BOOL HvUtilBitIsSet(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitSetBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition);

VOID HvUtilLog(LPCSTR MessageFormat, ...);

VOID HvUtilLogDebug(LPCSTR MessageFormat, ...);

VOID HvUtilLogSuccess(LPCSTR MessageFormat, ...);

VOID HvUtilLogError(LPCSTR MessageFormat, ...);

