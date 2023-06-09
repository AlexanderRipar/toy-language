#include "ast_gen.hpp"

#include <cassert>
#include <cstdlib>

struct pstate
{
	const Token* beg;

	const Token* end;

	const Token* curr;

	ast::Result rst;
};

static bool error_out_of_memory(pstate& s, const char* ctx) noexcept
{
	s.rst.tag = ast::Result::Tag::OutOfMemory;

	s.rst.error_ctx = ctx;

	s.rst.message = "Out of memory.";

	return false;
}

static bool error_invalid_syntax(pstate& s, const char* ctx, const Token& curr, const char* msg) noexcept
{
	s.rst.tag = ast::Result::Tag::InvalidSyntax;

	s.rst.error_ctx = ctx;

	s.rst.message = msg;

	s.rst.problematic_token = &curr;

	return false;
}

static bool error_unexpected_token(pstate& s, const char* ctx, const Token& curr, Token::Tag expected) noexcept
{
	s.rst.tag = ast::Result::Tag::UnexpectedToken;

	s.rst.error_ctx = ctx;

	s.rst.expected_token = expected;

	s.rst.problematic_token = &curr;

	return false;
}

static bool error_unexpected_end(pstate& s, const char* ctx) noexcept
{
	s.rst.tag = ast::Result::Tag::UnexpectedEndOfStream;

	s.rst.error_ctx = ctx;

	s.rst.message = "Unexpectedly ran out of input Tokens.";

	return false;
}


static const Token end_token{ Token::Tag::INVALID, 0, {} };

static const Token& peek(const pstate& s, usz offset = 0) noexcept
{
	if (s.curr + offset >= s.end)
		return end_token;

	return s.curr[offset];
}

static const Token& next(pstate& s, const char* ctx) noexcept
{
	if (s.curr >= s.end)
	{
		error_unexpected_end(s, ctx);

		return end_token;
	}

	return *s.curr++;
}

static const Token& expect(pstate& s, const char* ctx, Token::Tag expected) noexcept
{
	const Token& t = next(s, ctx);

	if (t.tag != expected)
	{
		if (t.tag == Token::Tag::INVALID)
			error_unexpected_end(s, ctx);
		else
			error_unexpected_token(s, ctx, t, expected);

		return end_token;
	}

	return t;
}

static bool next_if(pstate& s, Token::Tag expected) noexcept
{
	const Token& t = peek(s);

	if (t.tag == expected)
		++s.curr;

	return t.tag == expected;
}



template<typename T>
static bool alloc(pstate& s, const char* ctx, T** out) noexcept
{
	if ((*out = static_cast<T*>(malloc(sizeof(T)))) != nullptr)
	{
		memset(*out, 0, sizeof(T));

		return true;
	}

	return error_out_of_memory(s, ctx);
}

template<typename T, typename... ParseArgs>
static bool alloc_and_parse(pstate& s, const char* ctx, T** out, ParseArgs... parse_args) noexcept
{
	if (!alloc(s, ctx, out))
		return false;

	return parse(s, **out, parse_args...);
}

template<typename T, u16 VecSize, typename... ParseArgs>
static bool alloc_and_parse(pstate& s, const char* ctx, vec<T, VecSize>& out, ParseArgs... parse_args) noexcept
{
	if (!out.push_back({}))
		return error_out_of_memory(s, ctx);

	return parse(s, out.last(), parse_args...);
}


static bool hex_val(char c, usz& inout_v) noexcept
{
	inout_v <<= 4;

	if (c >= '0' && c <= '9')
		inout_v += c - '0';
	else if (c >= 'A' && c <= 'F')
		inout_v += 10 + c - 'A';
	else if (c >= 'a' && c <= 'f')
		inout_v += 10 + c -  'a';
	else
		return false;

	return true;
}

static bool dec_val(char c, usz& inout_v) noexcept
{
	inout_v *= 10;

	if (c >= '0' && c <= '9')
		inout_v += c - '0';
	else
		return false;

	return true;
}

static bool oct_val(char c, usz& inout_v) noexcept
{
	inout_v <<= 3;

	if (c >= '0' && c <= '7')
		inout_v += c - '0';
	else
		return false;

	return true;
}

static bool bin_val(char c, usz& inout_v) noexcept
{
	inout_v <<= 1;

	inout_v |= c == '1';
	
	return c == '0' || c == '1';
}



static bool parse(pstate& s, ast::Definition& out) noexcept;

static bool parse(pstate& s, ast::Expr& out, bool allow_assignment = true) noexcept;

static bool parse(pstate& s, ast::Literal& out) noexcept;



struct ShuntingYardOp
{
	enum class Tag : u8
	{
		EMPTY = 0,
		BinaryOp,
		UnaryOp,
		ParenBeg,
		BracketBeg,
		Mut,
	} tag = Tag::EMPTY;

	u8 precedence;

	bool is_left_assoc;

	union
	{
		ast::BinaryOp::Op binary_op;

		ast::UnaryOp::Op unary_op;
	};

	constexpr ShuntingYardOp() noexcept
		: tag{ Tag::EMPTY }, precedence{ 0 }, is_left_assoc{ 0 }, binary_op{ ast::BinaryOp::Op::NONE } {}

