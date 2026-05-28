	.intel_syntax noprefix
	.text
	.global ffi_asm_perform_native_call
	.type ffi_asm_perform_native_call, @function

ffi_asm_perform_native_call:

	# Arguments
	# - in_xmm_arg_count    rdi
	# - in_arg_values       rsi
	# - in_native_callee    rdx

	# ARG REGS
	# rdi
	# rsi
	# rdx
	# rcx
	# r8
	# r9
	#
	# VOL REGS
	# rax (ret)
	# rdi (arg 1, ret ext)
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


	# Preserve stack pointer in rbp and save non-volatile registers.

	push rbp
	mov rbp, rsp

	push rbx

	# Set up xmm argument-register count for variadic callees.

	mov rax, %rdi                      # %rax = in_xmm_arg_count

	# Set up xmm argument-register values.

	vmovsd xmm0, [rsi]
	vmovsd xmm1, [rsi + 8]
	vmovsd xmm2, [rsi + 16]
	vmovsd xmm3, [rsi + 24]
	vmovsd xmm4, [rsi + 32]
	vmovsd xmm5, [rsi + 40]
	vmovsd xmm6, [rsi + 48]
	vmovsd xmm7, [rsi + 56]

	# Skip past xmm argument values and stack argument value count, reading
	# back the latter.

	add rsi, 72                        # arg_values += 72

	mov rdi, [rsi - 8]                 # stack_arg_count = [arg_values - 8]



	# Adjust stack to make space for stack argument values, while preserving
	# 16-byte alignment.

	mov rcx, rdi                       # stack_arg_size = stack_arg_count
	shl rcx, 3                         # stack_arg_size <<= 3 (*= 8)

	sub rsp, rcx                       # %rsp -= stack_arg_size

	and rsp, -16                       # %rsp = %rsp & ~0xF



	# Copy stack argument values onto stack.

	xor rbx, rbx                       # stack_arg_index = 0

NEXT_STACK_ARG:

	cmp rbx, rdi
	je STACK_ARGS_DONE                 # if stack_arg_index == stack_arg_count then goto STACK_ARGS_DONE

	mov rcx [rsi + rbx * 8]            # curr_stack_arg_value = [arg_values + stack_arg_index * 8]
	mov [rsp + rbx * 8], rcx           # [%rsp + stack_arg_index * 8] = curr_stack_arg_value

	inc rbx                            # stack_arg_index += 1

	jmp NEXT_STACK_ARG



	# Preserve the native callee address (volatile) and return class address
	# (non-volatile).

	lea rsi, [rsi + rdi * 8]           # arg_values += stack_arg_count * 8

	lea rbx, [rsi + 48]                # ret_class_addr = arg_values + 48

	mov r10, rdx                       # native_callee = in_native_callee



	# Set up general-purpose argument-register values.

	mov rdi, [rsi]
	mov rsi, [rsi + 8]
	mov rdx, [rsi + 16]
	mov rcx, [rsi + 24]
	mov r8,  [rsi + 32]
	mov r9,  [rsi + 40]



	# Call out to native callee

	call r10                           # call native_callee



	# Retrieve return class, destination and size, branching to `DONE` if the
	# return value is already in memory.

	mov rdi, [rbx]                     # ret_class = [ret_class_addr]

	test rdi, rdi
	jz DONE                            # if ret_class == SYSV_RET_COPY_NONE goto DONE

	mov rsi, [rbx + 8]                 # ret_dst = [ret_class_addr + 8]

	mov rbx, [rbx + 16]                # ret_size = [ret_class_addr + 16]



	# Copy return value out of rax / xmm0.

	mov rcx, rbx                       # ret_size_lo = ret_size

	cmp rcx, 8
	jbe RET_SIZE_LO_SET                # if ret_size_lo <= 8 goto RET_SIZE_LO_SET

	mov rcx, 8                         # ret_size_lo = 8

RET_SIZE_LO_SET:

	xor r8, r8                         # ret_offset = 0

	test rdi, 1
	jnz RET_HANDLE_RAX_BYTE            # if ret_class & 0x3 == SYSV_RET_COPY_GPR_LO goto RET_HANDLE_RAX_BYTE

	movq rax, xmm0                     # ret_value_lo = %xmm0@f64

RET_HANDLE_RAX_BYTE:

	cmp r8, rcx
	je RET_HANDLE_HI                   # if ret_offset == ret_size_lo goto RET_HANDLE_HI

	mov [rsi + r8], al                 # [ret_dst + ret_offset] = ret_value_lo@u8
	shr rax, 8                         # ret_value_lo >>= 8
	inc r8                             # ret_offset += 1

	jmp RET_HANDLE_RAX_BYTE            # goto RET_HANDLE_RAX_BYTE



	# Copy return value out of rdx / xmm1.

RET_HANDLE_HI:

	test rdi, 8
	jz RET_HANDLE_RDX_BYTE             # if ret_class & 0xC != SYSV_RET_COPY_XMM_HI goto RET_HANDLE_RDX_BYTE

	movq rdx, xmm1                     # ret_value_hi = xmm1

RET_HANDLE_RDX_BYTE:

	cmp r8, rbx
	je DONE                            # if ret_offset == ret_size goto DONE

	mov [rsi + r8], dl                 # [ret_dst + ret_offset] = ret_value_hi@u8
	shr rdx, 8                         # ret_value_hi >>= 8
	inc r8                             # ret_offset += 1

	jmp RET_HANDLE_RDX_BYTE            # goto RET_HANDLE_RDX_BYTE



DONE:

	# Restore callee-preserved registers and stack, then return.

	mov rbx, [rbp - 8]

	mov rsp, rbp
	pop rbp

	ret





# This magical incantation is apparently necessary to avoid having an
# executable stack, idk.
.section .note.GNU-stack, "", @progbits
