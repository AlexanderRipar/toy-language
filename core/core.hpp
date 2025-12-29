#ifndef CORE_INCLUDE_GUARD
#define CORE_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/opt.hpp"
#include "../infra/minos/minos.hpp"



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

enum class GlobalFileIndex : u16;

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





// Pool for allocating other core handles.
// Amortizes memory allocation overhead for fixed-size elements of the
// structures referenced by handles.
struct HandlePool;

// Creates a `HandlePool`.
//
// `reserve` indicates as the maximum cumulative size of all allocations made
// from the pool. Note that the actual allocatable size may be lower due to
// padding introduced by alignment requirements.
//
// `commit_increment` specifies how many bytes to grow the pool's committed
// memory region by each time new memory beyond that already committed is
// requested.
// Note that both parameters may be rounded up internally, e.g. to satisfy the
// system's allocation granularity.
HandlePool* create_handle_pool(u32 reserve, u32 commit_increment) noexcept;

// Releases the memory held by `pool`.
// This invalidates all handles that were previosly allocated from `pool`.
void release_handle_pool(HandlePool* pool) noexcept;

// Do not call this function directly. Use `alloc_handle_from_pool` instead.
// Allocates a chunk of `bytes` bytes of memory from `pool`, satisfying the
// alignment indicated `alignment`.
void* alloc_handle_from_pool_raw(HandlePool* pool, u32 bytes, u32 alignment) noexcept;

// Allocates memory sufficiently sized and aligned to hold an object of type
// `T` from `pool`.
template<typename T>
T* alloc_handle_from_pool(HandlePool* pool) noexcept
{
	return static_cast<T*>(alloc_handle_from_pool_raw(pool, sizeof(T), alignof(T)));
}





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
		struct
		{
			struct
			{
				bool enable = false;

				Range<char8> log_filepath = {};
			} asts;

			struct
			{
				bool enable = false;

				Range<char8> log_filepath = {};
			} opcodes;

			struct
			{
				bool enable = false;

				Range<char8> log_filepath = {};
			} types;
		} imports;

		struct
		{
			bool enable = false;

			Range<char8> log_filepath = {};
		} config;

		struct
		{
			bool enable = true;

			Range<char8> log_filepath = {};

			s64 source_tab_size = 4;
		} diagnostics;
	} logging;

	bool compile_all = false;

	void* m_heap_ptr;

	Range<char8> m_config_filepath;
};

// Parses the file at `filepath` into a `Config` struct that is allocated into
// `alloc`. The file is expected to be in [TOML](https://toml.io) format.
Config* create_config(HandlePool* alloc, Range<char8> filepath) noexcept;

// Releases resources associated with a `Config` that was previously returned
// from `create_config`.
void release_config(Config* config) noexcept;

// Prints the values in the given `Config` to `out` in a readable, JSON-like
// format.
void print_config(minos::FileHandle out, const Config* config) noexcept;

// Prints help and explanations for the supported config parameters to the
// standard error stream. Only parameters that are nested less than `depth` are
// included in the output. If `depth` is set to `0`, all parameters are
// printed, regardless of depth.
void print_config_help(u32 depth = 0) noexcept;





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

// Creates an `IdentifierPool`, allocating the necessary storage from `alloc`.
// Resources associated with the created `IdentifierPool` can be freed using
// `release_identifier_pool`.
IdentifierPool* create_identifier_pool(HandlePool* alloc) noexcept;

// Releases the resources associated with the given `IdentifierPool`.
void release_identifier_pool(IdentifierPool* identifiers) noexcept;

// Returns the unique `IdentifierId` that corresponds to the given `identifier`
// in `identifiers`. All calls with the same `IdentifierPool` and same
// byte-for-byte `identifier` are guaranteed to return the same `IdentifierId`.
// All calls to the same `IdentifierPool` with distinct `identifier`s are
// guaranteed to return distinct `IdentifierId`s.
IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept;

// Same as `id_from_identifier`, but additionally sets `*out_attachment` to the
// value previously set for the given `identifier` by
// `identifier_set_attachment`, or `0` if no attachment has been set.
// Note that this call will never return `INVALID_IDENTIFIER_ID`.
IdentifierId id_and_attachment_from_identifier(IdentifierPool* identifiers, Range<char8> identifier, u8* out_attachment) noexcept;

// Sets the attachment associated with `identifier` in the given
// `IdentifierPool` to the given `attachment`. The given `identifier` must not
// have previously had an attachment set.
// Additionally, `attachment` must not be `0`.
void identifier_set_attachment(IdentifierPool* identifiers, Range<char8> identifier, u8 attachment) noexcept;

