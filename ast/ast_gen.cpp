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

static bool error_invalid_syntax(pstate& s, const char* ctx, const Token* curr, const char* msg) noexcept
{
	s.rst.tag = ast::Result::Tag::InvalidSyntax;

	s.rst.error_ctx = ctx;

	s.rst.message = msg;

	s.rst.problematic_token = curr;

	return false;
}

static bool error_unexpected_token(pstate& s, const char* ctx, const Token* curr, Token::Tag expected) noexcept
{
	s.rst.tag = ast::Result::Tag::UnexpectedToken;

	s.rst.error_ctx = ctx;

	s.rst.expected_token = expected;

	s.rst.problematic_token = curr;

	return false;
}

static bool error_unexpected_end(pstate& s, const char* ctx) noexcept
{
	s.rst.tag = ast::Result::Tag::UnexpectedEndOfStream;

	s.rst.error_ctx = ctx;

	s.rst.message = "Unexpectedly ran out of input Tokens.";

	return false;
}



static const Token* peek(const pstate& s, usz offset = 0) noexcept
{
	if (s.curr + offset >= s.end)
		return nullptr;

	return s.curr + offset;
}

static const Token* next(pstate& s, const char* ctx) noexcept
{
	if (s.curr >= s.end)
	{
		error_unexpected_end(s, ctx);

		return nullptr;
	}

	return s.curr++;
}

static const Token* expect(pstate& s, const char* ctx, Token::Tag expected) noexcept
{
	const Token* t = next(s, ctx);

	if (t != nullptr)
	{
		if (t->tag != expected)
			error_unexpected_token(s, ctx, t, expected);
		else
			return t;
	}

	return nullptr;
}

