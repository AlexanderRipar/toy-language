#ifndef AST_FMT_INCLUDE_GUARD
#define AST_FMT_INCLUDE_GUARD

#include "ast_data_structure.hpp"

void ast_print_text(const ast::FileModule& program) noexcept;

void ast_print_tree(const ast::FileModule& program) noexcept;

#endif // AST_FMT_INCLUDE_GUARD