// Returns the byte-sequence corresponding to the given `IdentifierId` in the
// given `IdentifierPool`. `id` must not be `INVALID_IDENTIFIER_ID` and must
// have been returned from a previous call to `id_from_identifier` or
// `id_and_attachment_from_identifier`.
Range<char8> identifier_name_from_id(const IdentifierPool* identifiers, IdentifierId id) noexcept;




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

	Definition_IsPub            = 0x01,
	Definition_IsMut            = 0x02,
	Definition_HasType          = 0x04,
	Definition_IsEval           = 0x08,

	If_HasWhere                 = 0x01,
	If_HasElse                  = 0x02,

	For_HasWhere                = 0x01,
	For_HasCondition            = 0x02,
	For_HasStep                 = 0x04,
	For_HasFinally              = 0x08,

	ForEach_HasWhere            = 0x01,
	ForEach_HasIndex            = 0x02,
	ForEach_HasFinally          = 0x04,

	Switch_HasWhere             = 0x20,

	Signature_HasExpects        = 0x01,
	Signature_HasEnsures        = 0x02,
	Signature_IsProc            = 0x04,
	Signature_HasReturnType     = 0x08,

	Trait_HasExpects            = 0x01,

	Impl_HasExpects             = 0x01,

	Catch_HasDefinition         = 0x01,

	OpSliceOf_HasBegin          = 0x01,
	OpSliceOf_HasEnd            = 0x02,

	Type_IsMut                  = 0x02,
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

struct alignas(8) AstFuncData
{
	static constexpr AstTag TAG = AstTag::Func;

	Maybe<ClosureListId> closure_list_id;

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
	Maybe<AstNode*> return_type;

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



// Creates an `AstPool`, allocating the necessary storage from `alloc`.
// Resources associated with the created `AstPool` can be freed using
// `release_ast_pool`.
AstPool* create_ast_pool(HandlePool* alloc) noexcept;

// Releases the resources associated with the given `AstPool`.
void release_ast_pool(AstPool* asts) noexcept;



// Converts `node` to its corresponding `AstNodeId`.
// `node` must be part of an AST created by a call to `complete_ast` on the
// same `AstPool`.
// Use `ast_node_from_id` to retrieve `node` from the returned `AstNodeId`.
AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept;

// Converts `id` to its corresponding `AstNode*`.
// `id` must have been obtained from a previous call to `id_from_ast_node` on
// the same `AstPool`.
AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept;



// Pushes a new `AstNode` with no attachment into `asts`'s ast builder.
// Nodes are pushed in post-order, meaning that children are pushed before
// parents, while siblings are pushed first to last.
// The created node will have `first_child` as its first child node. To create
// a node without children, pass `AstBuilderToken::NO_CHILDREN` for
// `first_child`.
// The node's `source_id`, `flags` and `tag` fields will be set to the given
// values. The node will not have an attachment.
// To complete the ast builder, call `complete_ast`.
AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept;

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
AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_dwords, const void* attachment) noexcept;

// Templated helper version of `push_node`.
// This makes it easier to create a node with a given `attachment`.
// Nodes are pushed in post-order, meaning that children are pushed before
// parents, while siblings are pushed first to last.
// `attachment` must be one of the `Ast*Data` structs. The created node's `tag`
// will be set to `T::TAG`.
// See the non-templated versions of `push_node` for further details.
template<typename T>
static AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, T attachment) noexcept
{
	static_assert(sizeof(T) % sizeof(u64) == 0);

	return push_node(asts, first_child, source_id, flags, T::TAG, sizeof(attachment) / sizeof(u64), &attachment);
}

// Completes the builder of `asts`, returning a pointer to the root node of the
// resulting ast.
// To fill the builder, use one of the `push_node` functions.
// After a call to this function, `asts`'s builder will be reset.
AstNode* complete_ast(AstPool* asts) noexcept;



ClosureList* alloc_closure_list(AstPool* asts, u16 entry_count) noexcept;

ClosureListId id_from_closure_list(AstPool* asts, ClosureList* closure_list) noexcept;

ClosureList* closure_list_from_id(AstPool* asts, ClosureListId id) noexcept;


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

SourceId source_id_of_ast_node(const AstPool* asts, const AstNode* node) noexcept;



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
	minos::FileHandle file;

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

	GlobalFileIndex file_index;

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

