1. [The Language](#the-language)
2. [Building](#building)


# The Language

This project currently goes under the Working name `eval`.

The semantics and even syntax are by no means finalized yet.


## Goals

The goals of `eval` are as follows:

1. Allow fine-grained memory control.
2. Allow anything that can happen at run-time to happen at compile-time.
3. Make initialization easy and reliable.
4. Provide genericity by making types and even definitions first-class citizens.
4. Make side-effects visible and contained.

The syntax is inspired by a mixture of Rust, Haskell, Javascript, Odin, and a
hodgepodge of other stuff. The compile general compile-time support is a
logical next step from that already found in languages such as C++
(`constexpr`) or Zig (`comptime`).


## A minimal program

A minimal program consists of a main procedure such as the following:

```
let main = proc(args: [][]u8) -> void => {
	std.print("Hello World!\n")
}
```

We can see here the general structure of a definition in `eval`: The keyword
`let`, followed by a name, an optional type prefixed by `:` (absent in the
above case), and a value prefixed by `=` - which is again optional. \
In the above case, the value of main is a `proc` - procedure - taking as its
single argument a slice of slices of u8s, which is accessible as `args` in the
procedure's body. Its return type is void. \

We can also already see that there are no semicolons or statement terminators
of any kind. Instead the syntax is designed to make it unambiguous where
expressions end, making explicit terminators redundant.


## Everything is an Expression

We have chosen to make the body of `main` a block, introduced by `{` and
terminated by `}`, as should be familiar from most C-family languages. Since
this block only contains a single "statement", we can omit it like so:

```
let main = proc(args: [][]u8) -> void => std.print("Hello World!\n")
```

Generally, most language constructs can be used in most places, since they are
generally expressions and not statements in the traditional sense. \
`if` can thus also be used like the ternary `?:` operator in many other
languages:

```
let x = if condition() then 1 else 2
```


## Mutability

Variables declared using `let` are by default immutable. This means that their
value cannot be altered after they are defined. \
To create a mutable variable the keyword `mut` is used instead of `let`:

```
let i = 5

i = 6 // compiler error

mut j = 7

j = 8 // OK
```

Immutability is always "deep", meaning that it is transitively applied to
members of a variable.

Along with this concept there is also that of the distinction between functions
(`func`) and procedures (`proc`). This is similar to that found in Nim, in that
`func`s may not access mutable global data, while `proc`s may. \
Interestingly, a pure function is then a `func` which only takes
value-arguments or immutable reference-arguments (i.e., pointers, slices, or
types containing them as members).


## Control Flow

There are only a few types of control flow constructs:

- Loops, introduced by the keyword `for` (there is no `while`; `for` does
  double-duty)
- Conditionals, introduced by the keyword `if`
- Return, which comes in two variants: `return value`, as well as `leave`. This
  is sadly necessary to avoid ambiguity in case of void returns due to the lack
  of an expression terminator.
- Break, which, again, comes in two variants: The familiar `break`, simply
  exiting the surrounding loop, and `yield value`, which exits with the given
  value. The rationale for having both is the same as with `return` and
  `leave`.

Each of these constructs is examined in more detail in the following sections.

### Loops

All types of loops are introduced by the keyword `for`. To write a traditional
`while` loop, the following syntax is used:

```
for condition do {
	// repeated until condition is false
}
```

The `do` in this case is optional, and mainly intended to make loops that have
non-block bodies more legible.

Additionally a step can be specified (equivalent to to third element in C's
`for` loops):

```
for i < 10, i += 1 {
	// Assuming i is initially 0, this will repeat 10 times
}
```

Variables can also be defined inside the loop header using the `where` keyword
inspired by Haskell:

```
for i < 10, i += 1 where mut i: u32 = 0 {
	// This will repeat 10 times, with i ranging from 0 to 9
}
```

The same can also be used with `if` and even function definitions.

Loops may yield a value using the `yield` and `finally` keywords:

```
let five = for i < 10, i += 1 where mut i: u32 = 0 {
	if i == 5 then
		yield i
} finally undefined
```

As soon as a `yield` is reached, the loop terminates and evaluates to the
yielded value (in this contrived case always 5).

If no `yield` is reached before the loop's condition becomes false, the value
provided by the finally clause is used instead. In the above example, the
keyword `undefined` means that we assure the compiler that this statement may
never be reached.

Infinite loops can be expressed by omitting the loop condition like so:

```
for do {
	// Round and round and round and ...
}
```

Lastly, ranged loops can be expressed as well, using the `<-` element-of
"operator":

```
let is: []u32 = some_slice()

for i <- is {
	// Do something with each i
}

// Or more succinctly using where

for i in is where is: []u32 = some_slice() {
	// Do something with each i
}
```

The iterated value must be of a type that implements the `Container` trait.
A slice of Ts for example has

```
impl Container([]T, T) = {
	// Compiler magic I guess
}
```


### Conditionals

Conditionals are introduced using the keyword `if` and subsume the role of
traditional if-statements as well as the ternary operator `?:`:

```
if condition then {
	consequent
} else {
	alternative
}
```

Just like the `do` for loops, the `then` is optional. `else` may also be
omitted, in which case the if's type must however be void:

```
if condition {
	std.print("condition was true\n")
}
```


### Switch

The final control-flow construct is the `switch`. It allows succinctly
executing code based on the value of a switched-over expression, working mostly
like that familiar from Java or C.

```
switch value
case a => got_a()
case b => got_b()
case c => got_c()
case _ => got_other()
```

Unlike C or C++, fallthrough between cases is not supported.

Instead of using `default` to indicate to code to execute in case no other
cases match, `case _` is used.

Just like `if` and `for`, `switch` is also an expression, meaning that it can
have a result, and also supports a `where` clause:

```
let x = switch y where y = some_value()
	case a => 1
	case b => 2
	case c => 3
	case _ => undefined
```


## Types

Types are first-class citizens, meaning that - at least during compile-time -
they can be stored in variables, inspected, and reassigned.

The basic built-in types are as follows:

- Unsinged integers - `u8`, `u16`, `u32` and `u64`. It is planned to eventually
  extend this to `u<any-bit-width>`.
- Signed integers - `s8`, `s16`, `s32` and `s64`. Just like for unsigneds,
  these shall eventually be arbitrary-width.
- IEEE 754 single- and double-precision floating point numbers - `f32` and
  `f64`.
- boolean truth values - `bool`
- The unit type - `void`
- slices, or counted pointers - `[]<element-type>`
- Pointers - `*<pointed-type>`, or `?<pointed-type>` to allow `null`. These do
  not support pointer arithmetic.
- Multi-pointers - `[*]<pointed-type>` or `[?]<pointed-type>`. These are
  equivalent to normal pointers but support pointer arithmetic and indexing
  like slices.
- Arrays - `[<count>]<element-type>`
- Tail-arrays - `[...]<element-type>` to support arbitrary-length aligned
  access to elements after a header (like a zero-length array in common C/C++
  extensions).
- Varargs - `...<arg-type>` to support variadic functions.
- Compile-time integers - `CompInteger`. This is the type assigned to integer
  literals. It is also a first-class citizen of the language, meaning e.g.
  `1 + 2` still results in a value of type `CompInteger`. \
  Unlike other types, this one supports implicit conversion to integer types.
- Compile-time floats - `CompFloat`, following the same semantics as
  `CompInteger` but for floating-point literals.
- Compile-time strings - `CompString`, again being the type assigned to string
  literals. These can be implicitly converted to `[<strlen>]u8`.
- The Type-Type - `Type` - to store other types
- The Definition-Type - `Definition`, holding a name, type, default value, and
  other tidbits such as mutability information.
- Composite and array initializer types - `CompComposite` and `CompArray`
  respectively. These act similar to the other `Comp*` types, but for composite
  and array initializers (discussed further under
  [Initialization](#initialization)).

Note that there is no dedicated character type. This is meant to drive home the
point that there is no such thing as a simple "character", but rather only
concepts such as code-units and -points in modern encodings (i.e., Unicode).
And even their usefulness is debatable. \
Instead, treating text as a slice of unsigned integers of an appropriate
bit-width is encouraged.

All types that have elements apart from arrays also support a `mut` between the
type introducer and the element type - as an example, `*mut u32` in case of a
pointer. \
This decouples the mutability of these reference types from that of their
referenced data:

```
let p0: *u32 = something() // Immutable pointer to immutable data
let p1: *mut u32 = something() // Immutable pointer to mutable data
mut p2: *u32 = something() // Mutable pointer to immutable data
mut p3: *mut u32 = something() // Mutable pointer to mutable data
```


### Composites

Composite types - `struct`, `union`, and anything in between - are created
using type builders, which expose three functions:

```
let create_type_builder = func() -> TypeBuilder // Creates a fresh type builder
let add_type_member = func(mut tb: TypeBuilder, definition: Definition, offset: ?s64) -> void // Adds a member to the type being built
let complete_type = func(tb: TypeBuilder, size: u64, align: u64, stride: u64) -> Type // Complete the type being built.
```

These are intended to be abstracted behind functional interfaces. To implement
the C `struct` keyword, the following function would suffice (admittedly using
some not-yet-introduced builtins to introspect definitions):

```
let CStruct = func(members: ...Definition) -> Type => {

	mut tb = create_type_builder()

	mut offset: s64 = 0

	mut max_align: u64 = 1

	for member <- members
	{
		if is_global(member) then
		{
			add_type_member(tb, member)
		}
		else
		{
			let align = alignof(type(member))

			if align > max_align then
				max_align = align

			offset = next_multiple(.of = offset, .factor = align)

			add_type_member(tb, member, .offset = &offset)

			offset += sizeof(type(member))
		}
	}

	let size = next_multiple(.of = offset, .factor = max_align)

	// The last expression in a block is also its value, meaning it is implicitly `return`ed
	complete_type(tb, .size = size /* C-structs are padded to their alignment */, .align = max_align, .stride = size)
}
```

This can now be used to create e.g. the same layout as an arbitrary `struct`:

```
struct MyStruct
{
	int8_t x = 4;

	static uint32_t* const glob; 

	int32_t y = 2;
};
```

becomes

```
let MyStruct = CStruct
(
	mut x: s8 = 4,
	global glob: *u32,
	mut y: s8 = 2, // Note that trailing commas are allowed. That is a good thing™
)
```

`union` can equally be derived, by adjusting member offsets and the resulting
type's total size.


### Type Equivalence

`Eval` uses a system somewhere in-between
[nominal](https://en.wikipedia.org/wiki/Nominal_type_system) and
[structural](https://en.wikipedia.org/wiki/Structural_type_system) type
equivalence that can be most easily understood by how it is implemented:

- Two references to the same primitive type are considered equivalent.
- Two types created by different calls to `complete_type` are considered
  distinct.
- Calls to all other functions that return types are effectively cached,
  meaning that the same type-valued function called twice with the same
  arguments returns the same type, since the same underlying call to
  `complete_type` gets reused.
- The results of calls to type-valued functions whose return type is marked
  with the `distinct` operator are considered distinct as when bound to an
  identifier. \
  However, they are still considered equivalent to the same call *not* bound to
  an identifier.

Some examples to illustrate this:

```
let T1 = func(def: Definition) -> Type => {

	mut tb = create_type_builder()

	add_type_member(tb, def, .offset = 0)

	complete_type(tb, .size = sizeof(def), .align = alignof(def), .stride = strideof(def))
}

// Word-for-word copy of T1
let T2 = func(def: Definition) -> Type => {

	mut tb = create_type_builder()

	add_type_member(tb, def, .offset = 0)

	complete_type(tb, .size = sizeof(def), .align = alignof(def), .stride = strideof(def))
}

let T1a = T1(x: u32)

let T1b = T1(x: u32)

let T1c = T1(y: u32)

let T2a = T2(x: u32)

assert(T1a == T1b) // Equivalent due to same arguments

assert(T1a != T1c) // Distict due to differing arguments

assert(T1a != T2a) // Distinct due to different underlying funcs

let D = func(def: Definition) -> Type = distinct T1(def)

assert(D(x: u32) == D(x: u32)) // Equivalent, since not bound to identifier

let Da = D(x: u32)

let Da2 = Da

let Db = D(x: u32)

assert(Da == D(x: u32)) // Equivalent, since one side is not yet bound to an identifier

assert(Da == Da2) // Equivalent, since both result from the same call site

assert(Da != Db) // Distinct, since both are bound to an identifier
```

This ensures that there are no accidental type equivalences due to equivalent
structures; It also allows type functions to be used directly and still be
considered equivalent, essentially allowing the same usage as C++ templates.

To elaborate a bit on the `distinct` operator (which is currently not yet
implemented or even syntactically supported): It cannot only be used on
function return values, but more generally on types, working similarly to
Haskell's [`newtype`](https://wiki.haskell.org/index.php?title=Newtype):

```
let MyU32 = distinct u32

let x: MyU32 = 1

let y: MyU32 = 2

let a: u32 = 3

let _ = x + y // OK

let _ = x + a // compile-time error due to incompatible types.
```


## Initialization

Initialization is supported by two constructs: Named arguments and compound
literals.

Named arguments have already been used in the [section on types](#types). They
take the form of a `.` followed by an identifier, an `=` and then the desired
value:

```
let my_func = double(value: u32) -> u32 = 2 * value

let four_o_four = my_func(.value = 202)
```

Named arguments can occur in any order in the call and be mixed with non-named
arguments. The semantics of this are similar to those of C-style `enum`s, with
non-named arguments following a named one being assigned to the following
parameters of the called function.

Additionally, there are compund literals, divided into two types, Array and
composite.

Array literals are started by `.[`, and ended by a matching `]`. The following
for example defined `my_array` as a four-element array holding the integer
values from 1 to 4.

```
let my_array: [_]u8 = .[1, 2, 3, 4]
```

Positional initialization is not yet supported, but definitely planned for the
future.

Composite literals in the meantime are similar to C99's or Zig's designated
initializers and enclosed in `.{` and a matching `}`. Each value can either be
positional or named, using the same syntax as named function arguments:

```
let Vec2 = Struct(x: f32, y: f32)

let my_vec: Vec2 = .{ .x = 0.5, .y = 0.8 }

let my_other_vec: Vec2 = .{ 1.0, 71.3 }
```


## Genericity

This is currently still up for consideration. I believe it should be based on
`trait`s, similar to those found in Rust or Haskell's `class`es. Traits in this
concept express a relation over types. \
A good example to consider seems to be that of iteration, noting that the
following is just a mess of ideas that are not even internally consistent at
this point:

```
// This would be a built-in trait supporting the `<-` element-of syntax in loops
let Container = trait(Cont: Type, Iter: Type, Elem: Type) =
{
	let iterator = func(cont: *Cont) -> Iter

	let has_next = func(iter: *Iter) -> bool

	let next = func(iter: *mut Iter) requires has_next(iter) -> *Elem
}

impl Container(.Cont = []u8, .Iter = []u8 /* Use a slice as an iterator for itself by advancing its beginning */, .Elem = u8) =
{
	let iterator = func(cont: *[]u8) -> []u8 = cont.*

	let has_next = func(iter: *[]u8) -> bool = count(iter.*) != 0

	let next = func(iter: *mut []u8) -> *u8 =
	{
		let head = &iter.*[0]

		iter.* = iter.*[1..]

		head
	}
}

// We can now use the `<-` element-of syntax on []u8, as we have defined an impl for it.

let iterating
for i <- my_slice where my_slice: []u8 = some_slice()
{
	// Do something with each i
}
```


## Contracts

Functional contracts, supported in the form of pre- and postconditions, are
supported by the keywords `requires` and `ensures` respectively (both initially
inspired by Midori). \
These can follow a function type and define the permissible values of inputs
and global state, or the possible resulting return value and global state
repsectively. The current intention is to runtime-check them in debug mode,
while using them for optimizations in release mode. During compile-time
evaluation they should probably always be checked.


# Building

## Prerequisites

To build this project, you will need

- A `cmake` installation of at least version 3.22
- One of the [supported C++ compilers](#supported-compilers)


## Building For Your Platform

Once `cmake` is installed configure your build system as follows:

- For msvc: `cmake -S . -B build/msvc -DCMAKE_CXX_COMPILER=msvc -DCMAKE_C_COMPILER=msvc`
- For gcc: `cmake -S . -B build/gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc`
- For clang: `cmake -S . -B build/clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang`

Note that the exact build directories can of course be set to anything of your
liking, but the given values correspond to those used by `build-all.ps1`,
making it possible to reuse the same build configuration when you decide to use
this wrapper.

Once the build system is configured run cmake again to actually perform your build:

```
cmake --build <build-directory-from-before>
```

This will create a binary compiled in debug mode somewhere inside your build
directory (the exact location depends on your compiler). \
To run the resulting compiler on the source.evl file found in the sample
directory, use

```
<build-directory-from-before>/<path-to-exe> -config sample/config.toml
```


## Running Tests

The project's test suite can be run as part of the cmake build by specifying
the `run-tests` target. By default this is not enabled to speed up quick
rebuilds, even though the test suite clocks in at under one second in total. 


## Multi-Compiler and -Platform Build Support

If you want to build the project on all available supported compilers and
platforms, you can use `build-all.ps1`. This is a wrapper around cmake which
invokes all the available [supported compilers](#supported-compilers) on your
system. \
If you test a new build configuration, please add it to the script, as well as
the [list of supported compilers](#supported-compilers).


## Supported Compilers

The following is a list of the compilers and platforms under which builds are known to succeed.

| Platform   | Compiler |       Versions      |
|------------|----------|---------------------|
| Windows 10 | msvc     | 19.33, 19.39, 19.44 |
| Ubuntu     | clang    | 18.1                |
| Ubuntu     | gcc      | 13.3                |
