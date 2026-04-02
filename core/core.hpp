#ifndef CORE_INCLUDE_GUARD
#define CORE_INCLUDE_GUARD

#include "../infra/types.hpp"
#include "../infra/range.hpp"
#include "../infra/opt.hpp"
#include "../infra/minos/minos.hpp"
#include "../infra/print/print.hpp"
#include "../infra/tree_schema.hpp"



// Forward declarations.
// These are necessary because the modules defining them would otherwise appear
// after those using them.

// Id used to refer to a type in the `TypePool`. See `TypePool` for further
// information.
enum class TypeId : u32;

// Id used to identify a particular source code location.
// This encodes the location's file, line and column. See `SourceReader` for
// further information.
enum class SourceId : u32;

enum class Builtin : u8;

enum class ForeverValueId : u32;

enum class ClosureId : u32;

enum class OpcodeId : u32;

enum class GlobalCompositeIndex : u16;

union alignas(4) NameBinding
{
	u32 unused_ = 0;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids anonymous strcuts
	#endif
	struct
	{
		u16 is_global : 1;

		u16 is_scoped : 1;
	};
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif

	struct
	{
		u16 is_global_ : 1;

		u16 is_scoped_ : 1;

		u16 unused_ : 6;

		u16 out : 8;

		u16 rank;
	} scoped;

	struct
	{
		u16 is_global_ : 1;

		u16 file_index_bits : 15;

		u16 rank;
	} global;

	struct
	{
		u16 is_global_ : 1;

		u16 is_scoped_ : 1;

		u16 unused_ : 14;

		u16 rank_in_closure;
	} closed;
};

struct CoreData;





struct ConfigLogFileRef
{
	bool enable = true;

	Range<char8> filepath = {};
};

// Structure holding config parameters used to parameterize the further
// compilation process.
// This is filled in by `create_config` and must only be read afterwards.
// To free it, use `release_config`.
struct Config
{
	struct
	{
		Range<char8> filepath = range::from_literal_string("main.evl");

		Range<char8> symbol = range::from_literal_string("main");
	} entrypoint;

	struct
	{
		struct
		{
			Range<char8> filepath = range::from_literal_string("prelude.evl");
		} prelude;
	} std;

	struct
	{
		u64 reserve = 1 << 30;

		u64 commit_increment = 1 << 18;

		u64 max_huge_alloc_count = 1 << 16;
	} heap;

	struct
	{
		struct
		{
			ConfigLogFileRef asts{ false, {} };

			ConfigLogFileRef opcodes{ false, {} };

			ConfigLogFileRef types{ false, {} };
		} imports;

		ConfigLogFileRef config{ false, {} };

		struct
		{
			ConfigLogFileRef file;

			s64 source_tab_size = 4;
		} diagnostics;
	} logging;

	struct
	{
		bool heap = true;

		bool ast_pool = true;

		bool error_sink = true;

		bool global_value_pool = true;

		bool identifier_pool = true;

		bool interpreter = true;

		bool lexical_analyser = true;

		bool opcode_pool = true;

		bool parser = true;

		bool source_reader = true;

		bool type_pool = true;
	} enable;

	bool compile_all = false;
};

Maybe<minos::FileHandle> config_open_log_file(ConfigLogFileRef file_ref, Maybe<minos::StdFileName> fallback) noexcept;

const TreeSchemaNode* config_schema() noexcept;





Maybe<void*> comp_heap_alloc(CoreData* core, u64 size, u64 align, bool allow_huge) noexcept;

void comp_heap_gc_begin(CoreData* core) noexcept;

bool comp_heap_gc_mark(CoreData* core, MutRange<byte> memory) noexcept;

void comp_heap_gc_end(CoreData* core) noexcept;

u64 comp_heap_arena_mark(CoreData* core) noexcept;

void* comp_heap_arena_alloc(CoreData* core, u64 size, u64 align) noexcept;

void comp_heap_arena_release(CoreData* core, u64 arena_mark) noexcept;

void* comp_heap_arena_release_and_preserve(CoreData* core, u64 arena_mark, MutRange<byte> preserve) noexcept;

byte* comp_heap_small_allocation_base(CoreData* core) noexcept;

byte* comp_heap_small_allocation_tip(CoreData* core) noexcept;





// Identifier Pool.
// This deduplicates identifiers, making it possible to refer to them with
// fixed-size `IdentifierId`s.
// Additionally supports storing an 8-bit attachment for each identifier, which
// is currently used to distinguish keywords and builtins from other
// identifiers.
// This is created by `create_identifier_pool` and freed by
// `release_identifier_pool`.
struct IdentifierPool;

// Id used to refer to an identifier. Obtained from `id_from_identifier` or
// `id_and_attachment_from_identifier` and usable with
// `identifier_name_from_id` to retrieve associated name.
enum class IdentifierId : u32
{
	// Used to indicate that there is no identifier associated with a
	// construct. No valid identifier will ever map to this `IdentifierId`.
	INVALID = 0,

	// Reserved value used synthetic definitions created during lowering of
	// set-operations to load-operate-set.
	FirstSynth = 1,

	// First reserved value used for synthetic definitions created during
	// lifting of values to locations. This pass reserves all identifiers in
	// the range [SecondSynth, FirstNatural).
	SecondSynth = 2,

	// First value used for user-defined identifiers.
	FirstNatural = 65536,
};

// Returns the unique `IdentifierId` that corresponds to the given `identifier`
// in `identifiers`. All calls with the same `IdentifierPool` and same
// byte-for-byte `identifier` are guaranteed to return the same `IdentifierId`.
// All calls to the same `IdentifierPool` with distinct `identifier`s are
// guaranteed to return distinct `IdentifierId`s.
IdentifierId id_from_identifier(CoreData* core, Range<char8> identifier) noexcept;

// Same as `id_from_identifier`, but additionally sets `*out_attachment` to the
// value previously set for the given `identifier` by
// `identifier_set_attachment`, or `0` if no attachment has been set.
// Note that this call will never return `INVALID_IDENTIFIER_ID`.
IdentifierId id_and_attachment_from_identifier(CoreData* core, Range<char8> identifier, u8* out_attachment) noexcept;

// Sets the attachment associated with `identifier` in the given
// `IdentifierPool` to the given `attachment`. The given `identifier` must not
// have previously had an attachment set.
// Additionally, `attachment` must not be `0`.
void identifier_set_attachment(CoreData* core, Range<char8> identifier, u8 attachment) noexcept;

// Returns the byte-sequence corresponding to the given `IdentifierId` in the
// given `IdentifierPool`. `id` must not be `INVALID_IDENTIFIER_ID` and must
// have been returned from a previous call to `id_from_identifier` or
// `id_and_attachment_from_identifier`.
Range<char8> identifier_name_from_id(const CoreData* core, IdentifierId id) noexcept;




enum class StrongCompareOrdering : u8
{
	Equal,
	LessThan,
	GreaterThan,
};

enum class WeakCompareOrdering : u8
{
	Equal,
	LessThan,
	GreaterThan,
	Unordered,
};

// Representation of a compile-time known, arbitrary-width signed integer.
// This is used to represent integer literals, and arithmetic involving them.
// Currently, it really only supports values in the range [-2^62, 2^62-1].
struct CompIntegerValue
{
	u64 rep;
};

// Representation of a compile-time known floating-point value.
struct CompFloatValue
{
	f64 rep;
};

// Creates a `CompIntegerValue` representing the given unsigned 64-bit value.
CompIntegerValue comp_integer_from_u64(u64 value) noexcept;

// Creates a `CompIntegerValue` representing the given signed 64-bit value.
CompIntegerValue comp_integer_from_s64(s64 value) noexcept;

// Creates a `CompIntegerValue` representing the given floating point value.
// If the given `value` is `+/-inf` or `nan`, the function returns `false` and
// leaves `*out` uninitialized. The same occurs if `round` is false and `value`
// does not represent a whole number.
// Otherwise, the function returns `true` and initializes `*out` with the
// integer value corresponding to the given floating point `value`.
bool comp_integer_from_comp_float(CompFloatValue value, bool round, CompIntegerValue* out) noexcept;

bool bits_from_comp_integer(CompIntegerValue value, u32 bits, bool is_signed, byte* out) noexcept;

// Attempts to extract the value of the given `CompIntegerValue` into a `u64`.
// If the value is outside the range of a 64-bit unsigned integer, `false` is
// returned and `*out` is left uninitialized. Otherwise `true` is returned and
// `*out` contains the value of the given `CompIntegerValue`.
bool u64_from_comp_integer(CompIntegerValue value, u8 bits, u64* out) noexcept;

// Attempts to extract the value of the given `CompIntegerValue` into a `s64`.
// If the value is outside the range of a 64-bit signed integer, `false` is
// returned and `*out` is left uninitialized. Otherwise `true` is returned and
// `*out` contains the value of the given `CompIntegerValue`.
bool s64_from_comp_integer(CompIntegerValue value, u8 bits, s64* out) noexcept;

