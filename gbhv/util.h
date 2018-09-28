#pragma once
#include "extern.h"

BOOL HvUtilBitIsSet(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitSetBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitGetBitRange(SIZE_T BitField, SIZE_T BitMax, SIZE_T BitMin);

VOID HvUtilLog(LPCSTR MessageFormat, ...);

VOID HvUtilLogDebug(LPCSTR MessageFormat, ...);

VOID HvUtilLogSuccess(LPCSTR MessageFormat, ...);

VOID HvUtilLogError(LPCSTR MessageFormat, ...);

