#include "tok/tok_gen.hpp"
#include "ast/ast_gen.hpp"
#include "ast/ast_fmt.hpp"
#include "util/file.hpp"

#include <cassert>
#include <cstdio>

static strview filename_from_filepath(const char* filepath) noexcept
{
	const char* last_name_beg = filepath;

	for (const char* curr = filepath; *curr != '\0'; ++curr)
	{
		if ((*curr == '/' || *curr == '\\'))
			last_name_beg = curr + 1;
	}

	const char* extension_beg = last_name_beg;

	while (*extension_beg != '\0' && *extension_beg != '.')
		++extension_beg;

	return strview{ last_name_beg, extension_beg };
}

int main(int32_t argc, const char** argv) noexcept
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s text-filename\n", argv[0]);

		return 2;
	}

	File file;

	if (!open_file(argv[1], File::Access::Read, File::Create::Normal, File::Create::Fail, file))
	{
		fprintf(stderr, "ERROR: Could not open file %s\n", argv[1]);

		return 1;
	}

	usz filesize;

	if (!get_file_size(file, filesize))
	{
		fprintf(stderr, "ERROR: Could not determine size of file %s\n", argv[1]);

		return 1;
	}

	char* file_content = static_cast<char*>(malloc(filesize));

	if (file_content == nullptr)
	{
		fprintf(stderr, "ERROR: malloc failed\n");

		return 1;
	}

	if (!read_file(file, file_content, static_cast<u32>(filesize), nullptr))
	{
		fprintf(stderr, "ERROR: Could not read from file %s\n", argv[1]);

		return 1;
	}

	fprintf(stderr, "Tokenizing...\n\n");

	vec<Token> tokens = tokenize(strview(file_content, filesize), false);

	const strview filename = filename_from_filepath(argv[1]);

	ast::FileModule file_module{};

	ast::Result rst = parse_program_unit(tokens, filename, file_module);
	
	switch (rst.tag)
	{
	case ast::Result::Tag::Ok:
		fprintf(stderr, "Great Success!\n");

		ast_print_text(file_module);

		ast_print_tree(file_module);

		break;

	case ast::Result::Tag::OutOfMemory:
	case ast::Result::Tag::NotImplemented:
	case ast::Result::Tag::UnexpectedEndOfStream: {
		fprintf(stderr, "%s: %s\n", rst.error_ctx, rst.message);

		break;
	}

	case ast::Result::Tag::InvalidSyntax: {
		fprintf(stderr, "%s: %s (Token %.*s on line %d)\n", rst.error_ctx, rst.message, static_cast<i32>(rst.problematic_token->data_strview().len()), rst.problematic_token->data_strview().begin(), rst.problematic_token->line_number);

		break;
	}

	case ast::Result::Tag::UnexpectedToken: {
		Token expected{ rst.expected_token, 0, {} };

		fprintf(stderr, "%s: (Expected %s instead of %.*s on line %d)\n", rst.error_ctx, expected.type_strview().begin(), static_cast<i32>(rst.problematic_token->data_strview().len()), rst.problematic_token->data_strview().begin(), rst.problematic_token->line_number);

		break;
	}

	default:
		assert(false);

		break;
	}

	return 0;
}
