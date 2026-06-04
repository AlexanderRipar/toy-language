#ifndef TOML_INCLUDE_GUARD
#define TOML_INCLUDE_GUARD

#include "../types.hpp"
#include "../range.hpp"
#include "../tree_schema/tree_schema.hpp"
#include "../print/print.hpp"

// Parses the data in `blob` into a `TreeSchema` tree. The data is
// expected to be in [TOML](https://toml.io) format.
// Note that the supplied `filepath` is used solely for error reporting,
// and not any filesystem operations.
// `error_sink` receives any errors that are encountered during parsing.
// `inout_alloc` must be a `TreeSchemaAllocator` previously initialized by
// `ts_allocator_create` and is used to allocate the parsed `TreeSchema`
// objects.
Maybe<TreeSchemaTable*> parse_toml_blob(Range<char8> blob, Range<char8> filepath, PrintSink error_sink, TreeSchemaAllocator* inout_alloc) noexcept;

// Reads the file identified by `filepath` and parses its contents into a
// `TreeSchema` tree. The data is expected to be in [TOML](https://toml.io)
// format.
// `error_sink` receives any errors that are encountered during file i/o or
// parsing.
// `inout_alloc` must be a `TreeSchemaAllocator` previously initialized by
// `ts_allocator_create` and is used to allocate the parsed `TreeSchema`
// objects.
Maybe<TreeSchemaTable*> parse_toml_file(Range<char8> filepath, PrintSink error_sink, TreeSchemaAllocator* inout_alloc) noexcept;

#endif // TOML_INCLUDE_GUARD
