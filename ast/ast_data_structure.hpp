#ifndef AST_DATA_STRUCTURE_INCLUDE_GUARD
#define AST_DATA_STRUCTURE_INCLUDE_GUARD
#include "../util/types.hpp"
#include "../util/strview.hpp"
#include "../util/str.hpp"
#include "../util/vec.hpp"

struct Definition;

struct NameRef;

struct Block;

struct VariableDef;

struct Statement;

struct Case;

struct Call;

struct UnaryOp;

struct BinaryOp;

struct Call;

struct If;

struct For;

struct When;

struct Switch;

struct Assignment;

struct Go;

struct Return;

struct Yield;

struct Expr;

struct TypeRef;



struct TypeName
{
	strview name;

	vec<Expr, 0> bounds;

	TypeName() noexcept = default;
	
	TypeName(const TypeName&) noexcept = delete;

	TypeName& operator=(TypeName&&) noexcept;
};

struct NameRef
{
	vec<TypeName, 1> parts;

	NameRef() noexcept = default;
	
	NameRef(const NameRef&) noexcept = delete;

	NameRef& operator=(NameRef&&) noexcept;
};

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
	
	StringLiteral(const StringLiteral&) noexcept = delete;
};

struct CharLiteral
{
	char8 value[4];
};

struct Literal
{
	enum class Type
	{
		EMPTY = 0,
		Integer,
		Float,
		String,
		Char,
	} type = Type::EMPTY;

	union
	{
		IntegerLiteral integer_literal;

		FloatLiteral float_literal;

		StringLiteral string_literal;

		CharLiteral char_literal;
	};

	~Literal() noexcept;

	Literal() noexcept = default;
	
	Literal(const Literal&) noexcept = delete;
};

struct Expr
{
	enum class Type
	{
		EMPTY = 0,
		BinaryOp,
		UnaryOp,
		Literal,
		NameRef,
		Call,
	} type = Type::EMPTY;

	union
	{
		BinaryOp* binary_op;

		UnaryOp* unary_op;

		Literal* literal;

		NameRef* name_ref;

		Call* call;
	};

	~Expr() noexcept;

	Expr() noexcept = default;
	
	Expr(const Expr&) noexcept = delete;

	Expr& operator=(Expr&&) noexcept;
};

struct TopLevelExpr
{
	enum class Type
	{
		EMPTY = 0,
		Expr,
		If,
		For,
		Block,
		Switch,
		When,
	} type = Type::EMPTY;

	union
	{
		Expr* expr;

		If* if_block;

		For* for_block;

		Block* block;

		Switch* switch_block;

		When* when_block;
	};

	~TopLevelExpr() noexcept;

	TopLevelExpr() noexcept = default;
	
	TopLevelExpr(const TopLevelExpr&) noexcept = delete;

	TopLevelExpr& operator=(TopLevelExpr&&) noexcept;
};

struct Statement
{
	enum class Type
	{
		EMPTY = 0,
		Block,
		If,
		For,
		When,
		Switch,
		Go,
		Return,
		Yield,
		VariableDef,
		Assignment,
		Call,
	} type = Type::EMPTY;

	union
	{
		Block* block;

		If* if_block;

		For* for_block;

		When* when_block;

		Switch* switch_block;

		Go* go_stmt;

		Return* return_stmt;

		Yield* yield_stmt;
		
		VariableDef* variable_def;

		Assignment* assignment;

		Call* call;
	};

	~Statement() noexcept;

	Statement() noexcept = default;
	
	Statement(const Statement&) noexcept = delete;
};

struct To
{
	vec<Case, 0> cases;

	To() noexcept = default;
	
	To(const To&) noexcept = delete;
};

struct Block
{
	vec<Statement, 0> statements;

	vec<Definition, 0> definitions;

	To to_block;

	Block() noexcept = default;

	Block(const Block&) noexcept = delete;
};

struct BinaryOp
{
	enum class Op : u8
	{
		EMPTY = 0,
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
	} op;

	Expr lhs;

	Expr rhs;

	BinaryOp() noexcept = default;

	BinaryOp(const BinaryOp&) noexcept = delete;
};

struct UnaryOp
{
	enum class Op : u8
	{
		EMPTY = 0,
		BitNot,
		LogNot,
		Neg,
	} op;

	Expr operand;

	UnaryOp() noexcept = default;

	UnaryOp(const UnaryOp&) noexcept = delete;
};

struct TypeRef
{
	enum class Type
	{
		EMPTY = 0,
		Ref,
		NameRef,
		Inline,
		TypeExpr,
	} type = Type::EMPTY;

