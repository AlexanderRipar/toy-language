#ifndef PASS_DATA_INCLUDE_GUARD
#define PASS_DATA_INCLUDE_GUARD

#include "../infra/common.hpp"
#include "../infra/alloc_pool.hpp"
#include "../infra/optptr.hpp"
#include "../infra/minos.hpp"



// Forward declarations.
// These are necessary because the modules defining them would otherwise appear
// after those using them.

// Id used to refer to a type in the `TypePool`. See `TypePool` for further
// information.
enum class TypeId : u32;

// A `TypeId` with one bit used for indicating mutability.
struct TypeIdWithAssignability
{
	u32 type_id : 31;

	u32 is_mut : 1;
};

// Id used to identify a particular source code location.
// This encodes the location's file, line and column. See `SourceReader` for
// further information.
enum class SourceId : u32;

// Id used to reference values with global lifetime.
// This includes the values of global variables, as well as default values. See
// `GlobalValuePool` for further information
enum class GlobalValueId : u32;





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
		Range<char8> filepath = range::from_literal_string("std.evl");
	} std;

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

			bool enable_prelude = false;

			Range<char8> log_filepath = {};
		} imports;

		struct
		{
			bool enable = false;

			Range<char8> log_filepath = {};
		} config;
	} logging;


	void* m_heap_ptr;

	Range<char8> m_config_filepath;
};

// Parses the file at `filepath` into a `Config` struct that is allocated into
// `alloc`. The file is expected to be in [TOML](https://toml.io) format.
Config* create_config(AllocPool* alloc, Range<char8> filepath) noexcept;

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
};

// Creates an `IdentifierPool`, allocating the necessary storage from `alloc`.
// Resources associated with the created `IdentifierPool` can be freed using
// `release_identifier_pool`.
IdentifierPool* create_identifier_pool(AllocPool* alloc) noexcept;

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

// Compares the values represented by two `CompIntegerValues`. Returns `true`
// if they are equal, `false` otherwise.
bool comp_integer_equal(CompIntegerValue lhs, CompIntegerValue rhs) noexcept;



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



// Adds the signed 64-bit values `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked(s64 a, s64 b, s64* out) noexcept;

// Subtracts the signed 64-bit values `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool sub_checked(s64 a, s64 b, s64* out) noexcept;

// Multiplies the signed 64-bit values `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool mul_checked(s64 a, s64 b, s64* out) noexcept;

// Adds the unsigned 64-bit values `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool add_checked(u64 a, u64 b, u64* out) noexcept;

// Subtracts the unsigned 64-bit values `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool sub_checked(u64 a, u64 b, u64* out) noexcept;

// Multiplies the unsigned 64-bit values `a` and `b`, returning `false` and
// leaving `*out` undefined if overflow occurred. Otherwise, `*out` is set to
// the result and `true` is returned.
bool mul_checked(u64 a, u64 b, u64* out) noexcept;





// Allocator for Abstract Syntax Trees (ASTs). These are created by parsing
// source files and subsequently annotated with type. and other information.
struct AstPool;

// Maximum nesting depth of AST nodes. Anything beyond this will result in an
// error during parsing.
// Note that this is actually pretty generous, and should really only
// potentially be a problem for changed `if ... else if ...` clauses or
// expressions of the form `a + b + ...`.
static constexpr s32 MAX_AST_DEPTH = 128;

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
	Block,
	If,
	For,
	ForEach,
	Switch,
	Case,
	Func,
	Trait,
	Impl,
	Catch,
	Identifer,
	LitInteger,
	LitFloat,
	LitChar,
	LitString,
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
	UOpTypeVar,
	UOpImpliedMember,
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
	OpMember,
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

	Definition_IsPub     = 0x01,
	Definition_IsMut     = 0x02,
	Definition_IsGlobal  = 0x04,
	Definition_IsAuto    = 0x08,
	Definition_IsUse     = 0x10,
	Definition_HasType   = 0x20,

	If_HasWhere          = 0x20,
	If_HasElse           = 0x01,

	For_HasCondition     = 0x01,
	For_HasWhere         = 0x20,
	For_HasStep          = 0x02,
	For_HasFinally       = 0x04,

	ForEach_HasWhere     = 0x20,
	ForEach_HasIndex     = 0x01,
	ForEach_HasFinally   = 0x02,

	Switch_HasWhere      = 0x20,

	Func_HasExpects      = 0x01,
	Func_HasEnsures      = 0x02,
	Func_IsProc          = 0x04,
	Func_HasReturnType   = 0x08,
	Func_HasBody         = 0x10,

	Trait_HasExpects     = 0x01,

	Impl_HasExpects      = 0x01,

	Catch_HasDefinition  = 0x01,

	Type_IsMut           = 0x02,
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
struct AstNode
{
	// Values for `internal_flags`. See that for further information.
	static constexpr u8 FLAG_LAST_SIBLING  = 0x01;
	static constexpr u8 FLAG_FIRST_SIBLING = 0x02;
	static constexpr u8 FLAG_NO_CHILDREN   = 0x04;

