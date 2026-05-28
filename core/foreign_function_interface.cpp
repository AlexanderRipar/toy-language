#include "core.hpp"
#include "structure.hpp"

#include "../infra/math.hpp"

extern "C"
{
	extern void ffi_asm_perform_native_call(u64 arg_count, const u64* arg_values, const void* native_callee);
}

// Pre-processes `arguments` and `return_value_dst` for native calls using the
// system's calling convention.
//
// `out->arg_count`
//   On Win32, receives the number of arguments the native callee expects,
//   including the implicit return value pointer, if applicable.
//
//   On System-V, receives the number of arguments the native callee expects
//   in xmm registers.
//
// `out->return_value_dst`
//   Receives a pointer to the address that will receive the value returned by
//   the native callee. `ffi_perform_native_call` will take care of moving the
//   return value here in case it is returned in a register.
//
// `out->arg_values`
//   On Win32, receives the argument values that the native callee will accept
//   via `ffi_perform_native_call`, with the first four going into both
//   general-purpose- and xmm-registers, and all of them (including the first
//   four) going on the stack.
//   Immediately after the argument values, a `WIN32_RET_COPY_*` value is
//   provided to indicate how the return value of the native callee needs to be
//   fixed up. If this is not `WIN32_RET_COPY_NONE`, the next element receives
//   the address into which the return value must be written.
//
//   On System-V, receives `argument_count` xmm values, followed by the number
//   64-bit words expected on the stack, and then the value of these words.
//   The next six elements contain the argument values expected in general
//   purpose registers, padded as necessary.
//   The next element contains a `SYSV_RET_COPY_*` value. If this is not
//   `SYSV_RET_COPY_NONE`, the following two elements respectively contain the
//   return value destination and the size of the return value.
void ffi_prepare_args_for_native_call(CoreData* core, Range<ScopeMember> arguments, byte* argument_data, byte* return_value_dst, TypeId return_type, FFINativeCallArgs* out) noexcept
{
#ifdef _WIN32

	static constexpr u8 WIN32_RET_COPY_NONE = 0;
	static constexpr u8 WIN32_RET_COPY_I8   = 1;
	//                  WIN32_RET_COPY_I16  = 2;
	//                  WIN32_RET_COPY_I32  = 3;
	//                  WIN32_RET_COPY_I64  = 4;
	static constexpr u8 WIN32_RET_COPY_FLT  = 5;
	static constexpr u8 WIN32_RET_COPY_DBL  = 6;

	u8 i = 0;

	const TypeTag return_type_tag = type_tag_from_id(core, return_type);

	u64 return_copy_kind;

	u64 return_copy_dst;

	if (return_type_tag == TypeTag::Integer || return_type_tag == TypeTag::Ptr || return_type_tag == TypeTag::Boolean)
	{
		const NumericType attach = return_type_tag == TypeTag::Integer
			? *type_attachment_from_id<NumericType>(core, return_type)
			: return_type_tag == TypeTag::Ptr ? NumericType{ 64, false } : NumericType{ 8, false };

		ASSERT_OR_IGNORE(attach.bits == 8 || attach.bits == 16 || attach.bits == 32 || attach.bits == 64);

		return_copy_kind = WIN32_RET_COPY_I8 + count_trailing_zeros_assume_one(attach.bits / 8);
		return_copy_dst = reinterpret_cast<u64>(return_value_dst);
	}
	else if (return_type_tag == TypeTag::Float)
	{
		const NumericType attach = *type_attachment_from_id<NumericType>(core, return_type);

		ASSERT_OR_IGNORE(attach.bits == 32 || attach.bits == 64);

		return_copy_kind = attach.bits == 32 ? WIN32_RET_COPY_FLT : WIN32_RET_COPY_DBL;
		return_copy_dst = reinterpret_cast<u64>(return_value_dst);
	}
	else
	{
		ASSERT_OR_IGNORE(return_type_tag == TypeTag::Composite);

		TypeMetrics metrics;
		
		if (!type_metrics_from_id(core, return_type, &metrics))
			ASSERT_UNREACHABLE;

		if (metrics.size == 1 || metrics.size == 2 || metrics.size == 4 || metrics.size == 8)
		{
			return_copy_kind = WIN32_RET_COPY_I8 + count_trailing_zeros_assume_one(metrics.size);
			return_copy_dst = reinterpret_cast<u64>(return_value_dst);
		}
		else
		{
			return_copy_kind = WIN32_RET_COPY_NONE;
			return_copy_dst = 0;

			out->arg_values[0] = reinterpret_cast<u64>(return_value_dst);

			i = 1;
		}
	}

	for (const ScopeMember* curr_argument = arguments.begin(); curr_argument != arguments.end(); curr_argument++)
	{
		const TypeTag param_type_tag = type_tag_from_id(core, curr_argument->type);

		const byte* const arg_src = argument_data + curr_argument->offset;

		u64 arg_value = 0;

		if (param_type_tag == TypeTag::Integer || param_type_tag == TypeTag::Ptr || param_type_tag == TypeTag::Boolean)
		{
			const NumericType attach = param_type_tag == TypeTag::Integer
				? *type_attachment_from_id<NumericType>(core, curr_argument->type)
				: param_type_tag == TypeTag::Ptr ? NumericType{ 64, false } : NumericType{ 8, false };

			ASSERT_OR_IGNORE(attach.bits == 8 || attach.bits == 16 || attach.bits == 32 || attach.bits == 64);

			memcpy(&arg_value, arg_src, attach.bits / 8);

			// Sign extend as needed. We take care not to shift by 64 (UB).
			if (attach.is_signed && attach.bits != 64 && (arg_value >> (attach.bits - 1)) == 1)
				arg_value |= (~static_cast<u64>(0)) << attach.bits;
		}
		else if (param_type_tag == TypeTag::Float)
		{
			const NumericType* attach = type_attachment_from_id<NumericType>(core, curr_argument->type);

			ASSERT_OR_IGNORE(attach->bits == 32 || attach->bits == 64);

			memcpy(&arg_value, arg_src, attach->bits / 8);
		}
		else
		{
			ASSERT_OR_IGNORE(param_type_tag == TypeTag::Composite);

			TypeMetrics metrics;

			if (!type_metrics_from_id(core, curr_argument->type, &metrics))
				ASSERT_UNREACHABLE;

			if (metrics.size == 1 || metrics.size == 2 || metrics.size == 4 || metrics.size == 8)
			{
				memcpy(&arg_value, arg_src, metrics.size);
			}
			else
			{
				arg_value = reinterpret_cast<u64>(arg_src);
			}
		}

		out->arg_values[i] = arg_value;

		i += 1;
	}

	const u8 first_return_slot = i < 4 ? 4 : i;

	out->arg_values[first_return_slot    ] = return_copy_kind;
	out->arg_values[first_return_slot + 1] = return_copy_dst;

	out->arg_count = i;

#else

	static constexpr u8 SYSV_RET_COPY_NONE   = 0;
	static constexpr u8 SYSV_RET_COPY_GPR_LO = 0x01;
	static constexpr u8 SYSV_RET_COPY_XMM_LO = 0x02;
	static constexpr u8 SYSV_RET_COPY_GPR_HI = 0x04;
	static constexpr u8 SYSV_RET_COPY_XMM_HI = 0x08;

	(void) core;

	(void) arguments;

	(void) argument_data;

	(void) return_value_dst;

	(void) return_type;

	(void) out;

	(void) SYSV_RET_COPY_NONE;
	(void) SYSV_RET_COPY_GPR_LO;
	(void) SYSV_RET_COPY_XMM_LO;
	(void) SYSV_RET_COPY_GPR_HI;
	(void) SYSV_RET_COPY_XMM_HI;

	TODO("Implement `ffi_prepare_args_for_native_call` for System-V.");

#endif // _WIN32
}

