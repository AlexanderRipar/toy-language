#include "pass_data.hpp"

#include "ast_attach.hpp"
#include "ast_helper.hpp"

#include <cstdio>

struct Typechecker
{
	Interpreter* interpreter;

	ScopePool* scopes;

	TypePool* types;

	IdentifierPool* identifiers;

	AstPool* asts;

	Scope* builtin_scope;
};



static AstBuilderToken push_std_def(AstBuilder* builder, IdentifierPool* identifiers) noexcept
{
	const AstBuilderToken import_builtin_token = push_node(builder, AstBuilder::NO_CHILDREN, AstTag::Builtin, static_cast<AstFlag>(Builtin::Import));

	push_node(builder, AstBuilder::NO_CHILDREN, AstFlag::EMPTY, ValStringData{ id_from_identifier(identifiers, range::from_literal_string("std.evl")) });

	const AstBuilderToken true_builtin_token = push_node(builder, AstBuilder::NO_CHILDREN, AstTag::Builtin, static_cast<AstFlag>(Builtin::True));

	push_node(builder, true_builtin_token, AstTag::Call, AstFlag::EMPTY);

	const AstBuilderToken import_call_token = push_node(builder, import_builtin_token, AstTag::Call, AstFlag::EMPTY);

	return push_node(builder, import_call_token, AstFlag::EMPTY, DefinitionData{ id_from_identifier(identifiers, range::from_literal_string("std")), INVALID_TYPE_ID, INVALID_VALUE_ID });
}

static void push_std_use(AstBuilder* builder, IdentifierPool* identifiers, Range<char8> identifier) noexcept
{
	const AstBuilderToken std_identifier_token = push_node(builder, AstBuilder::NO_CHILDREN, AstFlag::EMPTY, ValIdentifierData{ id_from_identifier(identifiers, range::from_literal_string("std")) });

	push_node(builder, AstBuilder::NO_CHILDREN, AstFlag::EMPTY, ValIdentifierData{ id_from_identifier(identifiers, identifier) });

	const AstBuilderToken op_member_token = push_node(builder, std_identifier_token, AstTag::OpMember, AstFlag::EMPTY);
	
	push_node(builder, op_member_token, AstFlag::Definition_IsUse, DefinitionData{ id_from_identifier(identifiers, identifier), INVALID_TYPE_ID, INVALID_VALUE_ID });
}

static AstNode* create_builtin_ast(AstBuilder* builder, IdentifierPool* identifiers, AstPool* asts) noexcept
{
	const AstBuilderToken first_child_token = push_std_def(builder, identifiers);

	push_std_use(builder, identifiers, range::from_literal_string("u8"));

	push_std_use(builder, identifiers, range::from_literal_string("u16"));

	push_std_use(builder, identifiers, range::from_literal_string("u32"));

	push_std_use(builder, identifiers, range::from_literal_string("u64"));

	push_std_use(builder, identifiers, range::from_literal_string("s8"));

	push_std_use(builder, identifiers, range::from_literal_string("s16"));

	push_std_use(builder, identifiers, range::from_literal_string("s32"));

	push_std_use(builder, identifiers, range::from_literal_string("s64"));

	push_std_use(builder, identifiers, range::from_literal_string("bool"));

	push_std_use(builder, identifiers, range::from_literal_string("type"));

	push_node(builder, first_child_token, AstFlag::EMPTY, FileData{ BlockData{ 11, INVALID_SCOPE_ID }, INVALID_IDENTIFIER_ID });

	return complete_ast(builder, asts);
}

static Scope* create_builtin_scope(AstBuilder* builder, IdentifierPool* identifiers, AstPool* asts, ScopePool* scopes) noexcept
{
	AstNode* builtin_ast = create_builtin_ast(builder, identifiers, asts);

	FileData* const attach = attachment_of<FileData>(builtin_ast);

	Scope* const scope = alloc_builtins_scope(scopes, builtin_ast, attach->root_block.definition_count);

	attach->root_block.scope_id = id_from_scope(scopes, scope);

	AstDirectChildIterator it = direct_children_of(builtin_ast);

	for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
	{
		AstNode* const definition = get_ptr(rst);

		if (definition->tag == AstTag::Definition)
		{
			if (!add_definition_to_scope(scope, definition))
				panic("Duplicate builtin definition :(\n");
		}
	}

	ASSERT_OR_IGNORE(scope->header.capacity == scope->header.used);

	return scope;
}