// Adds `lhs` and `rhs` together, returning a new `CompIntegerValue`
// representing the result.
CompIntegerValue comp_integer_add(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

// Subtracts `rhs` from 'lhs', returning a new `CompIntegerValue` representing
// the result.
CompIntegerValue comp_integer_sub(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

// Multiplies `lhs` with `rhs`, returning a new `CompIntegerValue` representing
// the result.
CompIntegerValue comp_integer_mul(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;

// Performs truncating division of `lhs` by 'rhs'.
// If `rhs` is `0`, `false` is returned and `*out` is left uninitialized.
// Otherwise, `true` is returned and `*out` receives the resulting value.
bool comp_integer_div(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the module of `lhs` by 'rhs'.
// If `rhs` is `0`, `false` is returned and `*out` is left uninitialized.
// Otherwise, `true` is returned and `*out` receives the resulting value.
bool comp_integer_mod(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Flips the sign of the given `CompIntegerValue`, returning a new
// `CompIntegerValue` holding the resulting value.
CompIntegerValue comp_integer_neg(CompIntegerValue value) noexcept;

// Shift `lhs` left by `rhs` bits, effectively calculating `lhs * 2^rhs`.
// If `rhs` is negative, returns `false` and leaves `*out` uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_shift_left(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Shift `lhs` right by `rhs` bits, effectively calculating `lhs / 2^rhs` where
// `/` is truncating division.
// If `rhs` is negative, returns `false` and leaves `*out` uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_shift_right(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the bitwise and of `lhs` and `rhs`.
// If either `lhs` or `rhs` are negative, returns `false` and leaves `*out`
// uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_bit_and(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the bitwise or of `lhs` and `rhs`.
// If either `lhs` or `rhs` are negative, returns `false` and leaves `*out`
// uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_bit_or(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

// Takes the bitwise exclusive or of `lhs` and `rhs`.
// If either `lhs` or `rhs` are negative, returns `false` and leaves `*out`
// uninitialized.
// Otherwise returns `true` and sets `*out` to the resulting value.
bool comp_integer_bit_xor(CompIntegerValue lhs, CompIntegerValue rhs, CompIntegerValue* out) noexcept;

CompIntegerValue comp_integer_bit_not(CompIntegerValue value) noexcept;

// Compares the values represented by two `CompIntegerValues`. Returns `true`
// if they are equal, `false` otherwise.
StrongCompareOrdering comp_integer_compare(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;



// Creates a `CompFloatValue` representing the given double-precision float
// value.
CompFloatValue comp_float_from_f64(f64 value) noexcept;

// Creates a `CompFloatValue` representing the given single-precision float
// value.
CompFloatValue comp_float_from_f32(f32 value) noexcept;

// Attempts to crete a `CompFloatValue` representing the given 64-bit unsigned
// integer. If the integer is not exactly representable, `false` is returned
// and `*out` is left uninitialized.
// Otherwise `true` is returned and `*out` receives the resulting value.
bool comp_float_from_u64(u64 value, CompFloatValue* out) noexcept;

// Attempts to crete a `CompFloatValue` representing the given 64-bit signed
// integer. If the integer is not exactly representable, `false` is returned
// and `*out` is left uninitialized.
// Otherwise `true` is returned and `*out` receives the resulting value.
bool comp_float_from_s64(s64 value, CompFloatValue* out) noexcept;

// Attempts to crete a `CompFloatValue` representing the given
// `CompIntegerValue`. If the integer is not exactly representable, `false`
// is returned and `*out` is left uninitialized.
// Otherwise `true` is returned and `*out` receives the resulting value.
bool comp_float_from_comp_integer(CompIntegerValue value, CompFloatValue* out) noexcept;

// Returns the value represented by the given `CompFloatValue` as a
// double-precision float.
f64 f64_from_comp_float(CompFloatValue value) noexcept;

// Returns the value represented by the given `CompFloatValue` as a
// single-precision float.
f32 f32_from_comp_float(CompFloatValue value) noexcept;

// Adds `lhs` and `rhs` together, returning a new `CompFloatValue` representing
// the result.
CompFloatValue comp_float_add(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Subtracts `rhs` from `lhs`, returning a new `CompFloatValue` representing
// the result.
CompFloatValue comp_float_sub(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Multiplies `lhs` with `rhs`, returning a new `CompFloatValue` representing
// the result.
CompFloatValue comp_float_mul(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Divides `lhs` by `rhs`, returning a new `CompFloatValue` representing the
// result.
CompFloatValue comp_float_div(CompFloatValue lhs, CompFloatValue rhs) noexcept;

// Takes the modulo of `lhs` by `rhs`, returning a new `CompFloatValue`
// representing the result.
CompFloatValue comp_float_neg(CompFloatValue value) noexcept;

WeakCompareOrdering comp_float_compare(CompFloatValue lhs, CompFloatValue rhs) noexcept;



// Adds the bytes in `lhs` and `rhs` as little-endian integers into `dst`.
// `dst`, `lhs` and `rhs` must all have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred, `true` otherwise.
bool bitwise_add(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept;

// Subtracts the bytes in `rhs` from those in `lhs` as little-endian integers,
// putting the resulting value into `dst`.
// `dst`, `lhs` and `rhs` must all have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred, `true` otherwise.
bool bitwise_sub(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept;

// Multiplies the bytes in `lhs` with those in `rhs` as little-endian integers,
// putting the resulting value into `dst`.
// `dst`, `lhs` and `rhs` must all have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred, `true` otherwise.
bool bitwise_mul(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept;

// Divides the bytes in `lhs` by those in `rhs` as little-endian integers,
// putting the resulting value into `dst`.
// `dst`, `lhs` and `rhs` must all have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred, `true` otherwise.
bool bitwise_div(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept;

// Takes `lhs` modulo the value in `rhs`, interpreted as little-endian
// integers, putting the resulting value into `dst`.
// `dst`, `lhs` and `rhs` must all have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred, `true` otherwise.
bool bitwise_mod(u16 bits, bool is_signed, MutRange<byte> dst, Range<byte> lhs, Range<byte> rhs) noexcept;

// Shifts the bytes in `lhs` by `rhs` bits to the left (upward in the bytewise
// representation, i.e. to a higher index), putting the result into `dst`.
// `dst` and `lhs` must have the same count. The shifted-out high bytes are
// discarded.
// `rhs` must be less than `bits`.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values,
// and only the `bits % 8` bits of `lhs`'s last byte are respected, with the
// others treated as 0.
// Bits in `dst` that do not receive a shifted bit are set to 0.
void bitwise_shift_left(u16 bits, MutRange<byte> dst, Range<byte> lhs, u64 rhs) noexcept;

// Shifts the bytes in `lhs` by `rhs` bits to the right (downward in the
// bytewise representation, i.e. to a lower index), putting the result into
// `dst`. The shifted-out low bytes are discarded.
// `dst` and `lhs` must have the same count.
// `rhs` must be less than `bits`.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values,
// and only the `bits % 8` bits of `lhs`'s last byte are respected, with the
// others treated as 0.
// If `is_arithmetic_shift` is true and the most significant bit of `src` is 1,
// then bits in `dst` that do not receive a shifted bit are set to 1. Otherwise
// they are set to 0.
void bitwise_shift_right(u16 bits, MutRange<byte> dst, Range<byte> lhs, u64 rhs, bool is_arithmetic_shift) noexcept;

// Negates the bytes in `operand`, interpreted as a little-endian integer,
// putting the resulting value into `dst`.
// `dst` and `operand` must have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred (i.e., if `operand` contained the most
// negative value representable in the given bit count), `true` otherwise.
bool bitwise_neg(u16 bits, MutRange<byte> dst, Range<byte> operand) noexcept;

// Computes the bitwise not of the bytes in `operand`, putting the resulting
// value into `dst`.
// `dst` and `operand` must have the same count.
// `bits` must be between `dst.count() * 8` and `dst.count() * 8 - 7`.
// If `bits` is not exactly `dst.count() * 8` only the low `bits % 8` bits of
// `dst`'s last byte are written, with the others keeping their initial values.
// Returns `false` if overflow occurred (i.e., if `operand` contained the most
// negative value representable in the given bit count), `true` otherwise.
void bitwise_not(u16 bits, MutRange<byte> dst, Range<byte> operand) noexcept;



// Safely adds the 8-bit unsigned integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_u8(u8 a, u8 b, u8* out) noexcept;

// Safely adds the 16-bit unsigned integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_u16(u16 a, u16 b, u16* out) noexcept;

// Safely adds the 32-bit unsigned integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_u32(u32 a, u32 b, u32* out) noexcept;

// Safely adds the 64-bit unsigned integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_u64(u64 a, u64 b, u64* out) noexcept;

// Safely adds the 8-bit signed integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_s8(s8 a, s8 b, s8* out) noexcept;

// Safely adds the 16-bit signed integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_s16(s16 a, s16 b, s16* out) noexcept;

// Safely adds the 32-bit signed integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_s32(s32 a, s32 b, s32* out) noexcept;

// Safely adds the 64-bit signed integers `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked_s64(s64 a, s64 b, s64* out) noexcept;


// Safely subtracts the 8-bit unsigned integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_u8(u8 a, u8 b, u8* out) noexcept;

// Safely subtracts the 16-bit unsigned integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_u16(u16 a, u16 b, u16* out) noexcept;

// Safely subtracts the 32-bit unsigned integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_u32(u32 a, u32 b, u32* out) noexcept;

// Safely subtracts the 64-bit unsigned integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_u64(u64 a, u64 b, u64* out) noexcept;

// Safely subtracts the 8-bit signed integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_s8(s8 a, s8 b, s8* out) noexcept;

// Safely subtracts the 16-bit signed integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_s16(s16 a, s16 b, s16* out) noexcept;

// Safely subtracts the 32-bit signed integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_s32(s32 a, s32 b, s32* out) noexcept;

// Safely subtracts the 64-bit signed integer `b` from `a`, returning `false`
// and leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set
// to the result and `true` is returned.
bool sub_checked_s64(s64 a, s64 b, s64* out) noexcept;


// Safely multiplies the 8-bit unsigned integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_u8(u8 a, u8 b, u8* out) noexcept;

// Safely multiplies the 16-bit unsigned integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_u16(u16 a, u16 b, u16* out) noexcept;

// Safely multiplies the 32-bit unsigned integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_u32(u32 a, u32 b, u32* out) noexcept;

// Safely multiplies the 64-bit unsigned integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_u64(u64 a, u64 b, u64* out) noexcept;

// Safely multiplies the 8-bit signed integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_s8(s8 a, s8 b, s8* out) noexcept;

// Safely multiplies the 16-bit signed integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_s16(s16 a, s16 b, s16* out) noexcept;

// Safely multiplies the 32-bit signed integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_s32(s32 a, s32 b, s32* out) noexcept;

// Safely multiplies the 64-bit signed integers `a` and `b`.
// If the result would overflow, `false` is returned, and `*out` is
// left undefined. Otherwise, `true` is returned, and `*out` is set to the
// division's result.
bool mul_checked_s64(s64 a, s64 b, s64* out) noexcept;





// Allocator for Abstract Syntax Trees (ASTs). These are created by parsing
// source files and subsequently annotated with type. and other information.
struct AstPool;

// Maximum nesting depth of AST nodes. Anything beyond this will result in an
// error during parsing.
// Note that this is actually pretty generous, and should really only
// potentially be a problem for changed `if ... else if ...` clauses or
// expressions of the form `a + b + ...`.
static constexpr s32 MAX_AST_DEPTH = 128;

// Maximum number of parameters a function may take. This is set to 64 to allow
// efficiently tracking seen args in a 64-bit mask.
static constexpr u32 MAX_FUNC_PARAM_COUNT = 64;

// Tag used to identify the kind of an `AstNode`.
enum class AstTag : u8
{
	INVALID = 0,
	Builtin,
	File,
	CompositeInitializer,
	ArrayInitializer,
	Wildcard,
	Where,
	Expects,
	Ensures,
	Definition,
	Parameter,
	Block,
	If,
	For,
	ForEach,
	Switch,
	Case,
	Func,
	Signature,
	Trait,
	Impl,
	Self,
	Catch,
	Unreachable,
	Undefined,
	Identifier,
	LitInteger,
	LitFloat,
	LitChar,
	LitString,
	OpSliceOf,
	Return,
	Leave,
	Yield,
	ParameterList,
	TraitParameterList,
	Call,
	UOpTypeTailArray,
	UOpTypeSlice,
	UOpTypeMultiPtr,
	UOpTypeOptMultiPtr,
	UOpEval,
	UOpTry,
	UOpDefer,
	UOpDistinct,
	UOpAddr,
	UOpDeref,
	UOpBitNot,
	UOpLogNot,
	UOpTypeOptPtr,
	UOpTypeVarArgs,
	ImpliedMember,
	UOpTypePtr,
	UOpNegate,
	UOpPos,
	OpAdd,
	OpSub,
	OpMul,
	OpDiv,
	OpAddTC,
	OpSubTC,
	OpMulTC,
	OpMod,
	OpBitAnd,
	OpBitOr,
	OpBitXor,
	OpShiftL,
	OpShiftR,
	OpLogAnd,
	OpLogOr,
	Member,
	OpCmpLT,
	OpCmpGT,
	OpCmpLE,
	OpCmpGE,
	OpCmpNE,
	OpCmpEQ,
	OpSet,
	OpSetAdd,
	OpSetSub,
	OpSetMul,
	OpSetDiv,
	OpSetAddTC,
	OpSetSubTC,
	OpSetMulTC,
	OpSetMod,
	OpSetBitAnd,
	OpSetBitOr,
	OpSetBitXor,
	OpSetShiftL,
	OpSetShiftR,
	OpTypeArray,
	OpArrayIndex,
	MAX,
};

// Flags specifying tag-specific information for an `AstNode`.
enum class AstFlag : u8
{
	EMPTY                = 0,

	Definition_IsPub                 = 0x01,
	Definition_IsMut                 = 0x02,
	Definition_HasType               = 0x04,
	Definition_IsEval                = 0x08,
	Definition_IsTemplatedParam      = 0x10,

	If_HasWhere                      = 0x01,
	If_HasElse                       = 0x02,

	For_HasWhere                     = 0x01,
	For_HasCondition                 = 0x02,
	For_HasStep                      = 0x04,
	For_HasFinally                   = 0x08,

	ForEach_HasWhere                 = 0x01,
	ForEach_HasIndex                 = 0x02,
	ForEach_HasFinally               = 0x04,

	Switch_HasWhere                  = 0x20,

	Signature_HasExpects             = 0x01,
	Signature_HasEnsures             = 0x02,
	Signature_IsProc                 = 0x04,
	Signature_HasTemplatedReturnType = 0x08,

	Trait_HasExpects                 = 0x01,

	Catch_HasDefinition              = 0x01,

	OpSliceOf_HasBegin               = 0x01,
	OpSliceOf_HasEnd                 = 0x02,

	Type_IsMut                       = 0x02,
};

// Id used to refer to an `AstNode` in the `AstPool`.
// The use of this is that it is smaller (4 instead of 8 bytes), and resistant
// to serialization, as the pool's base address can be ignored.
enum class AstNodeId : u32
{
	// Value used to indicate that there is no `AstNode` to represent.
	INVALID = 0,
};

// A node in an AST. This is really only the node's header, with potential
// additional "attachment" data located directly after it, depending on the
// node's `tag` value.
// After this attachment, the node's children follow, if there are any.
//
// Attachment data should only be accessed via the templated `attachment_of`
// function, which performs some sanity checks in debug builds.
// See `Ast[tag-name]Data` for the layout of this data, in
// dependence on the `tag`.
//
// Children should be accessed either via an iterator (`direct_children_of`,
// `preorder_ancestors_of` or `postorder_ancestors_of`) or via
// `first_child_of`. The presence of children can be checked via the
// `has_children` function.
//
// Following siblings should accessed via `next_sibling_of`. The presence of
// additional siblings can be checked via the `has_next_sibling` function.
struct alignas(8) AstNode
{
	static constexpr u8 STRUCTURE_FIRST_SIBLING = 0x01;

	static constexpr u8 STRUCTURE_LAST_SIBLING = 0x02;

	static constexpr u8 STRUCTURE_NO_CHILDREN = 0x04;

	// Indicates what kind of AST node is represented. This determines the
	// meaning of `flags` and the layout and semantics of the trailing data.
	AstTag tag;

	// Tag-dependent flags that contain additional information on the AST node.
	// In particular, for `AstTag::Builtin`, this contains a `Builtin`
	// enumerant instead of a combination of or'ed flags.
	AstFlag flags;

	u8 own_qwords;

	u8 structure_flags;

	// Number of four-byte units that are taken up by this node and its
	// children. Note that this is thus still meaningful if the node has no
	// next sibling (`internal_flags` contains `FLAG_LAST_SIBLING`). In this
	// case, it indicates the offset to an ancestor's next sibling.
	// This should not be read directly, and is instead used by various helper
	// functions.
	u32 next_sibling_offset;
};

// Token returned from and used by `push_node` to structure the created AST as
// it is created. See `push_node` for further information.
enum class AstBuilderToken : u32
{
	// Value used to indicate that a node created by `push_node` has no
	// children. This will never be returned from `push_node`. See `push_node`
	// for further information.
	NO_CHILDREN = ~0u,
};

enum class ClosureListId : u32
{
	INVALID = 0,
};

// Result of a call to `next(AstPreorderIterator*)` or
// `next(AstPostorderIterator)`. See `AstPreorderIterator` and
// `AstPostorderIterator` for further details.
struct AstIterationResult
{
	// `AstNode` at the iterator's position.
	AstNode* node;

	// Depth of the returned node, relative to the node from which this
	// iterator was created. A direct child has a `depth` of `0`, a grandchild
	// a of `1`, and so on.
	u32 depth;
};

// Iterator over the direct children of an `AstNode`.
// This is created by a call to `direct_children_of`, and can be iterated by
// calling `next(AstDirectChildIterator*)`.
//
// For an AST of the shape
//
// ```
// (R)
//  + A
//  | + X
//  | | ` K
//  | ` Y
//  ` B
//    ` Z
// ```
//
// where `R` is the root node and thus not iterated, this results in the
// iteration sequence
//
// `A`, `B`
struct AstDirectChildIterator
{
	AstNode* curr;
};

// Iterator over the ancestors of an `AstNode`, returning nodes in depth-first
// preorder.
//
// For an AST of the shape
//
// ```
// (R)
//  + A
//  | + X
//  | | ` K
//  | ` Y
//  ` B
//    ` Z
// ```
//
// where `R` is the root node and thus not iterated, this results in the
// iteration sequence
//
// `A`, `X`, `K`, `Y`, `B`, `Z`.
struct AstPreorderIterator
{
	AstNode* curr;

	u8 depth;

	s32 top;

	u8 prev_depths[MAX_AST_DEPTH];

	static_assert(MAX_AST_DEPTH <= UINT8_MAX);
};

// Iterator over the ancestors of an `AstNode`, returning nodes in depth-first
// postorder.
//
// For an AST of the shape
//
// ```
// (R)
//  + A
//  | + X
//  | | ` K
//  | ` Y
//  ` B
//    ` Z
// ```
//
// where `R` is the root node and thus not iterated, this results in the
// iteration sequence
//
// `K`, `X`, `Y`, `A`, `Z`, `B`.
struct AstPostorderIterator
{
	AstNode* base;

	s32 depth;

	u32 offsets[MAX_AST_DEPTH];
};

struct AstFlatIterator
{
	AstNode* curr;

	AstNode* end;
};

struct alignas(8) AstFileData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::File;

	u32 member_count;

	u32 unused_ = 0;
};

// Attachment of an `AstNode` with tag `AstTag::LitInteger`.
struct alignas(8) AstLitIntegerData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitInteger;

	// `CompIntegerValue` representing this literal's value.
	// This is under-aligned to 4 instead of 8 bytes since `AstNode`s - and
	// thus their attachments - are 4-byte aligned.
	CompIntegerValue value;
};

// Attachment of an `AstNode` with tag `AstTag::LitFloat`.
struct alignas(8) AstLitFloatData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitFloat;

	// `CompFloatValue` representing this literal's value.
	// This is under-aligned to 4 instead of 8 bytes since `AstNode`s - and
	// thus their attachments - are 4-byte aligned.
	CompFloatValue value;
};

// Attachment of an `AstNode` with tag `AstTag::LitChar`.
struct alignas(8) AstLitCharData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitChar;

	// Unicode codepoint representing this character literal's value.
	u32 codepoint;

	// Padding to ensure consistent binary representation and avoid compiler
	// warnings regarding padding due to `alignas`.
	u32 unused_ = 0;
};

// Attachment of an `AstNode` with tag `AstTag::Identifier`.
struct alignas(8) AstIdentifierData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Identifier;

	// `IdentifierId` of the identifier represented by this node.
	IdentifierId identifier_id;

	// Location of the definition this identifier is referring to, relative
	// to the identifier's position in the source code.
	// This is set by `resolve_names`.
	NameBinding binding;
};

// Attachment of an `AstNode` with tag `AstTag::Member`.
struct alignas(8) AstMemberData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Member;

	// `IdentifierId` of the member represented by this node.
	IdentifierId identifier_id;

	// Padding to ensure consistent binary representation and avoid compiler
	// warnings regarding padding due to `alignas`.
	u32 unused_ = 0;
};

// Attachment of an `AstNode` with tag `AstTag::ImpliedMember`.
struct alignas(8) AstImpliedMemberData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::ImpliedMember;

	// `IdentifierId` of the member represented by this node.
	IdentifierId identifier_id;

	// Padding to ensure consistent binary representation and avoid compiler
	// warnings regarding padding due to `alignas`.
	u32 unused_ = 0;
};

// Attachment of an `AstNode` with tag `AstTag::LitString`.
struct alignas(8) AstLitStringData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitString;

	// `ForeverValueId` of the global `u8` array representing this string's
	// value.
	ForeverValueId string_value_id;

	// `TypeId` of the `u8` array representing this string's value.
	TypeId string_type_id;
};

// Attachment of an `AstNode` with tag `AstTag::Definition`.
struct alignas(8) AstDefinitionData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Definition;

	// `IdentifierId` of the definition.
	IdentifierId identifier_id;

	// Padding to ensure consistent binary representation and avoid compiler
	// warnings regarding padding due to `alignas`.
	u32 unused_ = 0;
};

// Attachment of an `AstNode` with tag `AstTag::Parameter`.
struct alignas(8) AstParameterData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Parameter;

	// `IdentifierId` of the parameter.
	IdentifierId identifier_id;

	// Padding to ensure consistent binary representation and avoid compiler
	// warnings regarding padding due to `alignas`.
	u32 unused_ = 0;
};

struct alignas(8) AstSignatureData
{
	static constexpr AstTag TAG = AstTag::Signature;

	Maybe<ClosureListId> closure_list_id;

	u32 unused_ = 0;
};

struct alignas(8) AstTraitParameterListData
{
	static constexpr AstTag TAG = AstTag::TraitParameterList;

	u32 parameter_count;

	u32 unused_ = 0;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::Func`. To obtain this for a given node, call `get_func_info`.
struct SignatureInfo
{
	// `AstNode` with tag `AstTag::ParameterList` containing the function's
	// argument definitions as its children.
	AstNode* parameters;

	// Optional `AstNode` containing the function's return type expression if
	// it has one.
	AstNode* return_type;

	// Optional `AstNode` with tag `AstTag::Expects` containing the function's
	// `expects` clause if it has one.
	Maybe<AstNode*> expects;

	// Optional `AstNode` with tag `AstTag::Ensures` containing the function's
	// `ensures` clause if it has one.
	Maybe<AstNode*> ensures;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::Definition`. To obtain this for a given node, call
// `get_defintion_info`.
struct DefinitionInfo
{
	// An optional `AstNode` containing the definition's explicit type
	// expression if it has one.
	Maybe<AstNode*> type;

	// An optional `AstNode` containing the definition's value expression if it
	// has one.
	Maybe<AstNode*> value;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::If`. To obtain this for a given node, call `get_if_info`.
struct IfInfo
{
	// `AstNode` containing the if's condition.
	AstNode* condition;

	// `AstNode` containing the if's consequent, i.e., the expression that is
	// executed if the condition evaluated to `true`. In other words, the
	// `then` branch.
	AstNode* consequent;

	// Optional `AstNode` containing the if's alternative, i.e., the expression
	// executed if the condition evaluted to `false`. In other words, the
	// `else` branch. If there is no alternative, this is `none`.
	Maybe<AstNode*> alternative;

	// Optional `AstNode` with tag `AstTag::Where` containing the if's `where`
	// clause if it has one.
	Maybe<AstNode*> where;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::For`. To obtain this for a given node, call `get_for_info`.
struct ForInfo
{
	// `AstNode` containing the for's condition.
	AstNode* condition;

	// Optional `AstNode` containing the for's step if it has one.
	Maybe<AstNode*> step;

	// Optional `AstNode` with tag `AstTag::Where` containing the for's `where`
	// clause if it has one.
	Maybe<AstNode*> where;

	// `AstNode` containing the for's body.
	AstNode* body;

	// Optional `AstNode` containing the for's `finally` clause if it has one.
	Maybe<AstNode*> finally;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::ForEach`. To obtain this for a given node, call `get_foreach_info`.
struct ForEachInfo
{
	// `AstNode` with tag `AstTag::Definition` containing the foreach's element
	// definition.
	AstNode* element;

	// Optional `AstNode` with tag `AstTag::Defintion` containing the foreach's
	// index definition if it has one.
	Maybe<AstNode*> index;

	// `AstNode` containing the expression over which the foreach is iterating.
	AstNode* iterated;

	// Optional `AstNode` with tag `AstTag::Where` containing the foreach's
	// `where` clause if it has one.
	Maybe<AstNode*> where;

	// `AstNode` containing the foreach's body.
	AstNode* body;

	// `AstNode` containing the foreach's `finally` clause if it has one.
	Maybe<AstNode*> finally;
};

struct SwitchInfo
{
	AstNode* switched;

	Maybe<AstNode*> where;

	AstNode* first_case;
};

struct OpSliceOfInfo
{
	AstNode* sliced;

	Maybe<AstNode*> begin;

	Maybe<AstNode*> end;
};

struct ClosureListEntry
{
	u16 source_rank;

	u8 source_out;

	bool source_is_closure;
};

struct alignas(4) ClosureList
{
	u16 count;

	u16 unused_;

	#if COMPILER_GCC
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wpedantic" // ISO C++ forbids flexible array member
	#endif
	ClosureListEntry entries[];
	#if COMPILER_GCC
		#pragma GCC diagnostic pop
	#endif
};



// Bitwise `or` of two `AstFlag`s
inline constexpr AstFlag operator|(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

// Bitwise `and` of two `AstFlag`s
inline constexpr AstFlag operator&(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

// Bitwise `set-or` of two `AstFlag`s
inline constexpr AstFlag& operator|=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs | rhs;

	return lhs;
}

// Bitwise `set-and` of two `AstFlag`s
inline constexpr AstFlag& operator&=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs & rhs;

	return lhs;
}



// Converts `node` to its corresponding `AstNodeId`.
// `node` must be part of an AST created by a call to `complete_ast` on the
// same `AstPool`.
// Use `ast_node_from_id` to retrieve `node` from the returned `AstNodeId`.
AstNodeId id_from_ast_node(CoreData* core, AstNode* node) noexcept;

// Converts `id` to its corresponding `AstNode*`.
// `id` must have been obtained from a previous call to `id_from_ast_node` on
// the same `AstPool`.
AstNode* ast_node_from_id(CoreData* core, AstNodeId id) noexcept;



// Pushes a new `AstNode` with no attachment into `asts`'s ast builder.
// Nodes are pushed in post-order, meaning that children are pushed before
// parents, while siblings are pushed first to last.
// The created node will have `first_child` as its first child node. To create
// a node without children, pass `AstBuilderToken::NO_CHILDREN` for
// `first_child`.
// The node's `source_id`, `flags` and `tag` fields will be set to the given
// values. The node will not have an attachment.
// To complete the ast builder, call `complete_ast`.
AstBuilderToken push_node(CoreData* core, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept;

// Pushes a new `AstNode` with the given attachment into `asts`'s ast builder.
// Nodes are pushed in post-order, meaning that children are pushed before
// parents, while siblings are pushed first to last.
// The created node will have `first_child` as its first child node. To create
// a node without children, pass `AstBuilderToken::NO_CHILDREN` for
// `first_child`.
// The node's `source_id`, `flags` and `tag` fields will be set to the given
// values. The node will have an attachment of `attachment_dwords` four-byte
// units allocated, which will be initialized by a copy from the `attachment`
// argument. Note that the allocated attachment is four-byte aligned, so it is
// not possible to have attachments requiring a higher alignment.
// To complete the ast builder, call `complete_ast`.
AstBuilderToken push_node(CoreData* core, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_dwords, const void* attachment) noexcept;

// Templated helper version of `push_node`.
// This makes it easier to create a node with a given `attachment`.
// Nodes are pushed in post-order, meaning that children are pushed before
// parents, while siblings are pushed first to last.
// `attachment` must be one of the `Ast*Data` structs. The created node's `tag`
// will be set to `T::TAG`.
// See the non-templated versions of `push_node` for further details.
template<typename T>
static AstBuilderToken push_node(CoreData* core, AstBuilderToken first_child, SourceId source_id, AstFlag flags, T attachment) noexcept
{
	static_assert(sizeof(T) % sizeof(u64) == 0);

	return push_node(core, first_child, source_id, flags, T::TAG, sizeof(attachment) / sizeof(u64), &attachment);
}

// Completes the builder of `asts`, returning a pointer to the root node of the
// resulting ast.
// To fill the builder, use one of the `push_node` functions.
// After a call to this function, `asts`'s builder will be reset.
AstNode* complete_ast(CoreData* core) noexcept;



ClosureList* alloc_closure_list(CoreData* core, u16 entry_count) noexcept;

ClosureListId id_from_closure_list(CoreData* core, ClosureList* closure_list) noexcept;

ClosureList* closure_list_from_id(CoreData* core, ClosureListId id) noexcept;


// Checks whether `node` has any child nodes.
// If it does, returns `true`, otherwise returns `false`.
bool has_children(const AstNode* node) noexcept;

// Checks whether `node` has a next sibling.
// If it does, returns `true`, otherwise returns `false`.
bool has_next_sibling(const AstNode* node) noexcept;

// Checks whether the given `flag` is set in `node`s `flags` field.
// If it does, returns `true`, otherwise returns `false`.
bool has_flag(const AstNode* node, AstFlag flag) noexcept;

bool is_descendant_of(const AstNode* parent, const AstNode* child) noexcept;

// Returns the next sibling of `node`.
// This function must only be called on `AstNode`s that have a next sibling.
// This may be known from the context in which the function is called (e.g.,
// the `condition` of an `AstTag::If` node will always have a next sibling,
// namely a `where` or the `consequent`), or dynamically checked by a call to
// `has_next_sibling` (e.g., in case of the child of an `AstTag::Block` node).
AstNode* next_sibling_of(AstNode* node) noexcept;

// Returns the first child of `node`.
// This function must only be called on `AstNode`s that have children.
// This may be known from the node's `tag` (e.g., an `AstTag::OpAdd` node will
// always have exactly two children), or dynamically checked by a call to
// `has_children` (e.g., for an `AstTag::Block` node, which may represent an
// empty block).
AstNode* first_child_of(AstNode* node) noexcept;

// Retrieves a pointer to `node`'s attachment of type `T`, where `T` must be
// the `Ast*Data` struct corresponding to `node`'s `tag`.
template<typename T>
inline T* attachment_of(AstNode* node) noexcept
{
	static_assert(sizeof(AstNode) == sizeof(u64));

	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(AstNode) + sizeof(T) == node->own_qwords * sizeof(u64));

	return reinterpret_cast<T*>(node + 1);
}

// Retrieves a `const` pointer to `node`'s attachment of type `T`, where `T`
// must be the `Ast*Data` struct corresponding to `node`'s `tag`.
template<typename T>
inline const T* attachment_of(const AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->own_qwords * sizeof(u64));

	return reinterpret_cast<const T*>(node + 1);
}

SourceId source_id_of_ast_node(const CoreData* core, const AstNode* node) noexcept;



// Retrieves a `FuncInfo` struct corresponding to `node`'s child structure.
// `node`'s tag must be `AstTag::Func`.
SignatureInfo get_signature_info(AstNode* node) noexcept;

// Retrieves a `DefinitionInfo` struct corresponding to `node`'s child
// structure.
// `node`'s tag must be `AstTag::Definition`.
DefinitionInfo get_definition_info(AstNode* node) noexcept;

// Retrieves a `IfInfo` struct corresponding to `node`'s child structure.
// `node`'s tag must be `AstTag::If`.
IfInfo get_if_info(AstNode* node) noexcept;

// Retrieves a `ForInfo` struct corresponding to `node`'s child structure.
// `node`'s tag must be `AstTag::For`.
ForInfo get_for_info(AstNode* node) noexcept;

// Retrieves a `ForEachInfo` struct corresponding to `node`'s child structure.
// `node`'s tag must be `AstTag::ForEach`.
ForEachInfo get_foreach_info(AstNode* node) noexcept;

// Retrieves a `SwitchInfo` struct corresponding to `node`'s child structure.
// `node`'s tag must be `AstTag::Switch`.
SwitchInfo get_switch_info(AstNode* node) noexcept;

OpSliceOfInfo get_op_slice_of_info(AstNode* node) noexcept;

// Creates an iterator over `node`'s direct children, skipping over more
// removed ancestors.
// See `AstDirectChildIterator` for further details.
AstDirectChildIterator direct_children_of(AstNode* node) noexcept;

// Retrieves the next element of `iterator`. This function may only be called
// exactly once after `has_next` called on the same iterator has returned
// `true`.
AstNode* next(AstDirectChildIterator* iterator) noexcept;

// Checks whether `iterator` has an element to be returned by a future call to
// `next`. This call is idempotent.
bool has_next(const AstDirectChildIterator* iterator) noexcept;

// Creates an iterator over `node`'s ancestors, yielding them in preorder.
// See `AstPreorderIterator` for further details.
AstPreorderIterator preorder_ancestors_of(AstNode* node) noexcept;

// Retrieves the next element of `iterator`. This function may only be called
// exactly once after `has_next` called on the same iterator has returned
// `true`.
AstIterationResult next(AstPreorderIterator* iterator) noexcept;

// Checks whether `iterator` has an element to be returned by a future call to
// `next`. This call is idempotent.
bool has_next(const AstPreorderIterator* iterator) noexcept;

// Creates an iterator over `node`'s ancestors, yielding them in postorder.
// See `AstPostorderIterator` for further details.
AstPostorderIterator postorder_ancestors_of(AstNode* node) noexcept;

// Retrieves the next element of `iterator`. This function may only be called
// exactly once after `has_next` called on the same iterator has returned
// `true`.
AstIterationResult next(AstPostorderIterator* iterator) noexcept;

// Checks whether `iterator` has an element to be returned by a future call to
// `next`. This call is idempotent.
bool has_next(const AstPostorderIterator* iterator) noexcept;

AstFlatIterator flat_ancestors_of(AstNode* node) noexcept;

AstNode* next(AstFlatIterator* iterator) noexcept;

bool has_next(const AstFlatIterator* iterator) noexcept;



// Retrieves a string representing the given `tag`.
// If `tag` is not an enumerant of `AstTag`, it is treated as
// `AstTag::INVALID`.
const char8* tag_name(AstTag tag) noexcept;





// Source file reader.
// This handles reading and deduplication of source files, associating them
// with ASTs, and mapping `SourceId`s to files and concrete `SourceLocations`.
struct SourceReader;

// Id of a position in the program's source code. There is a one-to-one mapping
// between `SourceId`s and bytes across all source files read by a given
// `SourceReader`.
// A `SourceId` is synthesized for every `AstNode` during parsing (see
// `Parser`).
// This can then be mapped back to a `SourceFile` by a call to
// `source_file_from_source_id`, or to a `SourceLocation` by a call to
// `source_location_from_source_id`.
enum class SourceId : u32
{
	// Value reserved for constructs that cannot be meaningfully associated
	// with a read `SourceLocation`. In particular, this is used for the
	// builtin hard-coded prelude AST, which has no real source.
	// This must *not* be used in calls to `source_file_from_source_id`.
	// However, it *may* be used in calls to `source_location_from_source_id`.
	// In this case, a dummy `SourceLocation` indicating the hard-coded prelude
	// as its source is returned.
	INVALID = 0,
};

// Stores data on a source code file that has been read for compilation.
struct SourceFile
{
	// Handle to the file.
	Maybe<minos::FileHandle> file;

	// Id of the root of the associated AST. If the file has not yet been
	// parsed, this is set to `AstNodeId::INVALID`.
	AstNodeId ast;

	// Id of the type representing the file. If the file has not yet started to
	// be typechecked, this is set to `TypeId::INVALID`. As soon as
	// typechecking starts, it is set to the relevant open type id, to allow
	// pseudo-circular references (e.g., importing the file from inside
	// itself).
	TypeId type;

	// `SourceId` of the first byte in this file.
	SourceId source_id_base;

	GlobalCompositeIndex file_index;

	bool has_error;
};

// Combination of a `SourceFile` and its contents. This is returned by
// `read_source_file` and must be freed by `release_read` as soon as the file's
// contents are no longer needed. The `SourceFile` remains valid even after the
// call to `release_read`.
struct SourceFileRead
{
	// Pointer to the `SourceFile` associated with this read.
	SourceFile* source_file;

	// Content read from the file. This is only valid if for the initial read,
	// which can be determined by checking for `begin() != nullptr`.
	// Otherwise, the file has already been parsed into the AST rooted at that
	// id, meaning that its contents are not actually required.
	Range<char8> content;
};

// Description of a location in the program source code.
// This can be retrieved for a given `SourceId` by calling
// `source_location_from_source_id`.
struct SourceLocation
{
	// Path to the file. This is intended for diagnostics, and may not refer to
	// an actual existing file. In particular, this is the case if
	// `source_location_from_source_id` was called with `SourceId::INVALID`.
	Range<char8> filepath;

	// Line number, starting from `1` for the first line in a file.
	u32 line_number;

	// Column number in characters, starting from `1` for the first character
	// in a line.
	u32 column_number;

	// Byte offset of the first character in `context` from the first character
	// in the column. This is usually `0`, and only becomes positive for very
	// long columns, which might not fit into `context` and thus have their
	// beginning cut off.
	u32 context_offset;

	// Number of characters stored in `context`.
	u32 context_chars;

	// Number of tab characters between the start of the line and the character
	// at `column_number`.
	u32 tabs_before_column_number;

	// A copy of the text around the location.
	char8 context[512];
};

// Reads the file specified by `filepath` if it has not been read by a previous
// call. In case the file has already been read, the `source_file` member of
// the return value points to the `SourceFile` returned from the previous call,
// while the `content` member is empty.
// Due to the way this function interacts with `parse` in `import_file`,
// `source_file->ast` will be `AstNodeId::INVALID` if and only if this is
// the first read of the file (meaning `content` contains the file's contents).
// If this is a first read, `release_read` must be called once the file's
// content is no longer needed.
SourceFileRead read_source_file(CoreData* core, Range<char8> filepath) noexcept;

// Releases the contents and other associated resources of a `SourceFileRead`.
// The `SourceFile` pointed to by the `source_file` member is however left
// untouched.
void release_read(CoreData* core, SourceFileRead read) noexcept;

// Converts `source_id` to a `SourceLocation`.
// If `source_id` is `SourceId::INVALID`, a dummy location indicating the
// hard-coded prelude is returned.
SourceLocation source_location_from_source_id(CoreData* core, SourceId source_id) noexcept;

// Retrieves the path to the file to which the given `source_id` belongs.
// This path may not be the same as that from which the file was imported, but
// can be used to open the file.
// `source_id` must not be `SourceId::INVALID`.
Range<char8> source_file_path_from_source_id(CoreData* core, SourceId source_id) noexcept;





// Sink for compiler errors and warnings.
// This takes care of the finicky bits of error reporting, providing a
// convenient `SourceId`-based, printf-like interface.
struct ErrorSink;

enum class CompileError
{
	INVALID = 0,
	CompIntegerValueTooLarge,
	TypesCannotConvert,
	CompositeLiteralTargetIsMissingMember,
	CompositeLiteralTargetHasTooFewMembers,
	CompositeLiteralTargetMemberMappedTwice,
	CompositeLiteralMemberTypesCannotConvert,
	CompositeLiteralSourceIsMissingMember,
	NoCommonArgumentType,
	NoCommonArrayElementType,
	CallNoSuchNamedParameter,
	CallArgumentMappedTwice,
	CallTooManyArgs,
	CallMissingArg,
	SliceOperatorInvalidLhsType,
	SliceOperatorMultiPtrElidedEndIndex,
	SliceOperatorIndexOutOfBounds,
	SliceOperatorIndicesReversed,
	SliceOperatorIndexTooLarge,
	SliceOperatorIndexNegative,
	SliceOperatorUntypedArrayLiteral,
	DerefInvalidOperandType,
	BitNotInvalidOperandType,
	NegateInvalidOperandType,
	UnaryPlusInvalidOperandType,
	BinaryOperatorNumericInvalidArgumentType,
	BinaryOperatorIntegerInvalidArgumentType,
	BinaryOperatorIntegerOrBoolInvalidArgumentType,
	ArithmeticOverflow,
	DivideByZero,
	ModuloByZero,
	ShiftRHSNegative,
	ShiftRHSTooLarge,
	MemberNoSuchName,
	MemberInvalidLhsType,
	MemberNonGlobalAccessedThroughType,
	CompareIncomparableType,
	CompareUnorderedType,
	SetLhsNotMutable,
	TypeArrayCountTooLarge,
	TypeArrayCountNegative,
	ArrayIndexRhsTooLarge,
	ArrayIndexRhsNegative,
	ArrayIndexOutOfBounds,
	ArrayInitializerTooManyElements,
	ArrayInitializerMissingElement,
	ArrayInitializerDuplicateElement,
	ArrayInitializerIndexTooLarge,
	ArrayInitializerIndexNegative,
	ExpectedVoid,
	BuiltinCompleteTypeAlignTooLarge,
	BuiltinCompleteTypeAlignZero,
	BuiltinCompleteTypeAlignNotPowTwo,
	ReturntypeofTemplatedReturnType,
	CyclicGlobalInitializerDependency,
	UnreachableReached,
	GlobalNameNotDefined,
	ScopeTooManyDefinitions,
	ScopeDuplicateName,
	ScopeNameNotDefined,
	LexUnexpectedCharacter,
	LexNullCharacter,
	LexCommentMismatchedBegin,
	LexCommentMismatchedEnd,
	LexBuiltinUnknown,
	LexNumberWithBaseMissingDigits,
	LexNumberUnexpectedCharacterAfterDecimalPoint,
	LexNumberUnexpectedCharacterAfterInteger,
	LexNumberUnexpectedCharacterAfterFloat,
	LexNumberFloatTooLarge,
	LexCharacterBadSurrogateCodeUnit,
	LexCharacterBadLeadCodeUnit,
	LexCharacterEscapeSequenceLowerXBadChar,
	LexCharacterEscapeSequenceUpperXInvalidChar,
	LexCharacterEscapeSequenceUpperXCodepointTooLarge,
	LexCharacterEscapeSequenceUInvalidChar,
	LexCharacterEscapeSequenceUnknown,
	LexCharacterExpectedEnd,
	LexStringTooLong,
	LexStringCrossesNewline,
	LexStringMissingEnd,
	LexIdentifierInitialUnderscore,
	LexConfigUnexpectedControlCharacter,
	LexConfigSingleLineStringCrossesNewline,
	LexConfigUnexpectedCharacter,
	ParseUnaryOperatorMissingOperand,
	ParseBinaryOperatorMissingOperand,
	ParseOpenOperandCountTooLarge,
	ParseOpenOperatorCountTooLarge,
	ParseOperatorOperandCountMismatch,
	ParseFunctionParameterIsPub,
	ParseDefinitionMultiplePub,
	ParseDefinitionMultipleMut,
	ParseDefinitionMissingName,
	ParseDefinitionMissingEquals,
	ParseForeachExpectThinArrowLeft,
	ParseCaseMissingThinArrowRight,
	ParseSwitchMissingCase,
	ParseSignatureMissingReturnType,
	ParseSignatureMissingParenthesisAfterProc,
	ParseSignatureMissingParenthesisAfterFunc,
	ParseSignatureMissingParenthesisAfterTrait,
	ParseSignatureTooManyParameters,
	ParseSignatureUnexpectedParameterListEnd,
	ParseTraitMissingSetOrExpects,
	ParseTraitMissingSet,
	ParseUnexpectedTopLevelExpr,
	ParseCompositeLiteralUnexpectedToken,
	ParseArrayLiteralUnexpectedToken,
	ParseArrayTypeUnexpectedToken,
	ParseImpliedMemberUnexpectedToken,
	ParseExprExpectOperand,
	ParseCallTooManyArguments,
	ParseCallUnexpectedToken,
	ParseSliceUnexpectedToken,
	ParseArrayIndexUnexpectedToken,
	ParseCatchMissingThinArrowRightAfterDefinition,
	ParseMemberUnexpectedToken,
	ParseConfigKeyNestingLimitExceeded,
	ParseConfigKeyNotExpectingSubkeys,
	ParseConfigKeyDoesNotExist,
	ParseConfigExpectedKey,
	ParseConfigExpectedEquals,
	ParseConfigExpectedClosingCurlyOrComma,
	ParseConfigExpectedValue,
	ParseConfigWrongValueTypeForKey,
	ParseConfigEscapeSequenceLowerUTooFewCharacters,
	ParseConfigEscapeSequenceUpperUTooFewCharacters,
	ParseConfigEscapeSequenceUtfInvalidCharacter,
	ParseConfigEscapeSequenceUtfCodepointTooLarge,
	ParseConfigEscapeSequenceInvalid,
	ParseConfigPathTooLong,
	ParseConfigExpectedClosingBracket,
	ParseConfigExpectedEqualsOrDot,
};

struct ErrorRecord
{
	CompileError error;

	SourceId source_id;
};

// Records the given `error` into the `ErrorSink`, associating it with the
// given `source_id`.
// Future calls to `get_errors` or `print_errors` will include an `ErrorRecord`
// with members corresponding to the given arguments.
void record_error(CoreData* core, SourceId source_id, CompileError error) noexcept;

// Records the given `error` into the `ErrorSink`, associating it with the
// `SourceId` returned by
// `source_id_of_ast_node(<AstPool passed to create_error_sink>, source_node)`.
// Future calls to `get_errors` or `print_errors` will include an `ErrorRecord`
// with members corresponding to the given arguments.
void record_error(CoreData* core, const AstNode* source_node, CompileError error) noexcept;

// Prints all errors added to the given `ErrorSink` by previous calls to
// `record_error` to its log file, in the order they were added.
// If there is no log file (i.e., if the `log_file` argument of
// `create_error_sink` was `minos::FileHandle{}`), then no errors are printed.
void print_errors(CoreData* core) noexcept;

// Returns a range of `ErrorRecords` representing all previous calls to
// `record_error` on the given `ErrorSink`.
Range<ErrorRecord> get_errors(CoreData* core) noexcept;

// Appends the message for the given `CompileError` to `dst`, prefixing it with
// the `location`.
// This is mainly intended for usage with `Config` parsing, as there is no
// `ErrorSink` available at that point.
void print_error(minos::FileHandle dst, const SourceLocation* location, CompileError error, u8 tab_size) noexcept;

const char8* tag_name(CompileError error) noexcept;

Maybe<CompileError> compile_error_from_name(Range<char8> name) noexcept;





struct LexicalAnalyser;

bool set_prelude_scope(CoreData* core, AstNode* prelude, GlobalCompositeIndex file_index) noexcept;

bool resolve_names(CoreData* core, AstNode* root, GlobalCompositeIndex file_index) noexcept;





// Pool managing type information.
// This maps the structure of types to concise 32-bit `TypeId`s which can be
// relatively cheaply operated upon, while handling the interweaving of type
// creation and code evaluation, as well as circular type references, such as
// those resulting from linked list elements containing a pointer of their own
// type.
struct TypePool;

// Id used to refer to a type in a `TypePool`.
enum class TypeId : u32
{
	// Value reserved to indicate a missing type, e.g. in case it has not been
	// typechecked yet.
	INVALID = 0,
};

// Tag for discriminating between different kinds of types.
// The layout of the structural information stored for a type depends on its
// tag. For all tags other than `TypeTag::Composite`, this data can accessed by
// calling `simple_type_structure_from_id`.
enum class TypeTag : u8
{
	// Should not be used, reserved for sanity checking in debug builds.
	INVALID = 0,

	INDIRECTION,

	// Tag of the `Void` type. No additional structural data is stored.
	Void,

	// Tag of the `Type` type, used for referring to other types. No additional
	// structural data is stored.
	Type,

	// Tag of the `Definition` type, used for referring to definitions. No
	// additional structural data is stored.
	Definition,

	// Tag of literal integers and results of computations upon them. No
	// additional structural data is stored.
	CompInteger,

	// Tag of literal floating point numbers and results of computations upon
	// them. No additional structural data is stored.
	CompFloat,

	// Tag of the `Bool` type. No additional structural data is stored.
	Boolean,

	// Tag of the `TypeInfo` type. This type is used to implicitly convert
	// anything to a `Type`, which is e.g. necessary for functions such as
	// `typeof` or `sizeof`. No additional structural data is stored.
	TypeInfo,

	// Tag of the `TypeBuilder` type returned from the `_create_type` builtin.
	// No additional structural data is stored.
	TypeBuilder,

	// Tag used to indicate that something diverges, effectively making its
	// type irrelevant. This can be implicitly converted to anything else.
	// No additional structural data is stored.
	Divergent,

	// Tag of the `undefined` type, used for the `undefined` keyword.
	Undefined,

	// Tag of integer types. Its structure is represented by a `NumericType`.
	Integer,

	// Tag of integer types. Its structure is represented by a `NumericType`.
	Float,

	// Tag of slice types. Its structure is represented by a `ReferenceType`.
	Slice,

	// Tag of pointer types. Its structure is represented by a `ReferenceType`.
	Ptr,

	// Tag of array types. Its structure is represented by an `ArrayType`.
	Array,

	// Tag of function types. Its structure is represented by a `SignatureType`.
	Signature,

	// Tag of composite types created via `create_open_type`. These have a
	// more complex (and address-instable) representation, meaning that their
	// structural information cannot be accessed directly. Instead their
	// members can be looked up by name or rank, or iterated.
	// See `create_open_type`, `add_open_type_member`, `close_open_type`,
	// `set_incomplete_member_type_by_rank`,
	// `set_incomplete_member_value_by_rank`, `type_member_info_by_name`,
	// `type_member_info_by_rank`, `IncompleteMemberIterator` and
	// `MemberIterator`, which all solely operate on composite types.
	Composite,

	// Tag of tail array types. Its structure is represented by a
	// `ReferenceType`.
	TailArray,

	// Tag of composite literal types.
	// TODO: This is not used yet, and thus does not currently have an
	//       associated structure. However, it will likely need one.
	CompositeLiteral,

	// Tag of array literal types. Its structure is represented by an
	// `ArrayType`.
	ArrayLiteral,

	// Tag of variadic function argument types. Its structure is represented by
	// a `ReferenceType`.
	Variadic,

	// Tag of trait types.
	// TODO: This is not used yet, and thus does not currently have an
	//       associated structure. However, it will likely need one.
	Trait,
};

enum class TypeRelation : u8
{
	Equal,
	FirstConvertsToSecond,
	SecondConvertsToFirst,
	Unrelated,
};

enum class MemberByNameRst : u8
{
	Ok,
	Incomplete,
	NotFound,
};

// Allocation metrics returned by `type_metrics_by_id`, describing the size,
// stride and alignment of a type.
struct TypeMetrics
{
	// Size of the type, in bytes. Same as the value returned by `sizeof`
	u64 size;

	// Stride of the type, in bytes. Same as the value returned by `strideof`.
	// This value comes into play with arrays, where the address of the `i`th
	// element is calculated as `base_address + i * stride`.
	u64 stride;

	// Alignment of the type, in bytes. Same as the value returned by
	// `alignof`. This is guaranteed to be a nonzero power of two. Values of
	// the type must only be allocated at addresses that are multiples of this
	// alignment (i.e., have the low `log2(align)` bits set to zero).
	u32 align;
};

struct SignatureTypeInfo
{
	u8 templated_parameter_count;

	u8 parameter_count;

	bool is_func;
	
	bool has_templated_return_type;

	union
	{
		struct
		{
			TypeId type_id;
		} complete;

		struct
		{
			OpcodeId completion_id;
		} templated;
	} return_type;

	Maybe<ClosureId> closure_id;
};

struct MemberInfo
{
	TypeId type_id;

	Maybe<ForeverValueId> value_or_default_id;

	bool is_pub : 1;

	bool is_mut : 1;

	bool is_eval : 1;

	bool is_global : 1;

	u16 rank;

	s64 offset;
};

// Iterator over the members of a composite type.
// To create a `MemberIterator` call `members_of`.
// This iterator is resistant to the iterated type having its members or itself
// completed during iteration.
struct MemberIterator
{
	u8 kind_bits;

	u16 rank;

	u32 count;

	const IdentifierId* names;

	const void* types;

	Maybe<s64*> offsets;

	CoreData* core;
};



struct SignatureParameterInit
{
	IdentifierId name;

	union
	{
		struct
		{
			TypeId type_id;
			
			Maybe<ForeverValueId> default_id;
		} complete;

		struct
		{
			OpcodeId completion_id;
		} templated;
	};

	bool is_templated;

	bool is_eval;

	bool is_mut;
};

struct SignatureSealInfo
{
	Maybe<ClosureId> closure_id;

	union
	{
		struct
		{
			TypeId type_id;
		} complete;

		struct
		{
			OpcodeId completion_id;
		} templated;
	} return_type;

	bool has_templated_return_type;
};

struct TraitMemberInit
{
	IdentifierId name;

	OpcodeId type_completion_id;

	Maybe<OpcodeId> default_completion_id;

	bool is_mut;
};

struct ImplMemberInit
{
	IdentifierId name;

	OpcodeId completion_id;

	bool is_mut;
};

struct UserCompositeMemberInit
{
	IdentifierId name;

	TypeId type_id;

	Maybe<ForeverValueId> default_id;

	bool is_pub;

	bool is_mut;

	s64 offset;
};

struct UserCompositeSealInfo
{
	u64 size;

	u64 stride;

	u32 align;
};

struct FileCompositeMemberInit
{
	IdentifierId name;

	OpcodeId completion_id;

	bool is_pub;

	bool is_mut;
};



// Structural data for `Ptr`, `Slice` and `TailArray` types.
struct ReferenceType
{
	// `TypeId` of the referenced (or, in the case of `TailArray`, arguably
	// element) type.
	TypeId referenced_type_id;

	// For `Ptr`s, this indicates whether they are optional (`?` or `[?]`).
	// Otherwise this is `false`.
	bool is_opt;

	// For `Ptr`s, this indicates whether they are multi-pointers (`[*]` or
	// `[?]`). Otherwise this is `false`.
	bool is_multi;

	// Whether the referenced object(s) can be mutated through this reference.
	// `false` for `TailArray`.
	bool is_mut;

	// Explicit padding to allow consistent hashing without incurring undefined
	// behaviour by accessing structure padding.
	u8 unused_ = 0;
};

// Structural data for `Integer` and `Float` types.
struct NumericType
{
	// Number of bits in the type. Currently, this must be `8`, `16`, `32` or
	// `64` for `Integer` types, and `32` or `64` for `Float` types.
	u16 bits;

	// For `Integer` types, whether the value is signed. Always `true` for
	// `Float` types.
	bool is_signed;

	// Explicit padding to allow consistent hashing without incurring undefined
	// behaviour by accessing structure padding.
	u8 unused_ = 0;
};

// Structural data for `Array` types.
struct ArrayType
{
	// Number of elements in the array type.
	u64 element_count;

	// `TypeId` of the array's elements.
	Maybe<TypeId> element_type;

	// Explicit padding to allow consistent hashing without incurring undefined
	// behaviour by accessing structure padding.
	u32 unused_ = 0;
};



// Creates a "simple" type. This takes a `tag`, which
// must not expect any additional associated data.
// See `TypeTag` for details on which `tag`s are applicable.
TypeId type_create_simple(CoreData* core, TypeTag tag) noexcept;

// Creates a "numeric", i.e. `Integer` or `Float` type.
// This takes a tag indicating which type to create, along with a `NumericType`
// further describing it.
TypeId type_create_numeric(CoreData* core, TypeTag tag, NumericType attach) noexcept;

// Creates a "reference", i.e. `Ptr`, `Slice` or `TailArray` type.
// This takes a tag indicating which type to create, along with a
// `ReferenceType` further describing it.
TypeId type_create_reference(CoreData* core, TypeTag tag, ReferenceType attach) noexcept;

// Creates an "array", i.e. `Array` or `ArrayLiteral` type.
// This takes a tag indicating which type to create, along with an `ArrayType`
// further describing it.
TypeId type_create_array(CoreData* core, TypeTag tag, ArrayType attach) noexcept;



TypeId type_create_signature(CoreData* core, bool is_func, u8 parameter_count) noexcept;

void type_add_signature_parameter(CoreData* core, TypeId type_id, SignatureParameterInit init) noexcept;

TypeId type_seal_signature(CoreData* core, TypeId type_id, SignatureSealInfo seal_info) noexcept;

TypeId type_instantiate_templated_signature(CoreData* core, TypeId type_id) noexcept;

void type_complete_templated_signature_parameter(CoreData* core, TypeId type_id, u16 rank, TypeId member_type_id, Maybe<ForeverValueId> default_id) noexcept;



TypeId type_create_trait(CoreData* core, Range<IdentifierId> parameter_names) noexcept;



TypeId type_create_impl(CoreData* core, Range<TypeId> arguments, TypeId trait_type_id) noexcept;

bool type_add_impl_member(CoreData* core, TypeId type_id, ImplMemberInit init) noexcept;

TypeId type_seal_impl(CoreData* core, TypeId type_id) noexcept;



TypeId type_create_user_composite(CoreData* core, TypeTag tag, SourceId definition_site) noexcept;

bool type_add_user_composite_member(CoreData* core, TypeId type_id, UserCompositeMemberInit init) noexcept;

TypeId type_seal_user_composite(CoreData* core, TypeId type_id, UserCompositeSealInfo seal_info) noexcept;



TypeId type_create_file_composite(CoreData* core, u16 member_count, SourceId definition_site) noexcept;

void type_add_file_composite_member(CoreData* core, TypeId type_id, FileCompositeMemberInit init) noexcept;

void type_complete_file_composite_member(CoreData* core, TypeId type_id, u16 rank, TypeId member_type_id, ForeverValueId value_id) noexcept;



TypeRelation type_relation(CoreData* core, TypeId first_type_id, TypeId second_type_id) noexcept;

// Checks whether `type_id_a` and `type_id_b` refer to the same type or
// non-distinct aliases of the same type.
bool type_is_equal(CoreData* core, TypeId type_id_a, TypeId type_id_b) noexcept;



// Checks whether `type_metrics_from_id` may be called on `type_id`.
// This returns false iff `type_id` refers to a composite type that has a
// `disposition` of `TypeDisposition::User` and has not yet had
// `type_seal_composite` called on it.
bool type_has_metrics(CoreData* core, TypeId type_id) noexcept;

// Retrieves the `size`, `stride` and `align` of the type referenced by
// `type_id`. `size` and `alignment` may take any value, including `0`.
// `align` is guaranteed to be non-zero.
// This function must not be called on a composite type which has not received
// a previous call to `close_open_type`.
TypeMetrics type_metrics_from_id(CoreData* core, TypeId type_id) noexcept;

// Retrieves the `TypeTag` associated with the given `type_id`.
// For types created by `simple_type`, this is the value of the `tag`
// parameter.
// For types craeted by `alias_type`, this is the same as the `tag` of the
// aliased type.
// For types created by `create_open_type`, this is `TypeTag::Composite`.
TypeTag type_tag_from_id(CoreData* core, TypeId type_id) noexcept;

// Retrieves the number of members in the type referenced by `type_id`, which
// must reference a sealed composite type.
u32 type_member_count(CoreData* core, TypeId type_id) noexcept;

SignatureTypeInfo type_signature_info_from_id(CoreData* core, TypeId type_id) noexcept;

bool type_member_info_by_rank(CoreData* core, TypeId type_id, u16 rank, MemberInfo* out_info, OpcodeId* out_initializer) noexcept;

MemberByNameRst type_member_info_by_name(CoreData* core, TypeId type_id, IdentifierId name, MemberInfo* out_info, OpcodeId* out_initializer) noexcept;

IdentifierId type_member_name_by_rank(CoreData* core, TypeId type_id, u16 rank) noexcept;

// Do not call this function directly. Use `type_attachment_from_id` instead.
const void* type_attachment_from_id_raw(CoreData* core, TypeId type_id) noexcept;

// Retrieves the structural data associated with `type_id`, which must not
// refer to a composite type.
// The returned data is the same as that passed to the call to `simple_type`
// that created `type_id`, chaining through calls to `alias_type`.
template<typename T>
const T* type_attachment_from_id(CoreData* core, TypeId type_id) noexcept
{
	#ifndef NDEBUG
		const TypeTag tag = type_tag_from_id(core, type_id);

		if constexpr (is_same_cpp_type<T, ReferenceType>)
			ASSERT_OR_IGNORE(tag == TypeTag::Ptr || tag == TypeTag::Slice || tag == TypeTag::TailArray || tag == TypeTag::Variadic);
		else if constexpr (is_same_cpp_type<T, NumericType>)
			ASSERT_OR_IGNORE(tag == TypeTag::Integer || tag == TypeTag::Float);
		else if constexpr (is_same_cpp_type<T, ArrayType>)
			ASSERT_OR_IGNORE(tag == TypeTag::Array || tag == TypeTag::ArrayLiteral);
		else
			static_assert(false, "Unexpected attachment passed to type_attachment_from_id");
	#endif // !NDEBUG

	return reinterpret_cast<const T*>(type_attachment_from_id_raw(core, type_id));
}


// Retrieves a string representing the given `tag`.
// If `tag` is not an enumerant of `TypeTag`, it is treated as
// `TypeTag::INVALID`.
const char8* tag_name(TypeTag tag) noexcept;


// Creates an iterator over `type_id`s members.
// See `MemberIterator` for further details.
MemberIterator members_of(CoreData* core, TypeId type_id) noexcept;

// Retrieves the next element of `iterator`. This function may only be called
// exactly once after `has_next` called on the same iterator has returned
// `true`.
bool next(MemberIterator* it, MemberInfo* out_info, OpcodeId* out_initializer_id) noexcept;

// Checks whether `iterator` has an element to be returned by a future call to
// `next`. This call is idempotent.
bool has_next(const MemberIterator* it) noexcept;





struct GlobalValuePool;

struct CTValue
{
	MutRange<byte> bytes;

	u32 align : 31;

	u32 is_mut : 1;

	TypeId type;
};

enum class ForeverValueId : u32
{
	INVALID = 0,
};

enum class GlobalCompositeIndex : u16
{
	INVALID = 0,
};

enum class GlobalFileValueState : u8
{
	Complete,
	Uninitialized,
	Initializing,
};

struct ForeverCTValue
{
	CTValue value;

	ForeverValueId id;
};

GlobalCompositeIndex global_composite_reserve(CoreData* core, TypeId type_id, u16 definition_count) noexcept;

void global_composite_value_set_initializer(CoreData* core, GlobalCompositeIndex index, u16 rank, OpcodeId initializer) noexcept;

GlobalFileValueState global_composite_value_get(CoreData* core, GlobalCompositeIndex index, u16 rank, ForeverCTValue* out_value, OpcodeId* out_code) noexcept;

void global_composite_value_alloc_prepare(CoreData* core, GlobalCompositeIndex index, u16 rank, bool is_mut) noexcept;

ForeverValueId global_composite_value_alloc_initialized(CoreData* core, GlobalCompositeIndex index, u16 rank, CTValue initializer, TypeId* out_file_type) noexcept;

ForeverCTValue global_composite_value_alloc_uninitialized(CoreData* core, GlobalCompositeIndex index, u16 rank, TypeId type, TypeMetrics metrics, TypeId* out_file_type) noexcept;

void global_composite_value_alloc_initialized_complete(CoreData* core, GlobalCompositeIndex index, u16 rank) noexcept;

ForeverValueId forever_value_alloc_initialized(CoreData* core, bool is_mut, CTValue initializer) noexcept;

ForeverCTValue forever_value_alloc_uninitialized(CoreData* core, bool is_mut, TypeId type, TypeMetrics metrics) noexcept;

CTValue forever_value_get(CoreData* core, ForeverValueId id) noexcept;





// Parser, structuring source code into an Abstract Syntax Tree for further
// processing.
struct Parser;

// Parses `content` into an AST, returning its root node.
// `base_source_id` is the `SourceId` assigned to the first byte of `content`,
// with subsequent bytes receiving subsequent `SourceId`s.
// If `is_std` is `true`, builtins are allowed, otherwise they are disallowed.
// `filepath` is used for logging.
Maybe<AstNode*> parse(CoreData* core, Range<char8> content, SourceId source_id_base, bool is_std) noexcept;





struct OpcodePool;

enum class Opcode : u8
{
	INVALID = 0,
	EndCode,
	SetWriteCtx,
	ScopeBegin,
	ScopeEnd,
	ScopeEndPreserveTop,
	ScopeAllocTyped,
	ScopeAllocUntyped,
	FileGlobalAllocPrepare,
	FileGlobalAllocComplete,
	FileGlobalAllocTyped,
	FileGlobalAllocUntyped,
	PopClosure,
	LoadScope,
	LoadGlobal,
	LoadMember,
	LoadClosure,
	LoadBuiltin,
	ExecBuiltin,
	Signature,
	DynSignature,
	BindBody,
	PrepareArgs,
	ExecArgs,
	Call,
	Return,
	CompleteParamTypedNoDefault,
	CompleteParamTypedWithDefault,
	CompleteParamUntyped,
	ArrayPreInit,
	ArrayPostInit,
	CompositePreInit,
	CompositePostInit,
	If,
	IfElse,
	Loop,
	LoopFinally,
	Switch,
	AddressOf,
	Dereference,
	Slice,
	Index,
	BinaryArithmeticOp,
	Shift,
	BinaryBitwiseOp,
	BitNot,
	LogicalAnd,
	LogicalOr,
	LogicalNot,
	Compare,
	Negate,
	UnaryPlus,
	ArrayType,
	ReferenceType,
	Undefined,
	Unreachable,
	ValueInteger,
	ValueFloat,
	ValueString,
	ValueVoid,
	DiscardVoid,
	CheckTopVoid,
	CheckWriteCtxVoid,
	Trait,
	ImplSetSelf,
	ImplTraitCall,
	ImplBody,
	ImplMemberAllocPrepare,
	ImplMemberAllocExplicitType,
	ImplMemberAllocImplicitType,
	ImplMemberAllocComplete,
	GetSelf,
};

enum class OpcodeSliceKind : u8
{
	NoBounds,
	BeginBound,
	EndBound,
	BothBounds,
};

enum class OpcodeBinaryArithmeticOpKind : u8
{
	Add,
	Sub,
	Mul,
	Div,
	AddTC,
	SubTC,
	MulTC,
	Mod,
};

enum class OpcodeShiftKind : bool
{
	Left,
	Right,
};

enum class OpcodeBinaryBitwiseOpKind : u8
{
	And,
	Or,
	Xor,
};

enum class OpcodeCompareKind : u8
{
	LessThan,
	GreaterThan,
	LessThanOrEqual,
	GreaterThanOrEqual,
	NotEqual,
	Equal,
};

struct OpcodeReferenceTypeFlags
{
	u8 tag : 5;

	u8 is_opt : 1;

	u8 is_multi : 1;

	u8 is_mut : 1;
};

struct OpcodeSignatureFlags
{
	u8 is_func : 1;

	u8 has_templated_parameter_list : 1;

	u8 has_templated_return_type : 1;

	u8 unused_ : 5;
};

struct OpcodeSignaturePerParameterFlags
{
	u8 has_type : 1;

	u8 has_default : 1;

	u8 is_mut : 1;

	u8 is_eval : 1;

	u8 is_templated : 1;

	u8 unused_ : 3;
};

enum class OpcodeId : u32
{
	INVALID = 0,
};

const Maybe<Opcode*> opcodes_from_file_member_ast(CoreData* core, AstNode* node, GlobalCompositeIndex file_index, u16 rank) noexcept;

OpcodeId opcode_id_from_builtin(CoreData* core, Builtin builtin) noexcept;

OpcodeId id_from_opcode(CoreData* core, const Opcode* code);

const Opcode* opcode_from_id(CoreData* core, OpcodeId id) noexcept;

SourceId source_id_of_opcode(CoreData* core, const Opcode* code) noexcept;

const char8* tag_name(Opcode op) noexcept;




struct Interpreter;

enum class ClosureId : u32
{
	INVALID = 0,
};

// Builtin functions that the compiler needs to evaluate in a special way.
// These include functions that expose type, type creation, as well as
// introspection.
enum class Builtin : u8
{
	// Should not be used, reserved for sanity checking in debug builds.
	INVALID = 0,

	// Returns an integer type with the given bit width and signedness.
	// `let _integer = func(bits: CompInteger, is_signed: Bool) -> Type`
	Integer,

	// Returns a float type with the given bit width.
	// `let _float = func(bits: CompInteger) -> Type`
	Float,

	// Returns the `Type` type.
	// `let _type = func() -> Type`
	Type,

	// Returns the `Definition` type.
	// `let _definition = func() -> Type`
	Definition,

	// Returns the `TypeInfo` type.
	// `let _type_info = func() -> Type`
	TypeInfo,

	// Returns the type of its argument, or the value of its argument if it is
	// of type `Type`.
	// `let _typeof = func(arg: TypeInfo) -> Type`
	Typeof,

	// Returns the return type of its argument, which must refer to a function
	// or builtin type.
	// `let _returntypeof = func(arg: TypeInfo) -> Type`
	Returntypeof,

	// Returns the size of its argument.
	// `let _sizeof = func(arg: Type) -> CompInteger`
	Sizeof,

	// Returns the alignment of its argument.
	// `let _alignof = func(arg: Type) -> CompInteger`
	Alignof,

	// Returns the stride of its argument.
	// `let _strideof = func(arg: Type) -> CompInteger`
	Strideof,

	// TODO: This is not yet specified.
	Offsetof,

	// TODO: This is not yet specified.
	Nameof,

	// Imports the file specified by `path`, resolved relative to the path of
	// the file containing the `SourceId` `from`. If `is_std` is `true`,
	// builtins are allowed in the imported file, otherwise they are
	// disallowed.
	// The imported file's type is returned.
	// `let _import = func(path: []u8, is_std: Bool, from: u32) -> Type`
	Import,

	// Creates an empty, unsealed composite type with its `distinct_source_id`
	// set to `source_id`, returning it typed as a `TypeTag::TypeBuilder`.
	// `let _create_type_builder = func(source_id: u32) -> TypeBuilder`
	CreateTypeBuilder,

	// Adds a member to a `TypeBuilder`.
	// `let _add_type_member = func(builder: TypeBuilder, definition: Definition, offset: s64) -> Void`
	AddTypeMember,

	// Turns the given `TypeBuilder` into a `Type`, in effect calling
	// `type_seal_composite` with the given (range-checked) `size`, `align` and
	// `stride`, and then completing all of its members' types and values.
	// `let _complete_type = func(builder: TypeBuilder, size: u64, align: u64, stride: u64) -> Type`
	CompleteType,

	// Returns the `SourceId` of the call site as a `u32`.
	// `let _source_id = func() -> u32`
	SourceId,

	// Returns the `SourceId` of the call site's caller as a `u32`.
	// `let _caller_source_id = func() -> u32`
	CallerSourceId,

	// Returns the type of the passed definition, calculating it if
	// `has_pending_type` is set.
	// `let _definition_typeof = func(definition: Definition) -> Type`
	DefinitionTypeof,

	// Number of `Builtin`s, used for sizing arrays. Should not be used
	// otherwise.
	MAX,
};

bool import_prelude(CoreData* core, Range<char8> path) noexcept;

Maybe<TypeId> import_file(CoreData* core, Range<char8> path, bool is_std) noexcept;

bool evaluate_file_definition_by_name(CoreData* core, TypeId file_type, IdentifierId name) noexcept;

bool evaluate_all_file_definitions(CoreData* core, TypeId file_type) noexcept;

const char8* tag_name(Builtin builtin) noexcept;





struct CoreData;

CoreData* create_core_data(const Config* config) noexcept;

void release_core_data(CoreData* core) noexcept;

bool run_compilation(CoreData* core, bool main_is_std) noexcept;

#endif // CORE_INCLUDE_GUARD
