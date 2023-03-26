#include "ast_data_structure.hpp"

#include <cassert>

template<typename T>
static void obj_move(T* dst, T* src) noexcept
{
	memcpy(dst, src, sizeof(T));

	obj_zero(src);
}

template<typename T>
static void obj_zero(T* dst) noexcept
{
	memset(dst, 0, sizeof(T));
}



static void statement_cleanup_helper(Statement& stmt) noexcept
{
	if (stmt.assignment == nullptr)
		return;

	switch (stmt.type)
	{
	case Statement::Type::Block:
		stmt.block->~Block();
		break;

	case Statement::Type::If:
		stmt.if_block->~If();
		break;

	case Statement::Type::For:
		stmt.for_block->~For();
		break;

	case Statement::Type::Switch:
		stmt.switch_block->~Switch();
		break;

	case Statement::Type::VariableDef:
		stmt.variable_def->~VariableDef();
		break;

	case Statement::Type::Assignment:
		stmt.assignment->~Assignment();
		break;

	case Statement::Type::Call:
		stmt.call->~Name();
		break;

	case Statement::Type::Go:
		stmt.go_stmt->~Go();
		break;

	case Statement::Type::Return:
		stmt.return_stmt->~Return();
		break;

	case Statement::Type::Yield:
		stmt.yield_stmt->~Yield();
		break;

	default:
		assert(stmt.type == Statement::Type::EMPTY);
		break;
	}

	free(stmt.assignment);
}

static void typeref_cleanup_helper(TypeRef& typeref) noexcept
{
	if (typeref.ref == nullptr)
		return;

	switch (typeref.type)
	{
	case TypeRef::Type::Ref:
		typeref.ref->~TypeRef();
		break;

	case TypeRef::Type::Name:
		typeref.name_ref->~Name();
		break;

	case TypeRef::Type::Inline:
		typeref.inline_def->~Definition();
		break;

	case TypeRef::Type::TypeExpr:
		typeref.type_expr->~Expr();
		break;

	default:
		assert(typeref.type == TypeRef::Type::EMPTY);
		break;
	}

	free(typeref.ref);
}

static void traitmember_cleanup_helper(TraitMember& traitmember) noexcept
{
	if (traitmember.signature == nullptr)
		return;

	switch (traitmember.type)
	{
	case TraitMember::Type::Definition:
		traitmember.definition->~Definition();
		break;

	case TraitMember::Type::ProcSignature:
		traitmember.signature->~ProcSignature();
		break;

	default :
		assert(traitmember.type == TraitMember::Type::EMPTY);
		break;
	}

	free(traitmember.signature);
}

static void expr_cleanup_helper(Expr& expr) noexcept
{
	if (expr.binary_op == nullptr)
		return;

	switch (expr.type)
	{
	case Expr::Type::BinaryOp:
		expr.binary_op->~BinaryOp();
		break;

	case Expr::Type::UnaryOp:
		expr.unary_op->~UnaryOp();
		break;

	case Expr::Type::Literal:
		expr.literal->~Literal();
		break;

	case Expr::Type::Name:
		expr.name_ref->~Name();
		break;

	default:
		assert(expr.type == Expr::Type::EMPTY);
		break;
	}

	free(expr.binary_op);
}

static void toplevelexpr_cleanup_helper(TopLevelExpr& tl_expr) noexcept
{
	if (tl_expr.block == nullptr)
		return;

	switch (tl_expr.type)
	{
	case TopLevelExpr::Type::Expr: {
		tl_expr.expr->~Expr();
		break;
	}

	case TopLevelExpr::Type::If: {
		tl_expr.if_block->~If();
		break;
	}

	case TopLevelExpr::Type::For: {
		tl_expr.for_block->~For();
		break;
	}

	case TopLevelExpr::Type::Block: {
		tl_expr.block->~Block();
		break;
	}

	case TopLevelExpr::Type::Switch: {
		tl_expr.switch_block->~Switch();
		break;
	}

	default: {
		assert(tl_expr.type == TopLevelExpr::Type::EMPTY);
		break;
	}
	}

	free(tl_expr.block);
}

