#ifndef AST_GEN_INCLUDE_GUARD
#define AST_GEN_INCLUDE_GUARD

#include "token_gen.hpp"
#include "util/types.hpp"
#include "util/vec.hpp"
#include "util/strview.hpp"
#include "ast_data_structure.hpp"

struct Result
{
	enum class Type
	{
		Ok,
		OutOfMemory,
		InvalidSyntax,
		UnexpectedToken,
		NotImplemented,
		UnexpectedEndOfStream,
		Oopsie,
	} type = Type::Ok;

	Token::Type expected_token;

	const char* error_ctx;

	const char* message;

	const Token* problematic_token;
};

Result parse_program_unit(const vec<Token>& tokens, ProgramUnit& out_program_unit) noexcept;

#endif // AST_GEN_INCLUDE_GUARD
