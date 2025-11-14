#include "format.hpp"

// Helpers.

static u8 log2_ceil(u64 n) noexcept
{
	return 63 - count_leading_zeros_assume_one(n | 1);
}

static u8 log8_ceil(u64 n) noexcept
{
	return (log2_ceil(n) + 2) / 3;
}

static u8 log10_ceil(u64 n) noexcept
{
	u8 log = 1;

	while (n >= 10'000)
	{
		n /= 10'000;

		log += 4;
	}

	if (n >= 1000)
		return log + 3;
	else if (n >= 100)
		return log + 2;
	else if (n >= 10)
		return log + 1;
	else
		return log;
}

static u8 log16_ceil(u64 n) noexcept
{
	return (log2_ceil(n) + 3) / 4;
}



// Builtin FormatInsert Attachments.

struct IntFormatAttach
{
	u64 value;

	u8 bits;

	bool is_signed;
};

struct F32FormatAttach
{
	f32 value;
};

struct F64FormatAttach
{
	f64 value;
};

struct BoolFormatAttach
{
	bool value;
};

struct RangeFormatAttach
{
	Range<char8> value;
};



// Builtin Insert Formatting Functions.

static u64 print_insert_format_int(PrintState* state, const void* raw_attach, FormatSpec spec) noexcept
{
	const FormatFlag base_flag = spec.flags & (FormatFlag::B | FormatFlag::O | FormatFlag::X_Lo | FormatFlag::X_Hi | FormatFlag::C);

	if (spec.flags != base_flag)
		print_handle_flag_error();

	const IntFormatAttach* const attach = static_cast<const IntFormatAttach*>(raw_attach);

	char8 buf[64];

	u64 buf_used = 0;

	u64 value = attach->value;

	if (base_flag == FormatFlag::EMPTY)
	{
		if (attach->is_signed && static_cast<s64>(value) < 0)
		{
			// Two's complement negate without messing with signed overflow UB
			// around most negative s64 value.
			value = 1 + ~value;

			buf[0] = '-';
			
			buf_used = 1;
		}

		buf_used += log10_ceil(value);

		ASSERT_OR_IGNORE(buf_used >= 1 && buf_used <= sizeof(buf));

		u64 i = buf_used - 1;

		while (true)
		{
			buf[i] = '0' + value % 10;

			value /= 10;

			if (value == 0)
				break;

			i -= 1;
		}

		ASSERT_OR_IGNORE(static_cast<s64>(i) == 0);
	}
	else if (base_flag == FormatFlag::B)
	{
		buf_used = log2_ceil(value);

		ASSERT_OR_IGNORE(buf_used >= 1 && buf_used <= sizeof(buf));

		u64 i = buf_used - 1;

		while (true)
		{
			buf[i] = '0' + (value & 1);

			value >>= 1;

			if (value == 0)
				break;

			i -= 1;
		}

		ASSERT_OR_IGNORE(i == 0);
	}
	else if (base_flag == FormatFlag::O)
	{
		buf_used = log8_ceil(value);

		ASSERT_OR_IGNORE(buf_used >= 1 && buf_used <= sizeof(buf));

		u64 i = buf_used - 1;

		while (true)
		{
			buf[i] = '0' + (value & 7);

			value >>= 3;

			if (value == 0)
				break;

			i -= 1;
		}

		ASSERT_OR_IGNORE(i == 0);
	}
	else if (base_flag == FormatFlag::X_Lo || base_flag == FormatFlag::X_Hi)
	{
		const char8 alpha_base = base_flag == FormatFlag::X_Lo ? 'a' - 10 : 'A' - 10;

		buf_used = log16_ceil(value);

		ASSERT_OR_IGNORE(buf_used >= 1 && buf_used <= sizeof(buf));

		u64 i = buf_used - 1;

		while (true)
		{
			const u8 nybble = static_cast<u8>(value & 0xF);
	
			if (nybble >= 10)
				buf[i] = alpha_base + nybble;
			else
				buf[i] = '0' + nybble;

			value >>= 4;

			if (value == 0)
				break;

			i -= 1;
		}

		ASSERT_OR_IGNORE(i == 0);
	}
	else if (base_flag == FormatFlag::C)
	{
		if (attach->bits != 8)
			print_handle_flag_error();

		buf[0] = static_cast<char8>(attach->value);

		buf_used = 1;
	}
	else
	{
		print_handle_flag_error();
	}

	return print_write_chars(state, Range<char8>{buf, buf_used });
}

static u64 print_insert_format_f32(PrintState* state, const void* raw_attach, FormatSpec spec) noexcept
{
	(void) state;

	(void) raw_attach;

	(void) spec;

	return 0;
}

static u64 print_insert_format_f64(PrintState* state, const void* raw_attach, FormatSpec spec) noexcept
{
	(void) state;

	(void) raw_attach;

	(void) spec;

	return 0;
}

static u64 print_insert_format_bool(PrintState* state, const void* raw_attach, FormatSpec spec) noexcept
{
	if ((spec.flags | FormatFlag::B) != FormatFlag::B)
		print_handle_flag_error();

	const BoolFormatAttach* const attach = static_cast<const BoolFormatAttach*>(raw_attach);

	if (attach->value)
	{
		if ((spec.flags & FormatFlag::B) != FormatFlag::EMPTY)
			return print_write_chars(state, range::from_literal_string("1"));
		else
			return print_write_chars(state, range::from_literal_string("true"));
	}
	else
	{
		if ((spec.flags & FormatFlag::B) != FormatFlag::EMPTY)
			return print_write_chars(state, range::from_literal_string("0"));
		else
			return print_write_chars(state, range::from_literal_string("false"));
	}
}

static u64 print_insert_format_char_range(PrintState* state, const void* raw_attach, FormatSpec spec) noexcept
{
	if (spec.flags != FormatFlag::EMPTY)
		print_handle_flag_error();

	const RangeFormatAttach* const attach = static_cast<const RangeFormatAttach*>(raw_attach);

	return print_write_chars(state, attach->value);
}



// Builtin FormatInsert Mappings.

FormatInsert print_make_insert(u8 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { value, 8, false };

	return insert;
}

FormatInsert print_make_insert(u16 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { value, 16, false };

	return insert;
}

FormatInsert print_make_insert(u32 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { value, 32, false };

	return insert;
}

FormatInsert print_make_insert(u64 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { value, 64, false };

	return insert;
}

FormatInsert print_make_insert(s8 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { static_cast<u64>(static_cast<s64>(value)), 8, true };

	return insert;
}

FormatInsert print_make_insert(s16 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { static_cast<u64>(static_cast<s64>(value)), 16, true };

	return insert;
}

FormatInsert print_make_insert(s32 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { static_cast<u64>(static_cast<s64>(value)), 32, true };

	return insert;
}

FormatInsert print_make_insert(s64 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_int;
	*reinterpret_cast<IntFormatAttach*>(insert.attach) = { static_cast<u64>(value), 64, true };

	return insert;
}

FormatInsert print_make_insert(f32 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_f32;
	*reinterpret_cast<F32FormatAttach*>(insert.attach) = { value };

	return insert;
}

FormatInsert print_make_insert(f64 value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_f64;
	*reinterpret_cast<F64FormatAttach*>(insert.attach) = { value };

	return insert;
}

FormatInsert print_make_insert(bool value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_bool;
	*reinterpret_cast<BoolFormatAttach*>(insert.attach) = { value };

	return insert;
}

FormatInsert print_make_insert(Range<char8> value) noexcept
{
	FormatInsert insert;
	insert.format_func = print_insert_format_char_range;
	*reinterpret_cast<RangeFormatAttach*>(insert.attach) = { value };

	return insert;
}



struct MinosFileHandleSinkAttach
{
	minos::FileHandle filehandle;

	u64 offset;
};

static u64 print_sink_write_minos_filehandle(void* raw_attach, Range<char8> data) noexcept
{
	MinosFileHandleSinkAttach* const attach = static_cast<MinosFileHandleSinkAttach*>(raw_attach);

	if (!minos::file_write(attach->filehandle, data.as_byte_range(), attach->offset))
		return 0;

	attach->offset += data.count();

	return data.count();
}

FormatSink print_make_sink(minos::FileHandle filehandle) noexcept
{
	FormatSink sink;
	sink.write_func = print_sink_write_minos_filehandle;
	*reinterpret_cast<MinosFileHandleSinkAttach*>(sink.attach) = { filehandle, 0 };

	return sink;
}



struct MutRangeFormatSinkAttach
{
	MutRange<char8> buffer;
};

u64 print_sink_write_mut_range(void* raw_attach, Range<char8> data) noexcept
{
	MutRangeFormatSinkAttach* const attach = static_cast<MutRangeFormatSinkAttach*>(raw_attach);

	MutRange<char8> buffer = attach->buffer;

	const u64 written = buffer.count() < data.count() ? buffer.count() : data.count();

	memcpy(buffer.begin(), data.begin(), written);

	attach->buffer = MutRange<char8>{ buffer.begin() + written, buffer.end() };

	return written;
}

FormatSink print_make_sink(MutRange<char8> buffer) noexcept
{
	FormatSink sink;
	sink.write_func = print_sink_write_mut_range;
	*reinterpret_cast<MutRangeFormatSinkAttach*>(sink.attach) = { buffer };

	return sink;
}
