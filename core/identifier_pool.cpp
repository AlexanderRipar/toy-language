#include "pass_data.hpp"

#include "../infra/container.hpp"
#include "../infra/range.hpp"
#include "../infra/hash.hpp"

static constexpr AttachmentRange<char8, Token> KEYWORDS[] = {
	range::from_literal_string("if",      Token::KwdIf),
	range::from_literal_string("then",    Token::KwdThen),
	range::from_literal_string("else",    Token::KwdElse),
	range::from_literal_string("for",     Token::KwdFor),
	range::from_literal_string("do",      Token::KwdDo),
	range::from_literal_string("finally", Token::KwdFinally),
	range::from_literal_string("switch",  Token::KwdSwitch),
	range::from_literal_string("case",    Token::KwdCase),
	range::from_literal_string("eval",    Token::KwdEval),
	range::from_literal_string("try",     Token::KwdTry),
	range::from_literal_string("catch",   Token::KwdCatch),
	range::from_literal_string("defer",   Token::KwdDefer),
	range::from_literal_string("func",    Token::KwdFunc),
	range::from_literal_string("proc",    Token::KwdProc),
	range::from_literal_string("trait",   Token::KwdTrait),
	range::from_literal_string("impl",    Token::KwdImpl),
	range::from_literal_string("where",   Token::KwdWhere),
	range::from_literal_string("expects", Token::KwdExpects),
	range::from_literal_string("ensures", Token::KwdEnsures),
	range::from_literal_string("pub",     Token::KwdPub),
	range::from_literal_string("mut",     Token::KwdMut),
	range::from_literal_string("let",     Token::KwdLet),
	range::from_literal_string("auto",    Token::KwdAuto),
	range::from_literal_string("use",     Token::KwdUse),
	range::from_literal_string("global",  Token::KwdGlobal),
	range::from_literal_string("return",  Token::KwdReturn),
	range::from_literal_string("leave",  Token::KwdLeave),
	range::from_literal_string("yield",  Token::KwdYield),
	range::from_literal_string("distinct", Token::KwdDistinct)
};

struct IdentifierPool
{
	IndexMap<Range<char8>, IdentifierEntry> map;
};

IdentifierPool* create_identifier_pool(AllocPool* pool) noexcept
{
	IdentifierPool* const identifiers = static_cast<IdentifierPool*>(alloc_from_pool(pool, sizeof(IdentifierPool), alignof(IdentifierPool)));

	identifiers->map.init(1u << 24, 1u << 15, 1u << 31, 1u << 18);

	// Occupy index 0 with a nonsense value so it can be used as INVALID_IDENTIFIER_ID
	(void) identifiers->map.value_from(Range<char8>{ nullptr, nullptr }, fnv1a(Range<byte>{ nullptr, nullptr }));

	for (const AttachmentRange keyword : KEYWORDS)
	{
		IdentifierEntry* const entry = identifiers->map.value_from(keyword.range(), fnv1a(keyword.range().as_byte_range()));

		entry->m_token = keyword.attachment();
	}

	return identifiers;
}

IdentifierEntry* identifier_entry_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept
{
	return identifiers->map.value_from(identifier, fnv1a(identifier.as_byte_range()));
}

IdentifierId id_from_identifier(IdentifierPool* identifiers, Range<char8> identifier) noexcept
{
	return { identifiers->map.index_from(identifier, fnv1a(identifier.as_byte_range())) };
}

IdentifierEntry* identifier_entry_from_id(IdentifierPool* identifiers, IdentifierId id) noexcept
{
	return identifiers->map.value_from(id.rep);
}
