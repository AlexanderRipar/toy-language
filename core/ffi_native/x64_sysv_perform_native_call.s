	.intel_syntax noprefix
	.text
	.global ffi_x64_sysv_asm_perform_native_call
	.type ffi_x64_sysv_asm_perform_native_call, @function

ffi_x64_sysv_asm_perform_native_call:

	# ARG REGS
	# rdi
	# rsi
	# rdx
	# rcx
	# r8
	# r9
	#
	# VOL REGS
	# rax (ret lo)
	# rdi (arg 1, ret hi)
	# rsi (arg 2)
	# rdx (arg 3)
	# rcx (arg 4)
	# r8  (arg 5)
	# r9  (arg 6)
	# r10
	# r11
	#
	# NON-VOL REGS
	# rbx
	# rsp (stack pointer)
	# rbp (frame pointer)
	# r12
	# r13
	# r14
	# r15

	# Arguments
	# - in_stack_count      rdi
	# - in_stack_size       rsi
	# - in_arg_values       rdx
	# - in_stack_kinds_lo   rcx
	# - in_stack_kinds_hi   r8
	# - in_native_callee    r9


	# Preserve stack pointer in rbp and save non-volatile registers.

	push rbp
	mov rbp, rsp

	push rbx
	push r12
	push r13



	# Move xmm values into registers.

	vmovsd xmm0, [rdx     ]
	vmovsd xmm1, [rdx +  8]
	vmovsd xmm2, [rdx + 16]
	vmovsd xmm3, [rdx + 24]
	vmovsd xmm4, [rdx + 32]
	vmovsd xmm5, [rdx + 40]
	vmovsd xmm6, [rdx + 48]
	vmovsd xmm7, [rdx + 56]



	# Grow stack to accept stack arguments.

	sub rsp, rsi                       # %rsp -= in_stack_size
	and rsp, -16                       # %rsp &= ~15 (align to 16)



	# Move stack arguments into place.

	add rdx, 144                       # in_arg_values += 144 (start of stack values)

	xor rax, rax                       # stack_index = 0

	mov rbx, rsp                       # stack_offset = %rsp

NEXT_STACK_ARG:

	cmp rax, rdi
	je STACK_ARGS_DONE                 # if stack_index == in_stack_count goto STACK_ARGS_DONE

	cmp rax, 64
	cmove rcx, r8                      # if rax == 64 then in_stack_kinds_lo = in_stack_kinds_hi

	shr rcx                            # in_stack_kinds_lo >>= 1
	jb LARGE_STACK_ARG                 # if lost_bit == 1 goto LARGE_STACK_ARG

	mov r10, [rdx + rax * 8]           # stack_arg_value = [in_arg_values + stack_index * 8]
	mov [rbx], r10                     # [%rsp + stack_offset] = stack_arg_value

	inc rax                            # stack_index += 1
	add rbx, 8                         # stack_offset += 8

	jmp NEXT_STACK_ARG

LARGE_STACK_ARG:

	mov r10, [rdx + rax * 8]           # copy_size = [in_arg_values + stack_index * 8]
	mov r11, [rdx + rax * 8 + 8]       # copy_addr = [in_arg_values + stack_index * 8 + 8]

	add rax, 2                         # stack_index += 2

	xor r12, r12                       # copy_index = 0

NEXT_LARGE_ARG_BYTE:

	cmp r12, r10
	je NEXT_STACK_ARG                  # if copy_index == copy_size goto LARGE_ARG_DONE

	mov r13b, [r11 + r12]              # large_value_byte = [copy_addr + copy_index]               
	mov [rbx + r12], r13b              # [stack_offset + copy_index] = large_value_byte

	inc r12                            # copy_index += 1

	jmp NEXT_LARGE_ARG_BYTE

LARGE_ARG_DONE:

	lea rbx, [r10 + 7]                 # stack_offset += copy_size + 7
	and rbx, -8                        # stack_offset &= ~7 (align to 8)

	jmp NEXT_STACK_ARG

STACK_ARGS_DONE:



	# Save arguments to non-argument registers.

	mov r10, r9                        # native_callee = in_native_callee
	mov rbx, rdx                       # arg_values = in_arg_values @ start of stack values



	# Move gpr arguments values into place.

	mov rdi, [rbx - 80]
	mov rsi, [rbx - 72]
	mov rdx, [rbx - 64]
	mov rcx, [rbx - 56]
	mov r8,  [rbx - 48]
	mov r9,  [rbx - 40]


	# Move xmm argument register count into rax, as a hack for supporting
	# variadic functions, which expect it there.
	mov rax, [rbx - 32]

	call r10                           # call native_callee



	# Prepare first round of return register copy, skipping it if
	# `ret_copy_class` == 0 (i.e., there is an implicit out-pointer as the
	# first argument instead of returning through rax).

	mov rdx, [rbx - 24]                # ret_copy_class = [arg_values - 24]

	mov ecx, [rbx - 16]                # ret_copy_size = [arg_values - 16] (ret_copy_size_lo)

	mov rsi, [rbx - 8]                 # ret_copy_addr = [arg_values - 8]

	test rdx, rdx
	jz DONE                            # if ret_copy_class == 0 goto DONE



RET_COPY:

	xor r8, r8                         # ret_copy_index = 0

	test rdx, 2
	jz RET_COPY_RAX_BYTE               # if ret_class & 2 == 0 goto RET_COPY_RAX_BYTE

	movq rax, xmm0                     # %rax = %xmm0 (first gpr return register = first xmm return register)



RET_COPY_RAX_BYTE:

	# Copy `ret_copy_size` bytes from `%rax` to `ret_copy_addr`.

	mov [rsi + r8], al                 # [ret_copy_addr + ret_copy_index] = %al

	shr rax, 8                         # %rax >>= 8 (shift next byte into %al)

	inc r8                             # ret_copy_index += 1

	cmp r8, rcx
	jb RET_COPY_RAX_BYTE               # if ret_copy_index < ret_copy_size goto RET_COPY_RAX_BYTE



	# Shift out SYSV_RET_COPY_*_LO, completing if the remaining value is zero.

	shr rdx, 2                         # ret_copy_class >>= 2

	test rdx, rdx
	jz DONE                            # if ret_copy_class == 0 goto DONE



	# Adjust for second return register and then rerun copy.

	mov ecx, [rbx - 12]                 # ret_copy_size = [in_arg_values - 12] (ret_copy_size_hi)

	mov rax, rdi                       # %rax = %rdi ("shift" in second gpr return register)
	movq xmm0, xmm1                    # %xmm0 = %xmm1("shift" in second xmm return register)

	add rsi, 8                         # ret_copy_addr += 8

	jmp RET_COPY



DONE:

	# Restore callee-preserved registers and stack, then return.

	mov rbx, [rbp - 24]
	mov r12, [rbp - 16]
	mov r13, [rbp -  8]

	mov rsp, rbp
	pop rbp

	ret





# This magical incantation is apparently necessary to avoid having an
# executable stack, idk.
.section .note.GNU-stack, "", @progbits
