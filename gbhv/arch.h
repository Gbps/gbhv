#pragma once

#include "extern.h"

/*
 * Contains a capture of important special registers that will be used to setup
 * the guest VMCS.
 */
typedef struct _IA32_SPECIAL_REGISTERS
{
	/*
	 * Control register CR0
	 */
	CR0 RegisterCr0;
	/*
	 * Control register CR3
	 */
	CR3 RegisterCr3;

	/*
	 * Control register CR4
	 */
	CR4 RegisterCr4;

	/*
	 * Register containing the pointer to the Global Descriptor Table
	 */
	SEGMENT_DESCRIPTOR_REGISTER_64 RegisterGdt;

	/*
	 * Register containing the pointer to the Interrupt Descriptor Table
	 */
	SEGMENT_DESCRIPTOR_REGISTER_64 RegisterIdt;

	/*
	 * Debug register DR7
	 */
	DR7 RegisterDr7;

	/* 
	 * RFLAGS register 
	 */
	EFLAGS RegisterRflags;

	/* 
	 * Task register holding the task segment selector
	 */
	SEGMENT_SELECTOR RegisterTr;

	/*
	 * LDT register holding the local descriptor table segment selector
	 */
	SEGMENT_SELECTOR RegisterLdtr;

} IA32_SPECIAL_REGISTERS, *PIA32_SPECIAL_REGISTERS;


/*
 * Win32 Modified CONTEXT Structure with IA32-Doc Register Fields
 */

#pragma warning(push, 0)

 //
 // Context Frame
 //
 //  This frame has a several purposes: 1) it is used as an argument to
 //  NtContinue, 2) it is used to constuct a call frame for APC delivery,
 //  and 3) it is used in the user level thread creation routines.
 //
 //
 // The flags field within this record controls the contents of a CONTEXT
 // record.
 //
 // If the context record is used as an input parameter, then for each
 // portion of the context record controlled by a flag whose value is
 // set, it is assumed that that portion of the context record contains
 // valid context. If the context record is being used to modify a threads
 // context, then only that portion of the threads context is modified.
 //
 // If the context record is used as an output parameter to capture the
 // context of a thread, then only those portions of the thread's context
 // corresponding to set flags will be returned.
 //
 // CONTEXT_CONTROL specifies SegSs, Rsp, SegCs, Rip, and EFlags.
 //
 // CONTEXT_INTEGER specifies Rax, Rcx, Rdx, Rbx, Rbp, Rsi, Rdi, and R8-R15.
 //
 // CONTEXT_SEGMENTS specifies SegDs, SegEs, SegFs, and SegGs.
 //
 // CONTEXT_FLOATING_POINT specifies Xmm0-Xmm15.
 //
 // CONTEXT_DEBUG_REGISTERS specifies Dr0-Dr3 and Dr6-Dr7.
 //

typedef struct DECLSPEC_ALIGN(16) _REGISTER_CONTEXT {

	//
	// Register parameter home addresses.
	//
	// N.B. These fields are for convience - they could be used to extend the
	//      context record in the future.
	//

	ULONG64 P1Home;
	ULONG64 P2Home;
	ULONG64 P3Home;
	ULONG64 P4Home;
	ULONG64 P5Home;
	ULONG64 P6Home;

	//
	// Control flags.
	//

	ULONG ContextFlags;
	ULONG MxCsr;

	//
	// Segment Registers and processor flags.
	//

	SEGMENT_SELECTOR SegCS;
	SEGMENT_SELECTOR SegDS;
	SEGMENT_SELECTOR SegES;
	SEGMENT_SELECTOR SegFS;
	SEGMENT_SELECTOR SegGS;
	SEGMENT_SELECTOR SegSS;
	ULONG EFlags;

	//
	// Debug registers
	//

	ULONG64 Dr0;
	ULONG64 Dr1;
	ULONG64 Dr2;
	ULONG64 Dr3;
	ULONG64 Dr6;
	ULONG64 Dr7;

	//
	// Integer registers.
	//

	ULONG64 Rax;
	ULONG64 Rcx;
	ULONG64 Rdx;
	ULONG64 Rbx;
	ULONG64 Rsp;
	ULONG64 Rbp;
	ULONG64 Rsi;
	ULONG64 Rdi;
	ULONG64 R8;
	ULONG64 R9;
	ULONG64 R10;
	ULONG64 R11;
	ULONG64 R12;
	ULONG64 R13;
	ULONG64 R14;
	ULONG64 R15;

	//
	// Program counter.
	//

	ULONG64 Rip;

	//
	// Floating point state.
	//

	union {
		XMM_SAVE_AREA32 FltSave;
		struct {
			M128A Header[2];
			M128A Legacy[8];
			M128A Xmm0;
			M128A Xmm1;
			M128A Xmm2;
			M128A Xmm3;
			M128A Xmm4;
			M128A Xmm5;
			M128A Xmm6;
			M128A Xmm7;
			M128A Xmm8;
			M128A Xmm9;
			M128A Xmm10;
			M128A Xmm11;
			M128A Xmm12;
			M128A Xmm13;
			M128A Xmm14;
			M128A Xmm15;
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;

	//
	// Vector registers.
	//

	M128A VectorRegister[26];
	ULONG64 VectorControl;

	//
	// Special debug control registers.
	//

	ULONG64 DebugControl;
	ULONG64 LastBranchToRip;
	ULONG64 LastBranchFromRip;
	ULONG64 LastExceptionToRip;
	ULONG64 LastExceptionFromRip;
} REGISTER_CONTEXT, *PREGISTER_CONTEXT;

#pragma warning(pop)

#define DEBUG_PRINT_STRUCT_NAME(_STRUCT_NAME_) HvUtilLogDebug(#_STRUCT_NAME_ ": ")
#define DEBUG_PRINT_STRUCT_MEMBER(_STRUCT_MEMBER_) HvUtilLogDebug("    " #_STRUCT_MEMBER_ ": %i [0x%X]", Register._STRUCT_MEMBER_, Register._STRUCT_MEMBER_)


SIZE_T ArchGetHostMSR(ULONG MsrAddress);

UINT32 ArchGetCPUID(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister);

BOOL ArchIsCPUFeaturePresent(INT32 FunctionId, INT32 SubFunctionId, INT32 CPUIDRegister, INT32 FeatureBit);

BOOL ArchIsVMXAvailable();

IA32_VMX_BASIC_REGISTER ArchGetBasicVmxCapabilities();

VOID ArchEnableVmxe();
VOID ArchDisableVmxe();

VOID ArchCaptureSpecialRegisters(PIA32_SPECIAL_REGISTERS Registers);

/*
 * =======================================================================
 * ============ Below are definitions implemented in arch.asm ============
 * =======================================================================
 */

/*
 * Literally the contents of ntoskrnl's RtlCaptureContext to capture CPU register state.
 */
VOID ArchCaptureContext(PREGISTER_CONTEXT RegisterContext);

/*
 * Get the segment selector for the task selector segment (TSS)
 */
SEGMENT_SELECTOR ArchReadTaskRegister();

/*
 * Get the segment selector for the Local Descriptor Table (LDT)
 */
SEGMENT_SELECTOR ArchReadLocalDescriptorTableRegister();
