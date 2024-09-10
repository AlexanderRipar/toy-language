#include "parse.hpp"

#include <cstdlib>

#include "hash.hpp"

static constexpr const u32 MAX_IDENTIFIER_LENGTH = 128;

static constexpr const u32 MAX_NUMBER_LENGTH = 128;

static enum AstHeaderFlags : u8
{
	GENERAL_DEFINITION_COUNT_MASK = 0xF0,
	GENERAL_DEFINITION_COUNT_ONE = 0x10,

	GENERAL_ARGUMENT_MASK = 0x7E,
	GENERAL_ARGUMENT_ONE = 0x02,

	GENERAL_HAS_LABEL = 0x01,

	DEFINITION_IS_MUT = 0x01,
	DEFINITION_IS_PUB = 0x02,
	DEFINITION_IS_GLOBAL = 0x04,
	DEFINITION_HAS_TYPE = 0x08,
	DEFINITION_HAS_VALUE = 0x10,

	IF_HAS_ALTERNATIVE = 0x01,

	FOR_HAS_CONDITION = 0x01,
	FOR_HAS_STEP = 0x02,
	FOR_HAS_FINALLY = 0x04,

	FOREACH_HAS_INDEX = 0x01,

	SIGNATURE_IS_FUNC = 0x01,

	SWITCH_HAS_DEFAULT = 0x01,

	BREAK_HAS_LABEL = 0x01,
	BREAK_HAS_VALUE = 0x02,

	RETURN_HAS_VALUE = 0x01,
};

static enum class Status
{
	Ok,
	Incomplete,
	Error,
};



