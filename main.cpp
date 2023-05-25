#include "tok/tok_gen.hpp"
#include "ast/ast_gen.hpp"
#include "ast/ast_fmt.hpp"

#include <cassert>
#include <cstdio>
#include <Windows.h>

u32 get_file_contents(const char* filename, vec<char, 0>& out_content) noexcept
{
	HANDLE h = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (h == INVALID_HANDLE_VALUE)
		goto ERROR_LABEL;

	LARGE_INTEGER size;

	if (!GetFileSizeEx(h, &size))
		goto ERROR_LABEL;

	if (!out_content.reserve(size.QuadPart))
		goto ERROR_LABEL;

	out_content.set_size(size.QuadPart);

	usz read_so_far = 0;

	while (size.QuadPart != 0)
	{
		DWORD bytes_read;

		const DWORD to_read = static_cast<DWORD>(size.QuadPart > INT_MAX ? INT_MAX : size.QuadPart);

		if (!ReadFile(h, out_content.begin() + read_so_far, to_read, &bytes_read, nullptr))
			goto ERROR_LABEL;

		size.QuadPart -= to_read;
	}

	CloseHandle(h);

	return 0;

ERROR_LABEL:

	CloseHandle(h);

	out_content.clear();

	return GetLastError();
}

int main(int32_t argc, const char** argv) noexcept
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s text-filename\n", argv[0]);

		return 2;
	}

	vec<char, 0> file;

	if (u32 rst = get_file_contents(argv[1], file); rst != 0)
	{
		fprintf(stderr, "ERROR: Could not open file %s: %d\n", argv[1], rst);

		return 1;
	}
	
	fprintf(stderr, "Tokenizing...\n\n");

	vec<Token> tokens = tokenize(strview(file.data(), file.size()), false);

	// for (const Token& t : tokens)
	// {
	// 	strview data = t.data_strview();
	// 
	// 	strview type = t.type_strview();
	//
	// 	fprintf(stderr, "ln %d %.*s \"%.*s\"\n", t.line_number, static_cast<i32>(type.len()), type.begin(), static_cast<i32>(data.len()), data.begin());
	// }

	ast::FileModule file_module;

	ast::Result rst = parse_program_unit(tokens, file_module);
	
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
