#ifndef ERROR_INCLUDE_GUARD
#define ERROR_INCLUDE_GUARD

#include "common.hpp"

enum class ErrorCode : u16
{
    INVALID = 0,
    Lex_UnexpectedCharacter = 0x1000,
};

struct Error
{
    ErrorCode code;

    u16 character_count;

    u32 line_number;

    u32 character_number;

    void* error_data;
};

#endif // ERROR_INCLUDE_GUARD
