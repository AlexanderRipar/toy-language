#ifndef AST_DATA_STRUCTURE_INCLUDE_GUARD
#define AST_DATA_STRUCTURE_INCLUDE_GUARD

#include "../util/types.hpp"
#include "../util/strview.hpp"
#include "../util/str.hpp"
#include "../util/vec.hpp"

struct If;

struct For;

struct Switch;

struct Return;

struct Block;

struct Assignment;

struct Name;

struct Type;

struct UnaryOp;

struct BinaryOp;

struct Definition;

struct Catch;

struct Array;

struct Call;



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
	str value;

	StringLiteral() noexcept = default;

	StringLiteral& operator=(StringLiteral&& o) noexcept;

	StringLiteral& operator=(const StringLiteral& o) noexcept = delete;

	StringLiteral(StringLiteral&& o) noexcept = delete;

	StringLiteral(const StringLiteral& o) noexcept = delete;
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

	~Literal() noexcept;

	Literal() noexcept = default;

	Literal& operator=(Literal&& o) noexcept;

	Literal& operator=(const Literal& o) noexcept = delete;

	Literal(Literal&& o) noexcept = delete;

	Literal(const Literal& o) noexcept = delete;
};

struct Expr
{
	enum class Tag
	{
		EMPTY = 0,
		Ident,
		Literal,
		UnaryOp,
		BinaryOp,
		Call,
	} tag = Tag::EMPTY;

	u32 ident_len;

	union
	{
		const char* ident_beg;
		
		Literal* literal;

		UnaryOp* unary_op;

		BinaryOp* binary_op;

		Call* call;
	};
	
	~Expr() noexcept;

	Expr() noexcept = default;

	Expr& operator=(Expr&& o) noexcept;

	Expr& operator=(const Expr& o) noexcept = delete;

	Expr(Expr&& o) noexcept = delete;

	Expr(const Expr& o) noexcept = delete;
};

struct Argument
{
	enum class Tag
	{
		EMPTY = 0,
		Type,
		Expr,
	} tag = Tag::EMPTY;

	union
	{
		Type* type;

		Expr* expr;
	};

	~Argument() noexcept;

	Argument() noexcept = default;

	Argument& operator=(Argument&& o) noexcept;

	Argument& operator=(const Argument& o) noexcept = delete;

	Argument(Argument&& o) noexcept = delete;

	Argument(const Argument& o) noexcept = delete;
};

struct Call
{
	Expr callee;

	vec<Argument> args;

	Call() noexcept = default;

	Call& operator=(Call&& o) noexcept;

	Call& operator=(const Call& o) noexcept = delete;

	Call(Call&& o) noexcept = delete;

	Call(const Call& o) noexcept = delete;
};

struct TypeRef
{
	enum class Tag : u8
	{
		EMPTY = 0,
		Type,
		Expr,
		Ptr,
		MultiPtr,
		Slice,
		Array,
	} tag = Tag::EMPTY;

	bool is_mut;

	bool is_proc_param_ref;

	union
	{
		Type* type;

		Expr* expr;

		TypeRef* ptr_or_multiptr_or_slice;

		Array* array;
	};

	~TypeRef() noexcept;

	TypeRef() noexcept = default;

	TypeRef& operator=(TypeRef&& o) noexcept;

	TypeRef& operator=(const TypeRef& o) noexcept = delete;

	TypeRef(TypeRef&& o) noexcept = delete;

	TypeRef(const TypeRef& o) noexcept = delete;
};

struct Array
{
	TypeRef elem_type;

	Expr elem_cnt;

	Array() noexcept = default;

	Array& operator=(Array&& o) noexcept;

	Array& operator=(const Array& o) noexcept = delete;

	Array(Array&& o) noexcept = delete;

	Array(const Array& o) noexcept = delete;
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
	} op;

	Expr lhs;

	Expr rhs;

	BinaryOp() noexcept = default;

	BinaryOp& operator=(BinaryOp&& o) noexcept;

	BinaryOp& operator=(const BinaryOp& o) noexcept = delete;

	BinaryOp(BinaryOp&& o) noexcept = delete;

	BinaryOp(const BinaryOp& o) noexcept = delete;
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
	} op;

	Expr operand;

	UnaryOp() noexcept = default;

	UnaryOp& operator=(UnaryOp&& o) noexcept;

	UnaryOp& operator=(const UnaryOp& o) noexcept = delete;

	UnaryOp(UnaryOp&& o) noexcept = delete;

	UnaryOp(const UnaryOp& o) noexcept = delete;
};

struct TopLevelExpr
{
	enum class Tag
	{
		EMPTY = 0,
		Block,
		If,
		For,
		Switch,
		Catch,
		Try,
		Expr,
		Type,
		Undefined,
	} tag = Tag::EMPTY;

	union
	{
		Block* block_expr;

		If* if_expr;

		For* for_expr;