	constexpr ShuntingYardOp(u8 precedence, bool is_left_assoc, ast::BinaryOp::Op op) noexcept
		: tag{ Tag::BinaryOp }, precedence{ precedence }, is_left_assoc{ is_left_assoc }, binary_op{ op } {}

	constexpr ShuntingYardOp(u8 precedence, bool is_left_assoc, ast::UnaryOp::Op op) noexcept
		: tag{ Tag::UnaryOp }, precedence{ precedence }, is_left_assoc{ is_left_assoc }, unary_op{ op } {}

	constexpr ShuntingYardOp(Tag tag, u8 precedence = 255, bool is_left_assoc = true) noexcept
		: tag{ tag }, precedence{ precedence }, is_left_assoc{ is_left_assoc }, binary_op{ ast::BinaryOp::Op::NONE } {}
};

static bool token_tag_to_shunting_yard_op(const Token::Tag tag, bool is_binary, ShuntingYardOp& out) noexcept
{
	if (is_binary)
	{
		static constexpr const ShuntingYardOp binary_ops[] {
			{  4, true, ast::BinaryOp::Op::Add    },
			{  3, true, ast::BinaryOp::Op::Div    },
			{  3, true, ast::BinaryOp::Op::Mod    },
			{ 10, true, ast::BinaryOp::Op::BitOr  },
			{  9, true, ast::BinaryOp::Op::BitXor },
			{  5, true, ast::BinaryOp::Op::ShiftL },
			{  5, true, ast::BinaryOp::Op::ShiftR },
			{ 11, true, ast::BinaryOp::Op::LogAnd },
			{ 12, true, ast::BinaryOp::Op::LogOr  },
			{  6, true, ast::BinaryOp::Op::CmpLt  },
			{  6, true, ast::BinaryOp::Op::CmpLe  },
			{  6, true, ast::BinaryOp::Op::CmpGt  },
			{  6, true, ast::BinaryOp::Op::CmpGe  },
			{  7, true, ast::BinaryOp::Op::CmpNe  },
			{  7, true, ast::BinaryOp::Op::CmpEq  },
			{  1, true, ast::BinaryOp::Op::Member },
			{  4, true, ast::BinaryOp::Op::Sub    },
			{  3, true, ast::BinaryOp::Op::Mul    },
			{  8, true, ast::BinaryOp::Op::BitAnd },
		};

		const usz offset = static_cast<usz>(tag) - static_cast<usz>(Token::Tag::OpAdd);

		const bool is_in_range = offset < _countof(binary_ops);

		if (is_in_range)
			out = binary_ops[offset];

		return is_in_range;
	}
	else
	{
		static constexpr ShuntingYardOp unary_ops[] {
			{  2, false, ast::UnaryOp::Op::Neg          },
			{  2, false, ast::UnaryOp::Op::TypePtr      },
			{  2, false, ast::UnaryOp::Op::AddrOf       },
			{  2, false, ast::UnaryOp::Op::BitNot       },
			{  2, false, ast::UnaryOp::Op::LogNot       },
			{  2, false, ast::UnaryOp::Op::Deref        },
			{  2, false, ast::UnaryOp::Op::TypeVariadic },
			{ 13, false, ast::UnaryOp::Op::Try          },
		};

		const usz offset = static_cast<usz>(tag) - static_cast<usz>(Token::Tag::OpSub);

		const bool is_in_range = offset < _countof(unary_ops);

		if (is_in_range)
			out = unary_ops[offset];

		return is_in_range;
	}
}

static bool pop_shunting_yard_operator(pstate& s, vec<ShuntingYardOp, 32>& op_stk, vec<ast::Expr, 32>& expr_stk) noexcept
{
	constexpr const char* const ctx = "Expr";

	assert(op_stk.size() != 0);

	const ShuntingYardOp op = op_stk.last();

	op_stk.pop();

	if (op.tag == ShuntingYardOp::Tag::BinaryOp)
	{
		assert(expr_stk.size() >= 2);

		ast::BinaryOp* binary_op = nullptr;

		if (!alloc(s, ctx, &binary_op))
			return false;

		binary_op->rhs = expr_stk.last();

		expr_stk.pop();

		binary_op->lhs = expr_stk.last();

		binary_op->op = op.binary_op;

		memset(&expr_stk.last(), 0, sizeof(expr_stk.last()));

		expr_stk.last().binary_op = binary_op;

		expr_stk.last().tag = ast::Expr::Tag::BinaryOp;
	}
	else if (op.tag == ShuntingYardOp::Tag::UnaryOp)
	{
		assert(expr_stk.size() >= 1);

		ast::UnaryOp* unary_op = nullptr;

		if (!alloc(s, ctx, &unary_op))
			return false;

		unary_op->operand = expr_stk.last();

		unary_op->op = op.unary_op;

		memset(&expr_stk.last(), 0, sizeof(expr_stk.last()));

		expr_stk.last().unary_op = unary_op;

		expr_stk.last().tag = ast::Expr::Tag::UnaryOp;
	}
	else if (op.tag == ShuntingYardOp::Tag::Mut)
	{
		if (expr_stk.last().is_mut)
			return error_invalid_syntax(s, ctx, peek(s), "Multiple occurrences of Mut");

		expr_stk.last().is_mut = true;
	}
	else
	{
		assert(op.tag == ShuntingYardOp::Tag::ParenBeg || op.tag == ShuntingYardOp::Tag::BracketBeg);

		return error_invalid_syntax(s, ctx, peek(s), "Misnested brackets / parentheses");
	}

	return true;
}

