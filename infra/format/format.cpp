#include "format.hpp"

struct PrintState
{
	static constexpr u64 CAPACITY = 4096;

	FormatSink sink;

	u64 used_chars;

	char8 chars[CAPACITY];
};

struct FormatInfo
{
	FormatSpec spec;

	u64 i;

	u64 insert_index;
};



static FormatInfo parse_format_info(const char8* chars, u64 count, u64 default_insert_index, u64 i) noexcept
{
	FormatSpec spec;
	spec.flags = FormatFlag::EMPTY;
	spec.alignment = FormatAlignment::Default;
	spec.min_width = 0;
	spec.max_width = ~0u;

	u64 insert_index = default_insert_index;

	if (chars[i] >= '0' && chars[i] <= '9')
	{
		insert_index = chars[i] - '0';

		i += 1;

		while (true)
		{
			if (i == count)
				panic("vprint: Incomplete format specifier.\n");

			if (chars[i] < '0' || chars[i] > '9')
				break;

			insert_index = insert_index * 10 + chars[i] - '0';

			i += 1;
		}
	}

	if (chars[i] == ']')
		return FormatInfo{ spec, i + 1, insert_index };
	else if (chars[i] != ':')
		panic("vprint: Expected insert index, `]` or `:`.\n");

	while (true)
	{
		char8 flag = chars[i];

		switch (flag)
		{
		case ']':
		{
			return FormatInfo{ spec, i + 1, insert_index };
		}

		case 'b':
		{
			if ((spec.flags & FormatFlag::B) != FormatFlag::EMPTY)
				panic("vprint: Duplicate format flag `b`.\n");

			spec.flags |= FormatFlag::B;

			break;
		}

		case 'o':
		{
			if ((spec.flags & FormatFlag::O) != FormatFlag::EMPTY)
				panic("vprint: Duplicate format flag `o`.\n");

			spec.flags |= FormatFlag::O;

			break;
		}

		case 'x':
		{
			if ((spec.flags & (FormatFlag::X_Lo | FormatFlag::X_Hi)) != FormatFlag::EMPTY)
				panic("vprint: Duplicate format flag `x` or `X`.\n");

			spec.flags |= FormatFlag::X_Lo;

			break;
		}

		case 'X':
		{
			if ((spec.flags & (FormatFlag::X_Lo | FormatFlag::X_Hi)) != FormatFlag::EMPTY)
				panic("vprint: Duplicate format flag `x` or `X`.\n");

			spec.flags |= FormatFlag::X_Hi;

			break;
		}

		case 'c':
		{
			if ((spec.flags & FormatFlag::C) != FormatFlag::EMPTY)
				panic("vprint: Duplicate format flag `c`.\n");

			spec.flags |= FormatFlag::C;

			break;
		}

		case '<':
		{
			if (spec.alignment != FormatAlignment::Default)
				panic("vprint: Multiple format alignments.\n");

			spec.alignment = FormatAlignment::Left;

			break;
		}

		case '>':
		{
			if (spec.alignment != FormatAlignment::Default)
				panic("vprint: Multiple format alignments.\n");

			spec.alignment = FormatAlignment::Right;

			break;
		}

		case '^':
		{
			if (spec.alignment != FormatAlignment::Default)
				panic("vprint: Multiple format alignments.\n");

			spec.alignment = FormatAlignment::Center;

			break;
		}

		default:
			break;
		}

		i += 1;

		if (i == count)
			panic("vprint: Incomplete format specifier.\n");
	}

	if (chars[i] >= '0' && chars[i] <= '9')
	{
		u64 n = chars[i] - '0';

		while (true)
		{
			i += 1;

			if (i == count)
				panic("vprint: Incomplete format specifier.\n");

			if (chars[i] < '0' || chars[i] > '9')
				break;

			n = n * 10 + chars[i] - '0';
		}

		spec.min_width = static_cast<u32>(n);
	}

	if (chars[i] == '.')
	{
		i += 1;

		if (i == count || chars[i] < '0' || chars[i] > '9')
			panic("vprint: Incomplete format specifier.\n");

		u64 n = chars[i] - '0';

		while (true)
		{
			i += 1;

			if (i == count)
				panic("vprint: Incomplete format specifier.\n");

			if (chars[i] < '0' || chars[i] > '9')
				break;

			n = n * 10 + chars[i] - '0';
		}

		spec.max_width = static_cast<u32>(n);
	}

	if (chars[i] != ']')
		panic("vprint: Expected `]`.\n");

	return FormatInfo{ spec, i, insert_index };
}



