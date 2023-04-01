#include "ast_gen.hpp"

#include <cassert>
#include <cstdlib>

struct pstate
{
	const Token* beg;

	const Token* end;

	const Token* curr;

	Result rst;
};



static bool error_out_of_memory(pstate& s, const char* ctx) noexcept
{
	s.rst.tag = Result::Tag::OutOfMemory;

	s.rst.error_ctx = ctx;

	s.rst.message = "Out of memory.";

	return false;
}

static bool error_invalid_syntax(pstate& s, const char* ctx, const Token* curr, const char* msg) noexcept
{
	s.rst.tag = Result::Tag::InvalidSyntax;

	s.rst.error_ctx = ctx;

	s.rst.message = msg;

	s.rst.problematic_token = curr;

	return false;
}

static bool error_unexpected_token(pstate& s, const char* ctx, const Token* curr, Token::Tag expected) noexcept
{
	s.rst.tag = Result::Tag::UnexpectedToken;

	s.rst.error_ctx = ctx;

	s.rst.expected_token = expected;

	s.rst.problematic_token = curr;

	return false;
}

static bool error_unexpected_end(pstate& s, const char* ctx) noexcept
{
	s.rst.tag = Result::Tag::UnexpectedEndOfStream;

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

static const Token* expect(pstate& s, Token::Tag expected, const char* ctx) noexcept
{
	const Token* t = next(s, ctx);

	if (t->tag != expected)
	{
		error_unexpected_token(s, ctx, t, expected);

		return nullptr;
	}

	return t;
}



template<typename T>
static bool alloc(T** out) noexcept
{
	if ((*out = static_cast<T*>(malloc(sizeof(T)))) != nullptr)
	{
		memset(*out, 0, sizeof(T));

		return true;
	}

	return false;
}



struct SYOp
{
	u8 precedence;

	u8 opcnt;

	enum class Assoc : bool
	{
		Left = true,
		Right = false,
	} assoc;
	
	union
	{
		UnaryOp::Op uop;

		BinaryOp::Op bop;
	};

	constexpr SYOp() noexcept : precedence{ 0ui8 }, opcnt{ 0ui8 }, assoc{ Assoc::Right }, uop{ UnaryOp::Op::NONE } {}

	constexpr SYOp(u8 precedence, Assoc assoc, UnaryOp::Op op) noexcept : precedence{ precedence }, opcnt{ 1ui8 }, assoc{ assoc }, uop{ op } {}

	constexpr SYOp(u8 precedence, Assoc assoc, BinaryOp::Op op) noexcept : precedence{ precedence }, opcnt{ 2ui8 }, assoc{ assoc }, bop{ op } {}
};

static const SYOp* from_token(const Token* t, bool is_unary) noexcept
{
	static constexpr const SYOp bin_ops[]
	{
		{  3, SYOp::Assoc::Right, BinaryOp::Op::Mul    },
		{  3, SYOp::Assoc::Right, BinaryOp::Op::Div    },
		{  3, SYOp::Assoc::Right, BinaryOp::Op::Mod    },
		{  4, SYOp::Assoc::Left,  BinaryOp::Op::Add    },
		{  4, SYOp::Assoc::Left,  BinaryOp::Op::Sub    },
		{  5, SYOp::Assoc::Left,  BinaryOp::Op::ShiftL },
		{  5, SYOp::Assoc::Left,  BinaryOp::Op::ShiftR },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpLt  },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpLe  },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpGt  },
		{  6, SYOp::Assoc::Left,  BinaryOp::Op::CmpGe  },
		{  7, SYOp::Assoc::Left,  BinaryOp::Op::CmpEq  },
		{  7, SYOp::Assoc::Left,  BinaryOp::Op::CmpNe  },
		{  8, SYOp::Assoc::Left,  BinaryOp::Op::BitAnd },
		{  9, SYOp::Assoc::Left,  BinaryOp::Op::BitXor },
		{ 10, SYOp::Assoc::Left,  BinaryOp::Op::BitOr  },
		{ 11, SYOp::Assoc::Left,  BinaryOp::Op::LogAnd },
		{ 11, SYOp::Assoc::Left,  BinaryOp::Op::LogOr  },
	};

	static constexpr const SYOp un_ops[]
	{
		{  2, SYOp::Assoc::Left,  UnaryOp::Op::LogNot  },
		{  2, SYOp::Assoc::Left,  UnaryOp::Op::BitNot  },
	};

	static constexpr const SYOp un_minus{  2, SYOp::Assoc::Left,  UnaryOp::Op::Neg };

	if (!is_unary)
	{
		if (const isz idx = static_cast<isz>(t->tag) - static_cast<isz>(Token::Tag::OpMul); idx < _countof(bin_ops) && idx >= 0)
			return &bin_ops[idx];
		else
			return nullptr;
	}
	else if (t->tag == Token::Tag::OpSub)
	{
		return &un_minus;
	}
	else
	{
		if (const isz idx = static_cast<isz>(t->tag) - static_cast<isz>(Token::Tag::UOpLogNot); idx < _countof(un_ops) && idx >= 0)
			return &un_ops[static_cast<i32>(t->tag) - static_cast<i32>(Token::Tag::UOpLogNot)];
		else
			return nullptr;
	}
}

