
#include "util.h"

/**
 * Check if a bit is set in the bit field.
 */
BOOL HvUtilBitIsSet(SIZE_T BitField, SIZE_T BitPosition)
{
	return (BitField >> BitPosition) & 1UL;
}

/**
 * Set a bit in a bit field.
 */
SIZE_T HvUtilBitSetBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField | (1UL << BitPosition);
}

/*
 * Clear a bit in a bit field.
 */
SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField & ~(1UL << BitPosition);
}

/*
 * Print an error message.
 */
VOID HvLogError(LPCSTR ErrorMessage)
{
}