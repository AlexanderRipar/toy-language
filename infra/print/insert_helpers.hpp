#ifndef PRINT_INSERT_HELPERS_INCLUDE_GUARD
#define PRINT_INSERT_HELPERS_INCLUDE_GUARD

#include "print.hpp"

Maybe<char8*> ps_reserve_buffer(PrintState* state, u64 n) noexcept;

bool ps_fill(PrintState* state, u64 count, char8 fill_char) noexcept;

bool ps_prepad(PrintState* state, PrintAlignment alignment, u64 content_size, u64 frame_size, char8 padding) noexcept;

bool ps_postpad(PrintState* state, PrintAlignment alignment, u64 content_size, u64 frame_size, char8 padding) noexcept;

bool ps_print_chars(PrintState* state, Range<char8> value) noexcept;

#endif // PRINT_INSERT_HELPERS_INCLUDE_GUARD
