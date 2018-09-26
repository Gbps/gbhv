#pragma once

#include <ntdef.h>
#include <windef.h>

BOOL HvUtilBitIsSet(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitSetBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition);

VOID HvLogError(LPCSTR ErrorMessage);