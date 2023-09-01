#include "tok/tok_gen.hpp"
#include "ast/ast_gen.hpp"
#include "ast/ast_fmt.hpp"
#include "util/status.hpp"
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

static Status run(i32 argc, const char** argv) noexcept
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s text-filename\n", argv[0]);

		return STATUS_FROM_CUSTOM(CustomError::BadCommandLine);
	}

	File file;

	TRY(file_open(argv[1], File::Access::Read, File::Create::Normal, File::Create::Fail, file));

	usz filesize;

	TRY(file_get_size(file, filesize));

	char* file_content = static_cast<char*>(malloc(filesize));

	if (file_content == nullptr)
		return STATUS_FROM_CUSTOM(CustomError::OutOfMemory);

	TRY(file_read(file, file_content, static_cast<u32>(filesize), nullptr));

	fprintf(stderr, "Tokenizing...\n\n");

	vec<Token> tokens;
	
	TRY(tokenize(strview(file_content, filesize), false, tokens));

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

	return {};
}

int main(int32_t argc, const char** argv) noexcept
{
	if (const Status s = run(argc, argv); !s.is_ok())
	{
		char error_msg_buf[1024];

		static constexpr const char msg_too_long_msg[] = "[[Error message too long]]";

		if (s.error_message(error_msg_buf, sizeof(error_msg_buf)) > sizeof(error_msg_buf))
			memcpy(error_msg_buf, msg_too_long_msg, sizeof(msg_too_long_msg));

		const strview kind_name = s.kind_name();

		fprintf(stderr, "Encountered %.*sError 0x%x: \"%s\"\nwhile calling\n", static_cast<i32>(kind_name.len()), kind_name.begin(), s.error_code(), error_msg_buf);

		Slice<const ErrorLocation> error_trace = get_error_trace();

		if (error_trace.count() != 0)
			fprintf(stderr, "Trace (function that originated the error first):\n");
		else
			fprintf(stderr, "No trace available.\n");

		for (const ErrorLocation& loc : error_trace)
			fprintf(stderr, "    %s in function %s (line %d)\n", loc.file, loc.function, loc.line_number);

		if (const u32 dropped_count = get_dropped_trace_count(); dropped_count != 0)
			fprintf(stderr, "    ... And %d more\n", dropped_count);

		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
