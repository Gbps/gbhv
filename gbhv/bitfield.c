#include "bitfield.h"


/**
 * Check if a bit is set in the bit field.
 */
BOOL BitIsSet(SIZE_T BitField, SIZE_T BitPosition)
{
	return (BitField >> BitPosition) & 1UL;
}

/**
 * Set a bit in a bit field.
 */
SIZE_T BitSetBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField | (1UL << BitPosition);
}

/*
 * Clear a bit in a bit field.
 */
SIZE_T BitClearBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField & ~(1UL << BitPosition);
}