static TypeId interpret_type_expr(Typechecker* typechecker, Scope* enclosing_scope, AstNode* expr) noexcept
{
	Value* const type_value = interpret_expr(typechecker->interpreter, enclosing_scope, expr);

	if (dealias_type_entry(typechecker->types, type_value->header.type_id)->tag != TypeTag::Type)
		panic("Expected type expression\n");

	const TypeId type_id = *data<TypeId>(type_value);

	release_interpretation_result(typechecker->interpreter, type_value);

	return type_id;
}

static Scope* init_file_scope(Typechecker* typechecker, AstNode* root) noexcept
{
	ASSERT_OR_IGNORE(root->tag == AstTag::File);

	Scope* const scope = alloc_scope(typechecker->scopes, typechecker->builtin_scope, root, attachment_of<FileData>(root)->root_block.definition_count);

	AstDirectChildIterator it = direct_children_of(root);

	for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
	{
		AstNode* const node = get_ptr(rst);

		if (node->tag != AstTag::Definition)
			continue;

		if (!add_definition_to_scope(scope, node))
		{
			const Range<char8> name = identifier_entry_from_id(typechecker->identifiers, attachment_of<DefinitionData>(node)->identifier_id)->range();

			panic("Definition '%.*s' already exists\n", static_cast<s32>(name.count()), name.begin());
		}
	}

	attachment_of<FileData>(root)->root_block.scope_id = id_from_scope(typechecker->scopes, scope);

	return scope;
}

static Scope* init_signature_scope(Typechecker* typechecker, Scope* enclosing_scope, AstNode* signature) noexcept
{
	AstNode* const parameters = first_child_of(signature);

	u32 parameter_count = 0;

	AstDirectChildIterator it1 = direct_children_of(parameters);

	for (OptPtr<AstNode> rst = next(&it1); is_some(rst); rst = next(&it1))
		parameter_count += 1;

	Scope* const scope = alloc_scope(typechecker->scopes, enclosing_scope, signature, parameter_count);

	AstDirectChildIterator it2 = direct_children_of(parameters);

	for (OptPtr<AstNode> rst = next(&it2); is_some(rst); rst = next(&it2))
	{
		AstNode* const parameter = get_ptr(rst);

		ASSERT_OR_IGNORE(parameter->tag == AstTag::Definition);

		if (!add_definition_to_scope(scope, parameter))
		{
			const Range<char8> name = identifier_entry_from_id(typechecker->identifiers, attachment_of<DefinitionData>(parameter)->identifier_id)->range();

			panic("Definition '%.*s' already exists\n", static_cast<s32>(name.count()), name.begin());
		}
	}

	attachment_of<FuncData>(signature)->scope_id = id_from_scope(typechecker->scopes, scope);

	return scope;
}

Typechecker* create_typechecker(AllocPool* alloc, Interpreter* Interpreter, ScopePool* scopes, TypePool* types, IdentifierPool* identifiers, AstPool* asts, AstBuilder* builder) noexcept
{
	Typechecker* const typechecker = static_cast<Typechecker*>(alloc_from_pool(alloc, sizeof(Typechecker), alignof(Typechecker)));

	typechecker->interpreter = Interpreter;
	typechecker->scopes = scopes;
	typechecker->types = types;
	typechecker->identifiers = identifiers;
	typechecker->asts = asts;
	typechecker->builtin_scope = create_builtin_scope(builder, identifiers, asts, scopes);

	return typechecker;
}

void release_typechecker([[maybe_unused]] Typechecker* typechecker) noexcept
{
}