static bool get_bracket_op(pstate& s, u32& bracket_nesting, ShuntingYardOp& out) noexcept
{
	constexpr const char* const ctx = "Expr";

	const Token& t = peek(s);

	switch (t.tag)
	{
	case Token::Tag::INVALID:
		return error_unexpected_end(s, ctx);

	case Token::Tag::BracketEnd:
		next(s, ctx);
		
		out = { 2, false, ast::UnaryOp::Op::TypeSlice };

		return true;

	case Token::Tag::OpMul_Ptr:
	case Token::Tag::TripleDot:
		if (const Token& t1 = peek(s, 1); t1.tag == Token::Tag::BracketEnd)
		{
			next(s, ctx);

			next(s, ctx);

			if (t.tag == Token::Tag::OpMul_Ptr)
				out = { 2, false, ast::UnaryOp::Op::TypeMultiptr };
			else if (t.tag == Token::Tag::TripleDot)
				out = { 2, false, ast::UnaryOp::Op::TypeTailArray };
			else
				assert(false);

			return true;
		}

		break;

	default:
		break;
	}

	++bracket_nesting;

	out = { ShuntingYardOp::Tag::BracketBeg };

	return true;
}

static bool parse_simple_expr(pstate& s, ast::Expr& out) noexcept
{
	constexpr const char* const ctx = "Expr";

	bool expecting_operator = false;

	vec<ast::Expr, 32> expr_stk;

	vec<ShuntingYardOp, 32> op_stk;

	u32 paren_nesting = 0;

	u32 bracket_nesting = 0;

	const Token& first_token = peek(s);

	while (true)
	{
		const Token& t = peek(s);

		switch (t.tag)
		{
		case Token::Tag::INVALID: {

			if (!expecting_operator)
				return error_unexpected_end(s, ctx);

			goto POP_REMAINING_OPS;
		}

		case Token::Tag::Mut: {
			
			next(s, ctx);

			if (!op_stk.push_back({ ShuntingYardOp::Tag::Mut, 2, false }))
				return error_out_of_memory(s, ctx);

			break;
		}

		case Token::Tag::LitString:
		case Token::Tag::LitChar:
		case Token::Tag::LitInt:
		case Token::Tag::LitFloat:
		case Token::Tag::Ident: {

			if (expecting_operator)
				goto POP_REMAINING_OPS;

			if (!expr_stk.push_back({}))
				return error_out_of_memory(s, ctx);

			ast::Expr& expr = expr_stk.last();

			if (t.tag == Token::Tag::Ident)
			{
				next(s, ctx);

				expr.tag = ast::Expr::Tag::Ident;

				const strview ident_strview = t.data_strview();

				if (ident_strview.len() > UINT16_MAX)
					return error_invalid_syntax(s, ctx, t, "Length of ident somehow exceeds (2^16)-1");

				expr.ident_beg = ident_strview.begin();

				expr.ident_len = static_cast<u16>(ident_strview.len());
			}
			else
			{
				expr.tag = ast::Expr::Tag::Literal;

				if (!alloc_and_parse(s, ctx, &expr.literal))
					return false;
			}

			expecting_operator = true;

			break;
		}

		case Token::Tag::ParenBeg: {

			next(s, ctx);

			if (expecting_operator)
			{
				while (op_stk.size() != 0 && op_stk.last().precedence <= 1)
				{
					if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
						return false;
				}

				ast::Call* call = nullptr;

				if (!alloc(s, ctx, &call))
					return false;

				assert(expr_stk.size() != 0);

				call->callee = expr_stk.last();

				expr_stk.last().call = call;

				expr_stk.last().tag = ast::Expr::Tag::Call;

				if (next_if(s, Token::Tag::ParenEnd))
					break;

				while (true)
				{
					if (!alloc_and_parse(s, ctx, call->arguments))
						return false;

					if (const Token& t1 = next(s, ctx); t1.tag == Token::Tag::ParenEnd)
						break;
					else if (t1.tag != Token::Tag::Comma)
						return error_invalid_syntax(s, ctx, t1, "Expected ParenEnd or Comma");
				}
			}
			else
			{
				++paren_nesting;

				if (!op_stk.push_back({ ShuntingYardOp::Tag::ParenBeg }))
					return error_out_of_memory(s, ctx);
			}

			break;
		}

		case Token::Tag::ParenEnd: {

			if (!expecting_operator)
				return error_invalid_syntax(s, ctx, t, "Expected Ident, Literal or Unary Operator");

			if (paren_nesting == 0)
				goto POP_REMAINING_OPS;

			next(s, ctx);

			--paren_nesting;

			assert(op_stk.size() != 0);

			while (op_stk.last().tag != ShuntingYardOp::Tag::ParenBeg)
			{
				if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
					return false;

				assert(op_stk.size() != 0);
			}

			op_stk.pop();
			
			break;
		}

		case Token::Tag::BracketBeg: {

			if (expecting_operator)
			{
				if (const Token& t1 = peek(s, 1); t1.tag == Token::Tag::BracketEnd || t1.tag == Token::Tag::OpMul_Ptr || t1.tag == Token::Tag::TripleDot)
					goto POP_REMAINING_OPS;

				next(s, ctx);

				while (op_stk.size() != 0 && op_stk.last().precedence <= 1)
				{
					if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
						return false;
				}

				assert(expr_stk.size() != 0);

				ast::BinaryOp* index_op = nullptr;

				if (!alloc(s, ctx, &index_op))
					return false;

				index_op->lhs = expr_stk.last();

				index_op->op = ast::BinaryOp::Op::Index;

				expr_stk.last().binary_op = index_op;

				expr_stk.last().tag = ast::Expr::Tag::BinaryOp;

				if (!parse(s, index_op->rhs, false))
					return false;

				if (expect(s, ctx, Token::Tag::BracketEnd).tag == Token::Tag::INVALID)
					return false;
			}
			else
			{
				next(s, ctx);

				ShuntingYardOp op;

				if (!get_bracket_op(s, bracket_nesting, op))
					return false;

				if (!op_stk.push_back(op))
					return error_out_of_memory(s, ctx);
			}

			break;
		}

		case Token::Tag::BracketEnd: {

			if (!expecting_operator)
				return error_invalid_syntax(s, ctx, t, "Expected Ident, Literal or Unary Operator");

			if (bracket_nesting == 0)
				goto POP_REMAINING_OPS;

			next(s, ctx);

			--bracket_nesting;

			assert(op_stk.size() != 0);

			while (op_stk.last().precedence != 255)
			{
				if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
					return false;

				assert(op_stk.size() != 0);
			}

			if (op_stk.last().tag != ShuntingYardOp::Tag::BracketBeg)
				return error_invalid_syntax(s, ctx, t, "Misnested brackets / parentheses");

			op_stk.last() = { 2, true, ast::BinaryOp::Op::TypeArray };

			expecting_operator = false;

			break;
		}

		default: {

			ShuntingYardOp op{};

			if (!token_tag_to_shunting_yard_op(t.tag, expecting_operator, op))
			{
				if (expecting_operator)
					goto POP_REMAINING_OPS;
				else
					return error_invalid_syntax(s, ctx, t, "Expected Ident, Literal, Unary Operator, ParenBeg or BracketBeg");
			}

			next(s, ctx);

			while (op_stk.size() != 0)
			{
				const ShuntingYardOp prev = op_stk.last();

				if (prev.precedence >= op.precedence && !(prev.precedence == op.precedence && op.is_left_assoc))
					break;

				if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
					return false;
			}

			if (!op_stk.push_back(op))
				return error_out_of_memory(s, ctx);

			expecting_operator = false;

			break;
		}
		}
	}

POP_REMAINING_OPS:

	if (paren_nesting != 0)
		return error_invalid_syntax(s, ctx, first_token, "Unmatched ParenBeg");

	if (bracket_nesting != 0)
		return error_invalid_syntax(s, ctx, first_token, "Unmatched BracketBeg");

	while (op_stk.size() != 0)
	{
		if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
			return false;
	}

	if (expr_stk.size() != 1)
		return error_invalid_syntax(s, ctx, first_token, "Too many subexpressions");

	out = expr_stk.first();

	return true;
}



