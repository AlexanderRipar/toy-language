#ifndef DIAG_HELPERS_INCLUDE_GUARD
#define DIAG_HELPERS_INCLUDE_GUARD

#include "../infra/types.hpp"
#include "../infra/print/print.hpp"

PrintInsert create_id_print_insert(u32 id, Range<char8> prefix, Range<char8> suffix) noexcept;

bool print_format_id(PrintState* state, const void* insert_attach, PrintSpec spec) noexcept;

#endif // DIAG_HELPERS_INCLUDE_GUARD
