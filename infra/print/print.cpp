#include "print.hpp"

#include "../types.hpp"
#include "../assert.hpp"
#include "../panic.hpp"
#include "../math.hpp"
#include "../opt.hpp"
#include "../range.hpp"


struct PrintState
{
	PrintSink sink;

	u64 written;

	u64 buffer_used;

	char8 buffer[4096];
};

struct IntPrintAttach
{
	u64 value;

	u8 bits;

	bool is_signed;
};

struct F32PrintAttach
{
	f32 value;
};

struct F64PrintAttach
{
	f64 value;
};

struct BoolPrintAttach
{
	bool value;
};

struct CharRangePrintAttach
{
	Range<char8> value;
};



static Maybe<char8*> state_reserve_buffer(PrintState* state, u64 n) noexcept
{
	ASSERT_OR_IGNORE(array_count(state->buffer) >= n);

	if (array_count(state->buffer) - state->buffer_used < n)
	{
		const u64 written = state->sink.write_func(state->sink.attach, Range{ state->buffer, state->buffer_used });

		if (written != state->buffer_used)
			return none<char8*>();

		state->written += written;

		state->buffer_used = 0;
	}

	char8* const result = state->buffer + state->buffer_used;

	state->buffer_used += n;

	return some(result);
}

static bool state_fill(PrintState* state, u64 count, char8 fill_char) noexcept
{
	const u64 available = array_count(state->buffer) - state->buffer_used;

	const u64 initial_fill_size = count < available ? count : available;

	memset(state->buffer + state->buffer_used, fill_char, initial_fill_size);

	state->buffer_used += initial_fill_size;

	count -= initial_fill_size;

	while (count != 0)
	{
		const u64 written = state->sink.write_func(state->sink.attach, Range{ state->buffer, state->buffer_used });

		if (written != state->buffer_used)
			return false;

		state->written += written;

		const u64 fill_size = count < array_count(state->buffer) ? count : array_count(state->buffer);

		memset(state->buffer, fill_char, fill_size);

		state->buffer_used = fill_size;

		count -= fill_size;
	}

	return true;
}

static bool state_prepad(PrintState* state, PrintAlignment alignment, u64 content_size, u64 frame_size, char8 padding) noexcept
{
	if (alignment == PrintAlignment::Right)
	{
		if (content_size >= frame_size)
			return true;

		return state_fill(state, frame_size - content_size, padding);
	}
	else if (alignment == PrintAlignment::Center)
	{
		if (content_size >= frame_size)
			return true;

		const u64 padding_size = (1 + frame_size - content_size) / 2;

		return state_fill(state, padding_size, padding);
	}
	else
	{
		return true;
	}
}

static bool state_postpad(PrintState* state, PrintAlignment alignment, u64 content_size, u64 frame_size, char8 padding) noexcept
{
	if (alignment == PrintAlignment::Left)
	{
		if (content_size >= frame_size)
			return true;

		return state_fill(state, frame_size - content_size, padding);
	}
	else if (alignment == PrintAlignment::Center)
	{
		if (content_size >= frame_size)
			return true;

		const u64 padding_size = (frame_size - content_size) / 2;

		if (padding_size == 0)
			return true;

		return state_fill(state, padding_size, padding);
	}
	else
	{
		return true;
	}
}

static bool print_chars(PrintState* state, Range<char8> value) noexcept
{
	const u64 value_count = value.count();

	if (state->buffer_used + value_count <= array_count(state->buffer))
	{
		if (value_count != 0)
			memcpy(state->buffer + state->buffer_used, value.begin(), value_count);

		state->buffer_used += value_count;
	}
	else
	{
		const u64 written = state->sink.write_func(state->sink.attach, Range{ state->buffer, state->buffer_used });

		if (written != state->buffer_used)
			return false;

		state->written += written;

		if (value_count <= array_count(state->buffer))
		{
			memcpy(state->buffer, value.begin(), value_count);

			state->buffer_used = value_count;
		}
		else
		{
			const u64 value_written = state->sink.write_func(state->sink.attach, value);

			if (value_written != value_count)
				return false;

			state->written += value_written;

			state->buffer_used = 0;
		}
	}

	return true;
}



