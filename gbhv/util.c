
#include "util.h"
#include <stdarg.h>

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
	return BitField | (1ULL << BitPosition);
}

/*
 * Clear a bit in a bit field.
 */
SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField & ~(1ULL << BitPosition);
}

/*
 * Print a message to the kernel debugger.
 */
VOID HvUtilLog(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[*] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}

/*
 * Print a debug message to the kernel debugger.
 */
VOID HvUtilLogDebug(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[DEBUG] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}


/*
 * Print a success message to the kernel debugger.
 */
VOID HvUtilLogSuccess(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[+] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}

/*
 * Print an error to the kernel debugger with a format.
 */
VOID HvUtilLogError(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[!] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}