	// Indicates what kind of AST node is represented. This determines the
	// meaning of `flags` and the layout and semantics of the trailing data.
	AstTag tag;

	// Tag-dependent flags that contain additional information on the AST node.
	// In particular, for `AstTag::Builtin`, this contains a `Builtin`
	// enumerant instead of a combination of or'ed flags.
	AstFlag flags;

	// Number of four-byte units that this node and its trailing data encompasses.
	u8 data_dwords;

	// A combination of:
	//
	// - `FLAG_LAST_SIBLING`, meaning that this node has no further (following)
	//   siblings.
	// - `FLAG_FIRST_SIBLING`, meaning that this node has no preceding
	//   siblings, i.e., it is the first child of its parent. Note that this
	//   can be combined with `FLAG_LAST_SIBLING` for nodes with no siblings.
	// - `FLAG_NO_CHILDREN`, meaning that this node has no children.
	//
	// These values should not be read directly, and are instead used by
	// various helper functions and during AST construction.
	u8 internal_flags;

	// Number of four-byte units that are taken up by this node and its
	// children. Note that this is thus still meaningful if the node has no
	// next sibling (`internal_flags` contains `FLAG_LAST_SIBLING`). In this
	// case, it indicates the offset to an ancestor's next sibling.
	// This should not be read directly, and is instead used by various helper
	// functions.
	u32 next_sibling_offset;

	// Type of the expression represented by this node. If `is_assibnable` is
	// `true`, then the expression is assignable (i.e., is `mut` and has an
	// address).
	// Only contains a valid value after typechecking. Before that, it is set
	// to `with_assignability(INVALID_TYP_ID, false)`. 
	TypeIdWithAssignability type_id;

	// `SourceId` of the node. See `SourceReader` and further details.
	SourceId source_id;
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

// Result of a call to `next(AstPreorderIterator*)` or
// `next(AstPostorderIterator)`. See those functions for further details.
// Note that this must only be used after a call to
// `is_valid(AstIterationResult)`, with a return value of `false` indicating
// that the iterator is exhausted.
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

// Attachment of an `AstNode` with tag `AstTag::LitInteger`.
struct AstLitIntegerData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitInteger;

	#pragma pack(push)
	#pragma pack(4)
	// `CompIntegerValue` representing this literal's value.
	// This is under-aligned to 4 instead of 8 bytes since `AstNode`s - and
	// thus their attachments - are 4-byte aligned.
	CompIntegerValue value;
	#pragma pack(pop)
};

// Attachment of an `AstNode` with tag `AstTag::LitFloat`.
struct AstLitFloatData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitFloat;

	#pragma pack(push)
	#pragma pack(4)
	// `CompFloatValue` representing this literal's value.
	// This is under-aligned to 4 instead of 8 bytes since `AstNode`s - and
	// thus their attachments - are 4-byte aligned.
	CompFloatValue value;
	#pragma pack(pop)
};

// Attachment of an `AstNode` with tag `AstTag::LitChar`.
struct AstLitCharData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitChar;

	// Unicode codepoint representing this character literal's value.
	u32 codepoint;
};