// Creates a `SourceReader`, allocating the necessary storage from `alloc`.
// Resources associated with the created `SourceReader` can be freed using
// `release_source_reader`.
SourceReader* create_source_reader(HandlePool* pool) noexcept;

// Releases the resources associated with the given ``SourceReader`.
void release_source_reader(SourceReader* reader) noexcept;

// Reads the file specified by `filepath` if it has not been read by a previous
// call. In case the file has already been read, the `source_file` member of
// the return value points to the `SourceFile` returned from the previous call,
// while the `content` member is empty.
// Due to the way this function interacts with `parse` in `import_file`,
// `source_file->ast` will be `AstNodeId::INVALID` if and only if this is
// the first read of the file (meaning `content` contains the file's contents).
// If this is a first read, `release_read` must be called once the file's
// content is no longer needed.
SourceFileRead read_source_file(SourceReader* reader, Range<char8> filepath) noexcept;

// Releases the contents and other associated resources of a `SourceFileRead`.
// The `SourceFile` pointed to by the `source_file` member is however left
// untouched.
void release_read(SourceReader* reader, SourceFileRead read) noexcept;

// Converts `source_id` to a `SourceLocation`.
// If `source_id` is `SourceId::INVALID`, a dummy location indicating the
// hard-coded prelude is returned.
SourceLocation source_location_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

// Retrieves the path to the file to which the given `source_id` belongs.
// This path may not be the same as that from which the file was imported, but
// can be used to open the file.
// `source_id` must not be `SourceId::INVALID`.
Range<char8> source_file_path_from_source_id(SourceReader* reader, SourceId source_id) noexcept;





// Sink for compiler errors and warnings.
// This takes care of the finicky bits of error reporting, providing a
// convenient `SourceId`-based, printf-like interface.
struct ErrorSink;

enum class CompileError
{
	INVALID = 0,
	ImplictConversionIntegerConstantExceedsTargetBounds,
	ImplicitConversionTypesCannotConvert,
	ImplicitConversionCompositeLiteralTargetIsMissingMember,
	ImplicitConversionCompositeLiteralTargetHasTooFewMembers,
	ImplicitConversionCompositeLiteralTargetMemberMappedTwice,
	ImplicitConversionCompositeLiteralMemberTypesCannotConvert,
	ImplicitConversionCompositeLiteralSourceIsMissingMember,
	ImplicitConversionLocationRequired,
	UnifyNoCommonArgumentType,
	UnifyNoCommonArrayElementType,
	CallNoSuchNamedParameter,
	CallArgumentMappedTwice,
	CallTooManyArgs,
	CallMissingArg,
	SliceOperatorInvalidLhsType,
	SliceOperatorInvalidIndexType,
	SliceOperatorMultiPtrElidedEndIndex,
	SliceOperatorIndexOutOfBounds,
	SliceOperatorIndicesReversed,
	SliceOperatorIndexTooLarge,
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
	ShiftRHSNegative,
	ShiftRHSTooLarge,
	MemberNoSuchName,
	MemberInvalidLhsType,
	MemberNonGlobalAccessedThroughType,
	CompareIncomparableType,
	CompareUnorderedType,
	SetLhsNotMutable,
	TypeArrayCountInvalidType,
	TypeArrayCountTooLarge,
	ArrayIndexLhsInvalidType,
	ArrayIndexRhsInvalidType,
	ArrayIndexRhsTooLarge,
	ArrayIndexOutOfBounds,
	BuiltinCompleteTypeAlignTooLarge,
	BuiltinCompleteTypeAlignZero,
	BuiltinCompleteTypeAlignNotPowTwo,
	UnreachableReached,
	ClosureTooLarge,
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

// Creates a `ErrorSink`, allocating the necessary storage from `alloc`.
// Resources associated with the created `ErrorSink` can be freed using
// `release_error_sink`.
ErrorSink* create_error_sink(HandlePool* pool, SourceReader* reader, IdentifierPool* identifiers, AstPool* asts, u8 source_tab_size, minos::FileHandle log_file) noexcept;

// Releases the resources associated with the given `ErrorSink`.
void release_error_sink(ErrorSink* errors) noexcept;

// Records the given `error` into the `ErrorSink`, associating it with the
// given `source_id`.
// Future calls to `get_errors` or `print_errors` will include an `ErrorRecord`
// with members corresponding to the given arguments.
void record_error(ErrorSink* errors, SourceId source_id, CompileError error) noexcept;

// Records the given `error` into the `ErrorSink`, associating it with the
// `SourceId` returned by
// `source_id_of_ast_node(<AstPool passed to create_error_sink>, source_node)`.
// Future calls to `get_errors` or `print_errors` will include an `ErrorRecord`
// with members corresponding to the given arguments.
void record_error(ErrorSink* errors, const AstNode* source_node, CompileError error) noexcept;

// Prints all errors added to the given `ErrorSink` by previous calls to
// `record_error` to its log file, in the order they were added.
// If there is no log file (i.e., if the `log_file` argument of
// `create_error_sink` was `minos::FileHandle{}`), then no errors are printed.
void print_errors(ErrorSink* errors) noexcept;

// Returns a range of `ErrorRecords` representing all previous calls to
// `record_error` on the given `ErrorSink`.
Range<ErrorRecord> get_errors(ErrorSink* errors) noexcept;

// Appends the message for the given `CompileError` to `dst`, prefixing it with
// the `location`.
// This is mainly intended for usage with `Config` parsing, as there is no
// `ErrorSink` available at that point.
void print_error(minos::FileHandle dst, const SourceLocation* location, CompileError error, u8 tab_size) noexcept;





struct LexicalAnalyser;

LexicalAnalyser* create_lexical_analyser(HandlePool* alloc, IdentifierPool* identifiers, AstPool* asts, ErrorSink* errors) noexcept;

void release_lexical_analyser(LexicalAnalyser* lex) noexcept;

bool set_prelude_scope(LexicalAnalyser* lex, AstNode* prelude, GlobalFileIndex file_index) noexcept;

bool resolve_names(LexicalAnalyser* lex, AstNode* root, GlobalFileIndex file_index) noexcept;





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
	Func,