static bool parse(pstate& s, ast::FloatLiteral& out) noexcept
{
	static constexpr const char ctx[] = "FloatLiteral";

	const Token& t = expect(s, ctx, Token::Tag::LitFloat);

	if (t.tag == Token::Tag::INVALID)
		return false;

	out.value = atof(t.data_strview().begin());

	return true;
}

static bool parse(pstate& s, ast::IntegerLiteral& out) noexcept
{
	static constexpr const char ctx[] = "IntegerLiteral";

	const Token& t = expect(s, ctx, Token::Tag::LitInt);

	if (t.tag == Token::Tag::INVALID)
		return false;

	const strview str = t.data_strview();

	usz value = 0;

	if (str.len() >= 3 && str[0] == '0')
	{
		const char base = str[1];
		
		if (base == 'x' || base == 'X')
		{
			for (const char* c = str.begin() + 2; c != str.end(); ++c)
				if (!hex_val(*c, value))
					return error_invalid_syntax(s, ctx, t, "Not a valid hexadecimal string");

			out.value = value;

			return true;
		}
		else if (base == 'o' || base == 'O')
		{
			for (const char* c = str.begin() + 2; c != str.end(); ++c)
				if (!oct_val(*c, value))
					return error_invalid_syntax(s, ctx, t, "Not a valid octal string");

			out.value = value;

			return true;
		}
		else if (base == 'b' || base == 'B')
		{
			for (const char* c = str.begin() + 2; c != str.end(); ++c)
				if (!bin_val(*c, value))
					return error_invalid_syntax(s, ctx, t, "Not a valid octal string");

			out.value = value;

			return true;
		}
	}

	if (str.len() >= 2)
	{
		const char base = str[1];

		if (base == 'x' || base == 'X' || base == 'o' || base == 'O' || base == 'b' || base == 'B')
			return error_invalid_syntax(s, ctx, t, "Cannot have empty integer literal");
	}

	for (const char* c = str.begin(); c != str.end(); ++c)
		if (!dec_val(*c, value))
			return error_invalid_syntax(s, ctx, t, "Not a valid decimal string");

	out.value = value;

	return true;
}

