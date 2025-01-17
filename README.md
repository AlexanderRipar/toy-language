# Code Structure

## General odditities

Since I keep flip-flopping between styles, here's an arbitrary set of rules
intended to avoid useless refactorings.

- Member functions are only used in container templates.
- Constructors are never used.
- Namespaces are only used for module's public interfaces.

## What a Module looks like

Every module is designed as follows:

- One `.hpp` file containing the module's public definitions. This contains the
  declarations of all public functions as well as public type definitions.
  This is wrapped in a namespace indicating the module.
- Zero or more `.cpp` files containing the  definitions of public functions as
  well as internal definitions.

Data structures that cross module boundaries are declared in `schema.hpp`, with
their related functions defined in `schema/<structure_name.cpp`. Schema
consists of multiple namespaces, indicating grouping of data structures.