	enum class Mutability
	{
		Immutable = 0,
		Mutable,
		Const,
	} mutability;

	union
	{
		TypeRef* ref;

		NameRef* name_ref;

		Definition* inline_def;

		Expr* type_expr;
	};

	~TypeRef() noexcept;

	TypeRef() noexcept = default;

	TypeRef(const TypeRef&) noexcept = delete;
};

struct VariableDef
{
	strview ident;

	TypeRef opt_type_ref;

	TopLevelExpr opt_initializer;

	VariableDef() noexcept = default;

	VariableDef(const VariableDef&) noexcept = delete;

	VariableDef& operator=(VariableDef&&) noexcept;
};

struct If
{
	VariableDef opt_init;

	Expr condition;

	Statement body;

	Statement opt_else_body;

	If() noexcept = default;

	If(const If&) noexcept = delete;
};

struct Call
{
	NameRef proc_name_ref;

	vec<Expr, 0> args;

	Call() noexcept = default;

	Call(const Call&) noexcept = delete;

	Call& operator=(Call&&) noexcept;
};

struct AssignableExpr
{
	enum class Type
	{
		EMPTY = 0,
		NameRef,
		Call,
	} type = Type::EMPTY;

	union
	{
		NameRef name_ref;

		Call call;
	};
	
	~AssignableExpr() noexcept;

	AssignableExpr() noexcept
	{
		memset(this, 0, sizeof(*this));
	}

	AssignableExpr(const AssignableExpr&) noexcept = delete;

	AssignableExpr& operator=(AssignableExpr&&) noexcept;
};

struct Assignment
{
	enum class Op
	{
		EMPTY = 0,
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
	} op;

	AssignableExpr assignee;

	TopLevelExpr assigned_value;

	Assignment() noexcept = default;

	Assignment(const Assignment&) noexcept = delete;
};

struct ForEach
{
	vec<strview, 1> idents;

	Expr iterated;
};

struct ForSignature
{
	enum class Type
	{
		EMPTY = 0,
		Normal,
		ForEach,
	} type;

	union
	{
		struct
		{
			VariableDef opt_init;

			Expr opt_condition;

			Assignment opt_step;
		} normal;

		ForEach for_each;
	};

	~ForSignature() noexcept;

	ForSignature() noexcept = default;

	ForSignature(const ForSignature&) noexcept = delete;

	ForSignature& operator=(ForSignature&&) noexcept;
};

struct For
{
	ForSignature signature;

	Statement body;

	Statement opt_until_body;

	For() noexcept = default;

	For(const For&) noexcept = delete;
};

struct When
{
	Expr condition;

	Statement body;

	Statement opt_else_body;

	When() noexcept = default;

	When(const When&) noexcept = delete;
};

struct Case
{
	Expr label;

	Statement body;

	Case() noexcept = default;

	Case(const Case&) noexcept = delete;

	Case& operator=(Case&&) noexcept;
};

struct Switch
{
	Expr switched;

	vec<Case, 0> cases;

	Switch() noexcept = default;

	Switch(const Switch&) noexcept = delete;
};

struct Go
{
	Expr label;

	Go() noexcept = default;

	Go(const Go&) noexcept = delete;
};

struct Return
{
	TopLevelExpr return_value;

	Return() noexcept = default;

	Return(const Return&) noexcept = delete;
};

struct Yield
{
	TopLevelExpr yield_value;

	Yield() noexcept = default;

	Yield(const Yield&) noexcept = delete;
};

struct ProcParam
{
	enum class Type
	{
		EMPTY = 0,
		VariableDef,
		GenericType,
	} type = Type::EMPTY;

	union
	{
		VariableDef variable_def;

		strview generic_type;
	};
	
	~ProcParam() noexcept;

	ProcParam() noexcept
	{
		memset(this, 0, sizeof(*this));
	};

	ProcParam& operator=(ProcParam&&) noexcept;
};

struct ProcSignature
{
	vec<ProcParam, 0> params;

	TypeRef return_type;

	ProcSignature() noexcept = default;

	ProcSignature(const ProcSignature&) noexcept = delete;
};

struct TypeMember
{
	strview opt_ident;

	TypeRef type_ref;

	bool is_pub;

	TypeMember() noexcept = default;

	TypeMember(const TypeMember&) noexcept = delete;
};

struct TypeValue
{
	strview ident;

	Expr value;

	TypeValue() noexcept = default;

	TypeValue(const TypeValue&) noexcept = delete;

	TypeValue& operator=(TypeValue&&) noexcept;
};

struct TraitMember
{
	enum class Type
	{
		EMPTY = 0,
		Definition,
		ProcSignature,
	} type = Type::EMPTY;

