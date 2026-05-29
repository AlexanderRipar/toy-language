.code
ffi_asm_perform_native_call PROC FRAME

	; Arguments:
	; - in_arg_count       rcx
	; - in_arg_values      rdx
	; - in_native_callee   r8
	;
	; Stack Growth
	; - args_size          rax
	;
	; Stack Copy
	; - arg_index          rax
	; - curr_arg_value     rbx
	;
	; Register Save & Init
	; - arg_count          rax (from arg_index)
	; - arg_values         rbx (from in_arg_values)
	; - native_callee      r10 (from in_native_callee)
	;
	; Return Class Save & Call
	; - ret_class_addr     rbx
	;
	; Return Value Move
	; - ret_val            rax (from `call`)
	; - ret_class          rcx
	; - ret_dst            rdx
	; - ret_size           rbx



	; Preserve stack pointer in rbp and save non-volatile registers.

	.pushreg rbp
	push rbp

	.pushreg rbx
	push rbx

	mov rbp, rsp
	.setframe rbp, 0

	.endprolog



	; Grow the stack to hold native arguments, guaranteeing "shadow space" for
	; at least four arguments (registers), as required by the win32 x64 calling
	; convention.
	; Additionally we make sure to align the stack to 16 bytes, remembering that
	; `call` itself pushes 8 bytes, meaning we must allocate `x` bytes, where
	; `x % 16 == 8`.

	mov rax, rcx                       ; args_size = in_arg_count

	cmp rax, 3
	ja GROW_STACK                      ; if args_size >= 4 goto GROW_STACK

	mov rax, 4                         ; args_size = 4

GROW_STACK:

	shl rax, 3                         ; args_size <<= 3 (or, args_size *= 8)
	sub rsp, rax                       ; rsp -= args_size
	and rsp, -16                       ; rsp &= ~15



	; Push arguments onto the stack, in the same memory-order as we received
	; them. From the callee's perspective, [rsp+0] holds the first arg, [rsp+8]
	; the second, and so on.

	xor rax, rax                        ; arg_index = 0

COPY_NEXT_ARG:

	cmp rax, rcx
	je COPY_ARGS_DONE                  ; if arg_index == in_arg_count goto COPY_ARGS_DONE

	mov rbx, [rdx + rax * 8]           ; curr_arg_value = [in_arg_values + arg_index * 8]
	mov [rsp + rax * 8], rbx           ; [rsp + arg_index * 8] = curr_arg_value

	inc rax                            ; arg_index += 1

	jmp COPY_NEXT_ARG                  ; goto COPY_NEXT_ARG

COPY_ARGS_DONE:



	; Initialize register arguments for callee.
	; First, we need to squirrel away our own arguments, noting that
	; in_arg_count is already in rax due to the loop we just executed.
	; Next, we move the first four entries of in_arg_values both into general
	; purpose registers and into the first four xmm registers. This means that
	; we set up the correct register contents regardless of whether the
	; arguments are floats or integers / pointers.

	mov rbx, rdx                       ; arg_values = in_arg_values
	mov r10, r8                        ; native_callee = in_native_callee

	mov rcx, [rbx]                     ; %rcx = [arg_values]
	vmovsd xmm0, QWORD PTR [rbx]       ; %xmm0 = [arg_values]

	mov rdx, [rbx + 8]                 ; %rdx = [arg_values + 8]
	vmovsd xmm1, QWORD PTR [rbx + 8]   ; %xmm1 = [arg_values + 8]

	mov r8, [rbx + 16]                 ; %r8 = [arg_values + 16]
	vmovsd xmm2, QWORD PTR [rbx + 16]  ; %xmm2 = [arg_values + 16]

	mov r9, [rbx + 24]                 ; %r9 = [arg_values + 24]
	vmovsd xmm3, QWORD PTR [rbx + 24]  ; %xmm3 = [arg_values + 24]



	; Store the address of the return value class in the non-volatile register
	; rbx, so we can inspect it after the call. Then, call out to the native
	; function.

	cmp rax, 4
	jae CALC_RET_CLASS_ADDR

	mov rax, 4

CALC_RET_CLASS_ADDR:

	lea rbx, [rbx + rax * 8]           ; ret_class_addr = arg_values + arg_count * 8

	call r10                           ; call native_callee



	; Move native return value into write context if necessary.

	mov ecx, [rbx]                     ; ret_class = [ret_class_addr]

	test ecx, ecx
	jz DONE                            ; if ret_class == RET_COPY_NONE goto DONE

	mov rdx, [rbx + 8]                 ; ret_dst = [ret_class_addr + 8]



	; Dispatch on `ret_class`.

	cmp ecx, 2
	jb RET_HANDLE_I8                   ; if ret_class == RET_COPY_I8  goto RET_HANDLE_I8
	je RET_HANDLE_I16                  ; if ret_class == RET_COPY_I16 goto RET_HANDLE_I16

	cmp ecx, 4
	jb RET_HANDLE_I32                  ; if ret_class == RET_COPY_I32 goto RET_HANDLE_I32
	je RET_HANDLE_I64                  ; if ret_class == RET_COPY_I64 goto RET_HANDLE_I64

	cmp ecx, 5
	je RET_HANDLE_FLT                  ; if ret_class == RET_COPY_FLT goto RET_HANDLE_FLT

	; Fallthrough if ret_class == RET_COPY_DBL



	vmovsd QWORD PTR [rdx], xmm0       ; [ret_dst] = %xmm0@f64
	jmp DONE

RET_HANDLE_I8:
	mov [rdx], al                      ; [ret_dst] = %al
	jmp DONE

RET_HANDLE_I16:
	mov [rdx], ax                      ; [ret_dst] = %ax
	jmp DONE

RET_HANDLE_I32:
	mov [rdx], eax                     ; [ret_dst] = %eax
	jmp DONE

RET_HANDLE_I64:
	mov [rdx], rax                     ; [ret_dst] = %rax
	jmp DONE

RET_HANDLE_FLT:
	vmovss DWORD PTR [rdx], xmm0       ; [ret_dst] = %xmm0@f32



DONE:

	; Restore registers and stack, then return.

	mov rsp, rbp

	pop rbx

	pop rbp

	ret

ffi_asm_perform_native_call ENDP

END