// Attachment of an `AstNode` with tag `AstTag::Identifier`.
struct AstIdentifierData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Identifer;

	// `IdentifierId` of the identifier represented by this node.
	IdentifierId identifier_id;
};

// Attachment of an `AstNode` with tag `AstTag::LitString`.
struct AstLitStringData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::LitString;

	// `GlobalValueId` of the global `u8` array representing this string's
	// value.
	GlobalValueId string_value_id;
};

// Attachment of an `AstNode` with tag `AstTag::Definition`.
struct AstDefinitionData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Definition;

	// `IdentifierId` of the definition.
	IdentifierId identifier_id;
};

// Attachment of an `AstNode` with tag `AstTag::Func`.
struct AstFuncData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Func;

	// `TypeId` of the function. This is important as a the `type_id` of a
	// function signature without a body will be set to `Type`, meaning that
	// the function type information would have to be recreated upon every
	// evaluation without this additional attachment.
	TypeId func_type_id;
};

// Attachment of an `AstNode` with tag `AstTag::Block`.
struct AstBlockData
{
	// Tag used for sanity checks in debug builds.
	static constexpr AstTag TAG = AstTag::Block;

	// `TypeId` of the type representing the scope introduced by this block.
	// Only present after the AST has been typechecked, and `INVALID_TYPE_ID`
	// before that.
	TypeId scope_type_id;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::Func`. To obtain this for a given node, call `get_func_info`.
struct FuncInfo
{
	// `AstNode` with tag `AstTag::ParameterList` containing the function's
	// argument definitions as its children.
	AstNode* parameters;

	// Optional `AstNode` containing the function's return type expression if
	// it has one.
	OptPtr<AstNode> return_type;

	// Optional `AstNode` with tag `AstTag::Expects` containing the function's
	// `expects` clause if it has one.
	OptPtr<AstNode> expects;

	// Optional `AstNode` with tag `AstTag::Ensures` containing the function's
	// `ensures` clause if it has one.
	OptPtr<AstNode> ensures;

	// optional `AstNode` containing the function's body if it has one.
	OptPtr<AstNode> body;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::Definition`. To obtain this for a given node, call
// `get_defintion_info`.
struct DefinitionInfo
{
	// An optional `AstNode` containing the definition's explicit type
	// expression if it has one.
	OptPtr<AstNode> type;

	// An optional `AstNode` containing the definition's value expression if it
	// has one.
	OptPtr<AstNode> value;
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
	OptPtr<AstNode> alternative;

	// Optional `AstNode` with tag `AstTag::Where` containing the if's `where`
	// clause if it has one.
	OptPtr<AstNode> where;
};

// Neatly structured summary of the child structure of an `AstNode` with tag
// `AstTag::For`. To obtain this for a given node, call `get_for_info`.
struct ForInfo
{
	// `AstNode` containing the for's condition.
	AstNode* condition;

	// Optional `AstNode` containing the for's step if it has one.
	OptPtr<AstNode> step;

	// Optional `AstNode` with tag `AstTag::Where` containing the for's `where`
	// clause if it has one.
	OptPtr<AstNode> where;

	// `AstNode` containing the for's body.
	AstNode* body;

	// Optional `AstNode` containing the for's `finally` clause if it has one.
	OptPtr<AstNode> finally;
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
	OptPtr<AstNode> index;

	// `AstNode` containing the expression over which the foreach is iterating.
	AstNode* iterated;

	// Optional `AstNode` with tag `AstTag::Where` containing the foreach's
	// `where` clause if it has one.
	OptPtr<AstNode> where;

	// `AstNode` containing the foreach's body.
	AstNode* body;