static const Token* next_if(pstate& s, Token::Tag expected) noexcept
{
	const Token* t = peek(s);

	if (t != nullptr && t->tag == expected)
	{
		++s.curr;

		return t;
	}

	return nullptr;
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



static bool parse(pstate& s, ast::Type& out) noexcept;

static bool parse(pstate& s, ast::Definition& out) noexcept;

static bool parse(pstate& s, ast::Expr& out, bool allow_assignment = true) noexcept;

static bool parse(pstate& s, ast::Literal& out) noexcept;



struct ShuntingYardOp
{
	u8 precedence;

	bool is_left_assoc;

	ast::BinaryOp::Op binary_op;

	ast::UnaryOp::Op unary_op;

	constexpr ShuntingYardOp() noexcept
		: precedence{ 0 }, is_left_assoc{ 0 }, binary_op{ ast::BinaryOp::Op::NONE }, unary_op{ ast::UnaryOp::Op::NONE } {}

	constexpr ShuntingYardOp(u8 precedence, bool is_left_assoc, ast::BinaryOp::Op op) noexcept
		: precedence{ precedence }, is_left_assoc{ is_left_assoc }, binary_op{ op }, unary_op{ ast::UnaryOp::Op::NONE } {}

	constexpr ShuntingYardOp(u8 precedence, bool is_left_assoc, ast::UnaryOp::Op op) noexcept
		: precedence{ precedence }, is_left_assoc{ is_left_assoc }, binary_op{ ast::BinaryOp::Op::NONE }, unary_op{ op } {}
};

static bool token_tag_to_shunting_yard_op(const Token::Tag tag, bool is_binary, ShuntingYardOp& out) noexcept
{
	if (is_binary)
	{
		static constexpr const ShuntingYardOp binary_ops[] {
			{  4, true, ast::BinaryOp::Op::Add    },
			{  4, true, ast::BinaryOp::Op::Sub    },
			{  3, true, ast::BinaryOp::Op::Mul    },
			{  3, true, ast::BinaryOp::Op::Div    },
			{  3, true, ast::BinaryOp::Op::Mod    },
			{  8, true, ast::BinaryOp::Op::BitAnd },
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
		};

		const usz offset = static_cast<usz>(tag) - static_cast<usz>(Token::Tag::OpAdd);

		const bool is_in_range = offset < _countof(binary_ops);

		if (offset < _countof(binary_ops))
			out = binary_ops[offset];

		return is_in_range;
	}

	switch (tag)
	{
	case Token::Tag::UOpBitNot:
		out = {  2 , false, ast::UnaryOp::Op::BitNot };
		break;

	case Token::Tag::UOpLogNot:
		out = {  2 , false, ast::UnaryOp::Op::LogNot };
		break;

	case Token::Tag::UOpDeref:
		out = {  2 , false, ast::UnaryOp::Op::Deref  };
		break;

	case Token::Tag::OpBitAnd_Ref:
		out = {  2 , false, ast::UnaryOp::Op::AddrOf };
		break;

	case Token::Tag::OpSub:
		out = {  2 , false, ast::UnaryOp::Op::Neg    };
		break;

	case Token::Tag::Try:
		out = { 13 , false, ast::UnaryOp::Op::Try    };
		break;

	default:
		return false;
	}

	return true;
}

static bool pop_shunting_yard_operator(pstate& s, vec<ShuntingYardOp, 32>& op_stk, vec<ast::Expr, 32>& expr_stk) noexcept
{
	constexpr const char* const ctx = "Expr";

	assert(op_stk.size() != 0);

	const ShuntingYardOp op = op_stk.last();

	op_stk.pop();

	if (op.binary_op != ast::BinaryOp::Op::NONE)
	{
		assert(expr_stk.size() >= 2);

		ast::BinaryOp* binary_op = nullptr;

		if (!alloc(s, ctx, &binary_op))
			return false;

		binary_op->rhs = expr_stk.last();

		expr_stk.pop();

		binary_op->lhs = expr_stk.last();

		binary_op->op = op.binary_op;

		expr_stk.last().binary_op = binary_op;

		expr_stk.last().tag = ast::Expr::Tag::BinaryOp;		
	}
	else
	{
		assert(op.unary_op != ast::UnaryOp::Op::NONE);

		assert(expr_stk.size() >= 1);

		ast::UnaryOp* unary_op = nullptr;

		if (!alloc(s, ctx, &unary_op))
			return false;

		unary_op->operand = expr_stk.last();

		unary_op->op = op.unary_op;

		expr_stk.last().unary_op = unary_op;

		expr_stk.last().tag = ast::Expr::Tag::UnaryOp;
	}

	return true;
}

static bool parse_simple_expr(pstate& s, ast::Expr& out) noexcept
{
	constexpr const char* const ctx = "Expr";

	bool expecting_operator = false;

	vec<ast::Expr, 32> expr_stk;

	vec<ShuntingYardOp, 32> op_stk;

	usz paren_nesting = 0;

	const Token* prev_paren_beg = nullptr;

	const Token* const first_token = peek(s);

	while (true)
	{
		const Token* t = peek(s);

		if (t == nullptr)
		{
			if (!expecting_operator)
				return error_unexpected_end(s, ctx);
			
			goto POP_REMAINING_OPS;
		}

		switch (t->tag)
		{
		case Token::Tag::Ident: {

			if (expecting_operator)
				goto POP_REMAINING_OPS;

			next(s, ctx);

			if (!expr_stk.push_back({}))
				return error_out_of_memory(s, ctx);

			ast::Expr& expr = expr_stk.last();

			const strview ident_strview = t->data_strview();

			if (ident_strview.len() > UINT32_MAX)
				return error_invalid_syntax(s, ctx, t, "Length of ident somehow exceeds (2^32)-1");

			expr.ident_beg = ident_strview.begin();

			expr.ident_len = static_cast<u32>(ident_strview.len());

			expr.tag = ast::Expr::Tag::Ident;

			expecting_operator = true;

			break;
		}

		case Token::Tag::LitString:
		case Token::Tag::LitChar:
		case Token::Tag::LitInt:
		case Token::Tag::LitFloat: {

			if (expecting_operator)
				goto POP_REMAINING_OPS;

			if (!expr_stk.push_back({}))
				return error_out_of_memory(s, ctx);

			ast::Expr& expr = expr_stk.last();

			if (!alloc(s, ctx, &expr.literal))
				return false;

			expr.tag = ast::Expr::Tag::Literal;

			if (!parse(s, *expr.literal))
				return false;

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

				if (next_if(s, Token::Tag::ParenEnd) != nullptr)
					break;
					
				while (true)
				{
					if (!call->arguments.push_back({}))
						return error_out_of_memory(s, ctx);

					if (!parse(s, call->arguments.last()))
						return false;

					if (const Token* t1 = next(s, ctx); t1 == nullptr)
						return false;
					else if (t1->tag == Token::Tag::ParenEnd)
						break;
					else if (t1->tag != Token::Tag::Comma)
						return error_invalid_syntax(s, ctx, t1, "Expected ParenEnd or Comma");
				}
			}
			else
			{
				prev_paren_beg = t;
				
				++paren_nesting;

				if (!op_stk.push_back({ 255, 1, ast::BinaryOp::Op::NONE }))
					return error_out_of_memory(s, ctx);
			}

			break;
		}

		case Token::Tag::ParenEnd: {

			if (!expecting_operator || paren_nesting == 0)
				goto POP_REMAINING_OPS;

			next(s, ctx);

			--paren_nesting;

			assert(op_stk.size() != 0);

			while (op_stk.last().precedence != 255)
			{
				if (!pop_shunting_yard_operator(s, op_stk, expr_stk))
					return false;

				assert(op_stk.size() != 0);
			}

			op_stk.pop();
			
			break;
		}

		case Token::Tag::BracketBeg: {

			if (!expecting_operator)
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

			if (expect(s, ctx, Token::Tag::BracketEnd) == nullptr)
				return false;

			break;
		}

		default: {

			ShuntingYardOp op{};

			if (!token_tag_to_shunting_yard_op(t->tag, expecting_operator, op))
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
		return error_invalid_syntax(s, ctx, prev_paren_beg, "Unmatched ParenBeg");

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

	const Token* t = expect(s, ctx, Token::Tag::LitFloat);

	if (t == nullptr)
		return false;

	out.value = atof(t->data_strview().begin());

	return true;
}

static bool parse(pstate& s, ast::IntegerLiteral& out) noexcept
{
	static constexpr const char ctx[] = "IntegerLiteral";

	const Token* t = expect(s, ctx, Token::Tag::LitInt);

	if (t == nullptr)
		return false;

	const strview str = t->data_strview();

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

	const Token* t = expect(s, ctx, Token::Tag::LitChar);

	if (t == nullptr)
		return false;

	const strview str = t->data_strview();

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

	const Token* t = expect(s, ctx, Token::Tag::LitString);

	if (t == nullptr)
		return false;

	const strview str = t->data_strview();

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

	const Token* t = peek(s);

	if (t == nullptr)
		return error_unexpected_end(s, ctx);

	switch (t->tag)
	{
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

	if (const Token* t = peek(s, 1); t != nullptr && (t->tag == Token::Tag::Colon || t->tag == Token::Tag::DoubleColon))
	{
		if (!alloc(s, ctx, &out.opt_init))
			return false;

		if (!parse(s, *out.opt_init))
			return false;

		if (expect(s, ctx, Token::Tag::Semicolon) == nullptr)
			return false;
	}

	if (next_if(s, Token::Tag::Do) != nullptr)
		return true;

	if (!parse(s, out.opt_condition, false))
		return false;

	if (next_if(s, Token::Tag::Semicolon) != nullptr)
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

	if (const Token* t = expect(s, ctx, Token::Tag::Ident); t == nullptr)
		return false;
	else
		out.loop_var = t->data_strview();

	if (next_if(s, Token::Tag::Comma) != nullptr)
	{
		if (const Token* t = expect(s, ctx, Token::Tag::Ident); t == nullptr)
			return false;
		else
			out.opt_index_var = t->data_strview();
	}

	if (expect(s, ctx, Token::Tag::ArrowLeft) == nullptr)
		return false;

	if (!parse(s, out.looped_over, false))
		return false;

	next_if(s, Token::Tag::Do);

	return true;
}

static bool parse(pstate& s, ast::Case& out) noexcept
{
	constexpr const char* const ctx = "Case";

	if (expect(s, ctx, Token::Tag::Case) == nullptr)
		return false;

	while (true)
	{
		if (!out.labels.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.labels.last(), false))
			return false;
		
		if (const Token* t = next(s, ctx); t == nullptr)
			return false;
		else if (t->tag == Token::Tag::ArrowRight)
			break;
		else if (t->tag != Token::Tag::Comma)
			return error_invalid_syntax(s, ctx, t, "Expected ArrowRight or Comma");
	}

	return parse(s, out.body);
}

static bool parse(pstate& s, ast::If& out) noexcept
{
	constexpr const char* const ctx = "If";

	if (expect(s, ctx, Token::Tag::If) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && (t->tag == Token::Tag::Colon || t->tag == Token::Tag::DoubleColon))
	{
		if (!alloc(s, ctx, &out.opt_init))
			return false;

		if (!parse(s, *out.opt_init))
			return false;

		if (expect(s, ctx, Token::Tag::Semicolon) == nullptr)
			return false;
	}

	if (!parse(s, out.condition, false))
		return false;

	next_if(s, Token::Tag::Then);

	if (!parse(s, out.body))
		return false;

	if (next_if(s, Token::Tag::Else) != nullptr)
	{
		if (!parse(s, out.opt_else_body))
			return false;
	}

	return true;
}

static bool parse(pstate& s, ast::For& out) noexcept
{
	constexpr const char* const ctx = "For";

	if (expect(s, ctx, Token::Tag::For) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && (t->tag == Token::Tag::Comma || t->tag == Token::Tag::ArrowLeft))
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

	if (next_if(s, Token::Tag::Finally) != nullptr)
	{
		if (!parse(s, out.opt_finally_body))
			return false;
	}

	return true;
}

static bool parse(pstate& s, ast::Switch& out) noexcept
{
	constexpr const char* const ctx = "Switch";

	if (expect(s, ctx, Token::Tag::Switch) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && (t->tag == Token::Tag::Colon || t->tag == Token::Tag::DoubleColon))
	{
		if (!alloc(s, ctx, &out.opt_init))
			return false;

		if (!parse(s, *out.opt_init))
			return false;

		if (expect(s, ctx, Token::Tag::Semicolon) == nullptr)
			return false;
	}

	if (!parse(s, out.switched_expr, false))
		return false;
		
	while (true)
	{
		if (!out.cases.push_back({}))
			return false;

		if (!parse(s, out.cases.last()))
			return false;

		if (const Token* t = peek(s); t == nullptr || t->tag != Token::Tag::Case)
			return true;
	}
}

static bool parse(pstate& s, ast::Block& out) noexcept
{
	constexpr const char* const ctx = "Block";

	if (expect(s, ctx, Token::Tag::SquiggleBeg) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t == nullptr)
			return error_unexpected_end(s, ctx);
		else if (t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (!out.statements.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.statements.last()))
			return false;
	}
}

static bool parse(pstate& s, ast::Signature& out) noexcept
{
	constexpr const char* const ctx = "ProcSignature";

	const Token* t = next(s, ctx);

	bool allow_no_args;

	bool allow_return_type;

	if (t == nullptr)
		return false;

	switch (t->tag)
	{
	case Token::Tag::Proc:
		allow_no_args = true;
		allow_return_type = true;
		break;

	case Token::Tag::Trait:
		allow_no_args = false;
		allow_return_type = false;
		break;
	
	default:
		return error_invalid_syntax(s, ctx, t, "Expected, Proc or Trait");
	}

	if (const Token* t1 = next_if(s, Token::Tag::ParenBeg); t1 == nullptr)
	{
		if (allow_no_args)
			goto RETURN_TYPE;
		
		return error_invalid_syntax(s, ctx, t1, "Expected ParenBeg");
	}

	if (next_if(s, Token::Tag::ParenEnd) != nullptr)
		goto RETURN_TYPE;

	while (true)
	{
		if (!out.parameters.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.parameters.last()))
			return false;

		if (const Token* t1 = next(s, ctx); t1 == nullptr)
			return error_unexpected_end(s, ctx);
		else if (t1->tag == Token::Tag::ParenEnd)
			break;
		else if (t1->tag != Token::Tag::Comma)
			return error_invalid_syntax(s, ctx, t1, "Expected ParenEnd or Comma");
	}

RETURN_TYPE:

	if (!allow_return_type)
		return true;

	if (next_if(s, Token::Tag::ArrowRight) != nullptr)
	{
		if (!parse(s, out.opt_return_type))
			return false;
	}

	return true;
}

static bool parse(pstate& s, ast::Impl& out) noexcept
{
	constexpr const char* const ctx = "Impl";

	if (expect(s, ctx, Token::Tag::Impl) == nullptr)
		return false;

	if (!parse(s, out.bound_trait))
		return false;

	return parse(s, out.body);
}

static bool parse(pstate& s, ast::Expr& out, bool allow_assignment) noexcept
{
	constexpr const char* const ctx = "Expr";

	const Token* t = peek(s);

	if (t == nullptr)
		return error_unexpected_end(s, ctx);

	switch (t->tag)
	{
	case Token::Tag::If:
		if (!alloc(s, ctx, &out.if_expr))
			return false;

		out.tag = ast::Expr::Tag::If;

		return parse(s, *out.if_expr);
	
	case Token::Tag::For:
		if (!alloc(s, ctx, &out.for_expr))
			return false;

		out.tag = ast::Expr::Tag::For;

		return parse(s, *out.for_expr);

	case Token::Tag::Switch:
		if (!alloc(s, ctx, &out.switch_expr))
			return false;

		out.tag = ast::Expr::Tag::Switch;

		return parse(s, *out.switch_expr);

	case Token::Tag::SquiggleBeg:
		if (!alloc(s, ctx, &out.block))
			return false;

		out.tag = ast::Expr::Tag::Block;

		return parse(s, *out.block);

	case Token::Tag::Return:
		next(s, ctx);

		if (!alloc(s, ctx, &out.return_or_break_or_defer))
			return false;

		out.tag = ast::Expr::Tag::Return;

		return parse(s, *out.return_or_break_or_defer, false);

	case Token::Tag::Break:
		next(s, ctx);

		if (!alloc(s, ctx, &out.return_or_break_or_defer))
			return false;

		out.tag = ast::Expr::Tag::Break;

		return parse(s, *out.return_or_break_or_defer, false);

	case Token::Tag::Defer:
		next(s, ctx);

		if (!alloc(s, ctx, &out.return_or_break_or_defer))
			return false;

		out.tag = ast::Expr::Tag::Defer;

		return parse(s, *out.return_or_break_or_defer);

	case Token::Tag::Impl:
		if (!alloc(s, ctx, &out.impl))
			return false;

		out.tag = ast::Expr::Tag::Impl;

		return parse(s, *out.impl);

	default:
		if (const Token* t1 = peek(s, 1); t1 == nullptr || (t1->tag != Token::Tag::Colon && t1->tag != Token::Tag::DoubleColon))
			break;

		if (!alloc(s, ctx, &out.definition))
			return false;

		out.tag = ast::Expr::Tag::Definition;

		return parse(s, *out.definition);
	}

	if (!parse_simple_expr(s, out))
		return false;

	t = peek(s);

	if (t == nullptr)
		return true;

	ast::BinaryOp::Op top_level_operator;

	if (t->tag == Token::Tag::Catch)
		top_level_operator = ast::BinaryOp::Op::Catch;
	else if (!allow_assignment)
		return true;

	switch (t->tag)
	{
	case Token::Tag::Catch:
		top_level_operator = ast::BinaryOp::Op::Catch;
		break;

	case Token::Tag::Set:
		top_level_operator = ast::BinaryOp::Op::Set;
		break;

	case Token::Tag::SetAdd:
		top_level_operator = ast::BinaryOp::Op::SetAdd;
		break;

	case Token::Tag::SetSub:
		top_level_operator = ast::BinaryOp::Op::SetSub;
		break;

	case Token::Tag::SetMul:
		top_level_operator = ast::BinaryOp::Op::SetMul;
		break;

	case Token::Tag::SetDiv:
		top_level_operator = ast::BinaryOp::Op::SetDiv;
		break;

	case Token::Tag::SetMod:
		top_level_operator = ast::BinaryOp::Op::SetMod;
		break;

	case Token::Tag::SetBitAnd:
		top_level_operator = ast::BinaryOp::Op::SetBitAnd;
		break;

	case Token::Tag::SetBitOr:
		top_level_operator = ast::BinaryOp::Op::SetBitOr;
		break;

	case Token::Tag::SetBitXor:
		top_level_operator = ast::BinaryOp::Op::SetBitXor;
		break;

	case Token::Tag::SetShiftL:
		top_level_operator = ast::BinaryOp::Op::SetShiftL;
		break;

	case Token::Tag::SetShiftR:
		top_level_operator = ast::BinaryOp::Op::SetShiftR;
		break;

	default:
		return true;
	}

	next(s, ctx);

	ast::BinaryOp* top_level_op = nullptr;

	if (!alloc(s, ctx, &top_level_op))
		return false;

	top_level_op->op = top_level_operator;

	top_level_op->lhs = out;

	out.tag = ast::Expr::Tag::BinaryOp;

	out.binary_op = top_level_op;

	return parse(s, top_level_op->rhs);
}

static bool parse(pstate& s, ast::Array& out) noexcept
{
	constexpr const char* const ctx = "Array";

	if (!parse(s, out.count, false))
		return false;

	if (expect(s, ctx, Token::Tag::BracketEnd) == nullptr)
		return false;

	return parse(s, out.elem_type);
}

static bool parse(pstate& s, ast::Type& out) noexcept
{
	constexpr const char* const ctx = "Type";

	if (next_if(s, Token::Tag::Mut) != nullptr)
		out.is_mut = true;

	const Token* t = peek(s);

	if (t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}

	switch (t->tag)
	{
	case Token::Tag::OpMul_Ptr:
		next(s, ctx);

		if (!alloc(s, ctx, &out.slice_or_ptr_or_multiptr))
			return false;

		out.tag = ast::Type::Tag::Ptr;

		return parse(s, *out.slice_or_ptr_or_multiptr);

	case Token::Tag::BracketBeg:
		next(s, ctx);

		if (const Token* t1 = peek(s); t1 == nullptr)
		{
			return error_unexpected_end(s, ctx);
		}
		else if (t1->tag == Token::Tag::OpMul_Ptr)
		{
			next(s, ctx);

			if (!alloc(s, ctx, &out.slice_or_ptr_or_multiptr))
				return false;

			out.tag = ast::Type::Tag::MultiPtr;

			return parse(s, *out.slice_or_ptr_or_multiptr);
		}
		else if (t1->tag == Token::Tag::BracketEnd)
		{
			next(s, ctx);
	
			if (!alloc(s, ctx, &out.slice_or_ptr_or_multiptr))
				return false;

			out.tag = ast::Type::Tag::Slice;

			return parse(s, *out.slice_or_ptr_or_multiptr);
		}
		else
		{
			if (!alloc(s, ctx, &out.array))
				return false;

			out.tag = ast::Type::Tag::Array;

			return parse(s, *out.array);
		}

	case Token::Tag::Proc:
	case Token::Tag::Trait:
		if (!alloc(s, ctx, &out.signature))
			return false;

		if (t->tag == Token::Tag::Proc)
			out.tag = ast::Type::Tag::ProcSignature;
		else if (t->tag == Token::Tag::Trait)
			out.tag = ast::Type::Tag::TraitSignature;
		else
			assert(false);

		return parse(s, *out.signature);

	default:
		if (!alloc(s, ctx, &out.expr))
			return false;

		out.tag = ast::Type::Tag::Expr;

		return parse(s, *out.expr, false);
	}
}

static bool parse(pstate& s, ast::Definition& out) noexcept
{
	constexpr const char* const ctx = "Definition";

	if (const Token* t = expect(s, ctx, Token::Tag::Ident); t == nullptr)
		return false;
	else
		out.ident = t->data_strview();

	if (const Token* t = next(s, ctx); t == nullptr)
		return false;
	else if (t->tag == Token::Tag::DoubleColon)
		out.is_comptime = true;
	else if (t->tag != Token::Tag::Colon)
		return error_invalid_syntax(s, ctx, t, "Expected Colon or DoubleColon");

	if (next_if(s, Token::Tag::Pub) != nullptr)
		out.is_pub = true;

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag != Token::Tag::Set)
	{
		if (!parse(s, out.opt_type))
			return false;
	}

	if (next_if(s, Token::Tag::Set))
		return parse(s, out.opt_value, false);

	if (out.opt_type.tag == ast::Type::Tag::EMPTY)
		return error_invalid_syntax(s, ctx, peek(s), "Expected a type, a value or both");

	return true;
}

static bool parse(pstate& s, ast::FileModule& out) noexcept
{
	constexpr const char* const ctx = "FileModule";

	while (true)
	{
		if (peek(s) == nullptr)
			return true;

		if (!out.exprs.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.exprs.last()))
			return false;
	}
}

ast::Result ast::parse_program_unit(const vec<Token>& tokens, FileModule& out) noexcept
{
	pstate s{ tokens.begin(), tokens.end(), tokens.begin(), {} };

	parse(s, out);

	return s.rst;
}