static bool expr_pop_syop(pstate& s, const Token* t, vec<SYOp, 32>& op_stack, vec<Expr, 32>& subexprs) noexcept
{
	static constexpr const char ctx[] = "Expr@pop_syop";

	SYOp popped = op_stack.last();

	op_stack.pop();

	Expr expr{};

	if (popped.opcnt > subexprs.size())
		return error_invalid_syntax(s, ctx, t, "Not enough subexpressions for binary operator");

	if (popped.opcnt == 1)
	{
		expr.tag = Expr::Tag::UnaryOp;

		if (!alloc(&expr.unary_op))
			return error_out_of_memory(s, ctx);

		expr.unary_op->op = popped.uop;

		expr.unary_op->operand = std::move(subexprs.last());

		subexprs.last() = std::move(expr);
	}
	else if (popped.opcnt == 2)
	{
		expr.tag = Expr::Tag::BinaryOp;

		if (!alloc(&expr.binary_op))
			return error_out_of_memory(s, ctx);

		expr.binary_op->op = popped.bop;

		expr.binary_op->rhs = std::move(subexprs.last());

		subexprs.pop();

		expr.binary_op->lhs = std::move(subexprs.last());

		subexprs.last() = std::move(expr);
	}
	else
		assert(false);

	return true;
}

static bool token_tag_to_assign_oper(const Token::Tag t, Assignment::Op& out_assign_op) noexcept
{
	const i32 idx = static_cast<i32>(t) - static_cast<i32>(Token::Tag::Set);

	if (idx < 0 || idx >= static_cast<i32>(Token::Tag::SetBitShr) - static_cast<i32>(Token::Tag::Set))
		return false;

	out_assign_op = static_cast<Assignment::Op>(idx + 1);

	return true;
}

static bool token_tag_is_type(const Token::Tag t) noexcept
{
	return t == Token::Tag::Proc || t == Token::Tag::Struct || t == Token::Tag::Union || t == Token::Tag::Enum || t == Token::Tag::Trait || t == Token::Tag::Module || t == Token::Tag::Impl;
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



static bool parse(pstate& s, Definition& out) noexcept;

static bool parse(pstate& s, Argument& out) noexcept;

static bool parse(pstate& s, TypeRef& out) noexcept;

static bool parse(pstate& s, Expr& out) noexcept;

static bool parse(pstate& s, Name& out) noexcept;

static bool parse(pstate& s, TopLevelExpr& out) noexcept;

static bool parse(pstate& s, Statement& out) noexcept;

static bool parse(pstate& s, If& out) noexcept;

static bool parse(pstate& s, For& out) noexcept;

static bool parse(pstate& s, Switch& out) noexcept;

static bool parse(pstate& s, Case& out) noexcept;



static bool parse(pstate& s, Assignment& out) noexcept
{
	static constexpr const char ctx[] = "Assignment";

	if (!parse(s, out.assignee))
		return false;

	if (const Token* t = next(s, ctx); t == nullptr)
		return false;
	else if (!token_tag_to_assign_oper(t->tag, out.op))
		return error_invalid_syntax(s, ctx, t, "Expected Assignment Operator");

	return parse(s, out.value);
}

static bool parse(pstate& s, To& out) noexcept
{
	static constexpr const char ctx[] = "To";

	if (expect(s, Token::Tag::To, ctx) == nullptr)
		return false;

	while (true)
	{
		if (!out.cases.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.cases.last()))
			return false;

		if (const Token* t = peek(s); t == nullptr || t->tag != Token::Tag::Case)
			return true;
	}
}

