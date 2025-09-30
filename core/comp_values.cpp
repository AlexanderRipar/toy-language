#include "core.hpp"

#include <cmath>
#include <intrin.h>

static constexpr u64 COMP_INTEGER_MAX = (static_cast<u64>(1) << 62) - 1;

static constexpr s64 COMP_INTEGER_MIN = static_cast<s64>((static_cast<u64>(static_cast<s64>(-1)) << 62));

enum class BinaryArithmeticOp
{
	Add,
	Sub,
	Mul,
	Div,
};

template<BinaryArithmeticOp op>
static bool bitwise_binary_arithmetic_op(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept
{
	ASSERT_OR_IGNORE(bits >= 8 && bits <= 64 && is_pow2(bits));

	ASSERT_OR_IGNORE(dst.count() != 0
	              && dst.count() == lhs.count()
	              && dst.count() == rhs.count()
	              && dst.count() == bits / 8);

	if (is_signed)
	{
		s64 lhs_value;

		s64 rhs_value;

		if (bits == 8)
		{
			lhs_value = *reinterpret_cast<const s8*>(lhs.begin());

			rhs_value = *reinterpret_cast<const s8*>(rhs.begin());
		}
		else if (bits == 16)
		{
			lhs_value = *reinterpret_cast<const s16*>(lhs.begin());

			rhs_value = *reinterpret_cast<const s16*>(rhs.begin());
		}
		else if (bits == 32)
		{
			lhs_value = *reinterpret_cast<const s32*>(lhs.begin());

			rhs_value = *reinterpret_cast<const s32*>(rhs.begin());
		}
		else
		{
			ASSERT_OR_IGNORE(bits == 64);

			lhs_value = *reinterpret_cast<const s64*>(lhs.begin());

			rhs_value = *reinterpret_cast<const s64*>(rhs.begin());
		}

		s64 dst_value;

		if constexpr (op == BinaryArithmeticOp::Add)
		{
			if (!add_checked_s64(lhs_value, rhs_value, &dst_value))
				return false;
		}
		else if constexpr (op == BinaryArithmeticOp::Sub)
		{
			if (!sub_checked_s64(lhs_value, rhs_value, &dst_value))
				return false;
		}
		else if constexpr (op == BinaryArithmeticOp::Mul)
		{
			if (!mul_checked_s64(lhs_value, rhs_value, &dst_value))
				return false;
		}
		else if constexpr (op == BinaryArithmeticOp::Div)
		{
			if (rhs_value == 0)
				return false;

			dst_value = lhs_value / rhs_value;
		}
		else
		{
			ASSERT_UNREACHABLE;
		}

		if (bits != 64)
		{
			const s64 min_value = -static_cast<s64>((static_cast<u64>(1) << (bits - 1)));

			const s64 max_value = static_cast<s64>((static_cast<u64>(1) << (bits - 1)) - 1);

			if (dst_value < min_value || dst_value > max_value)
				return false;
		}

		memcpy(dst.begin(), &dst_value, dst.count());
	}
	else
	{
		u64 lhs_value;

		u64 rhs_value;

		if (bits == 8)
		{
			lhs_value = *reinterpret_cast<const u8*>(lhs.begin());

			rhs_value = *reinterpret_cast<const u8*>(rhs.begin());
		}
		else if (bits == 16)
		{
			lhs_value = *reinterpret_cast<const u16*>(lhs.begin());

			rhs_value = *reinterpret_cast<const u16*>(rhs.begin());
		}
		else if (bits == 32)
		{
			lhs_value = *reinterpret_cast<const u32*>(lhs.begin());

			rhs_value = *reinterpret_cast<const u32*>(rhs.begin());
		}
		else
		{
			ASSERT_OR_IGNORE(bits == 64);

			lhs_value = *reinterpret_cast<const u64*>(lhs.begin());

			rhs_value = *reinterpret_cast<const u64*>(rhs.begin());
		}

		u64 dst_value;

		if constexpr (op == BinaryArithmeticOp::Add)
		{
			if (!add_checked_u64(lhs_value, rhs_value, &dst_value))
				return false;
		}
		else if constexpr (op == BinaryArithmeticOp::Sub)
		{
			if (!sub_checked_u64(lhs_value, rhs_value, &dst_value))
				return false;
		}
		else if constexpr (op == BinaryArithmeticOp::Mul)
		{
			if (!mul_checked_u64(lhs_value, rhs_value, &dst_value))
				return false;
		}
		else if constexpr (op == BinaryArithmeticOp::Div)
		{
			if (rhs_value == 0)
				return false;

			dst_value = lhs_value / rhs_value;
		}
		else
		{
			ASSERT_UNREACHABLE;
		}

		if (bits != 64)
		{
			const u64 max_value = (static_cast<u64>(1) << bits) - 1;

			if (dst_value > max_value)
				return false;
		}

		memcpy(dst.begin(), &dst_value, dst.count());
	}

	return true;
}

bool bitwise_add(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept
{
	return bitwise_binary_arithmetic_op<BinaryArithmeticOp::Add>(bits, is_signed, dst, lhs, rhs);
}

bool bitwise_sub(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept
{
	return bitwise_binary_arithmetic_op<BinaryArithmeticOp::Sub>(bits, is_signed, dst, lhs, rhs);
}

void bitwise_shift_left(u16 bits, MutRange<byte> dst, Range<byte> lhs, u64 rhs) noexcept
{
	ASSERT_OR_IGNORE(rhs < bits);

	const u64 shift_by_bytes = rhs / 8;

	const u8 shift_by_bits = rhs % 8;

	const u64 lhs_size = bits / 8;

	const u8 extra_bits = bits % 8;

	const u64 shift_size = lhs_size - shift_by_bytes;

	memset(dst.begin() + shift_size, 0, shift_by_bytes);

	u8 carry = 0;

	for (u64 i = 0; i != shift_size; ++i)
	{
		const u16 shifted = static_cast<u16>((static_cast<u16>(lhs[i]) << shift_by_bits) | carry);

		dst[i + shift_by_bytes] = static_cast<u8>(shifted);

		carry = static_cast<u8>(shifted >> 8);
	}

	if (extra_bits != 0)
	{
		TODO("Left-shift from non-whole byte.");
	}
}

void bitwise_shift_right(u16 bits, MutRange<byte> dst, Range<byte> lhs, u64 rhs, bool is_arithmetic_shift) noexcept
{
	ASSERT_OR_IGNORE(rhs < bits);

	const u64 shift_by_bytes = rhs / 8;

	const u8 shift_by_bits = rhs % 8;

	const u8 shift_by_bits_inverse = 8 - shift_by_bits;

	const u64 lhs_size = bits / 8;

	const u8 extra_bits = bits % 8;

	const u64 shift_size = lhs_size - shift_by_bytes;

	u8 fill = 0;

	if (is_arithmetic_shift)
	{
		if (extra_bits == 0)
		{
			if ((lhs[lhs_size - 1] & 0x80) != 0)
				fill = 0xFF;
		}
		else
		{
			const u8 msb_mask = static_cast<u8>(1) << (extra_bits - 1);

			if ((lhs[lhs_size] & msb_mask) != 0)
				fill = 0xFF;
		}
	}

	memset(dst.begin(), fill, shift_by_bytes);

	u8 carry = 0;

	if (extra_bits != 0)
	{
		TODO("Right-shift from non-whole byte.");
	}

	for (u64 i = 0; i != shift_size; ++i)
	{
		const u8 lhs_byte = lhs[i + shift_by_bytes];

		const u8 shifted = (lhs_byte >> shift_by_bits) | carry;

		dst[i + shift_by_bytes] = shifted;

		carry = static_cast<u8>(static_cast<u16>(lhs_byte) << shift_by_bits_inverse);
	}
}



bool add_checked_u8(u8 a, u8 b, u8* out) noexcept
{
	const u64 result = static_cast<u64>(a) + static_cast<u64>(b);

	*out = static_cast<u8>(result);

	return result <= UINT8_MAX;
}

bool add_checked_u16(u16 a, u16 b, u16* out) noexcept
{
	const u64 result = static_cast<u64>(a) + static_cast<u64>(b);

	*out = static_cast<u16>(result);

	return result <= UINT16_MAX;
}

bool add_checked_u32(u32 a, u32 b, u32* out) noexcept
{
	const u64 result = static_cast<u64>(a) + static_cast<u64>(b);

	*out = static_cast<u32>(result);

	return result <= UINT32_MAX;
}

bool add_checked_u64(u64 a, u64 b, u64* out) noexcept
{
	const u64 result = a + b;

	*out = result;

	return result >= a && result >= b;
}

bool add_checked_s8(s8 a, s8 b, s8* out) noexcept
{
	const s64 result = static_cast<s64>(a) + static_cast<s64>(b);

	*out = static_cast<s8>(result);

	return result >= INT8_MIN && result <= INT8_MAX;
}

bool add_checked_s16(s16 a, s16 b, s16* out) noexcept
{
	const s64 result = static_cast<s64>(a) + static_cast<s64>(b);

	*out = static_cast<s16>(result);

	return result >= INT16_MIN && result <= INT16_MAX;
}

bool add_checked_s32(s32 a, s32 b, s32* out) noexcept
{
	const s64 result = static_cast<s64>(a) + static_cast<s64>(b);

	*out = static_cast<s32>(result);

	return result >= INT32_MIN && result <= INT32_MAX;
}

bool add_checked_s64(s64 a, s64 b, s64* out) noexcept
{
	const u64 result = static_cast<u64>(static_cast<s64>(a)) + static_cast<u64>(static_cast<s64>(b));

	*out = static_cast<s64>(result);

	if (a < 0)
		return b >= INT64_MIN - a;
	else
		return INT64_MAX - a >= b;
}



bool sub_checked_u8(u8 a, u8 b, u8* out) noexcept
{
	const u64 result = static_cast<u64>(a) - static_cast<u64>(b);

	*out = static_cast<u8>(result);

	return result <= UINT8_MAX;
}

bool sub_checked_u16(u16 a, u16 b, u16* out) noexcept
{
	const u64 result = static_cast<u64>(a) - static_cast<u64>(b);

	*out = static_cast<u16>(result);

	return result <= UINT16_MAX;
}

bool sub_checked_u32(u32 a, u32 b, u32* out) noexcept
{
	const u64 result = static_cast<u64>(a) - static_cast<u64>(b);

	*out = static_cast<u32>(result);

	return result <= UINT32_MAX;
}

bool sub_checked_u64(u64 a, u64 b, u64* out) noexcept
{
	const u64 result = a - b;

	*out = result;

	return a >= b;
}

bool sub_checked_s8(s8 a, s8 b, s8* out) noexcept
{
	const s64 result = static_cast<s64>(a) - static_cast<s64>(b);

	*out = static_cast<s8>(result);

	return result >= INT8_MIN && result <= INT8_MAX;
}

bool sub_checked_s16(s16 a, s16 b, s16* out) noexcept
{
	const s64 result = static_cast<s64>(a) - static_cast<s64>(b);

	*out = static_cast<s16>(result);

	return result >= INT16_MIN && result <= INT16_MAX;
}

bool sub_checked_s32(s32 a, s32 b, s32* out) noexcept
{
	const s64 result = static_cast<s64>(a) - static_cast<s64>(b);

	*out = static_cast<s16>(result);

	return result >= INT16_MIN && result <= INT16_MAX;
}

bool sub_checked_s64(s64 a, s64 b, s64* out) noexcept
{
	const u64 result = static_cast<u64>(static_cast<s64>(a)) - static_cast<u64>(static_cast<s64>(b));

	*out = static_cast<s64>(result);

	if (a > 0 && b < 0)
		return result > 0;
	else if (a < 0 && b > 0)
		return result < 0;
	else
		return true;
}



bool mul_checked_u8(u8 a, u8 b, u8* out) noexcept
{
	const u64 result = static_cast<u64>(a) * static_cast<u64>(b);

	*out = static_cast<u8>(result);

	return result <= UINT8_MAX;
}

bool mul_checked_u16(u16 a, u16 b, u16* out) noexcept
{
	const u64 result = static_cast<u64>(a) * static_cast<u64>(b);

	*out = static_cast<u16>(result);

	return result <= UINT16_MAX;
}

bool mul_checked_u32(u32 a, u32 b, u32* out) noexcept
{
	const u64 result = static_cast<u64>(a) * static_cast<u64>(b);

	*out = static_cast<u32>(result);

	return result <= UINT32_MAX;
}

bool mul_checked_u64(u64 a, u64 b, u64* out) noexcept
{
#ifdef COMPILER_MSVC
	u64 high_result;

	const u64 result = _umul128(a, b, &high_result);

	*out = result;

	return a == 0 || b == 0 || (result >= a && result >= b && high_result == 0);
#else
	return !__builtin_mul_overflow(a, b, out);
#endif
}

bool mul_checked_s8(s8 a, s8 b, s8* out) noexcept
{
	const s64 result = static_cast<s64>(a) * static_cast<s64>(b);

	*out = static_cast<s8>(result);

	return result >= INT8_MIN && result <= INT8_MAX;
}

bool mul_checked_s16(s16 a, s16 b, s16* out) noexcept
{
	const s64 result = static_cast<s64>(a) * static_cast<s64>(b);

	*out = static_cast<s16>(result);

	return result >= INT16_MIN && result <= INT16_MAX;
}

bool mul_checked_s32(s32 a, s32 b, s32* out) noexcept
{
	const s64 result = static_cast<s64>(a) * static_cast<s64>(b);

	*out = static_cast<s32>(result);

	return result >= INT32_MIN && result <= INT32_MAX;
}

bool mul_checked_s64(s64 a, s64 b, s64* out) noexcept
{
#ifdef COMPILER_MSVC
	s64 high_result;

	const s64 result = _mul128(a, b, &high_result);

	*out = result;

	if (high_result != 0 && high_result != -1)
		return false;
	else if ((a <= 0) == (b <= 0))
		return result >= 0;
	else
		return result <= 0;
#else
	return !__builtin_mul_overflow(a, b, out);
#endif
}



static bool is_inlined(CompIntegerValue value) noexcept
{
	return (value.rep & 1) == 0;
}

static bool is_negative(CompIntegerValue value) noexcept
{
	ASSERT_OR_IGNORE(is_inlined(value));

	return static_cast<s64>(value.rep) < 0;
}



CompIntegerValue comp_integer_from_u64(u64 value) noexcept
{
	if (value > COMP_INTEGER_MAX)
		panic("Value %" PRIu64 " exceeds current supported maximum value of compile-time integers of %" PRIu64 ".\n", value, COMP_INTEGER_MAX);

	return { value << 1 };
}

CompIntegerValue comp_integer_from_s64(s64 value) noexcept
{
	if (value < COMP_INTEGER_MIN)
		panic("Value %" PRId64 " exceeds current supported minimum value of compile-time integers of %" PRId64 ".\n", value, COMP_INTEGER_MIN);

	if (static_cast<u64>(value) > COMP_INTEGER_MAX)
		panic("Value %" PRId64 " exceeds current supported maximum value of compile-time integers of %" PRIu64 ".\n", value, COMP_INTEGER_MAX);

	return { static_cast<u64>(value) << 1 };
}

bool comp_integer_from_comp_float(CompFloatValue value, bool round, CompIntegerValue* out) noexcept
{
	const f64 float_value = value.rep;

	if (std::isnan(float_value) || std::isinf(float_value) || (!round && ceil(float_value) != float_value))
		return false;

	if ((float_value < 0 && static_cast<s64>(float_value) < COMP_INTEGER_MIN) || (float_value > 0 && static_cast<u64>(float_value) > COMP_INTEGER_MAX))
		panic("Value %f exceeds range of current supported values of compile-time integers.\n", float_value);

	*out = { static_cast<u64>(static_cast<s64>(float_value)) << 1 };

	return true;
}

bool u64_from_comp_integer(CompIntegerValue value, u8 bits, u64* out) noexcept
{
	ASSERT_OR_IGNORE(bits <= 64);

	if (!is_inlined(value))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (is_negative(value))
		return false;

	const u64 u64_value = value.rep >> 1;

	if (bits != 64 && u64_value >= (static_cast<u64>(1) << bits))
		return false;

	*out = u64_value;

	return true;
}

bool s64_from_comp_integer(CompIntegerValue value, u8 bits, s64* out) noexcept
{
	ASSERT_OR_IGNORE(bits <= 64);

	if (!is_inlined(value))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	const s64 s64_value = static_cast<s64>(value.rep) >> 1;

	if (bits != 64 && (s64_value < -(static_cast<s64>(1) << (bits - 1)) || s64_value >= (static_cast<s64>(1) << (bits - 1))))
		return false;

	*out = s64_value;
	
	return true;
}

CompIntegerValue comp_integer_add(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	s64 result;

	if (!add_checked_s64(static_cast<s64>(lhs.rep), static_cast<s64>(rhs.rep), &result))
		panic("Value of subtraction of `CompIntegerValue`s exceeds currently supported maximum value.\n");

	return { static_cast<u64>(result) };
}

CompIntegerValue comp_integer_sub(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	s64 result;

	if (!sub_checked_s64(static_cast<s64>(lhs.rep), static_cast<s64>(rhs.rep), &result))
		panic("Value of subtraction of `CompIntegerValue`s exceeds currently supported maximum value.\n");

	return { static_cast<u64>(result) };
}

CompIntegerValue comp_integer_mul(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	s64 result;

	if (!mul_checked_s64(static_cast<s64>(lhs.rep), static_cast<s64>(rhs.rep) >> 1, &result))
		panic("Value of multiplication of `CompIntegerValue`s exceeds currently supported maximum value.\n");

	return { static_cast<u64>(result) };
}

bool comp_integer_div(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	const s64 lhs_value = static_cast<s64>(lhs.rep) >> 1;

	const s64 rhs_value = static_cast<s64>(rhs.rep) >> 1;

	if (rhs_value == 0)
		return false;

	*out = { static_cast<u64>((lhs_value / rhs_value) << 1) };

	return true;
}

bool comp_integer_mod(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	const s64 lhs_value = static_cast<s64>(lhs.rep) >> 1;

	const s64 rhs_value = static_cast<s64>(rhs.rep) >> 1;

	if (rhs_value == 0)
		return false;

	*out = { static_cast<u64>((lhs_value % rhs_value) << 1) };

	return true;
}

CompIntegerValue comp_integer_neg(CompIntegerValue value) noexcept
{
	if (!is_inlined(value))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (value.rep == static_cast<u64>(COMP_INTEGER_MIN) << 1)
		panic("Negation of most negative inlined `CompIntegerValue` not yet supported.\n");

	return { static_cast<u64>(-static_cast<s64>(value.rep >> 1)) << 1 };
}

bool comp_integer_shift_left(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (is_negative(rhs))
		return false;

	const u64 shift = rhs.rep >> 1;

	const u64 sign_mask = static_cast<u64>(static_cast<s64>(-1)) << (shift - 1);

	const u64 sign_bits = lhs.rep & sign_mask;

	if (sign_bits != 0 && sign_bits != sign_mask)
		panic("Value of left-shift of `CompIntegerValue` exceeds currently supported maximum value.\n");

	*out = { lhs.rep << shift };

	return true;
}

bool comp_integer_shift_right(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (is_negative(rhs))
		return false;

	const u64 shift = rhs.rep >> 1;

	*out = { (lhs.rep >> shift) & ~1 };

	return true;
}

bool comp_integer_bit_and(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (is_negative(lhs) || is_negative(rhs))
		return false;

	*out = { lhs.rep & rhs.rep };

	return true;
}

bool comp_integer_bit_or(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (is_negative(lhs) || is_negative(rhs))
		return false;

	*out = { lhs.rep | rhs.rep };

	return true;
}

bool comp_integer_bit_xor(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (is_negative(lhs) || is_negative(rhs))
		return false;

	*out = { lhs.rep ^ rhs.rep };

	return true;
}

StrongCompareOrdering comp_integer_compare(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	if (lhs.rep == rhs.rep)
		return StrongCompareOrdering::Equal;
	else if (lhs.rep < rhs.rep)
		return StrongCompareOrdering::LessThan;
	else
		return StrongCompareOrdering::GreaterThan;
}



CompFloatValue comp_float_from_f64(f64 value) noexcept
{
	return { value };
}

CompFloatValue comp_float_from_f32(f32 value) noexcept
{
	return { static_cast<f64>(value) };
}

bool comp_float_from_u64(u64 value, CompFloatValue* out) noexcept
{
	if (static_cast<u64>(static_cast<f64>(value)) != value)
		return false;

	*out = { static_cast<f64>(value) };

	return true;
}

bool comp_float_from_s64(s64 value, CompFloatValue* out) noexcept
{
	if (static_cast<s64>(static_cast<f64>(value)) != value)
		return false;

	*out = { static_cast<f64>(value) };

	return true;
}

bool comp_float_from_comp_integer(CompIntegerValue value, CompFloatValue* out) noexcept
{
	(void) value;

	(void) out;

	TODO("");
}

f64 f64_from_comp_float(CompFloatValue value) noexcept
{
	(void) value;

	TODO("");
}

f32 f32_from_comp_float(CompFloatValue value) noexcept
{
	(void) value;

	TODO("");
}

CompFloatValue comp_float_add(CompFloatValue lhs, CompFloatValue rhs) noexcept
{
	return CompFloatValue{ lhs.rep + rhs.rep };
}

CompFloatValue comp_float_sub(CompFloatValue lhs, CompFloatValue rhs) noexcept
{
	(void) lhs;

	(void) rhs;

	TODO("");
}

CompFloatValue comp_float_mul(CompFloatValue lhs, CompFloatValue rhs) noexcept
{
	(void) lhs;

	(void) rhs;

	TODO("");
}

CompFloatValue comp_float_div(CompFloatValue lhs, CompFloatValue rhs) noexcept
{
	(void) lhs;

	(void) rhs;

	TODO("");
}

CompFloatValue comp_float_neg(CompFloatValue value) noexcept
{
	(void) value;

	TODO("");
}

WeakCompareOrdering comp_float_compare(CompFloatValue lhs, CompFloatValue rhs) noexcept
{
	if (std::isnan(lhs.rep) || std::isnan(rhs.rep))
		return WeakCompareOrdering::Unordered;
	else if (lhs.rep == rhs.rep)
		return WeakCompareOrdering::Equal;
	else if (lhs.rep < rhs.rep)
		return WeakCompareOrdering::LessThan;
	else
		return WeakCompareOrdering::GreaterThan;
}