		Switch* switch_expr;

		Catch* catch_expr;

		Expr* simple_or_try_expr;

		Type* type_expr;
	};

	~TopLevelExpr() noexcept;

	TopLevelExpr() noexcept = default;

	TopLevelExpr& operator=(TopLevelExpr&& o) noexcept;

	TopLevelExpr& operator=(const TopLevelExpr& o) noexcept = delete;

	TopLevelExpr(TopLevelExpr&& o) noexcept = delete;

	TopLevelExpr(const TopLevelExpr& o) noexcept = delete;
};

struct Definition
{
	bool is_comptime;

	bool is_pub;

	strview opt_ident;

	TypeRef opt_type;

	TopLevelExpr opt_value;

	Definition() noexcept = default;

	Definition& operator=(Definition&& o) noexcept;

	Definition& operator=(const Definition& o) noexcept = delete;

	Definition(Definition&& o) noexcept = delete;

	Definition(const Definition& o) noexcept = delete;
};

struct EnumValue
{
	strview ident;

	Expr opt_value;

	EnumValue() noexcept = default;

	EnumValue& operator=(EnumValue&& o) noexcept;

	EnumValue& operator=(const EnumValue& o) noexcept = delete;

	EnumValue(EnumValue&& o) noexcept = delete;

	EnumValue(const EnumValue& o) noexcept = delete;
};

struct Statement
{
	enum class Tag
	{
		EMPTY = 0,
		If,
		For,
		Switch,
		Return,
		Yield,
		Break,
		Block,
		Call,
		Definition,
		Assignment,
		Defer,
		Undefined,
	} tag = Tag::EMPTY;

	union
	{
		If* if_stmt;

		For* for_stmt;

		Switch* switch_stmt;

		TopLevelExpr* return_or_yield_or_break_value;

		Block* block;

		Call* call;

		Definition* definition;

		Statement* deferred_stmt;

		Assignment* assignment;
	};

	~Statement() noexcept;

	Statement() noexcept = default;

	Statement& operator=(Statement&& o) noexcept;

	Statement& operator=(const Statement& o) noexcept = delete;

	Statement(Statement&& o) noexcept = delete;

	Statement(const Statement& o) noexcept = delete;
};

struct Catch
{
	Expr caught_expr;

	strview opt_error_ident;

	Statement stmt;

	Catch() noexcept = default;

	Catch& operator=(Catch&& o) noexcept;

	Catch& operator=(const Catch& o) noexcept = delete;

	Catch(Catch&& o) noexcept = delete;

	Catch(const Catch& o) noexcept = delete;
};

struct Assignment
{
	enum class Op
	{
		NONE = 0,
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

	Expr assignee;

	TopLevelExpr value;

	Assignment() noexcept = default;

	Assignment& operator=(Assignment&& o) noexcept;

	Assignment& operator=(const Assignment& o) noexcept = delete;

	Assignment(Assignment&& o) noexcept = delete;

	Assignment(const Assignment& o) noexcept = delete;
};

struct If
{
	Definition opt_init;

	Expr condition;

	Statement body;

	Statement opt_else_body;

	If() noexcept = default;

	If& operator=(If&& o) noexcept;

	If& operator=(const If& o) noexcept = delete;

	If(If&& o) noexcept = delete;

	If(const If& o) noexcept = delete;
};

struct ForEachSignature
{
	strview loop_variable;

	strview opt_step_variable;

	Expr loopee;

	ForEachSignature() noexcept = default;

	ForEachSignature& operator=(ForEachSignature&& o) noexcept;

	ForEachSignature& operator=(const ForEachSignature& o) noexcept = delete;

	ForEachSignature(ForEachSignature&& o) noexcept = delete;

	ForEachSignature(const ForEachSignature& o) noexcept = delete;
};

struct ForLoopSignature
{
	Definition opt_init;

	Expr opt_cond;

	Assignment opt_step;

	ForLoopSignature() noexcept = default;

	ForLoopSignature& operator=(ForLoopSignature&& o) noexcept;

	ForLoopSignature& operator=(const ForLoopSignature& o) noexcept = delete;

	ForLoopSignature(ForLoopSignature&& o) noexcept = delete;

	ForLoopSignature(const ForLoopSignature& o) noexcept = delete;
};

struct ForSignature
{
	enum class Tag
	{
		EMPTY = 0,
		ForEachSignature,
		ForLoopSignature,
	} tag = Tag::EMPTY;

	union
	{
		ForEachSignature for_each;

		ForLoopSignature for_loop;
	};

	~ForSignature() noexcept;

	ForSignature() noexcept = default;

	ForSignature& operator=(ForSignature&& o) noexcept;

	ForSignature& operator=(const ForSignature& o) noexcept = delete;

	ForSignature(ForSignature&& o) noexcept = delete;

	ForSignature(const ForSignature& o) noexcept = delete;
};

struct For
{
	ForSignature signature;

