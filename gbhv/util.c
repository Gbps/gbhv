
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
 * Certain control MSRs in VMX will ask that certain bits always be 0, and some always be 1.
 * 
 * In these MSR formats, the lower 32-bits specify the "must be 1" bits.
 * These bits are OR'd to ensure they are always 1, no matter what DesiredValue was set to.
 * 
 * The high 32-bits specify the "must be 0" bits.
 * These bits are AND'd to ensure these bits are always 0, no matter what DesiredValue was set to.
 */
SIZE_T HvUtilEncodeMustBeBits(SIZE_T DesiredValue, SIZE_T ControlMSR)
{
	LARGE_INTEGER ControlMSRLargeInteger;

	// LARGE_INTEGER provides a nice interface to get the top 32 bits of a 64-bit integer
	ControlMSRLargeInteger.QuadPart = ControlMSR;

	DesiredValue &= ControlMSRLargeInteger.HighPart;
	DesiredValue |= ControlMSRLargeInteger.LowPart;

	return DesiredValue;
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