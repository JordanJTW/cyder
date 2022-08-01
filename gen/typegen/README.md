# typegen

typegen is a compiler for a DSL to describe structs and types which are used to interoperate between cyder and the emulated 68k and generate C++ code to read/write structures out of memory with automatic BIG/little-endian conversions and pretty printers.

typegen is only designed to describe memory layouts _not_ control flow. Think of it more like the Protobuf compiler than a C++ compiler.

This is an experiment and may not pan out but could potentially simplify a lot of the code in cyder which currently is hand written to grab each field individually.

## Syntax

typegen has a Rust like syntax with two top-level declarations `type`s and `struct`s.

The only primitive types are numbers:
  - `u8` (unsigned byte)
  - `i16` (signed 16-bit number)
  - `u16` (unsigned 16-bit number)
  - `i32` (signed 32-bit number)
  - `u32` (unsigned 32-bit number)

`type` allows any type to be given an alias and `struct` describes a type with the specified members layout sequentially in memory.

## Example

A typegen definition file would look something like this with `type` and `struct` definitions:

```rust
// Type declaration: Interger is a u16
type Interger: u16;

// Struct declaration: Point is a struct with two Integer members
struct Point {
  x: Interger;
  y: Interger;
}
```

And would generate something like:

```cpp
// Generated header "types.h"

#include <cstdint>
#include <ostream>

#include "core/memory_region.h"

// Type declarations become using statements
using Interger = uint16_t;

// C++ struct matching the struct from the type definition file
struct Point {
  // Pointer offsets to each member of the struct
  enum Offset {
    x = 0,
    y = 2,
  };
  // Members
  Interger x;
  Interger y;
};

// Reads a `Point` from `core::MemoryRegion` at `Ptr`  
template <>
absl::StatusOr<Point> ReadType(const core::MemoryRegion&, Ptr);

// Pretty-prints `Point`
std::ostream& operator<<(std::ostream&, const Point&);
```

```cpp
// Generated source "types.cc"

#include "types.h"

#include "core/endian_helpers.h"
#include "core/status_helpers.h"

template <>
absl::StatusOr<Point> ReadType(const core::MemoryRegion& region, Ptr ptr) {
  Point obj;
  obj.x = betoh<Integer>(TRY(region.Copy<Integer>(ptr + Point::Offset::x)));
  obj.y = betoh<Integer>(TRY(region.Copy<Integer>(ptr + Point::Offset::y)));
  return obj;
}

std::ostream& operator<<(std::ostream& os, const Point& obj) {
  return os<< "x:" << obj.x<< "y:" << obj.y;
}

```