static bool parse(pstate& s, Block& out) noexcept
{
	static constexpr const char ctx[] = "Block";

	if (expect(s, Token::Tag::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			break;
		}

		if (!out.statements.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.statements.last()))
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::To)
	{
		if (!parse(s, out.opt_to))
			return false;
	}

	return true;
}

static bool parse(pstate& s, Go& out) noexcept
{
	static constexpr const char ctx[] = "Go";

	if (expect(s, Token::Tag::Go, ctx) == nullptr)
		return false;

	return parse(s, out.label);
}

static bool parse(pstate& s, ForEachSignature& out) noexcept
{
	static constexpr const char ctx[] = "ForEachSignature";

	if (const Token* t = expect(s, Token::Tag::Ident, ctx); t == nullptr)
		return false;
	else
		out.loop_variable = t->data_strview();

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Comma)
	{
		next(s, ctx);

		if (const Token* t1 = next(s, ctx); t1 == nullptr)
			return false;
		else
			out.loop_variable = t1->data_strview();
	}

	if (expect(s, Token::Tag::ArrowLeft, ctx) == nullptr)
		return false;

	if (!parse(s, out.loopee))
		return false;

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::Do)
	{
		next(s, ctx);

		return true;
	}
	else
	{
		return t->tag == Token::Tag::SquiggleBeg;
	}
}

static bool parse(pstate& s, ForLoopSignature& out) noexcept
{
	static constexpr const char ctx[] = "ForLoopSignature";

	if (const Token* t = peek(s, 1); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::Colon || t->tag == Token::Tag::DoubleColon)
	{
		if (!parse(s, out.opt_init))
			return false;

		if (const Token* t1 = peek(s); t == nullptr)
		{
			return error_unexpected_end(s, ctx);
		}
		else if (t1->tag == Token::Tag::Do)
		{
			next(s, ctx);

			return true;
		}
		else if (t1->tag == Token::Tag::SquiggleBeg)
		{
			return true;
		}
		else if (t1->tag != Token::Tag::Semicolon)
		{
			return error_invalid_syntax(s, ctx, t1, "Expected Do, SquiggleBeg or Semicolon");
		}
		
		next(s, ctx);
	}

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::Do)
	{
		next(s, ctx);

		return true;
	}
	else if (t->tag == Token::Tag::SquiggleBeg)
	{
		return true;
	}
	
	if (!parse(s, out.opt_cond))
		return false;

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::Do)
	{
		next(s, ctx);

		return true;
	}
	else if (t->tag == Token::Tag::SquiggleBeg)
	{
		return true;
	}
	else if (t->tag != Token::Tag::Semicolon)
	{
		return error_invalid_syntax(s, ctx, t, "Expected Do, SquiggleBeg or Semicolon");
	}

	if (!parse(s, out.opt_step))
		return false;

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::Do)
	{
		next(s, ctx);

		return true;
	}
	else
	{
		return t->tag == Token::Tag::SquiggleBeg;
	}
}

static bool parse(pstate& s, NamePart& out) noexcept
{
	static constexpr const char ctx[] = "NamePart";

	if (const Token* t = expect(s, Token::Tag::Ident, ctx); t == nullptr)
		return false;
	else
		out.ident = t->data_strview();

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::ParenBeg)
	{
		next(s, ctx);

		if (const Token* t1 = peek(s); t1 == nullptr)
		{
			return error_unexpected_end(s, ctx);
		}
		else if (t1->tag != Token::Tag::ParenEnd)
		{
			while (true)
			{
				if (!out.args.push_back({}))
					return false;

				if (!parse(s, out.args.last()))
					return false;

				if (const Token* t2 = next(s, ctx); t2 == nullptr)
					return false;
				else if (t2->tag == Token::Tag::ParenEnd)
					break;
				else if (t2->tag != Token::Tag::Comma)
					return error_invalid_syntax(s, ctx, t2, "Expected ParenEnd or Comma");
			}
		}
		else
		{
			next(s, ctx);
		}
	}

	return true;	
}