	// Tag of builtin types. Its structure is represented by a `SignatureType`.
	Builtin,

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

enum class TypeDisposition : u8
{
	INVALID = 0,
	Initializer,
	File,
	ParameterList,
	User,
};

enum class TypeRelation : u8
{
	Equal,
	Convertible,
	Unrelated, 
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

// Iterator over the members of a composite type.
// To create a `MemberIterator` call `members_of`.
// This iterator is resistant to the iterated type having its members or itself
// completed during iteration.
struct MemberIterator
{
	const void* structure;

	const IdentifierId* names;

	const void* members;

	TypePool* types;

	u8 member_stride;

	bool is_indirect;

	u16 rank;
};

struct MemberInit
{
	IdentifierId name;

	TypeId type_id;

	Maybe<ForeverValueId> default_id;

	bool is_pub : 1;

	bool is_mut : 1;

	bool is_eval : 1;

	s64 offset;
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

struct SignatureType2
{
	TypeId parameter_list_type_id;

	union
	{
		TypeId type_id;

		OpcodeId completion_id;
	} return_type;

	Maybe<ClosureId> closure_id;

	bool is_func;

	bool has_templated_parameter_list;

	bool has_templated_return_type;

	u8 parameter_count;
};

enum class MemberByNameRst : u8
{
	Ok,
	Incomplete,
	NotFound,
};



// Creates a `TypePool`, allocating the necessary storage from `alloc`.
// Resources associated with the created `TypePool` can be freed using
// `release_type_pool`.
TypePool* create_type_pool(HandlePool* alloc) noexcept;

// Releases the resources associated with the given `TypePool`.
void release_type_pool(TypePool* types) noexcept;


// Creates a "simple" type. This takes a `tag`, which
// must not expect any additional associated data.
// See `TypeTag` for details on which `tag`s are applicable.
TypeId type_create_simple(TypePool* types, TypeTag tag) noexcept;

// Creates a "numeric", i.e. `Integer` or `Float` type.
// This takes a tag indicating which type to create, along with a `NumericType`
// further describing it.
TypeId type_create_numeric(TypePool* types, TypeTag tag, NumericType attach) noexcept;

// Creates a "reference", i.e. `Ptr`, `Slice` or `TailArray` type.
// This takes a tag indicating which type to create, along with a
// `ReferenceType` further describing it.
TypeId type_create_reference(TypePool* types, TypeTag tag, ReferenceType attach) noexcept;

// Creates an "array", i.e. `Array` or `ArrayLiteral` type.
// This takes a tag indicating which type to create, along with an `ArrayType`
// further describing it.
TypeId type_create_array(TypePool* types, TypeTag tag, ArrayType attach) noexcept;

// Creates a "signature", i.e. a `Builtin` or `Function` type.
// This takes a tag indicating which type to create, along with a
// `SignatureType` further describing it.
TypeId type_create_signature(TypePool* types, TypeTag tag, SignatureType2 attach) noexcept;

// Creates a composite type with no members that can have members added by
// calling `type_add_composite_member` on it.
//
// `tag` must be one of `TypeTag::Composite` and `TypeTag::CompositeLiteral`
// and determines whether the created type will be a composite or a composite
// literal.
//
// `lexical_parent_source_id` indicates the type lexically surrounding the
// newly created type, and is used during name lookup.
//
// `distinct_source_id` serves to distinguish structurally equivalent types.
// This is usually set to the type's creation site's source, meaning that
// structurally equivalent types created at that site are equivalent. However,
// this can be initialized differently to get different distinguishing
// behaviour, e.g. to the creation function's caller to create distinct types
// for each callsite.
//
// `disposition` indicates what sort of type is being created.
// - `TypeDisposition::User` indicates a user-defined type, including e.g. the
//   result of `import`. These types support calls to `_sizeof`, `_alignof`,
//   etc. However, all their members must be of concrete types - otherwise
//   layout queries would not generally make sense.
// - `TypeDisposition::Signature` indicates that the type corresponds to a
//   function signature. These types do not support layout queries, but may
//   contain dependently typed members - i.e., parameters.
// - `TypeDisposition::Block` indicates that the type corresponds to a block.
//   These types behave similarly to `TypeDisposition::Signature`, apart from
//   an additional constraint on accessing their members: Since blocks are
//   ordered, members - corresponding to definitions - may only be referred to
//   from points in the source after they are defined.
//
// Call `type_seal_composite` to stop the addition of further members.
// Call `type_add_composite_member` to add a member to this type.
// Call `type_set_composite_member_info` to set the type or value of a member
// that was added with `has_pending_type` or `has_pending_value` respectively
// set to `true`.
TypeId type_create_composite(TypePool* types, TypeTag tag, TypeDisposition disposition, SourceId distinct_source_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept;

// Seals an open composite type, preventing the addition of further members and
// setting the type's metrics (`size`, `align` and `stride`).
// Members that have already been added but that have not yet had their type or
// value set can still be manipulated via `type_set_composite_member_info` even
// after this call returns.
// For types created with `TypeDisposition::Block` or
// `TypeDisposition::Signature`, `size`, `align` and `stride` must all be `0`,
// as their values are tracked implicitly as members are added and completed.
// Otherwise, `size` and `stride` are unrestricted, while `align` must not be
// `0`.
TypeId type_seal_composite(TypePool* types, TypeId type_id, u64 size, u32 align, u64 stride) noexcept;

// Adds a member to the open composite type referenced by `open_type_id`.
// The member is initialized with the data in `member`.
// Returns `true` if the member was successfully added, and `false` if there
// was a name collision with an existing member.
bool type_add_composite_member(TypePool* types, TypeId type_id, MemberInit init) noexcept;

void type_add_file_member(TypePool* types, TypeId type_id, IdentifierId name, OpcodeId completion_opcode, bool is_pub, bool is_mut) noexcept;

void type_add_templated_parameter_list_member(TypePool* types, TypeId type_id, IdentifierId name, OpcodeId completion_opcode, bool is_eval, bool is_mut) noexcept;

void type_set_templated_parameter_list_member_info(TypePool* types, TypeId type_id, u16 rank, TypeId member_type_id, Maybe<ForeverValueId> member_default_id) noexcept;

void type_set_file_member_info(TypePool* types, TypeId type_id, u16 rank, TypeId member_type_id, ForeverValueId value_id) noexcept;

TypeId type_copy_composite(TypePool* types, TypeId type_id, u32 initial_member_capacity, bool is_fixed_member_capacity) noexcept;

// Retrieves the number of members in the type referenced by `type_id`, which
// must reference a sealed composite type.
u32 type_get_composite_member_count(TypePool* types, TypeId type_id) noexcept;

void type_discard(TypePool* types, TypeId type_id) noexcept;



TypeRelation type_relation_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept;

// Checks whether `type_id_a` and `type_id_b` refer to the same type or
// non-distinct aliases of the same type.
bool type_is_equal(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;

// Checks whether `from_type_id` can be implicitly converted to `to_type_id`.
// If `from_type_id` and `to_type_id` refer to the same type according to
// `type_is_equal`, implicit conversion is allowed.
// Otherwise the allowed implicit conversion are as follows:
//
// |   `from`    |   `to`    |                    Notes & Semantics                    |
// |-------------|-----------|---------------------------------------------------------|
// | CompInteger | Integer   | Works for all `Integer` types, dynamic check for fit.   |
// | CompFloat   | Float     | Works for all `Float` types.                            |
// | Array       | Slice     | Creates a slice over the whole array.                   |
// | mut Slice   | Slice     |                                                         |
// | mut Ptr     | Ptr       |                                                         |
// | multi Ptr   | Ptr       |                                                         |
// | Ptr         | opt Ptr   |                                                         |
// | Divergent   | Anything  | `Divergent` can be converted to any other type.         |
// | Anything    | TypeInfo  | Any type can become a `TypeInfo` - this is its purpose. |
//
// Note that pointer conversions can occur together as one implicit conversion.
// E.g., `[*]mut u32` can be converted to `?u32`.
bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept;

// Returns the common type of `type_id_a` and `type_id_b`.
// If `type_id_a` and `type_id_b` are considered the equivalent by
// `type_is_equal`, the lower of the two is returned. This is done to ensure
// repeatability regardless of operand order.
// Otherwise, if one of the types is implicitly convertible to the other, the
// converted-to type is returned. E.g., when `type_id_a` is a `CompInteger` and
// `type_id_b` is an `Integer`, `type_id_b` is returned.
// If `type_id_a` and `type_id_b` do not share a common type, `TypeId::INVALID`
// is returned.
Maybe<TypeId> type_unify(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;



TypeDisposition type_disposition_from_id(TypePool* types, TypeId type_id) noexcept;

// Checks whether `type_metrics_from_id` may be called on `type_id`.
// This returns false iff `type_id` refers to a composite type that has a
// `disposition` of `TypeDisposition::User` and has not yet had
// `type_seal_composite` called on it. 
bool type_has_metrics(TypePool* types, TypeId type_id) noexcept;

// Retrieves the `size`, `stride` and `align` of the type referenced by
// `type_id`. `size` and `alignment` may take any value, including `0`.
// `align` is guaranteed to be non-zero.
// This function must not be called on a composite type which has not received
// a previous call to `close_open_type`.
TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept;

// Retrieves the `TypeTag` associated with the given `type_id`.
// For types created by `simple_type`, this is the value of the `tag`
// parameter.
// For types craeted by `alias_type`, this is the same as the `tag` of the
// aliased type.
// For types created by `create_open_type`, this is `TypeTag::Composite`.
TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept;

bool type_member_info_by_rank(TypePool* types, TypeId type_id, u16 rank, MemberInfo* out_info, OpcodeId* out_initializer);

MemberByNameRst type_member_info_by_name(TypePool* types, TypeId type_id, IdentifierId name, MemberInfo* out_info, OpcodeId* out_initializer) noexcept;

IdentifierId type_member_name_by_rank(TypePool* types, TypeId type_id, u16 rank) noexcept;

// Do not call this function directly. Use `type_attachment_from_id` instead.
const void* type_attachment_from_id_raw(TypePool* types, TypeId type_id) noexcept;

// Retrieves the structural data associated with `type_id`, which must not
// refer to a composite type.
// The returned data is the same as that passed to the call to `simple_type`
// that created `type_id`, chaining through calls to `alias_type`.
template<typename T>
const T* type_attachment_from_id(TypePool* types, TypeId type_id) noexcept
{
	#ifndef NDEBUG
		const TypeTag tag = type_tag_from_id(types, type_id);

		if constexpr (is_same_cpp_type<T, ReferenceType>)
			ASSERT_OR_IGNORE(tag == TypeTag::Ptr || tag == TypeTag::Slice || tag == TypeTag::TailArray || tag == TypeTag::Variadic);
		else if constexpr (is_same_cpp_type<T, NumericType>)
			ASSERT_OR_IGNORE(tag == TypeTag::Integer || tag == TypeTag::Float);
		else if constexpr (is_same_cpp_type<T, ArrayType>)
			ASSERT_OR_IGNORE(tag == TypeTag::Array || tag == TypeTag::ArrayLiteral);
		else if constexpr (is_same_cpp_type<T, SignatureType2>)
			ASSERT_OR_IGNORE(tag == TypeTag::Func || tag == TypeTag::Builtin);
		else
			static_assert(false, "Unexpected attachment passed to type_attachment_from_id");
	#endif // !NDEBUG

	return reinterpret_cast<const T*>(type_attachment_from_id_raw(types, type_id));
}


// Retrieves a string representing the given `tag`.
// If `tag` is not an enumerant of `TypeTag`, it is treated as
// `TypeTag::INVALID`.
const char8* tag_name(TypeTag tag) noexcept;


// Creates an iterator over `type_id`s members.
// See `MemberIterator` for further details.
MemberIterator members_of(TypePool* types, TypeId type_id) noexcept;

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

enum class GlobalFileIndex : u16
{
	INVALID = 0,
};

struct ForeverCTValue
{
	CTValue value;

	ForeverValueId id;
};

GlobalValuePool* create_global_value_pool(HandlePool* handles) noexcept;

void release_global_value_pool(GlobalValuePool* globals) noexcept;

GlobalFileIndex file_values_reserve(GlobalValuePool* globals, TypeId file_type_id, u16 definition_count) noexcept;

void file_value_set_initializer(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, OpcodeId initializer) noexcept;

TypeId type_id_from_global_file_index(GlobalValuePool* globals, GlobalFileIndex file_index) noexcept;

bool file_value_get(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, ForeverCTValue* out_value, OpcodeId* out_code) noexcept;

ForeverValueId file_value_alloc_initialized(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, bool is_mut, CTValue initializer) noexcept;

ForeverCTValue file_value_alloc_uninitialized(GlobalValuePool* globals, GlobalFileIndex file_index, u16 rank, bool is_mut, TypeId type, TypeMetrics metrics) noexcept;

ForeverValueId forever_value_alloc_initialized(GlobalValuePool* globals, bool is_mut, CTValue initializer) noexcept;

ForeverCTValue forever_value_alloc_uninitialized(GlobalValuePool* globals, bool is_mut, TypeId type, TypeMetrics metrics) noexcept;

CTValue forever_value_get(GlobalValuePool* globals, ForeverValueId id) noexcept;





// Parser, structuring source code into an Abstract Syntax Tree for further
// processing.
struct Parser;

// Creates a `Parser`, allocating the necessary storage from `alloc`.
// Resources associated with the created `Parser` can be freed using
// `release_parser`.
Parser* create_parser(HandlePool* pool, IdentifierPool* identifiers, GlobalValuePool* globals, TypePool* types, AstPool* asts, ErrorSink* errors) noexcept;

// Releases the resources associated with the given `Parser`.
void release_parser(Parser* parser) noexcept;

// Parses `content` into an AST, returning its root node.
// `base_source_id` is the `SourceId` assigned to the first byte of `content`,
// with subsequent bytes receiving subsequent `SourceId`s.
// If `is_std` is `true`, builtins are allowed, otherwise they are disallowed.
// `filepath` is used for logging.
Maybe<AstNode*> parse(Parser* parser, Range<char8> content, SourceId source_id_base, bool is_std) noexcept;





struct OpcodePool;

// let f = func(T: Type, t: T) -> T => t
//
// [SignatureBeginGeneric 2]
// [LoadGlobal `Type`]
// [DynSignatureCloseOver]
// [EndSignatureClosure]
// [LoadSignatureClosed 0 <Type>]
// [ParamInit `T` .type]
// [LoadMember 0 0 <T>]
// [ParamInit `t` .type]
// [LoadMember 0 0 <T>]
// [SignatureEndGeneric]
// [CodeRef {addr} <t>]
// [Bind]
//
// [LoadLocal 0 0 <t>]
// [End]
//
//
// let g = func(T: Type, t: T) -> (func() -> T) => (func() -> T => t)
//
// [SignatureBeginGeneric .func 2]
// [LoadGlobal `Type`]
// [DynSignatureCloseOver]
// [EndSignatureClosure]
// [LoadSignatureClosed 0 <Type>]
// [ParamInit `T` .type]
// [LoadMember 0 0 <T>]
// [ParamInit `t` .type]
// [SignatureBegin .func 1]
// [LoadMember 0 0 <T>]
// [SignatureEnd]
// [SignatureEndGeneric]
// [CodeRef {addr} <(func() -> T => t)]
// [BindBody]
// [End]
//
// [SignatureBegin .func]
// [LoadMember 1 0 <T>]
// [SignatureEnd]
// [ClosureBegin]
// [LoadMember t]
// [CloseOver]
// [ClosureEnd]
// [CodeRef {addr} <t>]
// [BindBodyWithClosure]
// [End]
//
// [LoadClosed 0 <t>]
// [End]
//
//
// let c = g(u32, 1)
//
// [LoadGlobal 0 1 <g>]
// [LoadGlobal 1 {x} <u32>]
// [LoadInteger 1]
// [Bind 2 #next 2]
// [Call]
// [End]
//
//
// let one = c()
//
// [LoadGlobal 0 2 <c>]
// [Bind 0]
// [Call]
// [End]
//
//
// let c2 = g(.t = 1, .T = u64)
//
// [LoadGlobal 0 1 <g>]
// [LoadInteger 1]
// [LoadGlobal 1 {y} <u64>]
// [Bind 2 #name `t` #name `T`]
// [Call]
// [InitGlobalAndEnd 0 4 <c2>]
//
//
// let arr = .[0, 1, c()]
//
// [LoadInteger 0]
// [LoadInteger 1]
// [LoadGlobal 0 2 <c>]
// [Bind 0]
// [Call]
// [ArrayInitializer 3]
// [ElemNext 3]
// [ArrayEnd]
// [End]
//
//
// let fn = func(x : u32) -> u32 = if x == 0 then 0 else fn(x - 1)
//
// [SignatureBegin 1]
// [LoadGlobal 1 {x} <u32>]
// [InitParam `x` .type]
// [LoadGlobal 1 {x} <u32>]
// [SignatureEnd]
// [CodeRef {addr} <if x == 0 then 0 else fn(x - 1)>]
//
// [LoadMember 0 0 <x>]
// [LoadInteger 0]
// [CmpEqual]
// [IfElse {addr1} <0> {addr2} <f(x - 1)>]
// [End]
//
// [LoadInteger 0]
// [End]
//
// [LoadGlobal 0 6 <fn>]
// [LoadMember 0 0 <x>]
// [LoadInteger 1]
// [Add]
// [Bind 1 #next 1]
// [Call]
// [End]
enum class Opcode : u8
{
	INVALID = 0,
	EndCode,
	SetWriteCtx,
	ScopeBegin,
	ScopeEnd,
	ScopeAllocTyped,
	ScopeAllocUntyped,
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
	BindBodyWithClosure,
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

struct OpcodeEffects
{
	s32 values_diff;

	s32 scopes_diff;

	s32 write_ctxs_diff;

	s32 closures_diff;
};

OpcodePool* create_opcode_pool(HandlePool* handles, AstPool* asts) noexcept;

void release_opcode_pool(OpcodePool* opcodes) noexcept;

const Maybe<Opcode*> opcodes_from_file_member_ast(OpcodePool* opcodes, AstNode* node, GlobalFileIndex file_index, u16 rank) noexcept;

OpcodeId opcode_id_from_builtin(OpcodePool* opcodes, Builtin builtin) noexcept;

OpcodeId id_from_opcode(OpcodePool* opcodes, const Opcode* code);

const Opcode* opcode_from_id(OpcodePool* opcodes, OpcodeId id) noexcept;

OpcodeEffects opcode_effects(const Opcode* code) noexcept;

SourceId source_id_of_opcode(OpcodePool* opcodes, const Opcode* code) noexcept;

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

Interpreter* create_interpreter(HandlePool* handles, AstPool* asts, TypePool* types, GlobalValuePool* globals, OpcodePool* opcodes, SourceReader* reader, Parser* parser, IdentifierPool* identifiers, LexicalAnalyser* lex, ErrorSink* errors, minos::FileHandle asts_log_file, minos::FileHandle imported_opcodes_log_file, minos::FileHandle types_log_file) noexcept;

void release_interpreter(Interpreter* interp) noexcept;

bool import_prelude(Interpreter* interp, Range<char8> path) noexcept;

Maybe<TypeId> import_file(Interpreter* interp, Range<char8> path, bool is_std) noexcept;

bool evaluate_file_definition_by_name(Interpreter* interp, TypeId file_type, IdentifierId name) noexcept;

bool evaluate_all_file_definitions(Interpreter* interp, TypeId file_type) noexcept;

const char8* tag_name(Builtin builtin) noexcept;




struct CoreData
{
	HandlePool* alloc;

	Config* config;

	IdentifierPool* identifiers;

	SourceReader* reader;

	ErrorSink* errors;

	GlobalValuePool* globals;

	TypePool* types;

	AstPool* asts;

	Parser* parser;

	OpcodePool* opcodes;

	LexicalAnalyser* lex;

	Interpreter* interp;
};

CoreData create_core_data(Range<char8> config_filepath) noexcept;

void release_core_data(CoreData* core) noexcept;

bool run_compilation(CoreData* core, bool main_is_std) noexcept;

#endif // CORE_INCLUDE_GUARD