TypeId typecheck_expr(Typechecker* typechecker, Scope* enclosing_scope, AstNode* expr) noexcept
{
	switch (expr->tag)
	{
	case AstTag::ValInteger:
	case AstTag::ValChar:
	{
		return id_from_type(typechecker->types, TypeTag::CompInteger, TypeFlag::EMPTY, {});
	}

	case AstTag::ValFloat:
	{
		return id_from_type(typechecker->types, TypeTag::CompFloat, TypeFlag::EMPTY, {});
	}

	case AstTag::ValString:
	{
		return id_from_type(typechecker->types, TypeTag::CompString, TypeFlag::EMPTY, {});
	}

	case AstTag::ValIdentifer:
	{
		ValIdentifierData* const identifier_data = attachment_of<ValIdentifierData>(expr);

		const ScopeLookupResult lookup = lookup_identifier_recursive(enclosing_scope, identifier_data->identifier_id);

		if (!is_valid(lookup))
		{
			const Range<char8> name = identifier_entry_from_id(typechecker->identifiers, identifier_data->identifier_id)->range();

			panic("Could not find definition for identifier '%.*s'\n", static_cast<s32>(name.count()), name.begin());
		}

		AstNode* const definition = lookup.definition;

		DefinitionData* const definition_data = attachment_of<DefinitionData>(definition);

		if (definition_data->type_id == INVALID_TYPE_ID)
			typecheck_definition(typechecker, lookup.enclosing_scope, definition);

		return definition_data->type_id;
	}

	case AstTag::OpLogAnd:
	case AstTag::OpLogOr:
	{
		AstNode* const lhs = first_child_of(expr);

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId lhs_type_id = typecheck_expr(typechecker, enclosing_scope, lhs);

		const TypeId rhs_type_id = typecheck_expr(typechecker, enclosing_scope, rhs);

		if (dealias_type_entry(typechecker->types, lhs_type_id)->tag != TypeTag::Boolean)
			panic("Left-hand-side of '%s' must be of type bool\n", ast_tag_name(expr->tag));

		if (dealias_type_entry(typechecker->types, rhs_type_id)->tag != TypeTag::Boolean)
			panic("Right-hand-side of '%s' must be of type bool\n", ast_tag_name(expr->tag));

		return id_from_type(typechecker->types, TypeTag::Boolean, TypeFlag::EMPTY, {});
	}

	case AstTag::OpTypeArray:
	{
		AstNode* const count = first_child_of(expr);

		Value* const count_value = interpret_expr(typechecker->interpreter, enclosing_scope, count);

		TypeEntry* const count_type = dealias_type_entry(typechecker->types, count_value->header.type_id);

		u64 the_count;

		if (count_type->tag == TypeTag::CompInteger)
		{
			if (!comp_integer_as_u64(data<CompIntegerValue>(count_value), &the_count))
				panic("Array count expression value out of range [0, 2^64-1]\n");
		}
		else if (count_type->tag == TypeTag::Integer)
		{
			IntegerType* const integer_type = count_type->data<IntegerType>();

			if (integer_type->bits == 8)
				the_count = *data<u8>(count_value);
			else if (integer_type->bits == 16)
				the_count = *data<u16>(count_value);
			else if (integer_type->bits == 32)
				the_count = *data<u32>(count_value);
			else if (integer_type->bits == 64)
				the_count = *data<u64>(count_value);
			else
				panic("Integer bit width of %u in array count expression is not currently supported\n", integer_type->bits);

			if ((count_type->flags & TypeFlag::Integer_IsSigned) == TypeFlag::Integer_IsSigned && (the_count & (static_cast<u64>(1) << (integer_type->bits - 1))) != 0)
				panic("Array count expression value negative\n");
		}
		else
		{
			panic("Unexpected non-integer type in array count expression\n");
		}

		release_interpretation_result(typechecker->interpreter, count_value);

		AstNode* const element_type = next_sibling_of(count);

		Value* const element_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, element_type);

		if (dealias_type_entry(typechecker->types, element_type_value->header.type_id)->tag != TypeTag::Type)
			panic("Expected type expression as array's element type\n");

		const TypeId element_type_id = *data<TypeId>(element_type_value);

		release_interpretation_result(typechecker->interpreter, element_type_value);

		ArrayType array_type{};
		array_type.count = the_count;
		array_type.element_id = element_type_id;

		const TypeId array_type_id = id_from_type(typechecker->types, TypeTag::Array, TypeFlag::EMPTY, range::from_object_bytes(&array_type));

		return id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, range::from_object_bytes(&array_type_id));
	}

	case AstTag::UOpTypeSlice:
	case AstTag::UOpTypeMultiPtr:
	case AstTag::UOpTypeOptMultiPtr:
	case AstTag::UOpTypeOptPtr:
	case AstTag::UOpTypePtr:
	{
		Value* const pointer_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, expr);

		ASSERT_OR_IGNORE(dealias_type_entry(typechecker->types, pointer_type_value->header.type_id)->tag == TypeTag::Type);

		const TypeId pointer_type_id = *data<TypeId>(pointer_type_value);

		release_interpretation_result(typechecker->interpreter, pointer_type_value);

		return id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, range::from_object_bytes(&pointer_type_id));
	}

	case AstTag::OpArrayIndex:
	{
		AstNode* const array = first_child_of(expr);

		const TypeId array_type_id = typecheck_expr(typechecker, enclosing_scope, array);

		TypeEntry* const entry = dealias_type_entry(typechecker->types, array_type_id);

		TypeId element_type_id;

		if (entry->tag == TypeTag::Array)
		{
			element_type_id = entry->data<ArrayType>()->element_id;
		}
		else if (entry->tag == TypeTag::Slice)
		{
			element_type_id = entry->data<SliceType>()->element_id;
		}
		else if (entry->tag == TypeTag::Ptr && (entry->flags & TypeFlag::Ptr_IsMulti) == TypeFlag::Ptr_IsMulti)
		{
			element_type_id = entry->data<PtrType>()->pointee_id;
		}
		else
		{
			panic("Expected first operand of array index operation to be of array, slice or multi-pointer type\n");
		}

		AstNode* const index = next_sibling_of(array);

		const TypeId index_type_id = typecheck_expr(typechecker, enclosing_scope, index);

		TypeEntry* const index_type_entry = dealias_type_entry(typechecker->types, index_type_id);

		if (index_type_entry->tag != TypeTag::Integer && index_type_entry->tag != TypeTag::CompInteger)
			panic("Expected index operand of array index operation to be of integer type\n");

		return element_type_id;
	}

	case AstTag::Block:
	{
		BlockData* const block_data = attachment_of<BlockData>(expr);

		Scope* const block_scope = alloc_scope(typechecker->scopes, enclosing_scope, expr, block_data->definition_count);

		block_data->scope_id = id_from_scope(typechecker->scopes, block_scope);

		AstDirectChildIterator it = direct_children_of(expr);

		const TypeId void_type_id = id_from_type(typechecker->types, TypeTag::Void, TypeFlag::EMPTY, {});

		TypeId last_child_type_id = void_type_id;

		for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
		{
			AstNode* const child = get_ptr(rst);

			last_child_type_id = typecheck_expr(typechecker, block_scope, child);

			TypeEntry* const child_type_entry = dealias_type_entry(typechecker->types, last_child_type_id);

			if (child_type_entry->tag != TypeTag::Void && child_type_entry->tag != TypeTag::Definition && has_next_sibling(child))
				panic("Non-void expression at non-terminal position inside block\n");
		}

		return last_child_type_id;
	}

	case AstTag::If:
	{
		const IfInfo if_info = get_if_info(expr);

		const TypeId condition_type_id = typecheck_expr(typechecker, enclosing_scope, if_info.condition);

		TypeEntry* const condition_type_entry = dealias_type_entry(typechecker->types, condition_type_id);

		if (condition_type_entry->tag != TypeTag::Boolean)
			panic("Expected if condition to be of bool type\n");

		// TODO: Typecheck where
		if (is_some(if_info.where))
			panic("Where clause not supported yet\n");

		const TypeId consequent_type_id = typecheck_expr(typechecker, enclosing_scope, if_info.consequent);

		TypeEntry* const consequent_type_entry = dealias_type_entry(typechecker->types, consequent_type_id);

		if (is_some(if_info.alternative))
		{
			const TypeId alternative_type_id = typecheck_expr(typechecker, enclosing_scope, get_ptr(if_info.alternative));

			TypeEntry* const alternative_type_entry = dealias_type_entry(typechecker->types, alternative_type_id);

			// TODO: Support non-exact matches, especially in case of chained if-elseif-...-else and switch

			const OptPtr<TypeEntry> common_type_entry = find_common_type_entry(typechecker->types, consequent_type_entry, alternative_type_entry);

			if (is_none(common_type_entry))
				panic("Incompatible types between if branches\n");

			return id_from_type_entry(typechecker->types, get_ptr(common_type_entry));
		}
		else if (consequent_type_entry->tag == TypeTag::Void)
		{
			return consequent_type_id;
		}
		else
		{
			panic("Body of if without else must be of type void\n");
		}
	}

	case AstTag::Func:
	{
		const FuncInfo func_info = get_func_info(expr);

		FuncData* const func_data = attachment_of<FuncData>(expr);

		if (is_some(func_info.return_type))
		{
			Value* const return_type_value = interpret_expr(typechecker->interpreter, enclosing_scope, get_ptr(func_info.return_type));

			TypeEntry* return_type_entry = dealias_type_entry(typechecker->types, return_type_value->header.type_id);

			if (return_type_entry->tag != TypeTag::Type)
				panic("Expected type expression as %s's return type\n", has_flag(expr, AstFlag::Func_IsProc) ? "proc" : "func");

			const TypeId return_type_id = *data<TypeId>(return_type_value);

			release_interpretation_result(typechecker->interpreter, return_type_value);

			func_data->return_type_id = return_type_id;
		}
		else
		{
			func_data->return_type_id = id_from_type(typechecker->types, TypeTag::Void, TypeFlag::EMPTY, {});
		}

		Scope* const signature_scope = init_signature_scope(typechecker, enclosing_scope, expr);

		FuncTypeBuilder* const builder = alloc_func_type_builder(typechecker->types);

		AstDirectChildIterator it = direct_children_of(func_info.parameters);

		for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
		{
			AstNode* const parameter = get_ptr(rst);

			typecheck_definition(typechecker, signature_scope, parameter);

			DefinitionData* const parameter_data = attachment_of<DefinitionData>(parameter);

			add_func_type_param(typechecker->types, builder, { 0, 0, has_flag(parameter, AstFlag::Definition_IsMut), parameter_data->identifier_id, parameter_data->type_id, INVALID_VALUE_ID /* TODO */ });
		}

		func_data->signature_type_id = complete_func_type(typechecker->types, builder, func_data->return_type_id, has_flag(expr, AstFlag::Func_IsProc));

		if (is_some(func_info.body))
		{
			TypeId const returned_type_id = typecheck_expr(typechecker, signature_scope, get_ptr(func_info.body));

			if (!can_implicity_convert_from_to(typechecker->types, returned_type_id, func_data->return_type_id))
				panic("Mismatch between declared and actual return type\n");
		}

		return func_data->signature_type_id;
	}

	case AstTag::Call:
	{
		AstNode* const callee = first_child_of(expr);

		const TypeId callee_type_id = typecheck_expr(typechecker, enclosing_scope, callee);

		TypeEntry* const entry = dealias_type_entry(typechecker->types, callee_type_id);

		if (entry->tag != TypeTag::Func && entry->tag != TypeTag::Builtin)
			panic("Expected func or proc before call\n");

		FuncType* const func_type = entry->data<FuncType>();

		AstNode* curr = callee;

		for (u32 i = 0; i != func_type->header.parameter_count; ++i)
		{
			if (!has_next_sibling(curr))
				panic("Too few parameters in call (expected %u but got %u)\n", func_type->header.parameter_count, i);

			curr = next_sibling_of(curr);

			const TypeId parameter_type_id = typecheck_expr(typechecker, enclosing_scope, curr);

			if (!can_implicity_convert_from_to(typechecker->types, parameter_type_id, func_type->params[i].type))
				panic("Mismatch between expected and actual call parameter type\n");
		}

		if (has_next_sibling(curr))
		{
			u32 actual_parameter_count = func_type->header.parameter_count;

			do
			{
				curr = next_sibling_of(curr);

				actual_parameter_count += 1;
			}
			while (has_next_sibling(curr));

			panic("Too many parameters in call (expected %u but got %u)\n", func_type->header.parameter_count, actual_parameter_count);
		}

		return func_type->header.return_type_id;
	}

	case AstTag::OpAdd:
	case AstTag::OpSub:
	case AstTag::OpMul:
	case AstTag::OpDiv:
	{
		AstNode* const lhs = first_child_of(expr);

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId lhs_type_id = typecheck_expr(typechecker, enclosing_scope, lhs);

		const TypeId rhs_type_id = typecheck_expr(typechecker, enclosing_scope, rhs);

		const OptPtr<TypeEntry> common_type = find_common_type_entry(typechecker->types, type_entry_from_id(typechecker->types, lhs_type_id), type_entry_from_id(typechecker->types, rhs_type_id));

		if (is_none(common_type))
			panic("Operands of incompatible types supplied to binary operator '%s'\n", ast_tag_name(expr->tag));

		return id_from_type_entry(typechecker->types, get_ptr(common_type));
	}

	case AstTag::OpMember:
	{
		AstNode* const lhs = first_child_of(expr);

		AstNode* const rhs = next_sibling_of(lhs);

		const TypeId lhs_type_id = typecheck_expr(typechecker, enclosing_scope, lhs);

		TypeEntry* entry = dealias_type_entry(typechecker->types, lhs_type_id);

		if (entry->tag == TypeTag::Type)
		{
			const TypeId lhs_value_type_id = interpret_type_expr(typechecker, enclosing_scope, lhs);

			TypeEntry* const lhs_value_entry = type_entry_from_id(typechecker->types, lhs_value_type_id);

			if (lhs_value_entry->tag != TypeTag::Composite)
				panic("Expected either composite value or composite type as left-hand-side of '.' member access operator\n");

			CompositeType* const composite = entry->data<CompositeType>();

			return typecheck_expr(typechecker, composite->header.scope, rhs);
		}
		else if (entry->tag == TypeTag::Composite)
		{
			CompositeType* const composite = entry->data<CompositeType>();
	
			return typecheck_expr(typechecker, composite->header.scope, rhs);
		}
		else
		{
			panic("Expected either composite value or composite type as left-hand-side of '.' member access operator\n");
		}
	}

	case AstTag::Definition:
	{
		if (!add_definition_to_scope(enclosing_scope, expr))
		{
			const Range<char8> name = identifier_entry_from_id(typechecker->identifiers, attachment_of<DefinitionData>(expr)->identifier_id)->range();

			panic("Definition '%.*s' already exists\n", static_cast<s32>(name.count()), name.begin());
		}

		typecheck_definition(typechecker, enclosing_scope, expr);

		return id_from_type(typechecker->types, TypeTag::Definition, TypeFlag::EMPTY, {});
	}

	case AstTag::Builtin:
	{
		return typecheck_builtin(typechecker, static_cast<Builtin>(expr->flags));
	}

	case AstTag::File:
	case AstTag::ParameterList:
	case AstTag::Case:
	{
		panic("Unexpected AST node type '%s' passed to typecheck_expr\n", ast_tag_name(expr->tag));
	}

	case AstTag::CompositeInitializer:
	case AstTag::ArrayInitializer:
	case AstTag::Wildcard:
	case AstTag::Where:
	case AstTag::Expects:
	case AstTag::Ensures:
	case AstTag::For:
	case AstTag::ForEach:
	case AstTag::Switch:
	case AstTag::Trait:
	case AstTag::Impl:
	case AstTag::Catch:
	case AstTag::Return:
	case AstTag::Leave:
	case AstTag::Yield:
	case AstTag::UOpTypeTailArray:
	case AstTag::UOpEval:
	case AstTag::UOpTry:
	case AstTag::UOpDefer:
	case AstTag::UOpAddr:
	case AstTag::UOpDeref:
	case AstTag::UOpBitNot:
	case AstTag::UOpLogNot:
	case AstTag::UOpTypeVar:
	case AstTag::UOpImpliedMember:
	case AstTag::UOpNegate:
	case AstTag::UOpPos:
	case AstTag::OpAddTC:
	case AstTag::OpSubTC:
	case AstTag::OpMulTC:
	case AstTag::OpMod:
	case AstTag::OpBitAnd:
	case AstTag::OpBitOr:
	case AstTag::OpBitXor:
	case AstTag::OpShiftL:
	case AstTag::OpShiftR:
	case AstTag::OpCmpLT:
	case AstTag::OpCmpGT:
	case AstTag::OpCmpLE:
	case AstTag::OpCmpGE:
	case AstTag::OpCmpNE:
	case AstTag::OpCmpEQ:
	case AstTag::OpSet:
	case AstTag::OpSetAdd:
	case AstTag::OpSetSub:
	case AstTag::OpSetMul:
	case AstTag::OpSetDiv:
	case AstTag::OpSetAddTC:
	case AstTag::OpSetSubTC:
	case AstTag::OpSetMulTC:
	case AstTag::OpSetMod:
	case AstTag::OpSetBitAnd:
	case AstTag::OpSetBitOr:
	case AstTag::OpSetBitXor:
	case AstTag::OpSetShiftL:
	case AstTag::OpSetShiftR:
		panic("Unimplemented AST node tag '%s' in typecheck_expr\n", ast_tag_name(expr->tag));

	default:
		ASSERT_UNREACHABLE;
	}
}