static bool is_identifer_char(char8 c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool streqc(Range<char8> a, Range<char8> b) noexcept
{
	if (a.count() != b.count())
		return false;

	return memcmp(a.begin(), b.begin(), a.count()) == 0;
}

static LexResult lex(ParseState*) noexcept;

static LexResult check_end(ParseState* state, u32 remaining, Lexeme on_success) noexcept
{
	if (state->begin + remaining != state->end)
		return { Lexeme::ERROR };
	else if (!state->is_last)
		return { Lexeme::END };

	state->begin = state->end;

	return { on_success };
}

static LexResult check_end_with_value(ParseState* state, u32 remaining, LexResult on_success) noexcept
{
	if (state->begin + remaining != state->end)
		return { Lexeme::ERROR };
	else if (!state->is_last)
		return { Lexeme::END };

	state->begin = state->end;

	return on_success;
}

static LexResult lex_skip_block_comment(ParseState* state) noexcept
{
	const char8* curr = state->begin;

	u32 comment_nesting = state->comment_nesting;

	while (true)
	{
		if (*curr == '\0')
		{
			state->comment_nesting = comment_nesting;

			state->begin = curr;

			return check_end(state, 0, Lexeme::ERROR);
		}
		else if (*curr == '/')
		{
			if (curr[1] == '\0')
			{
				state->comment_nesting = comment_nesting;

				state->begin = curr - 1;

				return check_end(state, 1, Lexeme::ERROR);
			}
			else if (curr[1] == '*')
			{
				curr += 2;

				comment_nesting += 1;
			}
			else
			{
				curr += 1;
			}
		}
		else if (*curr == '*')
		{
			if (curr[1] == '\0')
			{
				state->comment_nesting = comment_nesting;

				state->begin = curr - 1;

				return check_end(state, 1, Lexeme::ERROR);
			}
			else if (curr[1] == '/')
			{
				curr += 2;

				if (comment_nesting == 1)
					break;

				comment_nesting -= 1;
			}
			else
			{
				curr += 1;
			}
		}
		else
		{
			curr += 1;
		}
	}

	state->begin = curr;

	return lex(state);

}

static LexResult lex_skip_line_comment(ParseState* state) noexcept
{
	const char8* curr = state->begin;

	while (*curr != '\n')
	{
		if (*curr == '\0')
		{
			state->is_line_comment = true;

			return check_end(state, 0, Lexeme::END);
		}
	}

	state->begin = curr;

	return lex(state);
}

static LexResult lex_symbol(ParseState* state) noexcept
{
	ASSERT_OR_IGNORE(!state->is_line_comment);

	ASSERT_OR_IGNORE(state->comment_nesting == 0);

	const char8* begin = state->begin;

	switch (*state->begin)
	{
	case '#':
	{
		state->begin += 1;

		return { Lexeme::Pragma };
	}

	case '(':
	{
		state->begin += 1;

		return { Lexeme::ParenBeg };
	}

	case ')':
	{
		state->begin += 1;

		return { Lexeme::ParenEnd };
	}

	case '[':
	{
		state->begin += 1;

		return { Lexeme::BracketBeg };
	}

	case ']':
	{
		state->begin += 1;

		return { Lexeme::BracketEnd };
	}

	case '{':
	{
		state->begin += 1;

		return { Lexeme::CurlyBeg };
	}

	case '}':
	{
		state->begin += 1;

		return { Lexeme::CurlyEnd };
	}

	case '=':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Set);
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::Eq };
		}
		else if (begin[1] == '>')
		{
			state->begin += 2;

			return { Lexeme::WideArrow };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Set };
		}
	}

	case ':':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Colon);
		}
		else if (begin[1] == '+')
		{
			if (begin[2] == '\0')
			{
				return check_end(state, 2, Lexeme::AddTC);
			}
			else if (begin[2] == '=')
			{
				state->begin += 3;

				return { Lexeme::SetAddTC };
			}
			else
			{
				state->begin += 2;

				return { Lexeme::AddTC };
			}
		}
		else if (begin[1] == '-')
		{
			if (begin[2] == '\0')
			{
				return check_end(state, 2, Lexeme::SubTC);
			}
			else if (begin[2] == '=')
			{
				state->begin += 3;

				return { Lexeme::SetSubTC };
			}
			else
			{
				state->begin += 2;

				return { Lexeme::SubTC };
			}
		}
		else if (begin[1] == '*')
		{
			if (begin[2] == '\0')
			{
				return check_end(state, 2, Lexeme::MulTC);
			}
			else if (begin[2] == '=')
			{
				state->begin += 3;

				return { Lexeme::SetMulTC };
			}
			else
			{
				state->begin += 2;

				return { Lexeme::MulTC };
			}
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Colon };
		}
	}

	case '.':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Dot);
		}
		else if (begin[1] == '.')
		{
			if (begin[2] == '\0')
			{
				return check_end(state, 2, Lexeme::ERROR);
			}
			else if (begin[2] == '.')
			{
				state->begin += 3;

				return { Lexeme::TripleDot };
			}
			else
			{
				return { Lexeme::ERROR };
			}
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Dot };
		}
	}

	case ';':
	{
		state->begin += 1;

		return { Lexeme::Semicolon };
	}

	case ',':
	{
		state->begin += 1;

		return { Lexeme::Comma };
	}

	case '?':
	{
		state->begin += 1;

		return { Lexeme::Opt };
	}

	case '@':
	{
		state->begin += 1;

		return { Lexeme::At };
	}

	case '+':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Add);
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetAdd };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Add };
		}
	}

	case '-':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Sub);
		}
		else if (begin[1] == '>')
		{
			state->begin += 2;

			return { Lexeme::ThinArrow };
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetSub };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Sub };
		}
	}

	case '*':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Mul);
		}
		else if (begin[1] == '/')
		{
			return { Lexeme::ERROR };
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetMul };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Mul };
		}
	}

	case '/':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Div);
		}
		else if (begin[1] == '/')
		{
			const char8* curr = begin + 2;

			while (*curr != '\n')
			{
				if (*curr == '\0')
				{
					state->is_line_comment = true;

					state->begin = curr;

					return check_end(state, 0, Lexeme::END);
				}

				curr += 1;
			}
			
			state->begin = curr;

			return lex(state);
		}
		else if (begin[1] == '*')
		{
			return lex_skip_block_comment(state);
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetDiv };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Div };
		}
	}

	case '%':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Mod);
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetMod };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Mod };
		}
	}

	case '<':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Lt);
		}
		else if (begin[1] == '<')
		{
			if (begin[2] == '\0')
			{
				return check_end(state, 2, Lexeme::ShiftL);
			}
			else if (begin[2] == '=')
			{
				state->begin += 3;

				return { Lexeme::SetShiftL };
			}
			else
			{
				state->begin += 2;

				return { Lexeme::ShiftL };
			}
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::Le };
		}
		else if (begin[1] == '-')
		{
			state->begin += 2;

			return { Lexeme::Element };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Lt };
		}
	}

	case '>':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::Gt);
		}
		else if (begin[1] == '>')
		{
			if (begin[2] == '\0')
			{
				return check_end(state, 2, Lexeme::ShiftR);
			}
			else if (begin[2] == '=')
			{
				state->begin += 3;

				return { Lexeme::SetShiftR };
			}
			else
			{
				state->begin += 2;

				return { Lexeme::ShiftR };
			}
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::Ge };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::Gt };
		}
	}

	case '&':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::BitAnd);
		}
		else if (begin[1] == '&')
		{
			state->begin += 2;

			return { Lexeme::LogAnd };
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetBitAnd };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::BitAnd };
		}
	}

	case '|':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::BitOr);
		}
		else if (begin[1] == '|')
		{
			state->begin += 2;

			return { Lexeme::LogOr };
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetBitOr };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::BitOr };
		}
	}

	case '^':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::BitXor);
		}
		else if (begin[1] == '^')
		{
			state->begin += 2;

			return { Lexeme::LogXor };
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::SetBitXor };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::BitXor };
		}
	}

	case '~':
	{
		state->begin += 1;

		return { Lexeme::BitNot };
	}

	case '!':
	{
		if (begin[1] == '\0')
		{
			return check_end(state, 1, Lexeme::LogNot);
		}
		else if (begin[1] == '=')
		{
			state->begin += 2;

			return { Lexeme::Ne };
		}
		else
		{
			state->begin += 1;

			return { Lexeme::LogNot };
		}
	}

	case '\0':
			return check_end(state, 0, Lexeme::END);

	default:
		return { Lexeme::ERROR };
	}
}

