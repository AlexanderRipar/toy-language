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

	struct Array;

	struct ProcSignature;

	struct Definition;



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
			Definition,
		} tag = Tag::EMPTY;

		u32 ident_len;

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

			Expr* return_or_break_or_defer;

			Definition* definition;
		};
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
		} op = Op::NONE;

		Expr operand;
	};

	struct Block
	{
		vec<Expr, 0> statements;
	};

	struct Type
	{
		enum class Tag
		{
			EMPTY = 0,
			Expr,
			ProcSignature,
			Array,
			Slice,
			Ptr,
			MultiPtr,
		} tag = Tag::EMPTY;

		bool is_mut;

		union
		{
			Expr* expr;

			ProcSignature* proc_signature;

			Array* array;

			Type* slice_or_ptr_or_multiptr;
		};
	};

	struct Array
	{
		Expr count;

		Type elem_type;
	};

	struct Definition
	{
		strview ident;

		Type opt_type;

		Expr opt_value;

		bool is_pub;

		bool is_comptime;
	};

	struct ProcSignature
	{
		Type opt_return_type;

		vec<Definition, 0> parameters;
	};

	struct Argument
	{
		enum class Tag
		{
			EMPTY = 0,
			Expr,
			Definition,
		} tag = Tag::EMPTY;

		union
		{
			Expr* expr;

			Definition* definition;	
		};
	};

	struct Call
	{
		Expr callee;

		vec<Argument, 0> arguments;
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
		Expr label;

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

		vec<Definition, 0> definitions;
	};

	void cleanup(FileModule& file_module) noexcept;
}

#endif // AST_DATA_STRUCTURE_INCLUDE_GUARD
