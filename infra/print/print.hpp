#ifndef PRINT_INCLUDE_GUARD
#define PRINT_INCLUDE_GUARD

#include "../types.hpp"
#include "../opt.hpp"
#include "../range.hpp"
#include "../minos/minos.hpp"

struct PrintState;

using print_sink_write_func = u64 (*) (void* sink_attach, Range<char8> buffer) noexcept;

struct PrintSink
{
	print_sink_write_func write_func;

	byte alignas(8) attach[24];
};

struct PrintInsert;

using print_format_func = bool (*) (PrintState* state, const void* insert_attach, Maybe<const char8*> spec) noexcept;

struct PrintInsert
{
	print_format_func format_func;

	byte alignas(8) attach[24];
};

PrintInsert print_make_insert(u8) noexcept;
PrintInsert print_make_insert(u16) noexcept;
PrintInsert print_make_insert(u32) noexcept;
PrintInsert print_make_insert(u64) noexcept;
PrintInsert print_make_insert(s8) noexcept;
PrintInsert print_make_insert(s16) noexcept;
PrintInsert print_make_insert(s32) noexcept;
PrintInsert print_make_insert(s64) noexcept;
PrintInsert print_make_insert(f32) noexcept;
PrintInsert print_make_insert(f64) noexcept;
PrintInsert print_make_insert(bool) noexcept;
PrintInsert print_make_insert(const char8*) noexcept;
PrintInsert print_make_insert(Range<char8>) noexcept;

PrintSink print_make_sink(minos::FileHandle file) noexcept;
PrintSink print_make_sink(MutRange<char8> buffer) noexcept;

s64 vprint(PrintSink sink, Range<char8> format, Range<PrintInsert> inserts) noexcept;

template<typename... Inserts>
s64 print(PrintSink sink, Range<char8> format, Inserts... inserts) noexcept
{
	PrintInsert erased_inserts[] = { print_make_insert(inserts)... };

	return vprint(sink, format, Range{ erased_inserts });
}

template<typename... Inserts>
s64 print(PrintSink sink, const char8* format, Inserts... inserts) noexcept
{
	return print(sink, range::from_cstring(format), inserts...);
}

template<typename Sink, typename... Inserts>
s64 print(Sink sink, Range<char8> format, Inserts... inserts) noexcept
{
	return print(print_make_sink(sink), format, inserts...)
}

template<typename Sink, typename... Inserts>
s64 print(Sink sink, const char8* format, Inserts... inserts) noexcept
{
	return print(print_make_sink(sink), range::from_cstring(format), inserts...);
}

#endif // PRINT_INCLUDE_GUARD
