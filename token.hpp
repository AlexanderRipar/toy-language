#ifndef TOKEN_INCLUDE_GUARD
#define TOKEN_INCLUDE_GUARD

#include "common.hpp"
#include <cassert>

enum class Token : u8
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

static constexpr u32 MAX_TOKEN_TAG_BITS = 6;

static_assert(static_cast<u32>(Token::MAX) < (1 << MAX_TOKEN_TAG_BITS));

struct BuiltinToken
{
private:

	Token m_rep;

public:

	BuiltinToken() noexcept = default;

	BuiltinToken(Token tag) noexcept : m_rep{ tag } {}

	Token tag() const noexcept
	{
		return m_rep;
	}
};

struct NamedToken
{
private:

	u32 m_rep;

public:
	NamedToken() noexcept = default;

	NamedToken(Token tag, u32 index) noexcept : m_rep{ static_cast<u32>(tag) | static_cast<u32>(index << MAX_TOKEN_TAG_BITS) }
	{
		assert(index < (1 << (32 - MAX_TOKEN_TAG_BITS)));
	}

	Token tag() const noexcept
	{
		return static_cast<Token>(m_rep & ((1 << MAX_TOKEN_TAG_BITS) - 1));
	}

	s32 index() const noexcept
	{
		return static_cast<s32>(m_rep >> MAX_TOKEN_TAG_BITS);
	}
};

#endif // TOKEN_INCLUDE_GUARD