static LexResult lex_number(ParseState* state) noexcept
{
	const char8* curr = state->begin;

	u64 value = 0;

	if (*curr == '0')
	{
		if (curr[1] == '\0')
		{
			return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstInteger, value });
		}
		else if (curr[1] == 'x')
		{
			curr += 2;

			while (true)
			{
				if (*curr >= '0' && *curr <= '9')
					value = value * 16 + *curr - '0';
				else if (*curr >= 'a' && *curr <= 'f')
					value = value * 16 + *curr - 'a' + 10;
				else if (*curr >= 'A' && *curr <= 'F')
					value = value * 16 + *curr - 'A' + 10;
				else
					break;

				curr += 1;
			}
		
			if (*curr == '\0')
				return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstInteger, value });

			if (curr == state->begin + 2)
				return { Lexeme::ERROR };
		}
		else if (curr[1] == 'o')
		{
			curr += 2;

			while (*curr >= '0' && *curr <= '7')
			{
				value = value * 8 + *curr - '0';

				curr += 1;
			}
		
			if (*curr == '\0')
				return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstInteger, value });

			if (curr == state->begin + 2)
				return { Lexeme::ERROR };
		}
		else if (curr[1] == 'b')
		{
			curr += 2;

			while (*curr == '0' || *curr == '1')
			{
				value = value * 2 + *curr - '0';

				curr += 1;
			}
		
			if (*curr == '\0')
				return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstInteger, value });

			if (curr == state->begin + 2)
				return { Lexeme::ERROR };
		}
		else
		{
			goto NO_BASE_PREFIX;
		}
	}
	else
	{
	NO_BASE_PREFIX:

		ASSERT_OR_IGNORE(*curr >= '0' && *curr <= '9');

		while (*curr >= '0' && *curr <= '9')
		{
			value = value * 10 + *curr - '0';

			curr += 1;
		}

		if (*curr == '\0')
			return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstInteger, value });
		else if (*curr == '.')
		{
			curr += 1;

			if (*curr == '\0')
				return check_end(state, static_cast<u32>(curr - state->begin), Lexeme::ERROR);
			else if (*curr < '0' || *curr > '9')
				return { Lexeme::ERROR };

			curr += 1;

			while (*curr >= '0' && *curr <= '9')
				curr += 1;

			if (*curr == '\0')
			{
				char8* parsed_double_end;

				const double float_value = strtod(state->begin, &parsed_double_end);

				ASSERT_OR_IGNORE(parsed_double_end == curr);

				return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstFloat, float_value});
			}
			
			if (*curr == 'e')
				goto FLOAT_EXPONENT;

			if (is_identifer_char(*curr))
				return { Lexeme::ERROR };
			
			char8* parsed_double_end;

			const double double_value = strtod(state->begin, &parsed_double_end);

			if (parsed_double_end != curr)
				return { Lexeme::ERROR };

			return { Lexeme::ConstFloat, double_value };
		}
		else if (*curr == 'e')
		{
		FLOAT_EXPONENT:

			curr += 1;

			if (*curr == '+' || *curr == '-')
				curr += 1;

			if (*curr == '\0')
				return check_end(state, static_cast<u32>(curr - state->begin), Lexeme::ERROR);
			else if (*curr < '0' || *curr > '9')
				return { Lexeme::ERROR };

			curr += 1;

			while (*curr >= '0' && *curr <= '9')
				curr += 1;

			if (*curr == '\0')
			{
				char8* parsed_double_end;

				const double double_value = strtod(state->begin, &parsed_double_end);

				ASSERT_OR_IGNORE(parsed_double_end == curr);

				return check_end_with_value(state, static_cast<u32>(curr - state->begin), { Lexeme::ConstFloat, double_value});
			}

			if (is_identifer_char(*curr))
				return { Lexeme::ERROR };

			char8* parsed_double_end;

			const double float_value = strtod(state->begin, &parsed_double_end);

			if (parsed_double_end != curr)
				return { Lexeme::ERROR };

			return { Lexeme::ConstFloat, float_value };			
		}
	}

	if (is_identifer_char(*curr))
		return { Lexeme::ERROR };

	state->begin = curr;

	return { Lexeme::ConstInteger, value };
}

