#ifndef TOML_INCLUDE_GUARD
#define TOML_INCLUDE_GUARD

#include "../tree_schema.hpp"

// Parses the file at `filepath` into a `Config` struct. The file is expected
// to be in [TOML](https://toml.io) format.
bool parse_toml(Range<char8> filepath, const TreeSchemaNode* schema, MutRange<byte> inout_parsed, MutRange<byte>* out_allocation) noexcept;

// Releases resources associated with a `Config` that was previously returned
// from `parse_toml`. Accepts the result of that function's
// `out_allocation` output-parameter.
void release_toml(MutRange<byte> allocation) noexcept;

#endif // TOML_INCLUDE_GUARD