	// `AstNode` containing the foreach's `finally` clause if it has one.
	OptPtr<AstNode> finally;
};

inline AstFlag operator|(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

inline AstFlag operator&(AstFlag lhs, AstFlag rhs) noexcept
{
	return static_cast<AstFlag>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

inline AstFlag& operator|=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs | rhs;

	return lhs;
}

inline AstFlag& operator&=(AstFlag& lhs, AstFlag rhs) noexcept
{
	lhs = lhs & rhs;

	return lhs;
}


AstPool* create_ast_pool(AllocPool* pool) noexcept;

void release_ast_pool(AstPool* asts) noexcept;

AstNodeId id_from_ast_node(AstPool* asts, AstNode* node) noexcept;

AstNode* ast_node_from_id(AstPool* asts, AstNodeId id) noexcept;


static inline AstNode* apply_offset_(AstNode* node, ureg offset) noexcept
{
	static_assert(sizeof(AstNode) % sizeof(u32) == 0 && alignof(AstNode) % sizeof(u32) == 0);

	return reinterpret_cast<AstNode*>(reinterpret_cast<u32*>(node) + offset);
}

static inline bool has_children(const AstNode* node) noexcept
{
	return (node->internal_flags & AstNode::FLAG_NO_CHILDREN) == 0;
}

static inline bool has_next_sibling(const AstNode* node) noexcept
{
	return (node->internal_flags & AstNode::FLAG_LAST_SIBLING) == 0;
}

static inline bool has_flag(AstNode* node, AstFlag flag) noexcept
{
	return (static_cast<u8>(node->flags) & static_cast<u8>(flag)) != 0;
}

static inline AstNode* next_sibling_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_next_sibling(node));

	return apply_offset_(node, node->next_sibling_offset);
}

static inline AstNode* first_child_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(has_children(node));

	return apply_offset_(node, node->data_dwords);
}

template<typename T>
static inline T* attachment_of(AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

	return reinterpret_cast<T*>(node + 1);
}

template<typename T>
static inline const T* attachment_of(const AstNode* node) noexcept
{
	ASSERT_OR_IGNORE(T::TAG == node->tag);

	ASSERT_OR_IGNORE(sizeof(T) + sizeof(AstNode) == node->data_dwords * sizeof(u32));

	return reinterpret_cast<const T*>(node + 1);
}


AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag) noexcept;

AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, AstTag tag, u8 attachment_dwords, const void* attachment) noexcept;

template<typename T>
static inline AstBuilderToken push_node(AstPool* asts, AstBuilderToken first_child, SourceId source_id, AstFlag flags, T attachment) noexcept
{
	static_assert(sizeof(T) % sizeof(u32) == 0);

	return push_node(asts, first_child, source_id, flags, T::TAG, sizeof(attachment) / sizeof(u32), &attachment);
}

AstNode* complete_ast(AstPool* asts) noexcept;


FuncInfo get_func_info(AstNode* func) noexcept;

DefinitionInfo get_definition_info(AstNode* definition) noexcept;

IfInfo get_if_info(AstNode* node) noexcept;

ForInfo get_for_info(AstNode* node) noexcept;

ForEachInfo get_foreach_info(AstNode* node) noexcept;


static inline bool is_valid(AstIterationResult result) noexcept
{
	return result.node != nullptr;
}

AstDirectChildIterator direct_children_of(AstNode* node) noexcept;

OptPtr<AstNode> next(AstDirectChildIterator* iterator) noexcept;

AstPreorderIterator preorder_ancestors_of(AstNode* node) noexcept;

AstIterationResult next(AstPreorderIterator* iterator) noexcept;

AstPostorderIterator postorder_ancestors_of(AstNode* node) noexcept;

AstIterationResult next(AstPostorderIterator* iterator) noexcept;


const char8* tag_name(AstTag tag) noexcept;





struct SourceReader;

enum class SourceId : u32
{
	INVALID = 0,
};

struct SourceFile
{
	minos::FileHandle file;

	AstNodeId ast_root;

	SourceId source_id_base;
};

struct SourceFileRead
{
	SourceFile* source_file;

	Range<char8> content;
};

struct SourceLocation
{
	Range<char8> filepath;

	u32 line_number;

	u32 column_number;

	u32 context_offset;

	u32 context_chars;

	char8 context[512];
};

SourceReader* create_source_reader(AllocPool* pool) noexcept;

void release_source_reader(SourceReader* reader) noexcept;

SourceFileRead read_source_file(SourceReader* reader, Range<char8> filepath) noexcept;

void release_read(SourceReader* reader, SourceFileRead read) noexcept;

SourceLocation source_location_from_ast_node(SourceReader* reader, AstNode* node) noexcept;

