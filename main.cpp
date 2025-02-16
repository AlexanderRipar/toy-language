#include "config.hpp"
#include "pass_data.hpp"
#include "diag/diag.hpp"
#include "infra/hash.hpp"
#include "ast_helper.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

static TypeId resolve_main(Config* config, Typechecker* typechecker, IdentifierPool* identifiers, ScopePool* scopes, AstNode* root) noexcept
{
	Scope* const main_file_scope = alloc_file_scope(scopes, root);

	const OptPtr<AstNode> opt_main_def = lookup_identifier_local(main_file_scope, id_from_identifier(identifiers, config->entrypoint.symbol));

	if (is_none(opt_main_def))
		panic("Could not find definition for entrypoint symbol \"%.*s\" at top level of source file \"%.*s\"\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin(), static_cast<s32>(config->entrypoint.filepath.count()), config->entrypoint.filepath.begin());

	AstNode* const main_def = get_ptr(opt_main_def);

	ASSERT_OR_IGNORE(main_def->tag == AstTag::Definition);

	if (!has_children(main_def))
		panic("Expected definition of entrypoint symbol \"%.*s\" to have a value\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	const DefinitionInfo main_info = get_definition_info(main_def);

	if (is_none(main_info.value))
		panic("Expected definition of entrypoint symbol \"%.*s\" to have a value\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	AstNode* const main_func = get_ptr(main_info.value);

	if (!has_flag(main_func, AstFlag::Func_HasBody))
		panic("Expected entrypoint \"%.*s\" to have a body", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	fprintf(stderr, "\n------------ %.*s AST ------------\n\n", static_cast<s32>(config->entrypoint.symbol.count()), config->entrypoint.symbol.begin());

	diag::print_ast(stderr, identifiers, main_func);

	typecheck_definition(typechecker, main_file_scope, main_def);

	return attachment_of<DefinitionData>(main_def)->type_id;
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

		Parser* const parser = create_parser(alloc, identifiers);

		SourceReader* const reader = create_source_reader(alloc);

		TypePool* const types = create_type_pool(alloc);

		AstPool* const asts = create_ast_pool(alloc);

		ValuePool* const values = create_value_pool(alloc);

		AstNode* const builtins = create_builtin_definitions(asts, identifiers, types, values, get_ast_builder(parser));

		ScopePool* const scopes = create_scope_pool(alloc, builtins);

		Interpreter* const interpreter = create_interpreter(alloc, scopes, types, values, identifiers);

		Typechecker* const typechecker = create_typechecker(alloc, interpreter, scopes, types, identifiers);

		set_interpreter_typechecker(interpreter, typechecker);
		
		request_read(reader, config.entrypoint.filepath, id_from_identifier(identifiers, config.entrypoint.filepath));

		SourceFile file;

		if (!await_completed_read(reader, &file))
			panic("Could not read main source file\n");

		AstNode* root = parse(parser, file, asts);

		release_read(reader, file);

		fprintf(stderr, "\n------------ %.*s AST ------------\n\n", static_cast<s32>(config.entrypoint.filepath.count()), config.entrypoint.filepath.begin());

		diag::print_ast(stderr, identifiers, root);

		const TypeId main_type_id = resolve_main(&config, typechecker, identifiers, scopes, root);

		fprintf(stderr, "\n------------ %.*s TYPE ------------\n\n", static_cast<s32>(config.entrypoint.symbol.count()), config.entrypoint.symbol.begin());

		diag::print_type(stderr, identifiers, types, main_type_id);

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
