
.CODE

; Literally the contents of RtlCaptureContext from ntoskrnl
ArchCaptureContext PROC
	; Floating point reigsters
	pushfq

	; Low GP registers
	mov [rcx+078h], rax
	mov [rcx+080h], rcx
	mov [rcx+088h], rdx
	mov [rcx+0B8h], r8
	mov [rcx+0C0h], r9
	mov [rcx+0C8h], r10
	mov [rcx+0D0h], r11

	; Low XMM Registers
	movaps xmmword ptr [rcx+01A0h], xmm0
	movaps xmmword ptr [rcx+01B0h], xmm1
	movaps xmmword ptr [rcx+01C0h], xmm2
	movaps xmmword ptr [rcx+01D0h], xmm3
	movaps xmmword ptr [rcx+01E0h], xmm4
	movaps xmmword ptr [rcx+01F0h], xmm5

	; Segment selectors
	mov word ptr [rcx+038h], cs
	mov word ptr [rcx+03Ah], ds
	mov word ptr [rcx+03Ch], es
	mov word ptr [rcx+042h], ss
	mov word ptr [rcx+03Eh], fs
	mov word ptr [rcx+040h], gs

	; High GP registers
	mov [rcx+090h], rbx
	mov [rcx+0A0h], rbp
	mov [rcx+0A8h], rsi
	mov [rcx+0B0h], rdi
	mov [rcx+0D8h], r12
	mov [rcx+0E0h], r13
	mov [rcx+0E8h], r14
	mov [rcx+0F0h], r15

	; FPU Control Word
	fnstcw word ptr [rcx+0100h]
	mov dword ptr [rcx+0102h], 0

	; High XMM Registers
	movaps xmmword ptr [rcx+0200h], xmm6
	movaps xmmword ptr [rcx+0210h], xmm7
	movaps xmmword ptr [rcx+0220h], xmm8
	movaps xmmword ptr [rcx+0230h], xmm9
	movaps xmmword ptr [rcx+0240h], xmm10
	movaps xmmword ptr [rcx+0250h], xmm11
	movaps xmmword ptr [rcx+0260h], xmm12
	movaps xmmword ptr [rcx+0270h], xmm13
	movaps xmmword ptr [rcx+0280h], xmm14
	movaps xmmword ptr [rcx+0290h], xmm15

	; XMM control/status register
	stmxcsr dword ptr [rcx+0118h]
	stmxcsr dword ptr [rcx+034h]

	; Fix context RSP values
	lea rax, [rsp+010h]
	mov [rcx+098h], rax
	mov rax, [rsp+08h]
	mov [rcx+0F8h], rax
	mov eax, [rsp]
	mov [rcx+044h], eax

	mov dword ptr [rcx+030h], 10000Fh
	
	; Return
	add rsp, 8
	ret
ArchCaptureContext ENDP

; Reads the task register segment selector and returns it
ArchReadTaskRegister PROC
	str ax
	ret
ArchReadTaskRegister ENDP

; Reads the LDT segment selector and returns it
ArchReadLocalDescriptorTableRegister PROC
	sldt ax
	ret
ArchReadLocalDescriptorTableRegister ENDP

END