static LexResult lex_name(ParseState* state) noexcept
{
	const char8* curr = state->begin + 1;

	while (is_identifer_char(*curr))
		curr += 1;

	const Range<char8> identifer{ state->begin, curr };

	LexResult rst;

	if (streqc(identifer, range_from_literal_string("let")))
		rst = { Lexeme::Let };
	else if (streqc(identifer, range_from_literal_string("mut")))
		rst = { Lexeme::Mut };
	else if (streqc(identifer, range_from_literal_string("pub")))
		rst = { Lexeme::Pub };
	else if (streqc(identifer, range_from_literal_string("global")))
		rst = { Lexeme::Global };
	else if (streqc(identifer, range_from_literal_string("eval")))
		rst = { Lexeme::Eval };
	else if (streqc(identifer, range_from_literal_string("func")))
		rst = { Lexeme::Func };
	else if (streqc(identifer, range_from_literal_string("proc")))
		rst = { Lexeme::Proc };
	else if (streqc(identifer, range_from_literal_string("if")))
		rst = { Lexeme::If };
	else if (streqc(identifer, range_from_literal_string("then")))
		rst = { Lexeme::Then };
	else if (streqc(identifer, range_from_literal_string("else")))
		rst = { Lexeme::Else };
	else if (streqc(identifer, range_from_literal_string("for")))
		rst = { Lexeme::For };
	else if (streqc(identifer, range_from_literal_string("each")))
		rst = { Lexeme::Each };
	else if (streqc(identifer, range_from_literal_string("loop")))
		rst = { Lexeme::Loop };
	else if (streqc(identifer, range_from_literal_string("continue")))
		rst = { Lexeme::Continue };
	else if (streqc(identifer, range_from_literal_string("break")))
		rst = { Lexeme::Break };
	else if (streqc(identifer, range_from_literal_string("finally")))
		rst = { Lexeme::Finally };
	else if (streqc(identifer, range_from_literal_string("switch")))
		rst = { Lexeme::Switch };
	else if (streqc(identifer, range_from_literal_string("case")))
		rst = { Lexeme::Case };
	else if (streqc(identifer, range_from_literal_string("fallthrough")))
		rst = { Lexeme::Fallthrough };
	else if (streqc(identifer, range_from_literal_string("try")))
		rst = { Lexeme::Try };
	else if (streqc(identifer, range_from_literal_string("catch")))
		rst = { Lexeme::Catch };
	else if (streqc(identifer, range_from_literal_string("undefined")))
		rst = { Lexeme::Undefined };
	else if (streqc(identifer, range_from_literal_string("unreachable")))
		rst = { Lexeme::Unreachable };
	else if (streqc(identifer, range_from_literal_string("return")))
		rst = { Lexeme::Return };
	else
		rst = { Lexeme::Identifier, state->identifiers->index_from(state->thread_id, identifer, fnv1a(identifer.as_byte_range())) };

	state->begin = curr;

	// TODO: Handle keywords
	return rst;
}

