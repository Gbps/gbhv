
EXTERN HvInitializeLogicalProcessor : PROC

;DEBUG
EXTERN VmxExitRootMode : PROC

.CODE

; Saves all general purpose registers to the stack
; RSP is read from VMCS, so there's a placeholder.
; https://github.com/asamy/ksm/blob/e7e24931c9df26c33d6e2a0ea9a44c78d3ced7a6/vmx.asm#L41
PushGeneralPurposeRegisterContext MACRO
	push r15
	push r14
	push r13
	push r12
	push r11
	push r10
	push r9
	push r8
	push rdi
	push rsi
	push rbp
	sub rsp, 8 ; placeholder for rsp
	push rbx
	push rdx
	push rcx
	push rax
ENDM

; Saves all general purpose registers to the stack
; RSP is read from VMCS, so there's a placeholder.
; https://github.com/asamy/ksm/blob/e7e24931c9df26c33d6e2a0ea9a44c78d3ced7a6/vmx.asm#L62
PopGeneralPurposeRegisterContext MACRO
	pop	rax
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
ENDM


HvBeginInitializeLogicalProcessor PROC
	; DEBUG
	push rcx

	; Save floating point stack
	pushfq

	; Macro to push all the GP registers
	PushGeneralPurposeRegisterContext

	; First argument (RCX) will be the PVMM_PROCESSOR_CONTEXT pointer
	; Second argument (RDX) to the RSP value we need to return to (GuestRSP)
	mov rdx, rsp

	; Third argument (R8) to the label address that the guest will return to when
	; vmlaunch succeeds
	mov r8, guest_resumes_here
	
	; Shadow stack space
	sub rsp, 20h

	; Initialize the processor for vmx mode.
	; This *should* launch the processor into guest mode
	; If VMLAUNCH succeeds, execution will continue from `guest_resumes_here` in guest mode.
	; If VMLAUNCH fails, then this function will return with vmx disabled.
	call HvInitializeLogicalProcessor

	add rsp, 20h

	; Macro to restore GP registers
	PopGeneralPurposeRegisterContext

	; Restore floating point registers
	popfq

	; DEBUG
	pop rcx

	; Return unsucessful
	mov rax, 0
	ret

; If VMLAUNCH succeeds, execution will continue from `guest_resumes_here` in guest mode.
guest_resumes_here:

	; Macro to restore GP registers
	PopGeneralPurposeRegisterContext

	; Restore floating point registers
	popfq

	; DEBUG
	pop rcx

	add rsp, 20h
	call VmxExitRootMode
	sub rsp, 20h

	mov rax, 1
	ret
HvBeginInitializeLogicalProcessor ENDP

END