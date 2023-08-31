#ifndef AST_GEN_INCLUDE_GUARD
#define AST_GEN_INCLUDE_GUARD

#include "../tok/tok_gen.hpp"
#include "../util/types.hpp"
#include "../util/vec.hpp"
#include "../util/strview.hpp"
#include "ast_data_structure.hpp"


namespace ast
{
	struct Result
	{
		enum class Tag
		{
			Ok,
			OutOfMemory,
			InvalidSyntax,
			UnexpectedToken,
			UnexpectedEndOfStream,
		} tag = Tag::Ok;

		Token::Tag expected_token;

		const char* error_ctx;

		const char* message;

		const Token* problematic_token;
	};

	Result parse_program_unit(const vec<Token>& tokens, const strview filename, FileModule& out_program_unit) noexcept;
}

#endif // AST_GEN_INCLUDE_GUARD
