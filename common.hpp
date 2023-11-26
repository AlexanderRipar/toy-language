#ifndef COMMON_INCLUDE_GUARD
#define COMMON_INCLUDE_GUARD

#include <cstdint>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

using byte = u8;

using char8 = char;
using char16 = wchar_t;

using uint = u64;
using sint = s64;

template<typename T, uint COUNT>
inline uint array_count([[maybe_unused]] const T(&arr)[COUNT]) noexcept
{
	return COUNT;
}

#endif // COMMON_INCLUDE_GUARD