	Statement body;

	Statement opt_until_body;

	For() noexcept = default;

	For& operator=(For&& o) noexcept;

	For& operator=(const For& o) noexcept = delete;

	For(For&& o) noexcept = delete;

	For(const For& o) noexcept = delete;
};

struct Case
{
	Expr label;

	Statement body;

	Case() noexcept = default;

	Case& operator=(Case&& o) noexcept;

	Case& operator=(const Case& o) noexcept = delete;

	Case(Case&& o) noexcept = delete;

	Case(const Case& o) noexcept = delete;
};

struct Switch
{
	Expr switched;

	vec<Case, 0> cases;

	Switch() noexcept = default;

	Switch& operator=(Switch&& o) noexcept;

	Switch& operator=(const Switch& o) noexcept = delete;

	Switch(Switch&& o) noexcept = delete;

	Switch(const Switch& o) noexcept = delete;
};

struct Impl
{
	Call trait;

	vec<Definition, 1> definitions;

	Impl() noexcept = default;

	Impl& operator=(Impl&& o) noexcept;

	Impl& operator=(const Impl& o) noexcept = delete;

	Impl(Impl&& o) noexcept = delete;

	Impl(const Impl& o) noexcept = delete;
};

struct Block
{
	vec<Statement> statements;

	Block() noexcept = default;

	Block& operator=(Block&& o) noexcept;

	Block& operator=(const Block& o) noexcept = delete;

	Block(Block&& o) noexcept = delete;

	Block(const Block& o) noexcept = delete;
};

struct ProcSignature
{
	vec<Definition> parameters;

	TypeRef opt_return_type;

	ProcSignature() noexcept = default;

	ProcSignature& operator=(ProcSignature&& o) noexcept;

	ProcSignature& operator=(const ProcSignature& o) noexcept = delete;

	ProcSignature(ProcSignature&& o) noexcept = delete;

	ProcSignature(const ProcSignature& o) noexcept = delete;
};

struct Proc
{
	ProcSignature signature;

	Statement opt_body;

	Proc() noexcept = default;

	Proc& operator=(Proc&& o) noexcept;

	Proc& operator=(const Proc& o) noexcept = delete;

	Proc(Proc&& o) noexcept = delete;

	Proc(const Proc& o) noexcept = delete;
};

struct StructuredType
{
	vec<Definition, 0> members;

	StructuredType() noexcept = default;

	StructuredType& operator=(StructuredType&& o) noexcept;

	StructuredType& operator=(const StructuredType& o) noexcept = delete;

	StructuredType(StructuredType&& o) noexcept = delete;

	StructuredType(const StructuredType& o) noexcept = delete;
};

struct Enum
{
	TypeRef opt_enum_type;

	vec<EnumValue> values;

	vec<Definition> definitions;
	
	Enum() noexcept = default;

	Enum& operator=(Enum&& o) noexcept;

	Enum& operator=(const Enum& o) noexcept = delete;

	Enum(Enum&& o) noexcept = delete;

	Enum(const Enum& o) noexcept = delete;
};

struct Trait
{
	vec<Definition, 0> bindings;

	vec<Definition, 0> definitions;

	Trait() noexcept = default;

	Trait& operator=(Trait&& o) noexcept;

	Trait& operator=(const Trait& o) noexcept = delete;

	Trait(Trait&& o) noexcept = delete;

	Trait(const Trait& o) noexcept = delete;
};

struct Module
{
	vec<Definition, 0> definitions;

	Module() noexcept = default;

	Module& operator=(Module&& o) noexcept;

	Module& operator=(const Module& o) noexcept = delete;

	Module(Module&& o) noexcept = delete;

	Module(const Module& o) noexcept = delete;
};

struct Type
{
	enum class Tag
	{
		EMPTY = 0,
		Proc,
		Struct,
		Union,
		Enum,
		Trait,
		Module,
		Impl,
	} tag = Tag::EMPTY;

	union
	{
		Proc proc_type;

		StructuredType struct_or_union_type;

		Enum enum_type;

		Trait trait_type;

		Module module_type;

		Impl impl_type;
	};

	~Type() noexcept;

	Type() noexcept = default;

	Type& operator=(Type&& o) noexcept;

	Type& operator=(const Type& o) noexcept = delete;

	Type(Type&& o) noexcept = delete;

	Type(const Type& o) noexcept = delete;
};

struct ProgramUnit
{
	vec<Definition> definitions;

	ProgramUnit() noexcept = default;

	ProgramUnit& operator=(ProgramUnit&& o) noexcept;

	ProgramUnit& operator=(const ProgramUnit& o) noexcept = delete;

	ProgramUnit(ProgramUnit&& o) noexcept = delete;

	ProgramUnit(const ProgramUnit& o) noexcept = delete;
};

#endif // AST_DATA_STRUCTURE_INCLUDE_GUARD
