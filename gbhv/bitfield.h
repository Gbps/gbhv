#pragma once
#include <windef.h>
#include <basetsd.h>

BOOL BitIsSet(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T BitSetBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T BitClearBit(SIZE_T BitField, SIZE_T BitPosition);