static LexResult lex_prefix(ParseState* state) noexcept
{
	char8* const prefix = state->prefix;

	if (state->comment_nesting != 0)
	{
		ASSERT_OR_IGNORE(state->comment_nesting == 0 || ((*state->prefix == '/' || *state->prefix == '*') && state->prefix[1] == '\0'));

		if (*prefix == '/' && *state->begin == '*')
		{
			state->begin += 1;

			state->comment_nesting += 1;
		}
		else if (*prefix == '*' && *state->begin == '/')
		{
			state->begin += 1;

			state->comment_nesting -= 1;
		
			if (state->comment_nesting == 0)
				return lex(state);
		}

		return lex_skip_block_comment(state);
	}
	else if (state->is_line_comment)
	{
		state->is_line_comment = false;

		return lex_skip_line_comment(state);
	}
	else if (state->prefix_used == 0)
	{
		return lex(state);
	}
	else if ((*prefix >= 'a' && *prefix <= 'z') || (*prefix >= 'A' && *prefix <= 'Z') || *prefix == '_')
	{
		const char8* src = state->begin;

		char8* dst = prefix + state->prefix_used;

		while (is_identifer_char(*src))
		{
			if (dst - prefix == MAX_IDENTIFIER_LENGTH)
				return { Lexeme::ERROR };

			*dst = *src;

			src += 1;

			dst += 1;
		}

		dst[1] = ' ';

		state->begin = state->prefix;

		state->end = dst;

		const LexResult token = lex_name(state);

		const size_t consumed = state->begin - (state->prefix + state->prefix_used);

		const char8* const begin = state->begin;

		const char8* const end = state->end;

		state->begin = begin + consumed;

		state->end = end;

		return token;
	}
	else if (*prefix == '\'')
	{
		// TODO
		return { Lexeme::ERROR };
	}
	else if (*prefix == '"')
	{
		// TODO
		return { Lexeme::ERROR };
	}
	else if (*prefix >= '0' && *prefix <= '9')
	{
		const char8* src = state->begin;

		char8* dst = prefix + state->prefix_used;

		bool has_dot = false;

		bool has_exp = false;

		if (prefix[0] == '0')
		{
			if (prefix[1] == '\0' && (*src == 'x' || *src == 'o' || *src == 'b'))
			{
				*dst = *src;

				dst += 1;

				src += 1;
			}

			if (prefix[1] == 'x')
			{
				while ((*src >= '0' && *src <= '9') || (*src >= 'a' && *src <= 'f') || (*src >= 'A' && *src <= 'F'))
				{
					if (dst - prefix == MAX_NUMBER_LENGTH)
						return { Lexeme::ERROR };

					*dst = *src;

					dst += 1;

					src += 1;
				}
			}
			else if (prefix[1] == 'o')
			{
				while (*src >= '0' && *src <= '7')
				{
					if (dst - prefix == MAX_NUMBER_LENGTH)
						return { Lexeme::ERROR };

					*dst = *src;

					dst += 1;

					src += 1;
				}
			}
			else if (prefix[1] == 'b')
			{
				while (*src == '0' || *src == '1')
				{
					if (dst - prefix == MAX_NUMBER_LENGTH)
						return { Lexeme::ERROR };

					*dst = *src;

					dst += 1;

					src += 1;
				}
			}
			else
			{
				goto INTEGER_NO_PREFIX;
			}
		}
		else
		{
		INTEGER_NO_PREFIX:

			while (true)
			{
				if (*src == 'e' && !has_exp)
				{
					if (has_exp)
						break;

					if (src[1] == '+' || src[1] == '-')
					{
						if (dst - prefix == MAX_NUMBER_LENGTH)
							return { Lexeme::ERROR };

						*dst = *src;

						dst += 1;

						src += 1;
					}
				}
				else if ((*src < '0' || *src > '9') && (*src != '.' || has_dot))
				{
					break;
				}

				if (dst - prefix == MAX_NUMBER_LENGTH)
					return { Lexeme::ERROR };

				*dst = *src;

				dst += 1;

				src += 1;
			}
		}

		*dst = ' ';

		if (is_identifer_char(*src))
			return { Lexeme::ERROR };

		const char8* const begin = state->begin;

		const char8* const end = state->end;

		state->begin = state->prefix;

		state->end = dst;

		const LexResult token = lex_number(state);

		ASSERT_OR_IGNORE(((has_dot || has_exp) && token.lexeme == Lexeme::ConstInteger) || (!(has_dot || has_exp) && token.lexeme == Lexeme::ConstFloat));

		const size_t consumed = state->begin - (state->prefix + state->prefix_used);

		state->begin = begin + consumed;

		state->end = end;

		return token;
	}
	else if (*prefix == '/' && prefix[1] == '\0' && *state->begin == '/')
	{
		state->begin += 1;

		return lex_skip_line_comment(state);
	}
	else
	{
		memcpy(state->prefix + state->prefix_used, state->begin, 4);

		state->prefix[state->prefix_used + 4] = ' ';

		const char8* const begin = state->begin;

		const char8* const end = state->end;

		state->begin = state->prefix;

		state->end = state->prefix + state->prefix_used + 4;

		const LexResult token = lex_symbol(state);

		const size_t consumed = state->begin - (state->prefix + state->prefix_used);

		state->begin = begin + consumed;

		state->end = end;

		if (token.lexeme == Lexeme::END)
			return lex(state);

		return token;
	}
}