	union
	{
		Definition* definition;

		ProcSignature* signature;
	};

	~TraitMember() noexcept;

	TraitMember() noexcept = default;

	TraitMember(const TraitMember&) noexcept = delete;
};

struct TypeBindingConstraint
{
	NameRef bound_trait;

	TypeBindingConstraint() noexcept = default;

	TypeBindingConstraint(const TypeBindingConstraint&) noexcept = delete;

	TypeBindingConstraint& operator=(TypeBindingConstraint&&) noexcept;
};

struct ProcDef
{
	ProcSignature signature;

	Block body;

	ProcDef() noexcept = default;

	ProcDef(const ProcDef&) noexcept = delete;
};

struct StructDef
{
	vec<TypeMember> members;

	vec<Definition, 0> definitions;

	StructDef() noexcept = default;

	StructDef(const StructDef&) noexcept = delete;
};

struct UnionDef
{
	vec<TypeMember> members;

	vec<Definition, 0> definitions;

	UnionDef() noexcept = default;

	UnionDef(const UnionDef&) noexcept = delete;
};

struct EnumDef
{
	TypeRef enum_type;

	vec<TypeValue> values;

	vec<Definition, 0> definitions;

	EnumDef() noexcept = default;

	EnumDef(const EnumDef&) noexcept = delete;
};

struct BitsetDef
{
	TypeRef bitset_type;

	vec<TypeValue> values;

	vec<Definition, 0> definitions;

	BitsetDef() noexcept = default;

	BitsetDef(const BitsetDef&) noexcept = delete;
};

struct AliasDef
{
	TypeRef type_ref;

	AliasDef() noexcept = default;

	AliasDef(const AliasDef&) noexcept = delete;
};

struct NewTypeDef
{
	TypeRef type_ref;

	NewTypeDef() noexcept = default;

	NewTypeDef(const NewTypeDef&) noexcept = delete;
};

struct TraitDef
{
	vec<TraitMember, 0> members;

	TraitDef() noexcept = default;

	TraitDef(const TraitDef&) noexcept = delete;
};

struct AnnotationDef
{
	VariableDef arg;

	Block body;

	AnnotationDef() noexcept = default;

	AnnotationDef(const AnnotationDef&) noexcept = delete;
};

struct ModuleDef
{
	vec<Definition, 0> definitions;

	ModuleDef() noexcept = default;

	ModuleDef(const ModuleDef&) noexcept = delete;
};

struct TypeBinding
{
	vec<TypeBindingConstraint> constraints;

	TypeBinding() noexcept = default;

	TypeBinding(const TypeBinding&) noexcept = delete;
};

struct ValueBinding
{
	TypeRef type_ref;

	ValueBinding() noexcept = default;

	ValueBinding(const ValueBinding&) noexcept = delete;
};

struct Binding
{
	enum class Type
	{
		EMPTY = 0,
		TypeBinding,
		ValueBinding,
	} type = Type::EMPTY;

	strview ident;

	union 
	{
		TypeBinding type_binding;

		ValueBinding value_binding;
	};

	~Binding() noexcept;

	Binding() noexcept
	{
		memset(this, 0, sizeof(*this));
	};

	Binding(const Binding&) noexcept = delete;

	Binding& operator=(Binding&&) noexcept;
};

struct Definition
{
	enum class Type
	{
		EMPTY = 0,
		Proc,
		Struct,
		Union,
		Enum,
		Bitset,
		Alias,
		NewType,
		Trait,
		Impl,
		Annotation,
		Module,
	} type = Type::EMPTY;

	struct
	{
		bool has_ident : 1;
		bool is_pub : 1;
	} flags{};

	strview ident;

	vec<Binding> bindings;

	union
	{
		ProcDef proc_def;

		StructDef struct_def;

		UnionDef union_def;

		EnumDef enum_def;

		BitsetDef bitset_def;

		AliasDef alias_def;

		NewTypeDef newtype_def;

		TraitDef trait_def;

		ProcDef impl_def;

		AnnotationDef annotation_def;

		ModuleDef module_def;
	};

	~Definition() noexcept;

	Definition() noexcept;

	Definition& operator=(Definition&&) noexcept;

	Definition(const Definition&) = delete;
};

struct ProgramUnit
{
	vec<Definition> definitions{};

	ProgramUnit() noexcept = default;

	ProgramUnit(const ProgramUnit&) noexcept = delete;

	ProgramUnit(ProgramUnit&&) noexcept = default;
};

#endif // AST_DATA_STRUCTURE_INCLUDE_GUARD