static void definition_cleanup_helper(Definition& definition) noexcept
{
	switch (definition.type)
	{
	case Definition::Type::Proc:
		definition.proc_def.~ProcDef();
		break;

	case Definition::Type::Struct:
		definition.struct_def.~StructDef();
		break;

	case Definition::Type::Union:
		definition.union_def.~UnionDef();
		break;

	case Definition::Type::Enum:
		definition.enum_def.~EnumDef();
		break;

	case Definition::Type::Bitset:
		definition.bitset_def.~BitsetDef();
		break;

	case Definition::Type::Alias:
		definition.alias_def.~AliasDef();
		break;

	case Definition::Type::NewType:
		definition.newtype_def.~NewTypeDef();

	case Definition::Type::Trait:
		definition.trait_def.~TraitDef();
		break;

	case Definition::Type::Impl:
		definition.impl_def.~ProcDef();
		break;

	case Definition::Type::Annotation:
		definition.annotation_def.~AnnotationDef();
		break;

	case Definition::Type::Module:
		definition.module_def.~ModuleDef();
		break;

	default:
		assert(definition.type == Definition::Type::EMPTY);
		break;
	}
}

static void proc_param_cleanup_helper(ProcParam& obj) noexcept
{
	switch (obj.type)
	{
	case ProcParam::Type::VariableDef:
		obj.variable_def.~VariableDef();
		break;
	
	case ProcParam::Type::GenericType:
		// noop
		break;

	default:
		assert(obj.type == ProcParam::Type::EMPTY);
	}
}

static void for_signature_cleanup_helper(ForSignature& obj) noexcept
{
	switch (obj.type)
	{
	case ForSignature::Type::Normal:
		obj.normal.opt_init.~VariableDef();
		obj.normal.opt_condition.~Expr();
		obj.normal.opt_step.~Assignment();
		break;

	case ForSignature::Type::ForEach:
		obj.for_each.~ForEach();
		break;

	default:
		assert(obj.type == ForSignature::Type::EMPTY);
		break;
	}
}


Literal::~Literal() noexcept
{
	switch (type)
	{
	case Type::Integer:
		integer_literal.~IntegerLiteral();
		break;

	case Type::Float:
		float_literal.~FloatLiteral();
		break;

	case Type::String:
		string_literal.~StringLiteral();
		break;

	case Type::Char:
		char_literal.~CharLiteral();
		break;

	default:
		assert(false);
		break;
	}
}

Statement::~Statement() noexcept
{
	statement_cleanup_helper(*this);
}

TypeRef::~TypeRef() noexcept
{
	typeref_cleanup_helper(*this);
}

TraitMember::~TraitMember() noexcept
{
	traitmember_cleanup_helper(*this);
}

Expr::~Expr() noexcept
{
	expr_cleanup_helper(*this);
}

TopLevelExpr::~TopLevelExpr() noexcept
{
	toplevelexpr_cleanup_helper(*this);
}

Definition::~Definition() noexcept
{
	definition_cleanup_helper(*this);
}

ProcParam::~ProcParam() noexcept
{
	proc_param_cleanup_helper(*this);
}

ForSignature::~ForSignature() noexcept
{
	for_signature_cleanup_helper(*this);
}


Definition::Definition() noexcept
{
	memset(this, 0, sizeof(*this));
}



TypeValue& TypeValue::operator=(TypeValue&& o) noexcept
{
	ident = std::move(o.ident);

	value = std::move(o.value);

	return *this;
}

VariableDef& VariableDef::operator=(VariableDef&& o) noexcept
{
	ident = std::move(o.ident);
	
	opt_type_ref = std::move(o.opt_type_ref);

	opt_initializer = std::move(o.opt_initializer);

	return *this;
}

TypeName& TypeName::operator=(TypeName&& o) noexcept
{
	name = std::move(o.name);

	bounds = std::move(o.bounds);

	return *this;
}

Name& Name::operator=(Name&& o) noexcept
{
	parts = std::move(o.parts);

	return *this;
}

Case& Case::operator=(Case&& o) noexcept
{
	label = std::move(o.label);

	body = std::move(o.body);

	return *this;
}

Definition& Definition::operator=(Definition&& o) noexcept
{
	definition_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

TopLevelExpr& TopLevelExpr::operator=(TopLevelExpr&& o) noexcept
{
	toplevelexpr_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

Expr& Expr::operator=(Expr&& o) noexcept
{
	expr_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

ProcParam& ProcParam::operator=(ProcParam&& o) noexcept
{
	proc_param_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}

ForSignature& ForSignature::operator=(ForSignature&& o) noexcept
{
	for_signature_cleanup_helper(*this);

	obj_move(this, &o);

	return *this;
}
