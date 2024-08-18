#ifndef PARSE_INCLUDE_GUARD
#define PARSE_INCLUDE_GUARD

#include "common.hpp"
#include "range.hpp"
#include "threading.hpp"
#include "error.hpp"

enum class Lexeme : u8
{
	INVALID = 0,
	END,
	ERROR,
	Identifier,
	ConstString,
	ConstChar,
	ConstInteger,
	ConstFloat,
	Let,            // let
	Mut,            // mut
	Pub,            // pub
	Global,         // global
	Eval,           // eval
	Func,           // func
	Proc,           // proc
	If,             // if
	Then,           // then
	Else,           // else
	For,            // for
	Each,           // each
	Loop,           // loop
	Continue,       // continue
	Break,          // break
	Finally,        // finally
	Switch,         // switch
	Case,           // case
	Fallthrough,    // fallthrough
	Try,            // try
	Catch,          // catch
	Undefined,      // undefined
	Unreachable,    // unreachable
	Return,         // return
	Pragma,         // #
	ParenBeg,       // (
	ParenEnd,       // )
	BracketBeg,     // [
	BracketEnd,     // ]
	CurlyBeg,       // {
	CurlyEnd,       // }
	WideArrow,      // =>
	ThinArrow,      // ->
	Element,        // <-
	Colon,          // :
	Semicolon,      // ;
	TripleDot,      // ...
	BitNot,         // ~
	LogNot,         // !
	Comma,          // ,
	Opt,            // ?
	At,             // @
	Dot,            // .
	Deref,          // .*
	Add,            // +
	AddTC,          // +:
	Sub,            // -
	SubTC,          // -:
	Mul,            // *
	MulTC,          // *:
	Div,            // /
	Mod,            // %
	ShiftL,         // <<
	ShiftR,         // >>
	BitAnd,         // &
	BitOr,          // |
	BitXor,         // ^
	LogAnd,         // &&
	LogOr,          // ||
	LogXor,         // ^^
	Eq,             // ==
	Ne,             // !=
	Lt,             // <
	Le,             // <=
	Gt,             // >
	Ge,             // >=
	Set,            // =
	SetAdd,         // +=
	SetAddTC,       // +:=
	SetSub,         // -=
	SetSubTC,       // -:=
	SetMul,         // *=
	SetMulTC,       // *:=
	SetDiv,         // /=
	SetMod,         // %=
	SetShiftL,      // <<=
	SetShiftR,      // >>=
	SetBitAnd,      // &=
	SetBitOr,       // |=
	SetBitXor,      // ^=
};

struct LexResult
{
	Lexeme lexeme;

	union LexValue
	{
		u32 identifier;

		u64 integer_value;

		double float_value;

		constexpr LexValue() noexcept : integer_value{ 0 } {}

		constexpr LexValue(u32 identifier) noexcept : identifier{ identifier } {}

		constexpr LexValue(u64 integer_value) noexcept : integer_value{ integer_value } {}

		constexpr LexValue(double float_value) noexcept : float_value{ float_value } {}
	} value;
};

struct IdentifierMapEntry
{
	u32 hash;

	u32 next;

	u8 char_count;

	#pragma warning(push)
	#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
	char8 chars[];
	#pragma warning(pop)

	void init(Range<char8> key, u32 key_hash) noexcept
	{
		hash = key_hash;

		ASSERT_OR_IGNORE(key.count() <= 0xFF);

		char_count = static_cast<u8>(key.count());

		memcpy(chars, key.begin(), key.count());
	}

	static constexpr u32 stride() noexcept
	{
		return 8;
	}

	static u32 get_required_strides(Range<char8> key) noexcept
	{
		return static_cast<u32>(offsetof(IdentifierMapEntry, chars) + key.count() + stride() - 1) / stride();
	}

	u32 get_used_strides() const noexcept
	{
		return static_cast<u32>(offsetof(IdentifierMapEntry, chars) + char_count + stride() - 1) / stride();
	}

	u32 get_hash() const noexcept
	{
		return hash;
	}

	bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
	{
		return hash == key_hash && memcmp(chars, key.begin(), key.count()) == 0;
	}

	void set_next(u32 index) noexcept
	{
		next = index;
	}

	u32 get_next() const noexcept
	{
		return next;
	}
};

using IdentifierMap = ThreadsafeMap2<Range<char8>, IdentifierMapEntry>;

enum class ParseFrame : u8
{
	START = 0,

	Definition_AfterLet = 1,
	Definition_AfterPub,
	Definition_AfterGlobal,
	Definition_AfterMut,
	Definition_AfterIdentifier,
	Definition_AfterColon,
	Definition_AfterSet,

	Expr_AfterCurly = 1,
};

struct ParseState
{
	u32 comment_nesting : 10;

	u32 is_line_comment : 1;

	u32 is_last : 1;

	u32 expr_expecting_operator : 1;

	u16 prefix_used;

	u16 prefix_capacity;

	char8* prefix;

	const char8* begin;

	const char8* end;

	IdentifierMap* identifiers;

	u32 thread_id;

	u32 frame_count;

	ParseFrame frames[64];
	
	struct
	{
		u32 operator_count;

		u32 operand_count;

		u32 operand_tail;

		Lexeme operators[64];