static bool parse(pstate& s, ast::CharLiteral& out) noexcept
{
	static constexpr const char ctx[] = "CharLiteral";

	const Token& t = expect(s, ctx, Token::Tag::LitChar);

	if (t.tag == Token::Tag::INVALID)
		return false;

	const strview str = t.data_strview();

	if (str.len() == 0)
		return error_invalid_syntax(s, ctx, t, "Empty character literal");

	const unsigned char cp0 = static_cast<unsigned char>(str[0]);

	if (cp0 <= 0x7F)
	{
		usz codepoint = 0;

		if (str[0] == '\\')
		{
			if (str.len() == 1)
				return error_invalid_syntax(s, ctx, t, "Empty escape sequence");

			const char escapee = str[1];

			if (escapee == 'x')
			{
				if (str.len() <= 2)
					return error_invalid_syntax(s, ctx, t, "Empty hexadecimal character escape sequence");

				for (const char* c = str.begin() + 2; c != str.end(); ++c)
					if (!hex_val(*c, codepoint))
						return error_invalid_syntax(s, ctx, t, "Non-hexadecimal character in hexadecimal character escape sequence");
			}
			else if (escapee >= '0' && escapee <= '9')
			{
				for (const char* c = str.begin() + 2; c != str.end(); ++c)
					if (!dec_val(*c, codepoint))
						return error_invalid_syntax(s, ctx, t, "Non-decimal character in decimal character escape sequence");
			}
			else
			{
				switch(escapee)
				{
				case  'a': out.value[0] = '\a'; break;
				case  'b': out.value[0] = '\b'; break;
				case  'f': out.value[0] = '\f'; break;
				case  'n': out.value[0] = '\n'; break;
				case  'r': out.value[0] = '\r'; break;
				case  't': out.value[0] = '\t'; break;
				case  'v': out.value[0] = '\v'; break;
				case '\\': out.value[0] = '\\'; break;
				case '\'': out.value[0] = '\''; break;
				default: return error_invalid_syntax(s, ctx, t, "Unknown character escape sequence");
				}

				return true;
			}

			if (codepoint <= 0x7F)
			{
				out.value[0] = static_cast<char>(codepoint);
			}
			else if (codepoint <= 0x7FF)
			{
				out.value[0] = static_cast<char>(0xC0 | (codepoint >> 6));

				out.value[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else if (codepoint <= 0xFFFF)
			{
				out.value[0] = static_cast<char>(0xE0 | (codepoint >> 12));

				out.value[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));

				out.value[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else if (codepoint <= 0x10FFFF)
			{
				out.value[0] = static_cast<char>(0xF0 | (codepoint >> 18));

				out.value[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));

				out.value[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));

				out.value[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
			}
			else
			{
				return error_invalid_syntax(s, ctx, t, "Value of decimal character escape sequence exceeds maximal unicode code point (0x10FFFF)");
			}

			return true;
		}
		else if (str.len() != 1)
		{
			return error_invalid_syntax(s, ctx, t, "Invalid length for single-byte code point");
		}
	}
	else if (cp0 <= 0xBF)
	{
		if (str.len() != 2)
			return error_invalid_syntax(s, ctx, t, "Invalid length for two-byte code point");
	}
	else if (cp0 <= 0xDF)
	{
		if (str.len() != 3)
			return error_invalid_syntax(s, ctx, t, "Invalid length for three-byte code point");
	}
	else if (cp0 <= 0xEF)
	{
		if (str.len() != 4)
			return error_invalid_syntax(s, ctx, t, "Invalid length for four-byte code point");
	}
	else
	{
		return error_invalid_syntax(s, ctx, t, "Unexpected code unit");
	}

	out.value[0] = static_cast<char>(cp0);

	for (usz i = 1; i != str.len(); ++i)
		if ((str[i] & 0xC0) != 0x80)
			return error_invalid_syntax(s, ctx, t, "Invalid surrogate codeunit");
		else
			out.value[i] = str[i];

	return true;
}

static bool parse(pstate& s, ast::StringLiteral& out) noexcept
{
	static constexpr const char ctx[] = "StringLiteral";

	const Token& t = expect(s, ctx, Token::Tag::LitString);

	if (t.tag == Token::Tag::INVALID)
		return false;

	const strview str = t.data_strview();

	for (const char* c = str.begin(); c != str.end(); ++c)
	{
		if (*c == '\\')
		{
			char escaped;

			++c;

			if (c == str.end())
				return error_invalid_syntax(s, ctx, t, "Empty character escape sequence");

			switch (*c)
			{
			case  'a': escaped = '\a'; break;
			case  'b': escaped = '\b'; break;
			case  'f': escaped = '\f'; break;
			case  'n': escaped = '\n'; break;
			case  'r': escaped = '\r'; break;
			case  't': escaped = '\t'; break;
			case  'v': escaped = '\v'; break;
			case '\\': escaped = '\\'; break;
			case '\"': escaped = '"'; break;
			default: return error_invalid_syntax(s, ctx, t, "Invalid character escape sequence");
			}

			if (!out.value.push_back(escaped))
				return error_out_of_memory(s, ctx);
		}
		else if (!out.value.push_back(*c))
		{
			return error_out_of_memory(s, ctx);
		}
	}

	return true;
}

static bool parse(pstate& s, ast::Literal& out) noexcept
{
	constexpr const char* const ctx = "Literal";

	const Token& t = peek(s);

	switch (t.tag)
	{
	case Token::Tag::INVALID:
		return error_unexpected_end(s, ctx);

	case Token::Tag::LitString:
		out.tag = ast::Literal::Tag::StringLiteral;
		return parse(s, out.string_literal);

	case Token::Tag::LitChar:
		out.tag = ast::Literal::Tag::CharLiteral;
		return parse(s, out.char_literal);

	case Token::Tag::LitInt:
		out.tag = ast::Literal::Tag::IntegerLiteral;
		return parse(s, out.integer_literal);
	
	case Token::Tag::LitFloat:
		out.tag = ast::Literal::Tag::FloatLiteral;
		return parse(s, out.float_literal);

	default:
		return error_invalid_syntax(s, ctx, t, "Expected LitString, LitChar, LitInt or LitFloat");
	}
}

static bool parse(pstate& s, ast::ForLoopSignature& out) noexcept
{
	constexpr const char* const ctx = "ForLoopSignature";

	if (const Token& t = peek(s, 1); t.tag == Token::Tag::Colon || t.tag == Token::Tag::DoubleColon)
	{
		if (!alloc_and_parse(s, ctx, &out.opt_init))
			return false;

		if (expect(s, ctx, Token::Tag::Semicolon).tag == Token::Tag::INVALID)
			return false;
	}

	if (next_if(s, Token::Tag::Do))
		return true;

	if (!parse(s, out.opt_condition, false))
		return false;

	if (next_if(s, Token::Tag::Semicolon))
	{
		if (!parse(s, out.opt_step))
			return false;
	}

	next_if(s, Token::Tag::Do);

	return true;
}

static bool parse(pstate& s, ast::ForEachSignature& out) noexcept
{
	constexpr const char* const ctx = "ForEachSignature";

	if (const Token& t = expect(s, ctx, Token::Tag::Ident); t.tag == Token::Tag::INVALID)
		return false;
	else
		out.loop_var = t.data_strview();

	if (next_if(s, Token::Tag::Comma))
	{
		if (const Token& t = expect(s, ctx, Token::Tag::Ident); t.tag == Token::Tag::INVALID)
			return false;
		else
			out.opt_index_var = t.data_strview();
	}

	if (expect(s, ctx, Token::Tag::ArrowLeft).tag == Token::Tag::INVALID)
		return false;

	if (!parse(s, out.looped_over, false))
		return false;

	next_if(s, Token::Tag::Do);

	return true;
}

static bool parse(pstate& s, ast::Case& out) noexcept
{
	constexpr const char* const ctx = "Case";

	if (expect(s, ctx, Token::Tag::Case).tag == Token::Tag::INVALID)
		return false;

	while (true)
	{
		if (!alloc_and_parse(s, ctx, out.labels, false))
			return false;
		
		if (const Token& t = next(s, ctx); t.tag == Token::Tag::FatArrowRight)
			break;
		else if (t.tag != Token::Tag::Comma)
			return error_invalid_syntax(s, ctx, t, "Expected FatArrowRight or Comma");
	}

	return parse(s, out.body);
}

static bool parse(pstate& s, ast::If& out) noexcept
{
	constexpr const char* const ctx = "If";

	if (expect(s, ctx, Token::Tag::If).tag == Token::Tag::INVALID)
		return false;

	if (const Token& t = peek(s, 1); t.tag == Token::Tag::Colon || t.tag == Token::Tag::DoubleColon)
	{
		if (!alloc_and_parse(s, ctx, &out.opt_init))
			return false;

		if (expect(s, ctx, Token::Tag::Semicolon).tag == Token::Tag::INVALID)
			return false;
	}

	if (!parse(s, out.condition, false))
		return false;

	next_if(s, Token::Tag::Then);

	if (!parse(s, out.body))
		return false;

	if (next_if(s, Token::Tag::Else))
	{
		if (!parse(s, out.opt_else_body))
			return false;
	}

	return true;
}

static bool parse(pstate& s, ast::For& out) noexcept
{
	constexpr const char* const ctx = "For";

	if (expect(s, ctx, Token::Tag::For).tag == Token::Tag::INVALID)
		return false;

	if (const Token& t = peek(s, 1); t.tag == Token::Tag::Comma || t.tag == Token::Tag::ArrowLeft)
	{
		if (!parse(s, out.for_each_signature))
			return false;

		out.tag = ast::For::Tag::ForEachSignature;
	}
	else
	{
		if (!parse(s, out.for_loop_signature))
			return false;

		out.tag = ast::For::Tag::ForLoopSignature;
	}
	
	if (!parse(s, out.body))
		return false;

	if (next_if(s, Token::Tag::Finally))
	{
		if (!parse(s, out.opt_finally_body))
			return false;
	}

	return true;
}

static bool parse(pstate& s, ast::Switch& out) noexcept
{
	constexpr const char* const ctx = "Switch";

	if (expect(s, ctx, Token::Tag::Switch).tag == Token::Tag::INVALID)
		return false;

	if (const Token& t = peek(s, 1); t.tag == Token::Tag::Colon || t.tag == Token::Tag::DoubleColon)
	{
		if (!alloc_and_parse(s, ctx, &out.opt_init))
			return false;

		if (expect(s, ctx, Token::Tag::Semicolon).tag == Token::Tag::INVALID)
			return false;
	}

	if (!parse(s, out.switched_expr, false))
		return false;
		
	while (true)
	{
		if (!alloc_and_parse(s, ctx, out.cases))
			return false;

		if (const Token& t = peek(s); t.tag != Token::Tag::Case)
			return true;
	}
}

static bool parse(pstate& s, ast::Block& out, bool is_top_level = false) noexcept
{
	constexpr const char* const ctx = "Block";

	if (!is_top_level)
	{
		if (expect(s, ctx, Token::Tag::SquiggleBeg).tag == Token::Tag::INVALID)
			return false;
	}

	while (true)
	{
		if (const Token& t = peek(s); t.tag == Token::Tag::INVALID)
		{
			if (is_top_level)
				return true;

			return error_unexpected_end(s, ctx);
		}
		else if (t.tag == Token::Tag::SquiggleEnd)
		{
			if (is_top_level)
				return error_invalid_syntax(s, ctx, t, "SquiggleEnd at end of top level Block");

			next(s, ctx);

			return true;
		}

		if (!alloc_and_parse(s, ctx, out.statements))
			return false;
	}
}

static bool parse(pstate& s, ast::Signature& out) noexcept
{
	constexpr const char* const ctx = "ProcSignature";

	const Token& t = next(s, ctx);

	bool is_procish;

	switch (t.tag)
	{
	case Token::Tag::INVALID:
		return false;

	case Token::Tag::Proc:
	case Token::Tag::Func:
		is_procish = true;
		break;

	case Token::Tag::Trait:
		is_procish = false;
		break;
	
	default:
		return error_invalid_syntax(s, ctx, t, "Expected Proc, Func or Trait");
	}

	if (!next_if(s, Token::Tag::ParenBeg))
	{
		if (is_procish)
			goto RETURN_TYPE;
		
		return error_invalid_syntax(s, ctx, peek(s), "Expected ParenBeg");
	}

	if (next_if(s, Token::Tag::ParenEnd))
		goto RETURN_TYPE;

	while (true)
	{
		if (!alloc_and_parse(s, ctx, out.parameters))
			return false;

		if (const Token& t1 = next(s, ctx); t1.tag == Token::Tag::ParenEnd)
			break;
		else if (t1.tag != Token::Tag::Comma)
			return error_invalid_syntax(s, ctx, t1, "Expected ParenEnd or Comma");
	}

RETURN_TYPE:

	if (!is_procish)
		return true;

	if (next_if(s, Token::Tag::ArrowRight))
	{
		if (!parse(s, out.opt_return_type, false))
			return false;
	}

	return true;
}

static bool parse(pstate& s, ast::Impl& out) noexcept
{
	constexpr const char* const ctx = "Impl";

	if (expect(s, ctx, Token::Tag::Impl).tag == Token::Tag::INVALID)
		return false;

	if (!parse(s, out.bound_trait))
		return false;

	return parse(s, out.body);
}

static bool parse(pstate& s, ast::Expr& out, bool allow_assignment) noexcept
{
	constexpr const char* const ctx = "Expr";

	switch (peek(s).tag)
	{
	case Token::Tag::INVALID:
		return error_unexpected_end(s, ctx);

	case Token::Tag::Proc:
		out.tag = ast::Expr::Tag::ProcSignature;

		return alloc_and_parse(s, ctx, &out.signature);

	case Token::Tag::Func:
		out.tag = ast::Expr::Tag::FuncSignature;

		return alloc_and_parse(s, ctx, &out.signature);
		
	case Token::Tag::Trait:

		out.tag = ast::Expr::Tag::TraitSignature;

		return alloc_and_parse(s, ctx, &out.signature);

	case Token::Tag::If:
		out.tag = ast::Expr::Tag::If;

		return alloc_and_parse(s, ctx, &out.if_expr);
	
	case Token::Tag::For:
		out.tag = ast::Expr::Tag::For;

		return alloc_and_parse(s, ctx, &out.for_expr);

	case Token::Tag::Switch:
		out.tag = ast::Expr::Tag::Switch;

		return alloc_and_parse(s, ctx, &out.switch_expr);

	case Token::Tag::SquiggleBeg:
		out.tag = ast::Expr::Tag::Block;

		return alloc_and_parse(s, ctx, &out.block);

	case Token::Tag::Return:
		next(s, ctx);

		out.tag = ast::Expr::Tag::Return;

		return alloc_and_parse(s, ctx, &out.return_or_break_or_defer, false);

	case Token::Tag::Break:
		next(s, ctx);

		out.tag = ast::Expr::Tag::Break;

		return alloc_and_parse(s, ctx, &out.return_or_break_or_defer, false);

	case Token::Tag::Defer:
		next(s, ctx);

		out.tag = ast::Expr::Tag::Defer;

		return alloc_and_parse(s, ctx, &out.return_or_break_or_defer, false);

	case Token::Tag::Impl:
		out.tag = ast::Expr::Tag::Impl;

		return alloc_and_parse(s, ctx, &out.impl);

	case Token::Tag::Module:
		next(s, ctx);

		out.tag = ast::Expr::Tag::Module;

		return true; // Nothing to parse

	default:
		if (const Token& t = peek(s, 1); t.tag != Token::Tag::Colon && t.tag != Token::Tag::DoubleColon)
			break;

		out.tag = ast::Expr::Tag::Definition;

		return alloc_and_parse(s, ctx, &out.definition);
	}

	if (!parse_simple_expr(s, out))
		return false;

	const Token& t = peek(s);

	if (t.tag == Token::Tag::Catch)
	{
		next(s, ctx);

		ast::Catch* catch_expr;

		if (!alloc(s, ctx, &catch_expr))
			return false;

		catch_expr->caught_expr = out;

		out.tag = ast::Expr::Tag::Catch;

		out.catch_expr = catch_expr;

		if (peek(s, 1).tag == Token::Tag::ArrowRight)
		{
			if (const Token& ident_tok = expect(s, ctx, Token::Tag::Ident); ident_tok.tag == Token::Tag::INVALID)
				return false;
			else
				catch_expr->opt_caught_ident = ident_tok.data_strview();

			next(s, ctx);
		}

		return parse(s, catch_expr->catching_expr);
	}
	else if (allow_assignment && t.tag >= Token::Tag::Set && t.tag <= Token::Tag::SetShiftR)
	{
		next(s, ctx);

		const ast::BinaryOp::Op set_operator = static_cast<ast::BinaryOp::Op>(static_cast<usz>(ast::BinaryOp::Op::Set) + static_cast<usz>(t.tag) - static_cast<usz>(Token::Tag::Set));

		ast::BinaryOp* top_level_op = nullptr;

		if (!alloc(s, ctx, &top_level_op))
			return false;

		top_level_op->op = set_operator;

		top_level_op->lhs = out;

		out.tag = ast::Expr::Tag::BinaryOp;

		out.binary_op = top_level_op;

		return parse(s, top_level_op->rhs);
	}
	else
	{
		return true;
	}
}

static bool parse(pstate& s, ast::Definition& out) noexcept
{
	constexpr const char* const ctx = "Definition";

	if (const Token& t = expect(s, ctx, Token::Tag::Ident); t.tag == Token::Tag::INVALID)
		return false;
	else
		out.ident = t.data_strview();

	if (const Token& t = next(s, ctx); t.tag == Token::Tag::DoubleColon)
		out.is_comptime = true;
	else if (t.tag != Token::Tag::Colon)
		return error_invalid_syntax(s, ctx, t, "Expected Colon or DoubleColon");

	while (true)
	{
		const Token& t = peek(s);

		if (t.tag == Token::Tag::Pub)
		{
			if (out.is_pub)
				return error_invalid_syntax(s, ctx, t, "Pub specified more than once");

			out.is_pub = true;
		}
		else if (t.tag == Token::Tag::Global)
		{
			if (out.is_global)
				return error_invalid_syntax(s, ctx, t, "Global specified more than once");

			out.is_global = true;
		}
		else
		{
			break;
		}

		next(s, ctx);
	}

	if (const Token& t = peek(s); t.tag == Token::Tag::INVALID)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t.tag != Token::Tag::Set)
	{
		if (!parse(s, out.opt_type, false))
			return false;
	}

	if (next_if(s, Token::Tag::Set))
		return parse(s, out.opt_value, false);

	if (out.opt_type.tag == ast::Expr::Tag::EMPTY)
		return error_invalid_syntax(s, ctx, peek(s), "Expected a type, a value or both");

	return true;
}

static bool parse(pstate& s, const strview filename, ast::FileModule& out) noexcept
{
	constexpr const char* const ctx = "FileModule";

	out.filename = filename;

	out.root_module.ident = filename;

	out.root_module.is_comptime = true;

	out.root_module.is_pub = true;

	out.root_module.opt_type.tag = ast::Expr::Tag::Module;

	out.root_module.opt_value.tag = ast::Expr::Tag::Block;

	return alloc_and_parse(s, ctx, &out.root_module.opt_value.block, true);
}

ast::Result ast::parse_program_unit(const vec<Token>& tokens, const strview filename, FileModule& out) noexcept
{
	pstate s{ tokens.begin(), tokens.end(), tokens.begin(), {} };

	parse(s, filename, out);

	return s.rst;
}
