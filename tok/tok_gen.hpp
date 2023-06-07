#ifndef TOK_GEN_INCLUDE_GUARD
#define TOK_GEN_INCLUDE_GUARD

#include <vector>
#include "../util/vec.hpp"
#include "../util/strview.hpp"
#include "../util/types.hpp"

struct Token
{
	enum class Tag
	{
		INVALID = 0,
		Ident,
		OpAdd,		// Start of range relied on for binary operators. These must remain continuous and in the same order.
		OpDiv,
		OpMod,
		OpBitOr,
		OpBitXor,
		OpShiftL,
		OpShiftR,
		OpLogAnd,
		OpLogOr,
		OpCmpLt,
		OpCmpLe,
		OpCmpGt,
		OpCmpGe,
		OpCmpNe,
		OpCmpEq,
		Dot,
		OpSub,		// Start of range relied on for unary operators. These must remain continuous and in the same order.
		OpMul_Ptr,	// yes, these overlap :)
		OpBitAnd_Ref,	// End of range relied on for binary operators.
		UOpBitNot,
		UOpLogNot,
		UOpDeref,
		TripleDot,
		Try,		// End of range relied on for unary operators.
		Catch,
		Index,
		Set,
		SetAdd,
		SetSub,
		SetMul,
		SetDiv,
		SetMod,
		SetBitAnd,
		SetBitOr,
		SetBitXor,
		SetShiftL,
		SetShiftR,
		Colon,
		Semicolon,
		Comma,
		ArrowLeft,
		ArrowRight,
		FatArrowRight,
		SquiggleBeg,
		SquiggleEnd,
		BracketBeg,
		BracketEnd,
		ParenBeg,
		ParenEnd,
		LitString,
		LitChar,
		LitInt,
		LitFloat,
		LitBadNumber,
		Hashtag,
		Comment,
		IncompleteComment,
		If,
		Then,
		Else,
		For,
		Do,
		Break,
		Finally,
		Switch,
		Case,
		Return,
		Defer,
		DoubleColon,
		Proc,
		Func,
		Trait,
		Module,
		Impl,
		Mut,
		Pub,
		Global,
		Undefined,
	} tag;

	u32 line_number;

	strview data;

	strview type_strview() const noexcept;

	strview data_strview() const noexcept;
};

vec<Token> tokenize(strview data, bool include_comments) noexcept;

#endif // TOK_GEN_INCLUDE_GUARD