		u32 operand_sizes[64];

		byte rpn_operands[3652];
	} expr;
};

static_assert(sizeof(ParseState) == 4096);

enum class AstType : u8
{
	INVALID = 0,
	Definition,
	Pragma,
	Case,
	If,
	For,
	Foreach,
	Switch,
	Block,
	Label,
	Break,
	Continue,
	Return,
	Signature,
	VariableRef,
	ConstInteger,
	ConstFloat,
	ConstCharacter,
	ConstString,
	UopEval,
	UopTry,
	UopLogNot,
	UopBitNot,
	UopDeref,
	UopAddrof,
	UopTypeVariadic,
	UopTypeOptional,
	UopTypePtr,
	UopTypeMultiptr,
	UopTypeSlice,
	UopTypeTailarray,
	OpAdd,
	OpSub,
	OpMul,
	OpDiv,
	OpMod,
	OpAddOv,
	OpSubOv,
	OpMulOv,
	OpLogAnd,
	OpLogOr,
	OpLogXor,
	OpBitAnd,
	OpBitOr,
	OpBitXor,
	OpShiftLeft,
	OpShiftRight,
	OpCmpGt,
	OpCmpGe,
	OpCmgLt,
	OpCmgLe,
	OpCmpNe,
	OpCmpEq,
	OpCatch,
	OpTypeArray,
	OpArrayderef,
	OpMember,
	OpSet,
	OpSetAdd,
	OpSetSub,
	OpSetMul,
	OpSetDiv,
	OpSetMod,
	OpSetAddOv,
	OpSetSubOv,
	OpSetMulOv,
	OpSetBitAnd,
	OpSetBitOr,
	OpSetBitXor,
	OpSetShiftLeft,
	OpSetShiftRight,
};

struct AstHeader
{
	AstType type;

	// Definition
	//     0    = is_mut
	//     1    = is_pub
	//     2    = is_global
	//     3    = has_type
	//     4    = has_value
	// If
	//     0    = has_alternative
	//     4..7 = definition_count
	// For
	//     0    = has_condition ... only if has_foreach is NOT set
	//     1    = has_step ... only if has_foreach is NOT set
	//     2    = has_finally
	//     4..7 = definition_count
	// Foreach
	//     0    = has_index
	//     4..7 = definition_count
	// Switch
	//     0    = has_default
	//     4..7 = definition_count
	// Block
	//     -
	// Break
	//     0    = has_label
	//     1    = has_value
	// Continue
	//     -
	// Return
	//     0    = has_value
	// Call
	//     1..7 = argument_count
	// Pragma
	//     1..7 = argument_count
	// Signature
	//     0    = is_func ... if set, this is a func. Otherwise, it is a proc
	// ConstInteger
	//     0..6 = inline_value ... contains the integer's value if is_inline is set. In this case, no further data follows the header 
	//     7    = is_inline
	// ConstChar
	//     0..6 = inline_value ... contains the codepoints single codeunit if is_inline is set. In this case, no further data follows the header
	//     7    = is_inline

	u8 flags;

	// Any
	//     pragma_count: ?u16 ... if has_pragmas is set
	//     pragmas: []Pragma ... if has_pragmas is set. Contains pragma_count elements
	// Definition
	//     identifier_id: u32
	//     type: ?SimpleExpr ... if has_type is set
	//     value: ?Expr ... if has_value is set
	// If
	//     label: ?u32 ... if has_label is set
	//     definitions: []Definition ... definition_count elements (possibly 0)
	//     condition: SimpleExpr
	//     consequent: Expr
	//     alternative: ?Expr ... if has_alternative is set
	// For
	//     label: ?u32 ... if has_label is set
	//     definitions: []Definition ... definition_count elements (possibly 0)
	//     condition: ?SimpleExpr ... if has_condition is set
	//     step: ?SimpleExpr ... if has_step is set
	//     body: Expr
	//     finally: ?Expr ... if has_finally is set
	// Switch
	//     label: ?u32 ... if has_label is set
	//     definitions: []Definition ... definition_count elements (possibly 0)
	//     condition: SimpleExpr
	//     cases: []Case
	// Block
	//     label: ?u32 ... if has_label is set
	//     stmts: []Expr
	// Break
	//     identifier_id: ?u32 ... if has_label is set
	//     value: ?Expr ... if has_value is set
	// Continue
	//     -
	// Return
	//     value: ?Expr ... if has_value is set
	// Call
	//     called: Expr
	//     arguments: []SimpleExpr
	// Pragma
	//     identifier_id: u32
	//     arguments: []SimpleExpr
	// ConstFloat
	//     value: f64
	// ConstInteger
	//     value: ?(u32 | u64 | u128) ... if is_inline is not set. Type depends on dwords field
	// ConstChar
	//     value: ?[]char8 ... if is_inline is not set. Utf-8 encoded codeunits of the character's codepoint. At most 4 codeunits
	// ConstString
	//     value_index: u32 ... index of the string constant
	// Op*
	//     lhs: SimpleExpr
	//     rhs: SimpleExpr
	// Uop*
	//     operand: SimpleExpr
	u16 dwords;
};

s32 parse(ParseState* state) noexcept;

#endif // PARSE_INCLUDE_GUARD