static bool parse(pstate& s, FloatLiteral& out) noexcept
{
	static constexpr const char ctx[] = "FloatLiteral";

	const Token* t = expect(s, Token::Tag::LitFloat, ctx);

	if (t == nullptr)
		return false;

	out.value = atof(t->data_strview().begin());

	return true;
}

static bool parse(pstate& s, IntegerLiteral& out) noexcept
{
	static constexpr const char ctx[] = "IntegerLiteral";

	const Token* t = expect(s, Token::Tag::LitInt, ctx);

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

static bool parse(pstate& s, CharLiteral& out) noexcept
{
	static constexpr const char ctx[] = "CharLiteral";

	const Token* t = expect(s, Token::Tag::LitChar, ctx);

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

static bool parse(pstate& s, StringLiteral& out) noexcept
{
	static constexpr const char ctx[] = "StringLiteral";

	const Token* t = expect(s, Token::Tag::LitString, ctx);

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

static bool parse(pstate& s, ProcSignature& out) noexcept
{
	static constexpr const char ctx[] = "ProcSignature";

	if (expect(s, Token::Tag::ParenBeg, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag != Token::Tag::ParenEnd)
	{
		while (true)
		{
			if (!out.parameters.push_back({}))
				return false;

			if (!parse(s, out.parameters.last()))
				return false;

			if (const Token* t1 = next(s, ctx); t1 == nullptr)
				return false;
			else if (t1->tag == Token::Tag::ParenEnd)
				break;
			else if (t1->tag != Token::Tag::Comma)
				return error_invalid_syntax(s, ctx, t, "Expected ParenEnd or Comma");
		}
	}
	else
	{
		next(s, ctx);
	}

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::ArrowRight)
	{
		next(s, ctx);

		if (!parse(s, out.opt_return_type))
			return false;
	}

	return true;
}

static bool parse(pstate& s, EnumValue& out) noexcept
{
	static constexpr const char ctx[] = "EnumValue";

	if (const Token* t = expect(s, Token::Tag::Ident, ctx); t == nullptr)
		return false;
	else
		out.ident = t->data_strview();

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Set)
	{
		next(s, ctx);

		if (!parse(s, out.opt_value))
			return false;
	}

	return true;
}

static bool parse(pstate& s, Statement& out) noexcept
{
	static constexpr const char ctx[] = "Statement";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::If)
	{
		if (!alloc(&out.if_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::If;

		return parse(s, *out.if_stmt);
	}
	else if (t->tag == Token::Tag::For)
	{
		if (!alloc(&out.for_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::For;

		return parse(s, *out.for_stmt);
	}
	else if (t->tag == Token::Tag::Switch)
	{
		if (!alloc(&out.switch_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::Switch;

		return parse(s, *out.switch_stmt);
	}
	else if (t->tag == Token::Tag::Return)
	{
		next(s, ctx);

		if (!alloc(&out.return_or_yield_value))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::Return;

		return parse(s, *out.return_or_yield_value);
	}
	else if (t->tag == Token::Tag::Yield)
	{
		next(s, ctx);

		if (!alloc(&out.return_or_yield_value))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::Yield;

		return parse(s, *out.return_or_yield_value);
	}
	else if (t->tag == Token::Tag::Go)
	{
		if (!alloc(&out.go_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::Go;

		return parse(s, *out.go_stmt);
	}
	else if (t->tag == Token::Tag::SquiggleBeg)
	{
		if (!alloc(&out.block))
			return error_out_of_memory(s, ctx);

		out.tag = Statement::Tag::Block;

		return parse(s, *out.block);
	}
	else if (t->tag == Token::Tag::Ident)
	{
		Assignment::Op op;

		if (const Token* t1 = peek(s, 1); t1 == nullptr)
		{
			return error_unexpected_end(s, ctx);
		}
		else if (t1->tag == Token::Tag::Colon || t1->tag == Token::Tag::DoubleColon)
		{
			if (!alloc(&out.definition))
				return error_out_of_memory(s, ctx);

			out.tag = Statement::Tag::Definition;

			return parse(s, *out.definition);
		}
		else if (token_tag_to_assign_oper(t->tag, op))
		{
			if (!alloc(&out.assignment))
				return error_out_of_memory(s, ctx);

			out.tag = Statement::Tag::Assignment;

			return parse(s, *out.assignment);
		}
		else
		{
			if (!alloc(&out.call))
				return error_out_of_memory(s, ctx);

			out.tag = Statement::Tag::Call;

			if (!parse(s, *out.call))
				return false;

			if (out.call->parts.last().args.size() == 0)
				return error_invalid_syntax(s, ctx, t, "Expected procedure call");

			return true;
		}
	}
	else
	{
		return error_invalid_syntax(s, ctx, t, "Expected If, For, Switch, Return, Yield, Go, SquiggleBeg or Ident");
	}
}

static bool parse(pstate& s, ForSignature& out) noexcept
{
	static constexpr const char ctx[] = "ForSignature";

	if (expect(s, Token::Tag::For, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && (t->tag == Token::Tag::Comma || t->tag == Token::Tag::ArrowLeft))
	{
		out.tag = ForSignature::Tag::ForEachSignature;

		return parse(s, out.for_each);
	}
	else
	{
		out.tag = ForSignature::Tag::ForLoopSignature;

		return parse(s, out.for_loop);
	}
}

static bool parse(pstate& s, Case& out) noexcept
{
	static constexpr const char ctx[] = "Case";

	if (expect(s, Token::Tag::Case, ctx) == nullptr)
		return false;

	if (!parse(s, out.label))
		return false;

	return parse(s, out.body);
}

static bool parse(pstate& s, Name& out) noexcept
{
	static constexpr const char ctx[] = "Name";

	while (true)
	{
		if (!out.parts.push_back({}))
			return false;

		if (!parse(s, out.parts.last()))
			return false;
			
		if (const Token* t = peek(s); t == nullptr || t->tag != Token::Tag::Dot)
			return true;

		next(s, ctx);
	}
}

static bool parse(pstate& s, Literal& out) noexcept
{
	static constexpr const char ctx[] = "Literal";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::LitString)
	{
		out.tag = Literal::Tag::StringLiteral;

		return parse(s, out.string_literal);
	}
	else if (t->tag == Token::Tag::LitChar)
	{
		out.tag = Literal::Tag::CharLiteral;

		return parse(s, out.char_literal);
	}
	else if (t->tag == Token::Tag::LitInt)
	{
		out.tag = Literal::Tag::IntegerLiteral;

		return parse(s, out.integer_literal);
	}
	else if (t->tag == Token::Tag::LitFloat)
	{
		out.tag = Literal::Tag::FloatLiteral;

		return parse(s, out.float_literal);
	}
	else
	{
		return error_invalid_syntax(s, ctx, t, "Expected LitString, LitChar, LitInt or LitFloat");
	}
}

static bool parse(pstate& s, Proc& out) noexcept
{
	static constexpr const char ctx[] = "Proc";

	if (!parse(s, out.signature))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Undefined)
	{
		next(s, ctx);

		return true;
	}

	return parse(s, out.body);
}

static bool parse(pstate& s, StructuredType& out) noexcept
{
	static constexpr const char ctx[] = "StructuredType";

	if (expect(s, Token::Tag::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (!out.members.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.members.last()))
			return false;
	}
}

static bool parse(pstate& s, Enum& out) noexcept
{
	static constexpr const char ctx[] = "Enum";

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::ParenBeg)
	{
		next(s, ctx);

		if (!parse(s, out.opt_enum_type))
			return false;

		if (expect(s, Token::Tag::ParenEnd, ctx) == nullptr)
			return false;
	}

	if (expect(s, Token::Tag::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (const Token* t = peek(s, 1); t != nullptr && (t->tag == Token::Tag::Colon || t->tag == Token::Tag::DoubleColon))
		{
			if (!out.definitions.push_back({}))
				return error_out_of_memory(s, ctx);

			if (!parse(s, out.definitions.last()))
				return false;
		}
		else
		{
			if (!out.values.push_back({}))
				return error_out_of_memory(s, ctx);

			if (!parse(s, out.values.last()))
				return false;
		}
	}
}

static bool parse(pstate& s, Trait& out) noexcept
{
	static constexpr const char ctx[] = "Trait";

	if (expect(s, Token::Tag::ParenBeg, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag != Token::Tag::ParenEnd)
	{
		while (true)
		{
			if (!out.bindings.push_back({}))
				return error_out_of_memory(s, ctx);

			if (!parse(s, out.bindings.last()))
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
		next(s, ctx);
	}

	if (expect(s, Token::Tag::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (!out.definitions.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.definitions.last()))
			return false;
	}
}

static bool parse(pstate& s, Module& out) noexcept
{
	static constexpr const char ctx[] = "Module";

	if (expect(s, Token::Tag::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (!out.definitions.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.definitions.last()))
			return false;
	}
}

static bool parse(pstate& s, Impl& out) noexcept
{
	static constexpr const char ctx[] = "Impl";

	if (!parse(s, out.trait_name))
		return false;

	if (expect(s, Token::Tag::SquiggleBeg, ctx) == nullptr)
		return false;

	while (true)
	{
		if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::SquiggleEnd)
		{
			next(s, ctx);

			return true;
		}

		if (!out.definitions.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.definitions.last()))
			return false;
	}
}

static bool parse(pstate& s, If& out) noexcept
{
	static constexpr const char ctx[] = "If";

	if (expect(s, Token::Tag::If, ctx) == nullptr)
		return false;

	if (const Token* t = peek(s, 1); t != nullptr && t->tag == Token::Tag::Colon)
	{
		if (!parse(s, out.opt_init))
			return false;

		if (expect(s, Token::Tag::Semicolon, ctx) == nullptr)
			return false;
	}

	if (!parse(s, out.condition))
		return false;
		
	if (!parse(s, out.body))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Else)
	{
		next(s, ctx);

		if (!parse(s, out.opt_else_body))
			return false;
	}

	return true;
}

static bool parse(pstate& s, For& out) noexcept
{
	static constexpr const char ctx[] = "For";

	if (!parse(s, out.signature))
		return false;
	
	if (!parse(s, out.body))
		return false;

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Until)
	{
		next(s, ctx);

		if (!parse(s, out.opt_until_body))
			return false;
	}

	return true;
}

static bool parse(pstate& s, Switch& out) noexcept
{
	static constexpr const char ctx[] = "Switch";

	if (expect(s, Token::Tag::Switch, ctx) == nullptr)
		return false;

	if (!parse(s, out.switched))
		return false;

	while (true)
	{
		if (!out.cases.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.cases.last()))
			return false;

		if (const Token* t = peek(s); t == nullptr || t->tag != Token::Tag::Case)
			return true;
	}
}

static bool parse(pstate& s, Expr& out) noexcept
{
	static constexpr const char ctx[] = "Expr";

	vec<SYOp, 32> op_stack;

	vec<Expr, 32> subexprs;

	bool expecting_operator = false;

	u32 paren_nesting = 0;

	while (true)
	{
		if (const Token* t = peek(s); t == nullptr)
		{
			if (!expecting_operator)
				return error_unexpected_end(s, ctx);

			// Currently this should also be error_unexpected_end,
			// but if there can ever be top-level VariableDefs,
			// this would be ok, so just break. Any error will be
			// handled by the caller anyways if further Tokens are
			// expected.
			break;
		}
		else if (t->tag == Token::Tag::LitString || t->tag == Token::Tag::LitChar || t->tag == Token::Tag::LitInt || t->tag == Token::Tag::LitFloat)
		{
			if (expecting_operator)
				break;

			if (!subexprs.push_back({}))
				return error_out_of_memory(s, ctx);

			Expr& expr = subexprs.last();
			
			expr.tag = Expr::Tag::Literal;

			if (!alloc(&expr.literal))
				return error_out_of_memory(s, ctx);

			if (!parse(s, *expr.literal))
				return false;

			expecting_operator = true;
		}
		else if (t->tag == Token::Tag::Ident)
		{
			if (expecting_operator)
				break;

			if (!subexprs.push_back({}))
				return error_out_of_memory(s, ctx);

			Expr& expr = subexprs.last();
			
			expr.tag = Expr::Tag::Name;

			if (!alloc(&expr.name))
				return error_out_of_memory(s, ctx);

			if (!parse(s, *expr.name))
				return false;
			
			expecting_operator = true;
		}
		else if (t->tag == Token::Tag::ParenBeg)
		{
			if (expecting_operator)
				break;

			next(s, ctx);

			++paren_nesting;

			if (!op_stack.push_back({ 255, SYOp::Assoc::Left, UnaryOp::Op::NONE }))
				return error_out_of_memory(s, ctx);
		}
		else if (t->tag == Token::Tag::ParenEnd)
		{
			if (!expecting_operator || paren_nesting == 0)
				break;

			next(s, ctx);

			--paren_nesting;

			assert(op_stack.size() != 0);

			while (op_stack.last().precedence != 255)
			{
				if (!expr_pop_syop(s, t, op_stack, subexprs))
					return false;

				assert(op_stack.size() != 0);
			}

			op_stack.pop();
		}
		else
		{
			const SYOp* oper = from_token(t, !expecting_operator);

			if (oper == nullptr)
			{
				if (expecting_operator)
					break;
				else
					return error_invalid_syntax(s, ctx, t, "Expected a unary operator");
			}

			next(s, ctx);

			while (op_stack.size() != 0)
			{
				const SYOp& last = op_stack.last();

				if (last.precedence >= oper->precedence && !(last.precedence == oper->precedence && oper->assoc == SYOp::Assoc::Left))
					break;

				if (!expr_pop_syop(s, t, op_stack, subexprs))
					return false;
			}

			if (!op_stack.push_back(*oper))
				return error_out_of_memory(s, ctx);
			
			expecting_operator = false;
		}
	}

	if (paren_nesting != 0)
		return error_invalid_syntax(s, ctx, nullptr, "Unmatched ParenBeg");

	while (op_stack.size() != 0)
		if (!expr_pop_syop(s, nullptr, op_stack, subexprs))
			return false;

	if (subexprs.size() != 1)
		return error_invalid_syntax(s, ctx, nullptr, "Too many subexpressions");

	out = std::move(subexprs.first());

	return true;
}

static bool parse(pstate& s, Type& out) noexcept
{
	static constexpr const char ctx[] = "Type";

	if (const Token* t = next(s, ctx); t == nullptr)
	{
		return false;
	}
	else if (t->tag == Token::Tag::Proc)
	{
		out.tag = Type::Tag::Proc;

		return parse(s, out.proc_type);
	}
	else if (t->tag == Token::Tag::Struct)
	{
		out.tag = Type::Tag::Struct;

		return parse(s, out.struct_or_union_type);
	}
	else if (t->tag == Token::Tag::Union)
	{
		out.tag = Type::Tag::Union;

		return parse(s, out.struct_or_union_type);
	}
	else if (t->tag == Token::Tag::Enum)
	{
		out.tag = Type::Tag::Enum;

		return parse(s, out.enum_type);
	}
	else if (t->tag == Token::Tag::Trait)
	{
		out.tag = Type::Tag::Trait;

		return parse(s, out.trait_type);
	}
	else if (t->tag == Token::Tag::Module)
	{
		out.tag = Type::Tag::Module;

		return parse(s, out.module_type);
	}
	else if (t->tag == Token::Tag::Impl)
	{
		out.tag = Type::Tag::Impl;

		return parse(s, out.impl_type);
	}
	else
	{
		return error_invalid_syntax(s, ctx, t, "Expected Proc, Struct, Union, Enum, Trait, Module or Impl");
	}
}

static bool parse(pstate& s, Catch& out) noexcept
{
	static constexpr const char ctx[] = "Catch";

	if (expect(s, Token::Tag::Catch, ctx) == nullptr)
		return false;

	if (const Token* t = expect(s, Token::Tag::Ident, ctx); t == nullptr)
		return false;
	else
		out.error_ident = t->data_strview();

	return parse(s, out.stmt);
}

static bool parse(pstate& s, TopLevelExpr& out) noexcept
{
	static constexpr const char ctx[] = "TopLevelExpr";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::If)
	{
		if (!alloc(&out.if_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = TopLevelExpr::Tag::If;

		if (!parse(s, *out.if_stmt))
			return false;
	}
	else if (t->tag == Token::Tag::For)
	{
		if (!alloc(&out.for_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = TopLevelExpr::Tag::For;

		if (!parse(s, *out.for_stmt))
			return false;
	}
	else if (t->tag == Token::Tag::Switch)
	{
		if (!alloc(&out.switch_stmt))
			return error_out_of_memory(s, ctx);

		out.tag = TopLevelExpr::Tag::Switch;

		if (!parse(s, *out.switch_stmt))
			return false;
	}
	else if (token_tag_is_type(t->tag))
	{
		if (!alloc(&out.type))
			return error_out_of_memory(s, ctx);

		out.tag = TopLevelExpr::Tag::Type;

		if (!parse(s, *out.type))
			return false;
	}
	else
	{
		if (!alloc(&out.expr))
			return error_out_of_memory(s, ctx);

		out.tag = TopLevelExpr::Tag::Expr;

		if (!parse(s, *out.expr))
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Catch)
	{
		if (!alloc(&out.opt_catch))
			return error_out_of_memory(s, ctx);

		if (!parse(s, *out.opt_catch))
			return false;
	}

	return true;
}

static bool parse(pstate& s, Argument& out) noexcept
{
	static constexpr const char ctx[] = "Argument";

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (t->tag == Token::Tag::Proc || t->tag == Token::Tag::Struct || t->tag == Token::Tag::Union || t->tag == Token::Tag::Enum || t->tag == Token::Tag::Trait || t->tag == Token::Tag::Module)
	{
		if (!alloc(&out.type))
			return error_out_of_memory(s, ctx);

		out.tag = Argument::Tag::Type;

		return parse(s, *out.type);
	}
	else
	{
		if (!alloc(&out.expr))
			return error_out_of_memory(s, ctx);

		out.tag = Argument::Tag::Expr;

		return parse(s, *out.expr);
	}
}

static bool parse(pstate& s, TypeRef& out) noexcept
{
	static constexpr const char ctx[] = "TypeRef";

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::OpBitAnd_Ref)
	{
		next(s, ctx);

		out.is_ref = true;
	}

	if (const Token* t = peek(s); t == nullptr)
	{
		return error_unexpected_end(s, ctx);
	}
	else if (token_tag_is_type(t->tag))
	{
		if (!alloc(&out.type))
			return error_out_of_memory(s, ctx);

		out.tag = TypeRef::Tag::Type;

		return parse(s, *out.type);
	}
	else
	{
		if (!alloc(&out.name))
			return error_out_of_memory(s, ctx);

		out.tag = TypeRef::Tag::Name;

		return parse(s, *out.name);
	}
}

static bool parse(pstate& s, Definition& out) noexcept
{
	static constexpr const char ctx[] = "Definition";

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Ident)
	{
		next(s, ctx);

		out.ident = t->data_strview();
		
		if (const Token* t1 = next(s, ctx); t1 == nullptr)
			return false;
		else if (t1->tag == Token::Tag::DoubleColon)
			out.is_comptime = true;
		else if (t1->tag != Token::Tag::Colon)
			return error_invalid_syntax(s, ctx, t1, "Expected Colon or DoubleColon");
	}

	if (const Token* t = peek(s); t == nullptr || t->tag != Token::Tag::Set)
	{
		if (!parse(s, out.opt_type))
			return false;
	}

	if (const Token* t = peek(s); t != nullptr && t->tag == Token::Tag::Set)
	{
		next(s, ctx);

		if (!parse(s, out.opt_value))
			return false;
	}
	
	return true;
}

static bool parse(pstate& s, ProgramUnit& out) noexcept
{
	static constexpr const char ctx[] = "ProgramUnit";

	while (peek(s) != nullptr)
	{
		if (!out.definitions.push_back({}))
			return error_out_of_memory(s, ctx);

		if (!parse(s, out.definitions.last()))
			return false;
	}

	return true;
}

Result parse_program_unit(const vec<Token>& tokens, ProgramUnit& out_program_unit) noexcept
{
	pstate s{ tokens.begin(), tokens.end(), tokens.begin(), {} };

	parse(s, out_program_unit);

	return s.rst;
}