static bool print_format_int(PrintState* state, const void* insert_attach, PrintSpec spec) noexcept
{
	ASSERT_OR_IGNORE(is_none(spec.extended_spec));

	const IntPrintAttach* const attach = static_cast<const IntPrintAttach*>(insert_attach);

	u64 value = attach->value;

	bool is_negative = false;

	if (attach->is_signed)
	{
		const u64 sign_bit = static_cast<u64>(1) << (attach->bits - 1);

		if ((value & sign_bit) != 0)
		{
			is_negative = true;

			// Sign extend value. If it is 64 bits, we need to skip this, since
			// we would be shifting by 0.
			if (attach->bits != 64)
			{
				const u8 leading_bits = 64 - attach->bits;

				value = static_cast<u64>(static_cast<s64>(value << leading_bits) >> leading_bits);
			}

			// Perform explicit two's complement negation to avoid compiler
			// warnings about unary `-` applied to an unsigned type.
			value = 1 + ~value;
		}
	}

	const u8 required_chars = log10_ceil(value) + (is_negative ? 1 : 0);

	if (!state_prepad(state, spec.alignment, required_chars, spec.align_frame_size, spec.align_padding_char))
		return false;

	const Maybe<char8*> maybe_buffer = state_reserve_buffer(state, required_chars);

	if (is_none(maybe_buffer))
		return false;

	char8* const buffer = get(maybe_buffer);

	char8* curr = buffer + required_chars - 1;

	while (value >= 10)
	{
		*curr = '0' + value % 10;

		value /= 10;

		curr -= 1;
	}

	*curr = '0' + static_cast<char8>(value);

	curr -= 1;

	if (is_negative)
		*curr = '-';

	if (!state_postpad(state, spec.alignment, required_chars, spec.align_frame_size, spec.align_padding_char))
		return false;

	return true;
}

static bool print_format_f32(PrintState* state, const void* raw_attach, PrintSpec spec) noexcept
{
	ASSERT_OR_IGNORE(is_none(spec.extended_spec));

	(void) state;

	(void) raw_attach;

	TODO("Implement");
}

static bool print_format_f64(PrintState* state, const void* raw_attach, PrintSpec spec) noexcept
{
	ASSERT_OR_IGNORE(is_none(spec.extended_spec));

	(void) state;

	(void) raw_attach;

	TODO("Implement");
}

static bool print_format_bool(PrintState* state, const void* raw_attach, PrintSpec spec) noexcept
{
	ASSERT_OR_IGNORE(is_none(spec.extended_spec));

	const BoolPrintAttach* const attach = static_cast<const BoolPrintAttach*>(raw_attach);

	const char8* stringified_bool;

	u8 stringified_bool_chars;

	if (attach->value)
	{
		stringified_bool = "true";

		stringified_bool_chars = 4;
	}
	else
	{
		stringified_bool = "false";

		stringified_bool_chars = 5;
	}

	if (!state_prepad(state, spec.alignment, stringified_bool_chars, spec.align_frame_size, spec.align_padding_char))
		return false;

	const Maybe<char8*> maybe_buffer = state_reserve_buffer(state, stringified_bool_chars);

	if (is_none(maybe_buffer))
		return false;

	char8* const buffer = get(maybe_buffer);

	memcpy(buffer, stringified_bool, stringified_bool_chars);

	if (!state_postpad(state, spec.alignment, stringified_bool_chars, spec.align_frame_size, spec.align_padding_char))
		return false;

	return true;
}

static bool print_format_char_range(PrintState* state, const void* raw_attach, PrintSpec spec) noexcept
{
	ASSERT_OR_IGNORE(is_none(spec.extended_spec));

	const CharRangePrintAttach* const attach = static_cast<const CharRangePrintAttach*>(raw_attach);

	const Range<char8> value = attach->value;

	if (!state_prepad(state, spec.alignment, value.count(), spec.align_frame_size, spec.align_padding_char))
		return false;

	if (!print_chars(state, value))
		return false;

	if (!state_postpad(state, spec.alignment, value.count(), spec.align_frame_size, spec.align_padding_char))
		return false;

	return true;
}



// Builtin FormatInsert Mappings.

PrintInsert print_make_insert(u8 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { value, 8, false };

	return insert;
}

PrintInsert print_make_insert(u16 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { value, 16, false };

	return insert;
}

PrintInsert print_make_insert(u32 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { value, 32, false };

	return insert;
}

PrintInsert print_make_insert(u64 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { value, 64, false };

	return insert;
}

PrintInsert print_make_insert(s8 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { static_cast<u64>(static_cast<s64>(value)), 8, true };

	return insert;
}

PrintInsert print_make_insert(s16 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { static_cast<u64>(static_cast<s64>(value)), 16, true };

	return insert;
}

PrintInsert print_make_insert(s32 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { static_cast<u64>(static_cast<s64>(value)), 32, true };

	return insert;
}

