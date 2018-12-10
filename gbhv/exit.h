#pragma once

#include "extern.h"
#include "vmcs.h"

typedef struct _VMEXIT_CONTEXT
{
	/*
	 * General purpose register context pushed onto the stack by vmxdefs.asm in
	 * HvEnterFromGuest.
	 */
	PGPREGISTER_CONTEXT GuestContext;

	/*
	 * The state of the instruction pointer at the time of exit.
	 * Read from the guest state of the VMCS and restored from this value before returning.
	 */
	SIZE_T GuestRIP;

	/*
	 * The state of the RFLAGS register at the time of exit.
	 * Read from the guest state of the VMCS.
	 */
	union _GUEST_EFLAGS
	{
		SIZE_T RFLAGS;
		EFLAGS EFLAGS;
	} GuestFlags;

	/*
	 * Saved IRQL during exit handler.
	 */
	KIRQL SavedIRQL;

	/*
	 * The exit reason field.
	 *
	 * This field encodes the reason for the VM exit.
	 *
	 * See 24.9.1 Basic VM-Exit Information.
	 */
	VMX_EXIT_REASON ExitReason;

	/*
	 * Exit qualification (64 bits; 32 bits on processors that do not support Intel 64 architecture). This field contains
	 * additional information about the cause of VM exits due to the following: debug exceptions; page-fault
	 * exceptions; start-up IPIs (SIPIs); task switches; INVEPT; INVLPG;INVVPID; LGDT; LIDT; LLDT; LTR; SGDT;
	 * SIDT; SLDT; STR; VMCLEAR; VMPTRLD; VMPTRST; VMREAD; VMWRITE; VMXON; control-register accesses;
	 * MOV DR; I/O instructions; and MWAIT. The format of the field depends on the cause of the VM exit. See Section
	 * 27.2.1 for details.
	 */
	SIZE_T ExitQualification;

	/*
	 * The instruction length of an instruction that caused the exit.
	 *
	 * The following instructions cause VM exits unconditionally:
	 * INVEPT, INVVPID, VMCALL, VMCLEAR, VMLAUNCH, VMPTRLD, VMPTRST, VMRESUME, VMXOFF, and VMXON.
	 * CPUID, GETSEC, INVD, and XSETBV.
	 *
	 * VM-exit instruction length (32 bits). For VM exits resulting from instruction execution, this field receives the
	 * length in bytes of the instruction whose execution led to the VM exit.1 See Section 27.2.4 for details of when
	 * and how this field is used.
	 */
	SIZE_T InstructionLength;

	/*
	 * VM-exit instruction information (32 bits). This field is used for VM exits due to attempts to execute INS,
	 * INVEPT, INVVPID, LIDT, LGDT, LLDT, LTR, OUTS, SIDT, SGDT, SLDT, STR, VMCLEAR, VMPTRLD, VMPTRST,
	 * VMREAD, VMWRITE, or VMXON.2 The format of the field depends on the cause of the VM exit. See Section
	 * 27.2.4 for details.
	 */
	SIZE_T InstructionInformation;

	/*
	 * Guest-physical address (64 bits). This field is used VM exits due to EPT violations and EPT misconfigurations.
	   See Section 27.2.1 for details of when and how this field is used.
	 */
	SIZE_T GuestPhysicalAddress;

	/**
	 * If set to 1, execution in VMX root mode will end and return to a non-hijacked system. 
	 */
	BOOL ShouldStopExecution;

	/**
	 * If set to 1, the instruction pointer will be incremented by the size of the instruction.
	 */
	BOOL ShouldIncrementRIP;

} VMEXIT_CONTEXT, *PVMEXIT_CONTEXT;


BOOL HvExitDispatchFunction(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);

VOID VmxInitializeExitContext(PVMEXIT_CONTEXT ExitContext, PGPREGISTER_CONTEXT GuestRegisters);