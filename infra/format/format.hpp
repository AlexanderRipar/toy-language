#ifndef PRINT_INCLUDE_GUARD
#define PRINT_INCLUDE_GUARD

#include "../common.hpp"
#include "../minos/minos.hpp"

enum class FormatFlag : u16
{
	EMPTY = 0,
	B = 0x01,
	O = 0x02,
	X_Lo = 0x04,
	X_Hi = 0x08,
	C = 0x10,
};

inline FormatFlag operator&(FormatFlag lhs, FormatFlag rhs) noexcept
{
	return static_cast<FormatFlag>(static_cast<u16>(lhs) & static_cast<u16>(rhs));
}

inline FormatFlag operator|(FormatFlag lhs, FormatFlag rhs) noexcept
{
	return static_cast<FormatFlag>(static_cast<u16>(lhs) | static_cast<u16>(rhs));
}

inline FormatFlag& operator&=(FormatFlag& lhs, FormatFlag rhs) noexcept
{
	lhs = lhs & rhs;

	return lhs;
}

inline FormatFlag& operator|=(FormatFlag& lhs, FormatFlag rhs) noexcept
{
	lhs = lhs | rhs;

	return lhs;
}

enum class FormatAlignment : u8
{
	Default,
	Left,
	Right,
	Center,
};

struct FormatSpec
{
	FormatFlag flags;

	FormatAlignment alignment;

	u32 min_width;

	u32 max_width;
};

struct PrintState;

using print_insert_format_func = u64 (*) (PrintState* state, const void* attach, FormatSpec spec) noexcept;

using print_sink_write_func = u64 (*) (void* attach, Range<char8> data) noexcept;

struct FormatInsert
{
	print_insert_format_func format_func;

	byte attach[16];
};

struct FormatSink
{
	print_sink_write_func write_func;

	byte attach[16];
};



// Helpers for Insert Formatting Functions.

NORETURN void print_handle_flag_error() noexcept;

u64 print_pad(PrintState* state, char8 padding_char, u64 padding_count) noexcept;

u64 print_write_chars(PrintState* state, Range<char8> data) noexcept;

u64 print_write_char(PrintState* state, char8 data) noexcept;



// Builtin FormatInsert Mappings.

FormatInsert print_make_insert(u8) noexcept;

FormatInsert print_make_insert(u16) noexcept;

FormatInsert print_make_insert(u32) noexcept;

FormatInsert print_make_insert(u64) noexcept;

FormatInsert print_make_insert(s8) noexcept;

FormatInsert print_make_insert(s16) noexcept;

FormatInsert print_make_insert(s32) noexcept;

FormatInsert print_make_insert(s64) noexcept;

FormatInsert print_make_insert(f32) noexcept;

FormatInsert print_make_insert(f64) noexcept;

FormatInsert print_make_insert(bool) noexcept;

FormatInsert print_make_insert(Range<char8>) noexcept;



// Builtin Sink Adapters.

FormatSink print_make_sink(minos::FileHandle filehandle) noexcept;

FormatSink print_make_sink(MutRange<char8> buffer) noexcept;



// Print Functions.

u64 vprint(FormatSink sink, Range<char8> format, Range<FormatInsert> inserts) noexcept;

template<typename... Inserts>
u64 print(FormatSink sink, Range<char8> format, Inserts... inserts) noexcept
{
	const FormatInsert erased_inserts[]{ print_make_insert(inserts)... };

	return vprint(sink, format, Range<FormatInsert>{ erased_inserts });
}

template<typename Sink, typename... Inserts>
u64 print(Sink sink, Range<char8> format, Inserts... inserts) noexcept
{
	return print(make_sink(sink), format, inserts...);
}

template<typename Sink, typename... Inserts>
u64 print(Sink sink, const char8* format, Inserts... inserts) noexcept
{
	return print(make_sink(sink), range::from_cstring(format), inserts...);
}

#endif // PRINT_INCLUDE_GUARD
