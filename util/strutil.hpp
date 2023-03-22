#ifndef UTIL_STRUTIL_INCLUDE_GUARD
#define UTIL_STRUTIL_INCLUDE_GUARD

static constexpr bool is_alpha(char c) noexcept
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static constexpr bool is_whitespace(char c) noexcept
{
	return c == '\f' || c == '\r' || c == '\n' || c == '\t' || c == ' ';
}

static constexpr char ascii_lower(char c) noexcept
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 'a';

	return c;
}

#endif // UTIL_STRUTIL_INCLUDE_GUARD