void typecheck_definition(Typechecker* typechecker, Scope* enclosing_scope, AstNode* definition) noexcept
{
	ASSERT_OR_IGNORE(definition->tag == AstTag::Definition);

	ASSERT_OR_IGNORE(has_children(definition));

	const DefinitionInfo info = get_definition_info(definition);

	DefinitionData* const definition_data = attachment_of<DefinitionData>(definition);

	TypeId definition_type_id = INVALID_TYPE_ID;

	if (is_some(info.type))
	{
		definition_type_id = interpret_type_expr(typechecker, enclosing_scope, get_ptr(info.type));

		ASSERT_OR_IGNORE(definition_type_id != INVALID_TYPE_ID);
	}

	if (is_some(info.value))
	{
		const TypeId inferred_type_id = typecheck_expr(typechecker, enclosing_scope, get_ptr(info.value));

		if (definition_type_id == INVALID_TYPE_ID)
			definition_type_id = inferred_type_id;
		else if (!can_implicity_convert_from_to(typechecker->types, inferred_type_id, definition_type_id))
			panic("Incompatible types\n");
	}

	definition_data->type_id = definition_type_id;
}

TypeId typecheck_builtin(Typechecker* typechecker, Builtin builtin) noexcept
{
	switch (builtin)
	{
	case Builtin::Integer:
	{
		struct
		{
			FuncTypeHeader header;

			FuncTypeParam params[2];
		} func{};

		func.header.parameter_count = 2;
		func.header.return_type_id = id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, {});

		func.params[0].name = id_from_identifier(typechecker->identifiers, range::from_literal_string("bits"));
		func.params[0].type = id_from_type(typechecker->types, TypeTag::CompInteger, TypeFlag::EMPTY, {});
		func.params[0].default_value = INVALID_VALUE_ID;

		func.params[1].name = id_from_identifier(typechecker->identifiers, range::from_literal_string("is_signed"));
		func.params[1].type = id_from_type(typechecker->types, TypeTag::Boolean, TypeFlag::EMPTY, {});
		func.params[1].default_value = INVALID_VALUE_ID;

		return id_from_type(typechecker->types, TypeTag::Func, TypeFlag::EMPTY, range::from_object_bytes(&func));
	}

	case Builtin::Type:
	case Builtin::CompInteger:
	case Builtin::CompFloat:
	case Builtin::CompString:
	case Builtin::TypeBuilder:
	{
		FuncTypeHeader func{};

		func.return_type_id = id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, {});

		return id_from_type(typechecker->types, TypeTag::Builtin, TypeFlag::EMPTY, range::from_object_bytes(&func));
	}

	case Builtin::True:
	{
		FuncTypeHeader func{};

		func.return_type_id = id_from_type(typechecker->types, TypeTag::Boolean, TypeFlag::EMPTY, {});

		return id_from_type(typechecker->types, TypeTag::Builtin, TypeFlag::EMPTY, range::from_object_bytes(&func));
	}

	case Builtin::Typeof:
		panic("Builtin '_typeof' not yet implemented\n");

	case Builtin::Sizeof:
	case Builtin::Alignof:
	case Builtin::Strideof:
	case Builtin::Offsetof:
	{
		struct
		{
			FuncTypeHeader header;

			FuncTypeParam param;
		} func{};

		func.header.parameter_count = 1;
		func.header.return_type_id = id_from_type(typechecker->types, TypeTag::CompInteger, TypeFlag::EMPTY, {});

		func.param.name = id_from_identifier(typechecker->identifiers, range::from_literal_string("typ"));
		func.param.type = id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, {});
		func.param.default_value = INVALID_VALUE_ID;

		return id_from_type(typechecker->types, TypeTag::Builtin, TypeFlag::EMPTY, range::from_object_bytes(&func));
	}

	case Builtin::Nameof:
	{
		struct
		{
			FuncTypeHeader header;

			FuncTypeParam param;
		} func{};

		func.header.parameter_count = 1;
		func.header.return_type_id = id_from_type(typechecker->types, TypeTag::CompString, TypeFlag::EMPTY, {});

		func.param.name = id_from_identifier(typechecker->identifiers, range::from_literal_string("typ"));
		func.param.type = id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, {});
		func.param.default_value = INVALID_VALUE_ID;

		return id_from_type(typechecker->types, TypeTag::Builtin, TypeFlag::EMPTY, range::from_object_bytes(&func));
	}

	case Builtin::Import:
	{
		const IntegerType u8_data{ 8 };

		const TypeId u8_type_id = id_from_type(typechecker->types, TypeTag::Integer, TypeFlag::EMPTY, range::from_object_bytes(&u8_data));

		const SliceType u8_slice_data{ u8_type_id };

		const TypeId u8_slice_type_id = id_from_type(typechecker->types, TypeTag::Slice, TypeFlag::EMPTY, range::from_object_bytes(&u8_slice_data));

		struct
		{
			FuncTypeHeader header;

			FuncTypeParam params[2];
		} func{};

		func.header.parameter_count = 2;
		func.header.return_type_id = id_from_type(typechecker->types, TypeTag::Type, TypeFlag::EMPTY, {});

		func.params[0].name = id_from_identifier(typechecker->identifiers, range::from_literal_string("filepath"));
		func.params[0].type = u8_slice_type_id;
		func.params[0].default_value = INVALID_VALUE_ID;

		func.params[1].name = id_from_identifier(typechecker->identifiers, range::from_literal_string("is_std"));
		func.params[1].type = id_from_type(typechecker->types, TypeTag::Boolean, TypeFlag::EMPTY, {});
		func.params[1].default_value = INVALID_VALUE_ID;

		return id_from_type(typechecker->types, TypeTag::Builtin, TypeFlag::EMPTY, range::from_object_bytes(&func));
	}

	case Builtin::CreateTypeBuilder:
		panic("Builtin '_tb_creat' not yet implemented\n");

	case Builtin::AddTypeMember:
		panic("Builtin '_tb_add' not yet implemented\n");

	case Builtin::CompleteType:
		panic("Builtin '_tb_compl' not yet implemented\n");

	default:
		ASSERT_UNREACHABLE;
	}
}

