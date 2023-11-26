#ifndef TOKEN_INCLUDE_GUARD
#define TOKEN_INCLUDE_GUARD

#include "common.hpp"
#include <cassert>

struct Token
{
private:

	u32 m_rep;

public:

	static constexpr u8 MAX_TAG_BITS = 6;

	enum class Tag : u8
	{
		EMPTY = 0,
		KwdIf,      // if
		KwdThen,    // then
		KwdElse,    // else
		KwdFor,     // for
		KwdDo,      // do
		KwdFinally, // finally
		KwdSwitch,  // switch
		KwdCase,    // case
		KwdTry,     // try
		KwdCatch,   // catch
		KwdDefer,   // defer
		KwdFunc,    // func
		KwdProc,    // proc
		KwdTrait,   // trait
		KwdImpl,    // impl
		KwdWhere,   // where
		KwdLet,     // let
		KwdMut,     // mut
		KwdAuto,    // auto
		KwdPub,     // pub
		KwdGlobal,  // global
		OpAdd,      // +
		OpSub,      // -
		OpMul,      // *
		OpDiv,      // /
		OpMod,      // %
		OpAnd,      // &
		OpOr,       // |
		OpXor,      // ^
		OpShl,      // <<
		OpShr,      // >>
		OpLogAnd,   // &&
		OpLogOr,    // ||
		OpMember,   // .
		OpLt,       // <
		OpGt,       // >
		OpLe,       // <=
		OpGe,       // >=
		OpNe,       // !=
		OpEq,       // ==
		OpSet,      // =
		OpSetAdd,   // +=
		OpSetSub,   // -=
		OpSetMul,   // *=
		OpSetDiv,   // /=
		OpSetMod,   // %=
		OpSetAnd,   // &=
		OpSetOr,    // |=
		OpSetXor,   // ^=
		OpSetShl,   // <<=
		OpSetShr,   // >>=
		UOpAddr     = OpAnd,
		UOpDeref,   // .*
		UOpNot,     // ~
		UOpLogNot,  // !
		TypPtr      = OpMul,
		TypOptPtr,  // ?
		TypVar,     // ...
		Colon,      // :
		Comma,      // ,
		ArrowL,     // <-
		ArrowR,     // ->
		BracketL,   // [
		BracketR,   // ]
		SquiggleL,  // {
		SquiggleR,  // }
		ParenL,     // (
		ParenR,     // )
		LitInteger, // ( '0' - '9' )+
		LitFloat,   // ( '0' - '9' )+ '.' ( '0' - '9' )+
		LitChar,    // '\'' .* '\''
		LitString,  // '"' .* '"'
		Ident,      // ( 'a' - 'z' | 'A' - 'Z' ) ( 'a' - 'z' | 'A' - 'Z' | '0' - '9' | '_' )*
		MAX,
	};

	static_assert(static_cast<u8>(Tag::MAX) <= (1 << MAX_TAG_BITS));

	Token() noexcept = default;

	Token(Tag tag) noexcept : m_rep{ static_cast<u32>(tag) } {}

	Token(Tag tag, s32 index) noexcept : m_rep{ static_cast<u32>(tag) | static_cast<u32>(index << MAX_TAG_BITS) }
	{
		assert(index >= 0 && index < (1 << (32 - MAX_TAG_BITS)));
	}

	Tag tag() const noexcept
	{
		return static_cast<Tag>(m_rep & ((1 << MAX_TAG_BITS) - 1));
	}

	s32 index() const noexcept
	{
		return static_cast<s32>(m_rep >> MAX_TAG_BITS);
	}
};

#endif // TOKEN_INCLUDE_GUARD
