#ifndef AST_DATA_STRUCTURE_INCLUDE_GUARD
#define AST_DATA_STRUCTURE_INCLUDE_GUARD

#include "../util/types.hpp"
#include "../util/strview.hpp"
#include "../util/vec.hpp"

namespace ast
{
	struct Call;

	struct BinaryOp;

	struct UnaryOp;

	struct If;

	struct For;

	struct Switch;

	struct Block;

	struct Catch;

	struct Array;

	struct Signature;

	struct Definition;

	struct Impl;



	struct IntegerLiteral
	{
		u64 value;
	};

	struct FloatLiteral
	{
		f64 value;
	};

	struct StringLiteral
	{
		vec<char8> value;
	};

	struct CharLiteral
	{
		char8 value[4];
	};

	struct Literal
	{
		enum class Tag
		{
			EMPTY = 0,
			IntegerLiteral,
			FloatLiteral,
			StringLiteral,
			CharLiteral,
		} tag = Tag::EMPTY;

		union
		{
			IntegerLiteral integer_literal;

			FloatLiteral float_literal;

			StringLiteral string_literal;

			CharLiteral char_literal;
		};

		~Literal() = delete;
	};

	struct Expr
	{
		enum class Tag
		{
			EMPTY = 0,
			Ident,
			Call,
			Literal,
			BinaryOp,
			UnaryOp,
			If,
			For,
			Switch,
			Block,
			Return,
			Break,
			Defer,
			Catch,
			Definition,
			Impl,
			ProcSignature,
			FuncSignature,
			TraitSignature,
			Module,
		} tag = Tag::EMPTY;

		bool is_mut;

		u16 ident_len;

		union
		{
			const char8* ident_beg;

			Call* call;

			Literal* literal;

			BinaryOp* binary_op;

			UnaryOp* unary_op;

			If* if_expr;

			For* for_expr;

			Switch* switch_expr;

			Block* block;

			Catch* catch_expr;

			Expr* return_or_break_or_defer;

			Definition* definition;

			Impl* impl;

			Signature* signature;
		};
	};

	struct Catch
	{
		strview opt_caught_ident;

		Expr caught_expr;

		Expr catching_expr;
	};

	struct Impl
	{
		Expr bound_trait;

		Expr body;
	};

	struct BinaryOp
	{
		enum class Op : u8
		{
			NONE = 0,
			Add,
			Sub,
			Mul,
			Div,
			Mod,
			BitAnd,
			BitOr,
			BitXor,
			ShiftL,
			ShiftR,
			LogAnd,
			LogOr,
			CmpLt,
			CmpLe,
			CmpGt,
			CmpGe,
			CmpNe,
			CmpEq,
			Member,
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
			TypeArray,
		} op = Op::NONE;

		Expr lhs;

		Expr rhs;
	};

	struct UnaryOp
	{
		enum class Op : u8
		{
			NONE = 0,
			BitNot,
			LogNot,
			Deref,
			AddrOf,
			Neg,
			Try,
			TypePtr,
			TypeSlice,
			TypeMultiptr,
			TypeTailArray,
			TypeVariadic,
		} op = Op::NONE;

		Expr operand;
	};

	struct Block
	{
		vec<Expr, 0> statements;
	};

	struct Array
	{
		Expr count;

		Expr elem_type;
	};

	struct Definition
	{
		strview ident;

		Expr opt_type;

		Expr opt_value;

		bool is_pub;

		bool is_global;

		bool is_comptime;
	};

	struct Signature
	{
		Expr opt_return_type;

		vec<Definition, 0> parameters;
	};

	struct Call
	{
		Expr callee;

		vec<Expr> arguments;
	};

	struct If
	{
		Definition* opt_init;

		Expr condition;

		Expr body;

		Expr opt_else_body;
	};

	struct ForEachSignature
	{
		Expr looped_over;

		strview loop_var;

		strview opt_index_var;
	};

	struct ForLoopSignature
	{
		Definition* opt_init;

		Expr opt_condition;

		Expr opt_step;
	};

	struct For
	{
		enum class Tag
		{
			EMPTY = 0,
			ForEachSignature,
			ForLoopSignature,
		} tag = Tag::EMPTY;

		union
		{
			ForEachSignature for_each_signature;

			ForLoopSignature for_loop_signature;
		};

		Expr body;

		Expr opt_finally_body;
	};

	struct Case
	{
		vec<Expr> labels;

		Expr body;
	};

	struct Switch
	{
		Definition* opt_init;

		Expr switched_expr;

		vec<Case, 0> cases;
	};

	struct FileModule
	{
		strview filename;

		Definition root_module;
	};

	void cleanup(FileModule& file_module) noexcept;
}

#endif // AST_DATA_STRUCTURE_INCLUDE_GUARD