static LexResult lex(ParseState* state) noexcept
{
	while (*state->begin == ' ' || *state->begin == '\t' || *state->begin == '\r' || *state->begin == '\n')
		state->begin += 1;

	const char8* const begin = state->begin;

	if ((*begin >= 'a' && *begin <= 'z') || (*begin >= 'A' && *begin <= 'Z') || *begin == '_')
	{
		return lex_name(state);
	}
	else if (*begin >= '0' && *begin <= '9')
	{
		return lex_number(state);
	}
	else if (*begin == '\'')
	{
		// TODO
		return { Lexeme::ERROR };
	}
	else if (*begin == '"')
	{
		// TODO
		return { Lexeme::ERROR };
	}
	else
	{
		return lex_symbol(state);
	}
}




static AstHeader* ast_header(ParseState* state, AstHeader header) noexcept
{
	// TODO: Implement

	return {};
}

static void ast_append(ParseState* state, u32 value) noexcept
{
	// TODO: Implement
}



static ParseFrame pop_parse_frame(ParseState* state) noexcept
{
	if (state->frame_count == 0)
		return ParseFrame::START;

	const u32 top = state->frame_count - 1;

	state->frame_count = top;

	ASSERT_OR_IGNORE(state->frames[top] != ParseFrame::START);

	return state->frames[top];
}