TypeId typecheck_file(Typechecker* typechecker, AstNode* root) noexcept
{
	ASSERT_OR_IGNORE(root->tag == AstTag::File);

	Scope* const file_scope = init_file_scope(typechecker, root);

	AstDirectChildIterator it = direct_children_of(root);

	CompositeTypeBuilder* const builder = alloc_composite_type_builder(typechecker->types);

	for (OptPtr<AstNode> rst = next(&it); is_some(rst); rst = next(&it))
	{
		AstNode* const definition = get_ptr(rst);

		if (definition->tag != AstTag::Definition)
			panic("Top-level %s are not currently supported.\n", ast_tag_name(definition->tag));

		typecheck_definition(typechecker, file_scope, definition);

		if (has_flag(definition, AstFlag::Definition_IsGlobal))
			fprintf(stderr, "WARN: Redundant 'global' specifier on top-level definition. Top level definitions are implicitly global\n");

		DefinitionData* const attachment = attachment_of<DefinitionData>(definition);

		add_composite_type_member(typechecker->types, builder, {
			0,
			has_flag(definition, AstFlag::Definition_IsMut),
			has_flag(definition, AstFlag::Definition_IsPub),
			true,
			has_flag(definition, AstFlag::Definition_IsUse),
			attachment->identifier_id,
			attachment->type_id,
			INVALID_VALUE_ID /* TODO */,
			0
		});
	}

	attachment_of<FileData>(root)->root_block.scope_id = id_from_scope(typechecker->scopes, file_scope);

	return complete_composite_type(typechecker->types, typechecker->scopes, builder, 0, 1, 0);
}
