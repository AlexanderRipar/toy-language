#include "pass_data.hpp"

#include <cmath>

#if defined(COMPILER_MSVC)
	#include <intrin.h>

	bool add_overflow(s64 a, s64 b, s64* out) noexcept
	{
		return _addcarry_u64(0, static_cast<u64>(a), static_cast<u64>(b), reinterpret_cast<u64*>(out)) != 0;
	}

	bool sub_overflow(s64 a, s64 b, s64* out) noexcept
	{
		return _subborrow_u64(0, static_cast<u64>(a), static_cast<u64>(b), reinterpret_cast<u64*>(out)) != 0;
	}

	bool mul_overflow(s64 a, s64 b, s64* out) noexcept
	{
		s64 overflow;

		*out = _mul128(a, b, &overflow);

		return overflow != 0;
	}

	bool add_overflow(u64 a, u64 b, u64* out) noexcept
	{
		return _addcarry_u64(0, a, b, out) != 0;
	}

	bool sub_overflow(u64 a, u64 b, u64* out) noexcept
	{
		return _subborrow_u64(0, a, b, out) != 0;
	}

	bool mul_overflow(u64 a, u64 b, u64* out) noexcept
	{
		u64 overflow;

		*out = _umul128(a, b, &overflow);

		return overflow != 0;
	}
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	bool add_overflow(s64 a, s64 b, s64* out) noexcept
	{
		return __builtin_add_overflow(a, b, out);
	}

	bool sub_overflow(s64 a, s64 b, s64* out) noexcept
	{
		return __builtin_sub_overflow(a, b, out);
	}

	bool mul_overflow(s64 a, s64 b, s64* out) noexcept
	{
		return __builtin_mul_overflow(a, b, out);
	}

	bool add_overflow(u64 a, u64 b, u64* out) noexcept
	{
		return __builtin_add_overflow(a, b, out);
	}

	bool sub_overflow(u64 a, u64 b, u64* out) noexcept
	{
		return __builtin_sub_overflow(a, b, out);
	}

	bool mul_overflow(u64 a, u64 b, u64* out) noexcept
	{
		return __builtin_mul_overflow(a, b, out);
	}
#else
	#error("Unsupported compiler")
#endif

static constexpr u64 COMP_INTEGER_MAX = (static_cast<u64>(1) << 62) - 1;

static constexpr s64 COMP_INTEGER_MIN = static_cast<s64>((static_cast<u64>(static_cast<s64>(-1)) << 62));

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

	if (s64_value < -(static_cast<s64>(1) << (bits - 1)) || s64_value >= (static_cast<s64>(1) << (bits - 1)))
		return false;

	*out = s64_value;
	
	return true;
}

CompIntegerValue comp_integer_add(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	s64 result;

	if (add_overflow(static_cast<s64>(lhs.rep), static_cast<s64>(rhs.rep), &result))
		panic("Value of subtraction of `CompIntegerValue`s exceeds currently supported maximum value.\n");

	return { static_cast<u64>(result) };
}

CompIntegerValue comp_integer_sub(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	s64 result;

	if (sub_overflow(static_cast<s64>(lhs.rep), static_cast<s64>(rhs.rep), &result))
		panic("Value of subtraction of `CompIntegerValue`s exceeds currently supported maximum value.\n");

	return { static_cast<u64>(result) };
}

CompIntegerValue comp_integer_mul(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	s64 result;

	if (mul_overflow(static_cast<s64>(lhs.rep), static_cast<s64>(rhs.rep) >> 1, &result))
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

bool comp_integer_equal(CompIntegerValue lhs, CompIntegerValue rhs) noexcept
{
	if (!is_inlined(lhs) || !is_inlined(rhs))
		panic("Unexpected non-inlined `CompIntegerValue`.\n");

	return lhs.rep == rhs.rep;
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
	(void) lhs;

	(void) rhs;

	TODO("");
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