static Status push_parse_frame(ParseState* state, ParseFrame frame) noexcept
{
	ASSERT_OR_EXIT(state->frame_count < array_count(state->frames));

	state->frames[state->frame_count] = frame;

	state->frame_count += 1;

	return Status::Incomplete;
}


static bool is_operator(Lexeme lexeme) noexcept
{
	return (lexeme >= Lexeme::Dot && lexeme <= Lexeme::Ge) || lexeme == Lexeme::Catch;
}

static u8 get_operator_precedence(Lexeme lexeme) noexcept
{
	static constexpr u8 OPERATOR_PRECEDENCE[] {
		1, // .
		1, // .*
		3, // +
		3, // +:
		3, // -
		3, // -:
		2, // *
		2, // *:
		2, // /
		2, // %
		4, // <<
		4, // >>
		5, // &
		5, // |
		5, // ^
		7, // &&
		7, // ||
		7, // ^^
		6, // ==
		6, // !=
		6, // <
		6, // <=
		6, // >
		6, // >=
	};

	const sint index = static_cast<sint>(lexeme) - static_cast<sint>(Lexeme::Dot);

	if (index >= 0 && index < array_count(OPERATOR_PRECEDENCE))
		return OPERATOR_PRECEDENCE[index];

	ASSERT_OR_IGNORE(lexeme == Lexeme::Try
	              || lexeme == Lexeme::LogNot
	              || lexeme == Lexeme::BitNot
	              || lexeme == Lexeme::TripleDot
	              || lexeme == Lexeme::Opt);

	return 0;
}

static AstType operator_to_ast_type(Lexeme lexeme) noexcept
{
	// TODO
	return {};
}

static Status parse_expr(ParseState* state, LexResult& next) noexcept
{
	switch (pop_parse_frame(state))
	{
	case ParseFrame::START:
		
		if (next.lexeme == Lexeme::END)
		{
			return Status::Incomplete;
		}
		
		if (state->expr_expecting_operator)
		{
			if (!is_operator(next.lexeme))
				return Status::Error;

			const u8 precedence = get_operator_precedence(next.lexeme);

			while (true)
			{
				if (state->expr.operator_count == 0)
					break;

				const Lexeme prev_operator = state->expr.operators[state->expr.operator_count - 1];

				const u8 prev_precedence = get_operator_precedence(prev_operator);

				if (precedence > prev_precedence)
					break;

				if (state->expr.operand_count < 2)
					return Status::Error;

				const u32 new_operand_size = state->expr.operand_sizes[state->expr.operand_count - 2] + state->expr.operand_sizes[state->expr.operand_count - 1];

				state->expr.operand_sizes[state->expr.operand_count - 2] = new_operand_size;

				state->expr.operand_count -= 1;

				state->expr.operator_count -= 1;

				if (new_operand_size > UINT16_MAX)
					return Status::Error; // TODO: Handle properly

				const AstHeader operator_header{ operator_to_ast_type(prev_operator), 0, static_cast<u16>(new_operand_size) };

				if (state->expr.operand_tail + sizeof(operator_header) > sizeof(state->expr.rpn_operands))
					return Status::Error;

				memcpy(state->expr.rpn_operands + state->expr.operand_tail, &operator_header, sizeof(operator_header));

				state->expr.operand_tail += sizeof(operator_header);
			}

			if (state->expr.operator_count == array_count(state->expr.operators))
				return Status::Error;

			state->expr.operators[state->expr.operand_count] = next.lexeme;

			state->expr.operator_count += 1;
		}
		else
		{
			if (next.lexeme == Lexeme::Try)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::If)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::For)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::Switch)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::Func || next.lexeme == Lexeme::Proc)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::Undefined)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::CurlyBeg)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::ParenBeg)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::BracketBeg)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::TripleDot)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::Opt)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::Mul)
			{
				// TODO
				return Status::Error;
			}
			else if (next.lexeme == Lexeme::Sub)
			{
				// TODO
				return Status::Error;
			}
			else
			{
				return Status::Error;
			}
		}

	case ParseFrame::Expr_AfterCurly:

	default: ASSERT_UNREACHABLE;
	}
}

