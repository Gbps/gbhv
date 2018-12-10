#pragma once
#include "extern.h"
#include "vmm.h"
#include "ept.h"

/*
 * CPUID Function identifier to check if VMX is enabled.
 * 
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_FUNCTION 1

/* 
 * CPUID Subfunction identifier to check if VMX is enabled.
 * 
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_SUBFUNCTION 0

 /*
  * CPUID Register EAX.
  */
#define CPUID_REGISTER_EAX 0

 /*
  * CPUID Register EBX.
  */
#define CPUID_REGISTER_EBX 1

/*
 * CPUID Register ECX.
 */
#define CPUID_REGISTER_ECX 2

 /*
  * CPUID Register EDX.
  */
#define CPUID_REGISTER_EDX 3

/*
 * CPUID VMX support enabled bit.
 *
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_BIT 5

 /*
  * The VMCS and VMXON region can be, at max, 4096 bytes, or one page. But the manual straight up contradicts itself, so
  * I'm not sure. Best be safe and allocate two pages.
  *
  * Vol 3D A-1 Basic VMX Information
  *
  * Bits 44:32 report the number of bytes that software should allocate for the VMXON region and any VMCS region.
  * It is a value greater than 0 and at most 4096 (bit 44 is set if and only if bits 43:32 are clear).
*/
#define VMX_VMXON_NUMBER_PAGES 2
#define VMX_VMCS_NUMBER_PAGES 2

BOOL VmxLaunchProcessor(PVMM_PROCESSOR_CONTEXT Context);
BOOL VmxEnterRootMode(PVMM_PROCESSOR_CONTEXT Context);
BOOL VmxExitRootMode(PVMM_PROCESSOR_CONTEXT Context);

/*
 * Used to write a vmcs field using vmwrite and an ia32-doc register type.
 */
#define VmxVmwriteFieldFromRegister(_FIELD_DEFINE_, _REGISTER_VAR_) \
	VmError |= __vmx_vmwrite(_FIELD_DEFINE_, _REGISTER_VAR_.Flags) \
	//if (OsGetCurrentProcessorNumber() == 0) HvUtilLogDebug(#_FIELD_DEFINE_ " = 0x%llx", _REGISTER_VAR_.Flags);

/*
 * Used to write a vmcs field using vmwrite and an immediate value.
 */
#define VmxVmwriteFieldFromImmediate(_FIELD_DEFINE_, _IMMEDIATE_) \
	VmError |= __vmx_vmwrite(_FIELD_DEFINE_, _IMMEDIATE_) \
	//if (OsGetCurrentProcessorNumber() == 0) HvUtilLogDebug(#_FIELD_DEFINE_ " = 0x%llx", _IMMEDIATE_);

/*
 * Reads a value from the VMCS to a ia32-doc register type.
 */
#define VmxVmreadFieldToRegister(_FIELD_DEFINE_, _REGISTER_VAR_) \
	VmError |= __vmx_vmread(_FIELD_DEFINE_, _REGISTER_VAR_.Flags); \
	//if (OsGetCurrentProcessorNumber() == 0) HvUtilLogDebug(#_FIELD_DEFINE_ " = 0x%llx", *(_REGISTER_VAR_.Flags))

/*
 * Reads a value from the VMCS to an immediate value.
 */
#define VmxVmreadFieldToImmediate(_FIELD_DEFINE_, _IMMEDIATE_) \
	VmError |= __vmx_vmread(_FIELD_DEFINE_, _IMMEDIATE_); \
	//if (OsGetCurrentProcessorNumber() == 0) HvUtilLogDebug(#_FIELD_DEFINE_ " = 0x%llx", *(_IMMEDIATE_))

/*
 * Type of errors returned by vmx instructions (like vmwrite).
 */
typedef SIZE_T VMX_ERROR;

typedef struct _VMX_SEGMENT_DESCRIPTOR
{
	/*
	 * Selector (16 bits)
	 */
	SIZE_T Selector;

	/*
	 * Base address (64 bits; 32 bits on processors that do not support Intel 64 architecture). The base-address
	 * fields for CS, SS, DS, and ES have only 32 architecturally-defined bits; nevertheless, the corresponding
	 * VMCS fields have 64 bits on processors that support Intel 64 architecture.
	 */
	SIZE_T BaseAddress;

	/*
	 * Segment limit (32 bits). The limit field is always a measure in bytes.
	 */
	UINT32 SegmentLimit;

	/*
	 * Access rights (32 bits). The format of this field is given in Table 24-2 and detailed as follows:
	 * 
	 * • The low 16 bits correspond to bits 23:8 of the upper 32 bits of a 64-bit segment descriptor. While bits
	 *   19:16 of code-segment and data-segment descriptors correspond to the upper 4 bits of the segment
	 *   limit, the corresponding bits (bits 11:8) are reserved in this VMCS field.
	 * 
	 * • Bit 16 indicates an unusable segment. Attempts to use such a segment fault except in 64-bit mode.
	 *   In general, a segment register is unusable if it has been loaded with a null selector.
	 * 
	 * • Bits 31:17 are reserved.
	 */
	VMX_SEGMENT_ACCESS_RIGHTS AccessRights;
} VMX_SEGMENT_DESCRIPTOR, *PVMX_SEGMENT_DESCRIPTOR;

#pragma warning(push, 0)
typedef union _VMX_EXIT_REASON_FIELD_UNION
{
	struct
	{
		SIZE_T BasicExitReason : 16;
		SIZE_T MustBeZero1 : 11;
		SIZE_T WasInEnclaveMode : 1;
		SIZE_T PendingMTFExit : 1;
		SIZE_T ExitFromVMXRoot : 1;
		SIZE_T MustBeZero2 : 1;
		SIZE_T VmEntryFailure : 1;
	};

	SIZE_T Flags;
} VMX_EXIT_REASON, *PVMX_EXIT_REASON;

/*
 * From vmxdefs.asm:
 * 
 * Saved GP register context before calling into the vmexit handler.
 * 
 * 	pop	rax
	pop	rcx
	pop	rdx
	pop	rbx
	add	rsp, 8
	pop	rbp
	pop	rsi
	pop	rdi
	pop	r8
	pop	r9
	pop	r10
	pop	r11
	pop	r12
	pop	r13
	pop	r14
	pop	r15
 */
typedef struct _GPREGISTER_CONTEXT
{
	/* Populated from vmxdefs.asm */
	SIZE_T GuestRAX;
	SIZE_T GuestRCX;
	SIZE_T GuestRDX;
	SIZE_T GuestRBX;

	/* Populated from VMCS */
	SIZE_T GuestRSP;

	/* Populated from vmxdefs.asm */
	SIZE_T GuestRBP;
	SIZE_T GuestRSI;
	SIZE_T GuestRDI;
	SIZE_T GuestR8;
	SIZE_T GuestR9;
	SIZE_T GuestR10;
	SIZE_T GuestR11;
	SIZE_T GuestR12;
	SIZE_T GuestR13;
	SIZE_T GuestR14;
	SIZE_T GuestR15;
} GPREGISTER_CONTEXT, *PGPREGISTER_CONTEXT;


#pragma warning(pop)

VOID VmxGetSegmentDescriptorFromSelector(PVMX_SEGMENT_DESCRIPTOR VmxSegmentDescriptor, SEGMENT_DESCRIPTOR_REGISTER_64 GdtRegister, SEGMENT_SELECTOR SegmentSelector, BOOL ClearRPL);

VOID VmxPrintErrorState(PVMM_PROCESSOR_CONTEXT Context);

VOID __invept(SIZE_T Type, INVEPT_DESCRIPTOR* Descriptor);