SourceLocation source_location_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

SourceFile* source_file_from_source_id(SourceReader* reader, SourceId source_id) noexcept;

Range<char8> source_file_path(SourceReader* reader, SourceFile* source_file) noexcept;





struct ErrorSink;

ErrorSink* create_error_sink(AllocPool* pool, SourceReader* reader, IdentifierPool* identifiers) noexcept;

void release_error_sink(ErrorSink* errors) noexcept;

NORETURN void source_error(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

NORETURN void vsource_error(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void source_warning(ErrorSink* errors, SourceId source_id, const char8* format, ...) noexcept;

void vsource_warning(ErrorSink* errors, SourceId source_id, const char8* format, va_list args) noexcept;

void print_error(const SourceLocation* location, const char8* format, va_list args) noexcept;





struct GlobalValuePool;

enum class GlobalValueId : u32
{
	INVALID = 0,
};

struct GlobalValue
{
	TypeIdWithAssignability type;

	u32 bytes;

	void* address;
};

GlobalValuePool* create_global_value_pool(AllocPool* alloc) noexcept;

void release_global_value_pool(GlobalValuePool* globals) noexcept;

GlobalValueId make_global_value(GlobalValuePool* globals, TypeIdWithAssignability type, u64 size, u32 align, const void* opt_initial_value) noexcept;

GlobalValue global_value_from_id(GlobalValuePool* globals, GlobalValueId value_id) noexcept;





struct TypePool;

enum class TypeId : u32
{
	INVALID = 0,
	CHECKING,
	NO_TYPE,
};

enum class TypeTag : u8
{
	INVALID = 0,
	Void,
	Type,
	Definition,
	CompInteger,
	CompFloat,
	Integer,
	Float,
	Boolean,
	Slice,
	Ptr,
	Array,
	Func,
	Builtin,
	Composite,
	CompositeLiteral,
	ArrayLiteral,
	TypeBuilder,
	Variadic,
	Divergent,
	Trait,
	TypeInfo,
	TailArray,
};

struct TypeMetrics
{
	u64 size;

	u64 stride;

	u32 align;
};

struct IncompleteMemberIterator
{
	void* structure;

	void* name;

	u16 rank;

	TypeId type_id;
};

struct MemberIterator
{
	void* structure;

	void* name;

	u16 rank;

	TypeId type_id;
};

union DelayableTypeId
{
	TypeId complete;

	AstNodeId pending;
};

union DelayableValueId
{
	GlobalValueId complete;

	AstNodeId pending;
};

struct MemberInfo
{
	IdentifierId name;

	SourceId source;

	DelayableTypeId type;

	DelayableValueId value;

	bool is_global : 1;

	bool is_pub : 1;

	bool is_use : 1;

	bool is_mut : 1;

	bool has_pending_type : 1;

	bool has_pending_value : 1;

	u16 rank;

	TypeId completion_context_type_id;

	TypeId surrounding_type_id;

	s64 offset;
};

struct MemberInit
{
	IdentifierId name;

	SourceId source;

	DelayableTypeId type;

	DelayableValueId value;

	TypeId lexical_parent_type_id;

	bool is_global : 1;

	bool is_pub : 1;

	bool is_use : 1;

	bool is_mut : 1;

	bool has_pending_type : 1;

	bool has_pending_value : 1;

	s64 offset;
};

struct ReferenceType
{
	TypeId referenced_type_id;

	bool is_opt;

	bool is_multi;

	bool is_mut;

	u8 unused_ = 0;
};

struct NumericType
{
	u16 bits;

	bool is_signed;

	u8 unused_ = 0;
};

struct ArrayType
{
	u64 element_count;

	TypeId element_type;

	u32 unused_ = 0;
};

struct FuncType
{
	TypeId return_type_id;

	u16 param_count;

	bool is_proc;

	TypeId signature_type_id;
};

TypePool* create_type_pool(AllocPool* alloc, GlobalValuePool* globals, ErrorSink* errors) noexcept;

void release_type_pool(TypePool* types) noexcept;


TypeId simple_type(TypePool* types, TypeTag tag, Range<byte> data) noexcept;

TypeId alias_type(TypePool* types, TypeId aliased_type_id, bool is_distinct, SourceId source_id, IdentifierId name_id) noexcept;

TypeId create_open_type(TypePool* types, TypeId lexical_parent_type_id, SourceId source_id) noexcept;

void add_open_type_member(TypePool* types, TypeId open_type_id, MemberInit member) noexcept;

void close_open_type(TypePool* types, TypeId open_type_id, u64 size, u32 align, u64 stride) noexcept;

void set_incomplete_type_member_type_by_rank(TypePool* types, TypeId open_type_id, u16 rank, TypeId member_type_id) noexcept;

void set_incomplete_type_member_value_by_rank(TypePool* types, TypeId open_type_id, u16 rank, GlobalValueId member_value_id) noexcept;


bool is_same_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;

bool type_can_implicitly_convert_from_to(TypePool* types, TypeId from_type_id, TypeId to_type_id) noexcept;

TypeId common_type(TypePool* types, TypeId type_id_a, TypeId type_id_b) noexcept;


IdentifierId type_name_from_id(const TypePool* types, TypeId type_id) noexcept;

SourceId type_source_from_id(const TypePool* types, TypeId type_id) noexcept;

TypeId lexical_parent_type_from_id(const TypePool* types, TypeId type_id) noexcept;

TypeMetrics type_metrics_from_id(TypePool* types, TypeId type_id) noexcept;

TypeTag type_tag_from_id(TypePool* types, TypeId type_id) noexcept;

bool type_member_info_by_name(TypePool* types, TypeId type_id, IdentifierId member_name_id, MemberInfo* out) noexcept;

bool type_member_info_by_rank(TypePool* types, TypeId type_id, u16 rank, MemberInfo* out) noexcept;

const void* simple_type_structure_from_id(TypePool* types, TypeId type_id) noexcept;


const char8* tag_name(TypeTag tag) noexcept;


IncompleteMemberIterator incomplete_members_of(TypePool* types, TypeId type_id) noexcept;

MemberInfo next(IncompleteMemberIterator* it) noexcept;

bool has_next(const IncompleteMemberIterator* it) noexcept;

MemberIterator members_of(TypePool* types, TypeId type_id) noexcept;

MemberInfo next(MemberIterator* it) noexcept;

bool has_next(const MemberIterator* it) noexcept;





struct Parser;

Parser* create_parser(AllocPool* pool, IdentifierPool* identifiers, GlobalValuePool* globals, TypePool* types, AstPool* asts, ErrorSink* errors, minos::FileHandle log_file) noexcept;

AstNode* parse(Parser* parser, Range<char8> content, SourceId base_source_id, bool is_std, Range<char8> filepath) noexcept;





struct Interpreter;

enum class Builtin : u8
{
	INVALID = 0,
	Integer,
	Float,
	Type,
	Typeof,
	Returntypeof,
	Sizeof,
	Alignof,
	Strideof,
	Offsetof,
	Nameof,
	Import,
	CreateTypeBuilder,
	AddTypeMember,
	CompleteType,
	SourceId,
	MAX,
};

static inline TypeIdWithAssignability with_assignability(TypeId type_id, bool is_assignable) noexcept
{
	return TypeIdWithAssignability{ static_cast<u32>(type_id), is_assignable };
}

static inline bool is_assignable(TypeIdWithAssignability id) noexcept
{
	return id.is_mut;
}

static inline TypeId type_id(TypeIdWithAssignability id) noexcept
{
	return TypeId{ id.type_id };
}

Interpreter* create_interpreter(AllocPool* alloc, Config* config, SourceReader* reader, Parser* parser, TypePool* types, AstPool* asts, IdentifierPool* identifiers, GlobalValuePool* globals, ErrorSink* errors, minos::FileHandle log_file, bool log_prelude) noexcept;

void release_interpreter(Interpreter* interp) noexcept;

TypeId import_file(Interpreter* interp, Range<char8> filepath, bool is_std) noexcept;

const char8* tag_name(Builtin builtin) noexcept;

#endif // PASS_DATA_INCLUDE_GUARD
