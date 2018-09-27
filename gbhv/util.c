
#include "util.h"
#include <stdarg.h>
#include <wdm.h>

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
 * Print a message to the kernel debugger.
 */
VOID HvLog(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}

/*
 * Print an error to the kernel debugger with a format.
 */
VOID HvLogError(LPCSTR ErrorMessage, ...)
{
	va_list ArgumentList;

	HvLog("ERROR: ");

	va_start(ArgumentList, ErrorMessage);
	HvLog(ErrorMessage, ArgumentList);
	va_end(ArgumentList);
}