// Calls out to `native_callee`, with `args` previously preprocessed by
// `ffi_prepare_args_for_native_call`.
void ffi_perform_native_call(const void* native_callee, const FFINativeCallArgs* args) noexcept
{
	ffi_asm_perform_native_call(args->arg_count, args->arg_values, native_callee);
}



/*
struct ForeignFunctionTrampoline
{
	TypeId signature_type;

	u32 attach_size;

	byte attach[];
};

enum class X64GPR : u8
{
	INVALID = 0,
	AX,
	CX,
	DX,
	BX,
	SP,
	BP,
	SI,
	DI,
	R8,
	R9,
	R10,
	R11,
	R12,
	R13,
	R14,
	R15,
};

enum class X64XMM : u8
{
	INVALID = 0,
	XMM0,
	XMM1,
	XMM2,
	XMM3,
	XMM4,
	XMM5,
	XMM6,
	XMM7,
	XMM8,
	XMM9,
	XMM10,
	XMM11,
	XMM12,
	XMM13,
	XMM14,
	XMM15,
};

static void emit_instruction_raw(CoreData* core, u8 size, const byte* instruction) noexcept
{
	byte* const dst = core->ffi.trampolines.reserve(size);

	memcpy(dst, instruction, size);
}


static void emit_modrm_generic(CoreData* core, byte opcode, bool is_register_direct, u8 operand_size, X64GPR reg, X64GPR base, Maybe<X64GPR> index, s32 offset) noexcept
{
	if (is_some(index) && get(index) == X64GPR::SP)
	{
		// rsp cannot be encoded in the index field. Since our scale is fixed
		// to 1, we can swap with the base, as long as that is not also sp.

		ASSERT_OR_IGNORE(base != X64GPR::SP);

		const X64GPR tmp = get(index);

		index = some(base);

		base = tmp;
	}

	// OPERAND-SIZE-OVERRIDE + REX + OP + MOD.RM + SIB + OFF32
	byte insn[1 + 1 + 1 + 1 + 1 + 4];

	u8 i = 0;

	// operand size override byte for word-sized `mov`s.
	if (operand_size == 2)
	{
		insn[i] = 0x66;
		i += 1;
	}

	if (operand_size == 8 || reg >= X64GPR::R8 || base >= X64GPR::R8 || (is_some(index) && get(index) >= X64GPR::R8))
	{
		// `rex` prefix, as required.

		byte rex = 0b0100'0000;

		if (operand_size == 8)
			rex |= 0b1000;

		if (reg >= X64GPR::R8)
			rex |= 0b0100;

		if (is_some(index) && get(index) >= X64GPR::R8)
			rex |= 0b0010;

		if (base >= X64GPR::R8)
			rex |= 0b0001;

		insn[i] = rex;
		i += 1;
	}

	insn[i] = opcode;
	i += 1;

	ASSERT_OR_IGNORE(!is_register_direct || offset == 0);

	byte modrm;

	if (is_register_direct)
		modrm = 0b11'000'000;
	if (offset == 0)
		modrm = 0b00'000'000;
	else if (offset >= -128 && offset <= 127)
		modrm = 0b01'000'000;
	else
		modrm = 0b10'000'000;

	modrm |= ((static_cast<byte>(reg) - 1) & 0b111) << 3;

	if (is_some(index))
		modrm |= 0b00'000'100;
	else
		modrm |= (static_cast<byte>(base) - 1) & 0b111;

	insn[i] = modrm;
	i += 1;

	if (is_some(index) || base == X64GPR::SP)
	{
		// `mov x [rsp]` is handled specially as its encoding collides with the
		// r/m value 0b100 indicating the presence of a sib byte, meaning it
		// must be encoded inside sib, with the index set to 0b100 to indicate
		// that there is no actual index.
		ASSERT_OR_IGNORE(base != X64GPR::SP || get(index) != X64GPR::SP);

		byte sib = 0;

		sib |= (static_cast<byte>(base) - 1) & 0b111;

		if (base == X64GPR::SP)
			sib |= 0b00'100'000;
		else
			sib |= ((static_cast<byte>(get(index)) - 1) & 0b111) << 3;

		insn[i] = sib;
		i += 1;
	}

	if (offset < -128 || offset > 127)
	{
		memcpy(insn + i, &offset, 4);
		i += 4;
	}
	else if (offset != 0)
	{
		insn[i] = static_cast<byte>(offset);
		i += 1;
	}

	emit_instruction_raw(core, i, insn);
}

static void emit_vex_generic(CoreData* core, byte opcode, byte opcode_prefix, X64XMM xmm, X64GPR base, Maybe<X64GPR> index, s32 offset) noexcept
{
	// vmovss xmm15, [rax]
	//         11000101   C5   VEX-2-BYTE-PREFIX
	//      0 1111 0 10   7A   VEX-2-BYTE-2 ~reg=+0 ~op=15[fixed] size=xmm prefix=F3
	//         00010000   10   OPCODE (F3 10)
	//       00 111 000   38   MOD.RM indirect reg=r7 rm=r0
	//
	// vmovss xmm15, [r8]
	//         11000100   C4   VEX-3-BYTE-PREFIX
	//      0 1 0 00001   41   VEX-3-BYTE-2 ~reg=+0 ~index=+8 ~base+=0 op-map=1
	//      0 1111 0 10   7A   VEX-3-BYTE-3 op-ext=0 ~op=15[fixed] size=xmm prefix=F3
	//         00010000   10   OPCODE (F3 10)
	//         00111000   38   MOD.RM indirect reg=r7 rm=r0

	ASSERT_OR_IGNORE(is_none(index) || get(index) != X64GPR::SP || base != X64GPR::SP);

	if (is_some(index) && get(index) == X64GPR::SP)
	{
		const X64GPR tmp = get(index);

		index = some(base);

		base = tmp;
	}

	// VEX + OPCODE + MOD.RM + SIB + OFFSET-32
	byte insn[3 + 1 + 1 + 1 + 4];

	u8 i = 0;

	if (base < X64GPR::R8 && (is_none(index) || (get(index) < X64GPR::R8)))
	{
		insn[i] = 0xC5; // Two-byte VEX prefix.
		i += 1;

		byte vex_2 = 0b0'1111'0'00;

		if (xmm < X64XMM::XMM8)
			vex_2 |= 0b1'0000'0'00;

		if (opcode_prefix == 0x66)
			vex_2 |= 0b0'0000'0'01;
		else if (opcode_prefix == 0xF3)
			vex_2 |= 0b0'0000'0'10;
		else if (opcode_prefix == 0xF2)
			vex_2 |= 0b0'0000'0'11;
		else
			ASSERT_OR_IGNORE(opcode_prefix == 0);

		insn[i] = vex_2;
		i += 1;
	}
	else
	{
		insn[i] = 0xC4; // Three-byte VEX prefix.
		i += 1;

		byte vex_2 = 0b0'0'0'00001;

		if (xmm < X64XMM::XMM8)
			vex_2 |= 0b1'0'0'00000;

		if (is_some(index) && get(index) < X64GPR::R8)
			vex_2 |= 0b0'1'0'00000;

		if (base < X64GPR::R8)
			vex_2 |= 0b0'0'1'00000;

		insn[i] = vex_2;
		i += 1;

		byte vex_3 = 0b0'1111'0'00;

		if (opcode_prefix == 0x66)
			vex_3 |= 0b0'0000'0'01;
		else if (opcode_prefix == 0xF3)
			vex_3 |= 0b0'0000'0'10;
		else if (opcode_prefix == 0xF2)
			vex_3 |= 0b0'0000'0'11;
		else
			ASSERT_OR_IGNORE(opcode_prefix == 0);

		insn[i] = vex_3;
		i += 1;
	}

	insn[i] = opcode;
	i += 1;

	byte modrm;

	if (offset == 0)
		modrm = 0b00'000'000;
	else if (offset >= -128 && offset <= 127)
		modrm = 0b01'000'000;
	else
		modrm = 0b10'000'000;

	modrm |= ((static_cast<byte>(xmm) - 1) & 7) << 3;

	if (is_some(index))
		modrm |= 0b00'000'100;
	else
		modrm |= (static_cast<byte>(base) - 1) & 7;

	insn[i] = modrm;
	i += 1;

	if (base == X64GPR::SP || is_some(index))
	{
		byte sib = 0b00'000'000;

		if (is_some(index))
			sib |= ((static_cast<byte>(base) - 1) & 7) << 3;
		else
			sib |= 0b00'100'000;

		sib |= (static_cast<byte>(base) - 1) & 7;

		insn[i] = sib;
		i += 1;
	}

	if (offset < -128 || offset > 127)
	{
		memcpy(insn + i, &offset, 4);
		i += 4;
	}
	else if (offset != 0)
	{
		insn[i] = static_cast<byte>(offset);
		i += 1;
	}

	emit_instruction_raw(core, i, insn);
}



static void emit_move_memory_to_gpr(CoreData* core, u8 operand_size, X64GPR dst, X64GPR src_base, Maybe<X64GPR> src_index, s32 src_offset) noexcept
{
	// mov bl, [rbx]
	//         10001010   8A   OP
	//       00 011 011   1B   MOD.RM indirect, reg=r3, rm=r3
	//
	// mov r8b, [rbx]
	//     0100 0 1 0 0   44   REX size=default, reg=+8, sib-index=+0, rm=+0
	//         10001010   8A   OP
	//       00 000 011   03   MOD.RM indirect, reg=r0, rm=r3
	//
	// mov r8b, [rbx + rax]
	//     0100 0 1 0 0   44   REX size=default, reg=+8, sib-index=+0, rm=+0
	//         10001010   8A   OP
	//       00 000 100   04   MOD.RM indirect, reg=r0, rm=SIB
	//       00 000 011   03   SIB scale=0, index=r0, base=r3
	//
	// NOTE that [rsp+rsp] is not possible to encode due to the double-encoding of rsp in sub.index and sib.base
	// mov al, [rsp]
	//         10001010   8A   OP
	//       00 000 100   04   MOD.RM indirect, reg=r0, rm=SIB
	//       00 100 100   24   SIB scale=0, index=r4, base=r4
	//
	// mov eax, [r12]
	//     0100 0 0 0 1   41
	//         10001011   8B
	//       00 000 100   04
	//       00 100 100   24
	//
	// mov al, [r12 + r12]
	//     0100 0 0 1 1   43   REX size=default, reg=+0, sib-index=+8, rm=+8 
	//         10001010   8A   OP
	//       00 000 100   04   MOD.RM indirect, reg=r0, rm=SIB
	//       00 100 100   24   SIB scale=0, index=r4, base=r4
	//
	// mov al, [rcx]
	//         10001010   8A   OP
	//       00 000 001   01   MOD.RM indirect, reg=r0, rm=r1
	//
	// mov ax, [rcx]
	//         01100110   66   OPERAND-SIZE-OVERRIDE
	//         10001011   8B   OP
	//       00 000 001   01   MOD.RM indirect, reg=r0, rm=r1
	//
	// mov eax, [rcx]
	//         10001011   8B   OP
	//       00 000 001   01   MOD.RM indirect, reg=r0, rm=r1
	//
	// mov rax, [rcx]
	//     0100 1 0 0 0   48   REX size=64, reg=+, sib-index=+0, rm=+8
	//         10001011   8B   OP
	//       00 000 001   01   MOD.RM indirect, reg=r0, rm=r1
	//
	// mov eax, [rcx + 1]
	//         10001011   8b   OP
	//       01 000 001   41   MOD.RM indirect-off8, reg=r0, rm=r1
	//         00000001   01   OFF8=1
	//
	// mov eax, [rcx + 127]
	//         10001011   8b   OP
	//       01 000 001   41   MOD.RM indirect-off8, reg=r0, rm=r1
	//         01111111   01   OFF8=127
	//
	// mov eax, [rcx + 128]
	//         10001011   8b   OP
	//       10 000 001   81   MOD.RM indirect-off32, reg=r0, rm=r1
	//         10000000   01   OFF32=128
	//         00000000   00   |
	//         00000000   00   |
	//         00000000   00   ´

	// `mov` r/m to reg opcode. 0x8A for byte `mov`s, 0x8B for all other sizes.
	emit_modrm_generic(core, operand_size == 1 ? 0x8A : 0x8B, false, operand_size, dst, src_base, src_index, src_offset);
}

static void emit_move_gpr_to_gpr(CoreData* core, u8 operand_size, X64GPR dst, X64GPR src) noexcept
{
	// `mov` r/m to reg opcode.
	emit_modrm_generic(core, operand_size == 1 ? 0x8A : 0x8B, true, operand_size, dst, src, none<X64GPR>(), 0);
}

static void emit_move_gpr_to_memory(CoreData* core, u8 operand_size, X64GPR dst_base, Maybe<X64GPR> dst_index, s32 dst_offset, X64GPR src) noexcept
{
	// `mov` reg to r/m opcode. 0x88 for byte `mov`s, 0x89 for all other sizes.
	emit_modrm_generic(core, operand_size == 1 ? 0x88 : 0x89, false, operand_size, src, dst_base, dst_index, dst_offset);
}


static void emit_move_memory_to_xmm(CoreData* core, u8 operand_size, X64XMM dst, X64GPR src_base, Maybe<X64GPR> src_index, s32 src_offset) noexcept
{
	ASSERT_OR_IGNORE(operand_size == 4 || operand_size == 8);

	const byte opcode_prefix = operand_size == 4 ? 0xF3 : 0xF2;

	emit_vex_generic(core, 0x10, opcode_prefix, dst, src_base, src_index, src_offset);
}

static void emit_move_xmm_to_memory(CoreData* core, u8 operand_size, X64GPR dst_base, Maybe<X64GPR> dst_index, s32 dst_offset, X64XMM src) noexcept
{
	ASSERT_OR_IGNORE(operand_size == 4 || operand_size == 8);

	const byte opcode_prefix = operand_size == 4 ? 0xF3 : 0xF2;

	emit_vex_generic(core, 0x11, opcode_prefix, src, dst_base, dst_index, dst_offset);
}


static void emit_call_to_gpr(CoreData* core, X64GPR callee) noexcept
{
	// call rcx
	//         11111111   FF   OPCODE
	//       11 010 001   D1   MOD.RM direct reg=r2 rm=r1

	// MOD.RM reg must be equal to 2 (rdx) to encode call.
	// We set `operand_size` to 4, since call defaults to size 8 instead of the
	// usual 4, meaning we don't want REX.W set.
	emit_modrm_generic(core, 0xFF, true, 4, X64GPR::DX, callee, none<X64GPR>(), 0);
}

static void emit_ret(CoreData* core) noexcept
{
	const byte insn = 0xC3;

	emit_instruction_raw(core, 1, &insn);
}



static void emit_push(CoreData* core, X64GPR src) noexcept
{
	// MOD.RM reg must be equal to 6 (rsi) to encode push.
	// We set `operand_size` to 4, since push defaults to size 8 instead of the
	// usual 4, meaning we don't want REX.W set.
	emit_modrm_generic(core, 0xFF, true, 4, X64GPR::SI, src, none<X64GPR>(), 0);
}

static void emit_pop(CoreData* core, X64GPR dst) noexcept
{
	// MOD.RM reg must be equal to 0 (rax) to encode push
	// We set `operand_size` to 4, since pop defaults to size 8 instead of the
	// usual 4, meaning we don't want REX.W set.
	emit_modrm_generic(core, 0x8F, true, 4, X64GPR::AX, dst, none<X64GPR>(), 0);
}


static void emit_add_constant(CoreData* core, X64GPR dst, s32 value) noexcept
{
	const byte opcode = value >= -128 && value <= 127 ? 0x83 : 0x81;

	// We force `is_register_direct` to `true` but still emit an offset. This
	// kind of fakes the encoding of add, but works since we only allow
	// register-direct adds. MOD.RM reg must be set to 0 (rax) for add.
	emit_modrm_generic(core, opcode, true, 8, X64GPR::AX, dst, none<X64GPR>(), value);
}

static void emit_sub_constant(CoreData* core, X64GPR dst, s32 value) noexcept
{
	const byte opcode = value >= -128 && value <= 127 ? 0x83 : 0x81;

	// We force `is_register_direct` to `true` but still emit an offset. This
	// kind of fakes the encoding of sub, but works since we only allow
	// register-direct subs. MOD.RM reg must be set to 5 (rbp) for sub.
	emit_modrm_generic(core, opcode, true, 8, X64GPR::BP, dst, none<X64GPR>(), value);
}

static void emit_add_gpr(CoreData* core, X64GPR dst, X64GPR src) noexcept
{
	emit_modrm_generic(core, 0x01, true, 8, src, dst, none<X64GPR>(), 0);
}



static constexpr X64GPR SCRATCH_REG = X64GPR::AX;

static constexpr X64GPR ARGUMENT_SCOPE_REG = X64GPR::R10;

static constexpr X64GPR ARGUMENT_DATA_REG = X64GPR::R11;

static constexpr X64GPR NATIVE_CALLEE_REG = X64GPR::BX;

static constexpr X64GPR WRITE_CTX_BYTES_BEGIN_REG = X64GPR::DI;

static constexpr X64GPR ARG1_INT_REG = X64GPR::CX;

static constexpr X64GPR ARG2_INT_REG = X64GPR::DX;

static constexpr X64GPR ARG3_INT_REG = X64GPR::R8;

static constexpr X64GPR ARG4_INT_REG = X64GPR::R9;

enum class FFIImportRst : u8
{
	Ok = 0,
	TemplatedSignature,
	VariadicSignature,
	InvalidParameterType,
	InvalidReturnType,
	NoSuchLibrary,
	NoSuchFunction,
};

// Volatile registers:
// - AX    scratch
// - CX    1st argument
// - DX    2nd argument
// - R8    3rd argument
// - R9    4th argument
// - R10   argument_scope
// - R11   argument_data
//
// Non-volatile registers:
// - BX    native_callee
// - BP    -
// - DI    write_ctx_bytes_begin
// - SI    -
// - SP    -
// - R12   -
// - R13   -
// - R14   -
// - R15   -

static void emit_trampoline_prologue(CoreData* core) noexcept
{
#ifdef _WIN32

	// Save non-volatile register rbx.
	emit_push(core, NATIVE_CALLEE_REG);

	// Save non-volatile register rdi.
	emit_push(core, WRITE_CTX_BYTES_BEGIN_REG);

	// Move argument `byte* write_ctx_bytes_begin` to previously saved
	// non-volatile register rdi.
	emit_move_gpr_to_gpr(core, 8, WRITE_CTX_BYTES_BEGIN_REG, ARG1_INT_REG);

	// Move argument `const void* native_callee` to previously saved
	// non-volatile register rbx.
	emit_move_gpr_to_gpr(core, 8, NATIVE_CALLEE_REG, ARG2_INT_REG);

	// Move argument `const ScopeMember* argument_scope` to volatile register r10.
	emit_move_gpr_to_gpr(core, 8, ARGUMENT_SCOPE_REG, ARG3_INT_REG);

	// Move argument `byte* argument_data` to volatile register r10.
	emit_move_gpr_to_gpr(core, 8, ARGUMENT_DATA_REG, ARG4_INT_REG);

#else

	#error "Implement `emit_trampoline_prologue` for non-windows platforms."

#endif
}

static void emit_trampoline_epilogue(CoreData* core, u32 used_stack) noexcept
{
#ifdef _WIN32

	// Free stack space used for parameters.
	emit_add_constant(core, X64GPR::SP, used_stack);

	// Restore non-volatile register rbx.
	emit_pop(core, NATIVE_CALLEE_REG);

	// Restore non-volatile register rdi.
	emit_pop(core, WRITE_CTX_BYTES_BEGIN_REG);

	// Return to caller.
	emit_ret(core);

#else

	#error "Implement `emit_trampoline_epilogue` for non-windows platforms."

#endif
}

static u32 emit_integer_parameter(CoreData* core, u8 size, u8 rank) noexcept
{
#ifdef _WIN32

	// rax holds the offset into `argument_data` at which the argument begins.

	if (rank < 4)
	{
		static constexpr X64GPR DST_REGS[] = {
			ARG1_INT_REG,
			ARG2_INT_REG,
			ARG3_INT_REG,
			ARG4_INT_REG,
		};

		emit_move_memory_to_gpr(core, size, DST_REGS[rank], ARGUMENT_DATA_REG, some(SCRATCH_REG), 0);

		return 0;
	}
	else
	{
		// All arguments starting from the fourth go onto the stack. Load them
		// into rax, and then push them.

		emit_move_memory_to_gpr(core, size, SCRATCH_REG, ARGUMENT_DATA_REG, some(SCRATCH_REG), 0);

		emit_push(core, SCRATCH_REG);

		return 8;
	}

#else

	#error "TODO: Implement `emit_integer_parameter` on non-windows platforms"

#endif
}

static u32 emit_float_parameter(CoreData* core, u8 size, u8 rank) noexcept
{
#ifdef _WIN32

	// rax holds the offset into `argument_data` at which the argument begins.

	if (rank < 4)
	{
		const X64XMM dst_reg = static_cast<X64XMM>(static_cast<u8>(X64XMM::XMM0) + rank);

		emit_move_memory_to_xmm(core, size, dst_reg, ARGUMENT_DATA_REG, some(SCRATCH_REG), 0);

		return 0;
	}
	else
	{
		// All arguments starting from the fourth go onto the stack. Load them
		// into rax, and then push them.

		emit_move_memory_to_gpr(core, size, SCRATCH_REG, ARGUMENT_DATA_REG, some(SCRATCH_REG), 0);

		emit_push(core, SCRATCH_REG);

		return 8;
	}

#else

	#error "TODO: Implement `emit_float_parameter` on non-windows platforms"

#endif
}

static FFIImportRst emit_parameter(CoreData* core, TypeId type, u8 rank, u32* inout_stack) noexcept
{	
#ifdef _WIN32

	TypeTag type_tag = type_tag_from_id(core, type);

	while (type_tag == TypeTag::Self)
	{
		const SelfType* const attach = type_attachment_from_id<SelfType>(core, type);

		type = attach->base_type_id;

		type_tag = type_tag_from_id(core, type);
	}

	// Move the offset of the rank-th argument data relative to the scope data
	// stack from the scope member stack into rax.
	emit_move_memory_to_gpr(core, sizeof(ScopeMember::offset), X64GPR::AX, ARGUMENT_SCOPE_REG, none<X64GPR>(), rank * sizeof(ScopeMember) + offsetof(ScopeMember, offset));

	switch (type_tag)
	{
	case TypeTag::Boolean:
	{
		*inout_stack += emit_integer_parameter(core, 1, rank);

		return FFIImportRst::Ok;
	}

	case TypeTag::Integer:
	{
		const NumericType* const attach = type_attachment_from_id<NumericType>(core, type);

		ASSERT_OR_IGNORE(attach->bits >= 8 && attach->bits <= 64 && is_pow2(attach->bits));

		*inout_stack += emit_integer_parameter(core, static_cast<u8>(attach->bits / 8), rank);

		return FFIImportRst::Ok;
	}

	case TypeTag::Float:
	{
		const NumericType* const attach = type_attachment_from_id<NumericType>(core, type);

		ASSERT_OR_IGNORE(attach->bits == 32 || attach->bits == 64);

		*inout_stack += emit_float_parameter(core, static_cast<u8>(attach->bits / 8), rank);

		return FFIImportRst::Ok;
	}

	case TypeTag::Ptr:
	{
		*inout_stack += emit_integer_parameter(core, 8, rank);

		return FFIImportRst::Ok;
	}

	case TypeTag::Composite:
	{
		TypeMetrics metrics;

		if (!type_metrics_from_id(core, type, &metrics))
			ASSERT_UNREACHABLE;

		if (metrics.size == 1 || metrics.size == 2 || metrics.size == 4 || metrics.size == 8)
		{
			*inout_stack = emit_integer_parameter(core, static_cast<u8>(metrics.size), rank);
		}
		else if (rank < 4)
		{
			static constexpr X64GPR DST_REGS[] = {
				ARG1_INT_REG,
				ARG2_INT_REG,
				ARG3_INT_REG,
				ARG4_INT_REG,
			};

			// Put ARGUMENT_DATA_REG + SCRATCH_REG into DST_REGS[rank].

			emit_add_gpr(core, SCRATCH_REG, ARGUMENT_DATA_REG);

			emit_move_gpr_to_gpr(core, 8, DST_REGS[rank], SCRATCH_REG);

			return FFIImportRst::Ok;
		}
		else
		{
			// Put ARGUMENT_DATA_REG + SCRATCH_REG on the stack
			emit_add_gpr(core, SCRATCH_REG, ARGUMENT_DATA_REG);

			emit_push(core, SCRATCH_REG);

			*inout_stack += 8;

			return FFIImportRst::Ok;
		}
	}

	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	case TypeTag::Slice:
	case TypeTag::Array:
	case TypeTag::Signature:
	case TypeTag::TailArray:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Trait:
	{
		return FFIImportRst::InvalidParameterType;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
	case TypeTag::Self:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;

#else

	#error "TODO: Implement `emit_parameter` on non-windows platforms"

#endif
}

static FFIImportRst emit_return_value_pointer(CoreData* core, TypeId type, bool* out_is_by_pointer) noexcept
{
#ifdef _WIN32

	TypeTag type_tag = type_tag_from_id(core, type);

	while (type_tag == TypeTag::Self)
	{
		const SelfType* const attach = type_attachment_from_id<SelfType>(core, type);

		type = attach->base_type_id;

		type_tag = type_tag_from_id(core, type);
	}

	switch (type_tag)
	{
	case TypeTag::Boolean:
	case TypeTag::Integer:
	case TypeTag::Float:
	case TypeTag::Ptr:
	{
		*out_is_by_pointer = false;

		return FFIImportRst::Ok;
	}

	case TypeTag::Composite:
	{
		TypeMetrics metrics;

		const bool is_by_pointer = metrics.size != 1 && metrics.size != 2 && metrics.size != 4 && metrics.size != 8;

		if (is_by_pointer)
			emit_move_gpr_to_gpr(core, static_cast<u8>(metrics.size), ARG1_INT_REG, WRITE_CTX_BYTES_BEGIN_REG);

		*out_is_by_pointer = true;

		return FFIImportRst::Ok;
	}

	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	case TypeTag::Slice:
	case TypeTag::Array:
	case TypeTag::Signature:
	case TypeTag::TailArray:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Trait:
	{
		return FFIImportRst::InvalidReturnType;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
	case TypeTag::Self:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;

#else

	#error "TODO: Implement `emit_return_value_setup` on non-windows platforms"

#endif
}

static void emit_home_space(CoreData* core, u32* inout_stack) noexcept
{
#ifdef _WIN32

	// Align the stack to 16 bytes and allocate an additional 4 * 8 bytes as
	// home space for the native callee.
	const u32 home_space_stack = (*inout_stack & 15) + 4 * 8;

	emit_sub_constant(core, X64GPR::SP, home_space_stack);

	*inout_stack += home_space_stack;

#else

	#error "TODO: Implement `emit_home_space` on non-windows platforms"

#endif
}

static void emit_return_value_adjustment(CoreData* core, TypeId type) noexcept
{
#ifdef _WIN32

	TypeTag type_tag = type_tag_from_id(core, type);

	while (type_tag == TypeTag::Self)
	{
		const SelfType* const attach = type_attachment_from_id<SelfType>(core, type);

		type = attach->base_type_id;

		type_tag = type_tag_from_id(core, type);
	}

	switch (type_tag)
	{
	case TypeTag::Boolean:
	{
		emit_move_gpr_to_memory(core, 1, WRITE_CTX_BYTES_BEGIN_REG, none<X64GPR>(), 0, X64GPR::AX);

		return;
	}

	case TypeTag::Integer:
	{
		const NumericType* const attach = type_attachment_from_id<NumericType>(core, type);

		ASSERT_OR_IGNORE(attach->bits >= 8 && attach->bits <= 64 && is_pow2(attach->bits));

		emit_move_gpr_to_memory(core, static_cast<u8>(attach->bits / 8), WRITE_CTX_BYTES_BEGIN_REG, none<X64GPR>(), 0, X64GPR::AX);

		return;
	}

	case TypeTag::Float:
	{
		const NumericType* const attach = type_attachment_from_id<NumericType>(core, type);

		ASSERT_OR_IGNORE(attach->bits == 32 || attach->bits == 64);

		emit_move_xmm_to_memory(core, static_cast<u8>(attach->bits / 8), WRITE_CTX_BYTES_BEGIN_REG, none<X64GPR>(), 0, X64XMM::XMM0);

		return;
	}

	case TypeTag::Ptr:
	{
		emit_move_gpr_to_memory(core, 8, WRITE_CTX_BYTES_BEGIN_REG, none<X64GPR>(), 0, X64GPR::AX);

		return;
	}

	case TypeTag::Composite:
	{
		TypeMetrics metrics;
		
		if (!type_metrics_from_id(core, type, &metrics))
			ASSERT_UNREACHABLE;

		// Structures of size 1, 2, 4 or 8 bytes are handled as if they were
		// integers of the same size, so we need to move them from rax into the
		// write context.
		// When structures of other sizes are returned, a pointer to return
		// into is passed as an implicit first argument, so the write context
		// is written directly and we don't need to touch anything here.
		if (metrics.size == 1 || metrics.size == 2 || metrics.size == 4 || metrics.size == 8)
			emit_move_gpr_to_memory(core, static_cast<u8>(metrics.size), WRITE_CTX_BYTES_BEGIN_REG, none<X64GPR>(), 0, X64GPR::AX);
		
		return;
	}

	case TypeTag::INVALID:
	case TypeTag::INDIRECTION:
	case TypeTag::Void:
	case TypeTag::Type:
	case TypeTag::Definition:
	case TypeTag::CompInteger:
	case TypeTag::CompFloat:
	case TypeTag::TypeInfo:
	case TypeTag::TypeBuilder:
	case TypeTag::Divergent:
	case TypeTag::Undefined:
	case TypeTag::Slice:
	case TypeTag::Array:
	case TypeTag::Signature:
	case TypeTag::TailArray:
	case TypeTag::CompositeLiteral:
	case TypeTag::ArrayLiteral:
	case TypeTag::Trait:
	case TypeTag::Self:
		; // Fallthrough to unreachable.
	}

	ASSERT_UNREACHABLE;

#else

	#error "TODO: Implement `emit_return_value_adjustment` on non-windows platforms"

#endif
}

static FFIImportRst generate_trampoline(CoreData* core, TypeId signature_type, const void* function_address, ForeignFunctionTrampoline* trampoline) noexcept
{
	SignatureTypeInfo signature_info = type_signature_info_from_id(core, signature_type);

	if (signature_info.has_templated_return_type || signature_info.templated_parameter_count != 0)
		return FFIImportRst::TemplatedSignature;

	ASSERT_OR_IGNORE(is_none(signature_info.closure_id));

	if (signature_info.is_variadic)
		return FFIImportRst::VariadicSignature;

	emit_trampoline_prologue(core);

	bool is_return_by_pointer;

	if (const FFIImportRst rst = emit_return_value_pointer(core, signature_info.return_type.complete.type_id, &is_return_by_pointer); rst != FFIImportRst::Ok)
		return rst;

	u32 total_stack = 0;

	MemberIterator it = members_of(core, signature_type);

	while (has_next(&it))
	{
		MemberInfo parameter_info;

		OpcodeId unused_initializer;

		if (!next(&it, &parameter_info, &unused_initializer))
			ASSERT_UNREACHABLE;

		if (const FFIImportRst rst = emit_parameter(core, parameter_info.type_id, static_cast<u8>(parameter_info.rank + static_cast<u8>(is_return_by_pointer)), &total_stack); rst != FFIImportRst::Ok)
			return rst;
	}

	emit_home_space(core, &total_stack);

	// Call out to native function.
	emit_call_to_gpr(core, NATIVE_CALLEE_REG);

	emit_return_value_adjustment(core, signature_info.return_type.complete.type_id);

	emit_trampoline_epilogue(core, total_stack);

	return FFIImportRst::Ok;
}



FFIImportRst ffi_import(CoreData* core, TypeId signature_type, Range<char8> library_path, Range<char8> symbol, ForeignFunctionProc* out) noexcept
{
	minos::LibraryHandle library_handle;

	if (!minos::dynamic_library_create(library_path, &library_handle))
		return FFIImportRst::NoSuchLibrary;

	const void* function_address;

	if (!minos::dynamic_library_load_function(library_handle, symbol, &function_address))
		return FFIImportRst::NoSuchFunction;

	ForeignFunctionTrampoline* const trampoline = core->ffi.trampoline_map.value_from(function_address, fnv1a(range::from_object_bytes(&function_address)));

	if (trampoline->attach_size == 0)
	{
		const FFIImportRst trampoline_rst = generate_trampoline(core, signature_type, function_address, trampoline);

		if (trampoline_rst != FFIImportRst::Ok)
			return trampoline_rst;
	}

	*out = reinterpret_cast<ForeignFunctionProc>(trampoline + 1);

	return FFIImportRst::Ok;
}
*/
