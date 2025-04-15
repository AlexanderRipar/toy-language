#include "core/config.hpp"
#include "core/pass_data.hpp"
#include "core/ast_helper.hpp"
#include "diag/diag.hpp"
#include "infra/hash.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

static TypeId find_entrypoint(Config* config, IdentifierPool* identifiers, TypeEntry* file_type) noexcept
{
	ASSERT_OR_IGNORE(file_type->tag == TypeTag::Composite);

	Scope* const file_scope = file_type->data<CompositeType>()->header.scope;

	const IdentifierId entrypoint_identifier_id = id_from_identifier(identifiers, config->entrypoint.symbol);

	const OptPtr<AstNode> opt_entrypoint = lookup_identifier_local(file_scope, entrypoint_identifier_id);

	if (is_none(opt_entrypoint))
		panic("Could not find definition for entrypoint symbol \"%.*s\" at top level of source file \"%.*s\"\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin(), static_cast<s32>(config->entrypoint.filepath.count()), config->entrypoint.filepath.begin());

	AstNode* const entrypoint = get_ptr(opt_entrypoint);

	ASSERT_OR_IGNORE(entrypoint->tag == AstTag::Definition);
	
	if (!has_children(entrypoint))
		panic("Expected definition of entrypoint symbol \"%.*s\" to have a value\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	const DefinitionInfo entrypoint_info = get_definition_info(entrypoint);

	if (is_none(entrypoint_info.value))
		panic("Expected definition of entrypoint symbol \"%.*s\" to have a value\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	AstNode* const entrypoint_func = get_ptr(entrypoint_info.value);

	if (!has_flag(entrypoint_func, AstFlag::Func_HasBody))
		panic("Expected entrypoint \"%.*s\" to have a body", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());	

	fprintf(stderr, "\n------------ %.*s AST ------------\n\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	diag::print_ast(stderr, identifiers, entrypoint_func);

	return attachment_of<DefinitionData>(entrypoint)->type_id;
}

s32 main(s32 argc, const char8** argv)
{
	if (argc == 0)
	{
		fprintf(stderr, "No arguments provided (not even invocation)\n");

		return EXIT_FAILURE;
	}
	else if (argc == 2 && strcmp(argv[1], "-help") == 0)
	{
		print_config_help();

		return EXIT_SUCCESS;
	}
	else if (argc == 3 && strcmp(argv[1] , "-config") == 0)
	{
		Config config = read_config(range::from_cstring(argv[2]));

		print_config(&config);

		AllocPool* const alloc = create_alloc_pool(1u << 24, 1u << 18);

		IdentifierPool* const identifiers = create_identifier_pool(alloc);

		SourceReader* const reader = create_source_reader(alloc);

		ErrorSink* const errors = create_error_sink(alloc, reader);

		Parser* const parser = create_parser(alloc, identifiers, errors);

		TypePool* const types = create_type_pool(alloc);

		AstPool* const asts = create_ast_pool(alloc);

		ValuePool* const values = create_value_pool(alloc);

		ScopePool* const scopes = create_scope_pool(alloc);

		Interpreter* const interpreter = create_interpreter(alloc, reader, parser, asts, scopes, types, values, identifiers);

		Typechecker* const typechecker = create_typechecker(alloc, interpreter, scopes, types, identifiers, asts, get_ast_builder(parser));

		set_interpreter_typechecker(interpreter, typechecker);

		const TypeId main_file_type_id = import_file(interpreter, config.entrypoint.filepath, false);

		find_entrypoint(&config, identifiers, type_entry_from_id(types, main_file_type_id));

		deinit_config(&config);

		fprintf(stderr, "\nCompleted successfully\n");

		return EXIT_SUCCESS;
	}
	else
	{
		fprintf(stderr, "Usage: %s ( -help | -config <filepath> )\n", argv[0]);

		return EXIT_FAILURE;
	}
}