NORETURN void print_handle_flag_error() noexcept
{
	panic("vprint: Unsupported format flags.\n");
}

u64 print_pad(PrintState* state, char8 padding_char, u64 padding_count) noexcept
{
	const u64 used_chars = state->used_chars;

	if (used_chars + padding_count < PrintState::CAPACITY)
	{
		memset(state->chars + used_chars, static_cast<u8>(padding_char), padding_count);

		state->used_chars = used_chars + padding_count;

		return 0;
	}
	else
	{
		const u64 available = PrintState::CAPACITY - used_chars;

		memset(state->chars + used_chars, static_cast<u8>(padding_char), available);

		u64 written = state->sink.write_func(state->sink.attach, Range<char8>{ state->chars, PrintState::CAPACITY });

		padding_count -= available;

		const u64 padding_per_iteration = padding_count < PrintState::CAPACITY ? padding_count : PrintState::CAPACITY;

		memset(state->chars, static_cast<u8>(padding_char), padding_per_iteration);

		while (padding_count > PrintState::CAPACITY)
		{
			written += state->sink.write_func(state->sink.attach, Range<char8>{ state->chars, padding_per_iteration });

			padding_count -= padding_per_iteration;
		}

		state->used_chars = padding_count;

		return written;
	}
}

u64 print_write_chars(PrintState* state, Range<char8> data) noexcept
{
	const u64 used_chars = state->used_chars;

	if (data.count() + used_chars <= PrintState::CAPACITY)
	{
		memcpy(state->chars + used_chars, data.begin(), data.count());

		state->used_chars = used_chars + data.count();

		return 0;
	}
	else
	{
		u64 written = 0;

		if (used_chars != 0)
		{
			written += state->sink.write_func(state->sink.attach, Range<char8>{ state->chars, used_chars });

			state->used_chars = 0;
		}

		return written + state->sink.write_func(state->sink.attach, data);
	}
}

u64 print_write_char(PrintState* state, char8 data) noexcept
{
	const u64 used_chars = state->used_chars;

	if (used_chars < PrintState::CAPACITY)
	{
		state->sink.write_func(state->sink.attach, Range<char8>{ state->chars, used_chars });

		state->chars[0] = data;

		state->used_chars = 1;

		return 0;
	}
	else
	{
		state->chars[used_chars] = data;

		state->used_chars = used_chars + 1;

		return PrintState::CAPACITY;
	}
}



u64 vprint(FormatSink sink, Range<char8> format, Range<FormatInsert> inserts) noexcept
{
	PrintState buf;
	buf.sink = sink;
	buf.used_chars = 0;

	u64 insert_index = 0;

	u64 section_begin = 0;

	u64 i = 0;

	u64 written = 0;

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

		FormatSpec spec;

		if (i + 1 != count)
		{
			const char8 next = chars[i + 1];

			if (next == '%')
			{
				written += print_write_chars(&buf, Range<char8>{ chars + section_begin, chars + i + 1 });

				i += 2;

				section_begin = i;

				continue;
			}
			else if (next == '[')
			{
				written += print_write_chars(&buf, Range<char8>{ chars + section_begin, chars + i });

				const FormatInfo info = parse_format_info(chars, count, insert_index, i + 2);

				i = info.i;

				insert_index = info.insert_index;

				spec = info.spec;
			}
			else
			{
				written += print_write_chars(&buf, Range<char8>{ chars + section_begin, chars + i });

				i += 1;

				insert_index += 1;

				spec.flags = FormatFlag::EMPTY;
				spec.alignment = FormatAlignment::Default;
				spec.min_width = 0;
				spec.max_width = ~0u;
			}

			section_begin = i;

			if (insert_index >= inserts.count())
				panic("vprint: Insert index exceeds number of supplied inserts.\n");

			const FormatInsert* const insert = &inserts[insert_index];

			written += insert->format_func(&buf, insert->attach, spec);
		}
	}

	written += print_write_chars(&buf, Range<char8>{ chars + section_begin, chars + i });

	if (buf.used_chars != 0)
		buf.sink.write_func(buf.sink.attach, Range<char8>{ buf.chars, buf.used_chars });

	return written;
}