PrintInsert print_make_insert(s64 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_int;
	*reinterpret_cast<IntPrintAttach*>(insert.attach) = { static_cast<u64>(value), 64, true };

	return insert;
}

PrintInsert print_make_insert(f32 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_f32;
	*reinterpret_cast<F32PrintAttach*>(insert.attach) = { value };

	return insert;
}

PrintInsert print_make_insert(f64 value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_f64;
	*reinterpret_cast<F64PrintAttach*>(insert.attach) = { value };

	return insert;
}

PrintInsert print_make_insert(bool value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_bool;
	*reinterpret_cast<BoolPrintAttach*>(insert.attach) = { value };

	return insert;
}

PrintInsert print_make_insert(const char8* value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_char_range;
	*reinterpret_cast<CharRangePrintAttach*>(insert.attach) = { range::from_cstring(value) };

	return insert;
}

PrintInsert print_make_insert(Range<char8> value) noexcept
{
	PrintInsert insert;
	insert.format_func = print_format_char_range;
	*reinterpret_cast<CharRangePrintAttach*>(insert.attach) = { value };

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

PrintSink print_make_sink(minos::FileHandle filehandle) noexcept
{
	PrintSink sink;
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

PrintSink print_make_sink(MutRange<char8> buffer) noexcept
{
	PrintSink sink;
	sink.write_func = print_sink_write_mut_range;
	*reinterpret_cast<MutRangeFormatSinkAttach*>(sink.attach) = { buffer };

	return sink;
}



struct PrintSpecInfo
{
	u64 i;

	u64 insert_index;

	u64 next_insert_index;

	PrintAlignment alignment;

	char8 align_padding_char;

	bool align_padding_is_indirect;

	u64 align_padding_indirection_or_amount;

	Maybe<const char8*> extended_spec;
};

static PrintSpecInfo parse_print_spec(const char8* chars, u64 count, u64 default_insert_index, u64 i) noexcept
{
	if (i == count)
		panic("vprint: Incomplete format specifier.\n");

	u64 insert_index;

	// Insert index
	if (chars[i] == '$')
	{
		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");

		if (chars[i] < '0' || chars[i] > '9')
			panic("vprint: Expected number after `$` in format specifier.\n");

		insert_index = chars[i] - '0';

		i += 1;

		while (true)
		{
			if (i == count)
				panic("vprint: Incomplete format specifier.\n");

			if (chars[i] == ' ' || chars[i] == ']')
			{
				while (chars[i] == ' ')
				{
					i += 1;

					if (i == count)
						panic("vprint: Incomplete format specifier.\n");
				}

				break;
			}

			if (chars[i] < '0' || chars[i] > '9')
				panic("vprint: Expected ` `, `|` or `]` after insert index (`$n`) in format specifier.\n");

			insert_index = insert_index * 10 + chars[i] - '0';

			i += 1;
		}
	}
	else
	{
		insert_index = default_insert_index;
	}

	u64 next_insert_index = insert_index + 1;

	// Optional whitespace
	while (chars[i] == ' ')
	{
		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");
	}

	PrintAlignment alignment;
	char8 align_padding_char;
	bool align_padding_is_indirect;
	u64 align_padding_indirection_or_amount;

	// Alignment and padding
	if (chars[i] == '<' || chars[i] == '^' || chars[i] == '>')
	{
		if (chars[i] == '<')
			alignment = PrintAlignment::Left;
		else if (chars[i] == '^')
			alignment = PrintAlignment::Center;
		else if(chars[i] == '>')
			alignment = PrintAlignment::Right;
		else
			ASSERT_UNREACHABLE;

		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");

		align_padding_char = chars[i];

		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");

		if (chars[i] == '%')
		{
			align_padding_is_indirect = true;

			i += 1;

			if (i == count)
				panic("vprint: Incomplete format specifier.\n");

			align_padding_indirection_or_amount = next_insert_index;

			next_insert_index += 1;
		}
		else if (chars[i] >= '0' && chars[i] <= '9')
		{
			align_padding_is_indirect = false;

			align_padding_indirection_or_amount = 0;

			while (chars[i] >= '0' && chars[i] <= '9')
			{
				align_padding_indirection_or_amount = align_padding_indirection_or_amount * 10 + chars[i] - '0';

				i += 1;

				if (i == count)
					panic("vprint: Incomplete format specifier.\n");
			}
		}
		else
		{
			panic("vprint: Expected direct or indirect padding amount after alignment specifier and padding character.\n");
		}
	}
	else
	{
		alignment = PrintAlignment::None;
		align_padding_char = '\0';
		align_padding_is_indirect = false;
		align_padding_indirection_or_amount = 0;
	}

	// Optional whitespace
	while (chars[i] == ' ')
	{
		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");
	}

	Maybe<const char8*> extended_spec;

	// Extended type-specific spec
	if (chars[i] == '|')
	{
		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");

		extended_spec = some(chars + i);

		u64 bracket_nesting = 1;

		while (true)
		{
			if (chars[i] == '[')
			{
				bracket_nesting += 1;
			}
			else if (chars[i] == ']')
			{
				if (bracket_nesting == 1)
					break;

				bracket_nesting -= 1;
			}

			i += 1;

			if (i == count)
				panic("vprint: Incomplete format specifier.\n");
		}
	}
	else if (chars[i] == ']')
	{
		extended_spec = none<const char8*>();
	}
	else
	{
		panic("vprint: Incomplete format specifier.\n");
	}

	PrintSpecInfo rst;
	rst.i = i + 1;
	rst.insert_index = insert_index;
	rst.next_insert_index = next_insert_index;
	rst.alignment = alignment;
	rst.align_padding_char = align_padding_char;
	rst.align_padding_is_indirect = align_padding_is_indirect;
	rst.align_padding_indirection_or_amount = align_padding_indirection_or_amount;
	rst.extended_spec = extended_spec;

	return rst;
}



s64 vprint(PrintSink sink, Range<char8> format, Range<PrintInsert> inserts) noexcept
{
	PrintState state;
	state.sink = sink;
	state.buffer_used = 0;
	state.written = 0;

	u64 insert_index = 0;

	u64 section_begin = 0;

	u64 i = 0;

	const u64 count = format.count();

	const char8* const chars = format.begin();

	while (i != count)
	{
		const char8 curr = chars[i];

		if (curr != '%')
		{
			i += 1;

			continue;
		}

		PrintSpec spec;

		u64 next_insert_index;

		if (i + 1 != count && chars[i + 1] == '%')
		{
			if (!print_chars(&state, Range{ chars + section_begin, chars + i + 1 }))
				return -1;

			i += 2;

			section_begin = i;

			continue;
		}
		else if (i + 1 != count && chars[i + 1] == '[')
		{
			if (!print_chars(&state, Range{ chars + section_begin, chars + i }))
				return -1;

			const PrintSpecInfo info = parse_print_spec(chars, count, insert_index, i + 2);

			i = info.i;

			insert_index = info.insert_index;

			next_insert_index = info.next_insert_index;

			spec.alignment = info.alignment;
			spec.align_padding_char = info.align_padding_char;
			spec.extended_spec = info.extended_spec;
			
			if (info.align_padding_is_indirect)
			{
				if (info.align_padding_indirection_or_amount >= inserts.count())
					panic("vprint: Insert index exceeds number of supplied inserts.\n");

				const PrintInsert* const align_insert = &inserts[info.align_padding_indirection_or_amount];

				if (align_insert->format_func != &print_format_int)
					panic("vprint: Indirect padding size value is not an integer.\n");

				const IntPrintAttach* const align_attach = reinterpret_cast<const IntPrintAttach*>(align_insert->attach);

				if (align_attach->is_signed && ((static_cast<u64>(1) << (align_attach->bits - 1)) & align_attach->value) != 0)
					panic("vprint: Indirect padding size value is negative.\n");

				spec.align_frame_size = align_attach->value;
			}
			else
			{
				spec.align_frame_size = info.align_padding_indirection_or_amount;
			}
		}
		else
		{
			if (!print_chars(&state, Range{ chars + section_begin, chars + i }))
				return -1;

			i += 1;

			next_insert_index = insert_index + 1;

			spec.alignment = PrintAlignment::None;
			spec.align_padding_char = '\0';
			spec.align_frame_size = 0;
			spec.extended_spec = none<const char8*>();
		}

		section_begin = i;

		if (insert_index >= inserts.count())
			panic("vprint: Insert index exceeds number of supplied inserts.\n");

		const PrintInsert* const insert = &inserts[insert_index];

		insert_index = next_insert_index;

		if (!insert->format_func(&state, insert->attach, spec))
			return -1;
	}

	if (!print_chars(&state, Range{ chars + section_begin, chars  + i }))
		return -1;

	if (state.buffer_used != 0)
	{
		const u64 written = state.sink.write_func(state.sink.attach, Range{ state.buffer, state.buffer_used });

		if (written != state.buffer_used)
			return -1;

		state.written += state.buffer_used;
	}

	return state.written;
}

s64 print(PrintSink sink, Range<char8> format) noexcept
{
	return vprint(sink, format, {});
}
