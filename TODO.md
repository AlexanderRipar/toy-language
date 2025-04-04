# Issues

## Maintenance / Get To Know the Codebase

- Add a field of type `TypeId` to to `AstNode` (defined in pass_data.hpp).
  This likely requires reworking at least test/ast_tests.cpp to adjust for the
  relevant offsets. If there are any other functions that explicitly rely on
  the current size of an `AstNode`, make them as flexible as viable to future
  changes.
- Remove usage of `TypePool`, replacing it with `TypePool2`. Once this is done,
  rename all references to `TypePool2` (including the filenames of
  type_pool2.cpp and test/type_pool2_tests.cpp) by removing the `2`.
- Make test/CMakeLists.txt not duplicate all the dependencies declared in root
  CMakeLists.txt. This should be possible by using a variable which is them
  expanded in the `add_executable` call in both files.
- Find a way to represent a source location as a 32-bit id for use in `AstNode`.
  One possible approach to this could be to make these file-specific, storing
  the filename only in `AstTag::File` nodes, and then a byte offset into the
  file in its child nodes.
- Split infra/container.hpp into infra/reserved_vec.hpp and
  infra/index_map.hpp, containing the respective `struct`s. This will also
  require adjusting all `#include`s that reference infra/container.hpp.
- Remove remaining namespace uses excluding `minos` (e.g., `namespace thd` in
  infra/threading.hpp).


## Tests

- minos_tests.cpp:
  - Implement tests marked as TODO above `void minos_tests()`.

- Other:
  - Add tests for infra/threading.hpp, including `thd::Mutex`, `thd::Semaphore`
    and `thd::IndexStackListHeader`.
  - Add tests for infra/container.hpp, including `ReservedVec` and `IndexMap`.
    These should definitely include all relevant growth operations for the
	respective containers.
  - Add tests for read.cpp
  - Add tests for parse.cpp
  - Add tests for identifier_pool.cpp
  - Add tests for config.cpp


## Planning

- Create a concrete plan of how to implement the interplay between interpreter
  and typechecker.
- Discuss potential of unified memory pool with the data for all handles
  currently defined in pass_data.hpp stored at the beginning. The remainder of
  the pool can then be uniformly indexed (either as 4- or 8-byte units).
  Maybe this also opens up additional opportunities for simplification.