static Status parse_definition(ParseState* state, LexResult& next) noexcept
{
	AstHeader* header = ast_header(state, AstHeader{ AstType::If });

	switch (pop_parse_frame(state))
	{
	case ParseFrame::START:
		
		if (next.lexeme == Lexeme::END)
		{
			return Status::Incomplete;
		}
		else if (next.lexeme == Lexeme::Let)
		{
			next = lex(state);

			if (next.lexeme == Lexeme::END)
				return push_parse_frame(state, ParseFrame::Definition_AfterLet);

	case ParseFrame::Definition_AfterLet:;
		}
		else
		{
			if (next.lexeme == Lexeme::Pub)
			{
				header->flags |= AstHeaderFlags::DEFINITION_IS_PUB;
				
				next = lex(state);
				
				if (next.lexeme == Lexeme::END)
					return push_parse_frame(state, ParseFrame::Definition_AfterPub);

	case ParseFrame::Definition_AfterPub:;
			}

			if (next.lexeme == Lexeme::Global)
			{
				header->flags |= AstHeaderFlags::DEFINITION_IS_GLOBAL;
				
				next = lex(state);
				
				if (next.lexeme == Lexeme::END)
					return push_parse_frame(state, ParseFrame::Definition_AfterGlobal);

	case ParseFrame::Definition_AfterGlobal:;
			}

			if (next.lexeme == Lexeme::Mut)
			{
				header->flags |= AstHeaderFlags::DEFINITION_IS_MUT;
				
				next = lex(state);
				
				if (next.lexeme == Lexeme::END)
					return push_parse_frame(state, ParseFrame::Definition_AfterMut);

	case ParseFrame::Definition_AfterMut:;
			}

			if ((header->flags & (AstHeaderFlags::DEFINITION_IS_PUB | AstHeaderFlags::DEFINITION_IS_GLOBAL | AstHeaderFlags::DEFINITION_IS_MUT)) == 0)
				return Status::Error;
		}

		if (next.lexeme != Lexeme::Identifier)
			return Status::Error;

		ast_append(state, next.value.identifier);

		next = lex(state);

		if (next.lexeme == Lexeme::END)
			return push_parse_frame(state, ParseFrame::Definition_AfterIdentifier);

		if (next.lexeme == Lexeme::Colon)
		{
			header->flags |= AstHeaderFlags::DEFINITION_HAS_TYPE;

			next = lex(state);

	case ParseFrame::Definition_AfterColon:

			if (const Status s = parse_expr(state, next); s == Status::Error)
				return Status::Error;
			else if (s == Status::Incomplete)
				return push_parse_frame(state, ParseFrame::Definition_AfterColon);
		}

		if (next.lexeme == Lexeme::Set)
		{
			header->flags |= AstHeaderFlags::DEFINITION_HAS_VALUE;
			
			next = lex(state);

	case ParseFrame::Definition_AfterSet:

			if (const Status s = parse_expr(state, next); s == Status::Error)
				return Status::Error;
			else if (s == Status::Incomplete)
				return push_parse_frame(state, ParseFrame::Definition_AfterSet);
		}

		if ((header->flags & (AstHeaderFlags::DEFINITION_HAS_TYPE | AstHeaderFlags::DEFINITION_HAS_VALUE)) == 0)
			return Status::Error;

		return Status::Ok;

	default: ASSERT_UNREACHABLE;
	}
}

static Status parse_program(ParseState* state, LexResult& next) noexcept
{
	while (next.lexeme != Lexeme::END)
	{
		if (const Status s = parse_definition(state, next); s == Status::Error)
			return Status::Error;
		else if (s == Status::Incomplete)
			return Status::Incomplete; // Nothing to push, since we don't really store state here anyway
	}

	return Status::Ok;
}



s32 parse(ParseState* state) noexcept
{
	LexResult next = lex_prefix(state);

	if (parse_program(state, next) == Status::Error)
		return -1;

	ASSERT_OR_IGNORE(state->begin <= state->end);

	return static_cast<s32>(state->end - state->begin);
}
