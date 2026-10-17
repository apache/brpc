# Migrating butil ownership transfers to C++14

The C++03 move emulation in `butil/move.h` has been replaced by native
rvalue references and `std::move` from `<utility>`. The header and
`MOVE_ONLY_TYPE_FOR_CPP_03` macro have been removed.

For an existing butil resource owner, transfer ownership with `std::move`:

```cpp
#include <utility>
#include "butil/memory/scoped_ptr.h"

scoped_ptr<int> source(new int(42));
scoped_ptr<int> destination(std::move(source));
```

Return a local owner directly to allow copy elision or an implicit move.
`Pass()` and `scoped_ptr::PassAs<T>()` have been removed. Replace ownership
transfers with `std::move(owner)`, or return the local owner directly, including
when converting a `scoped_ptr<Derived>` to `scoped_ptr<Base>`.

A discarded `owner.Pass()` used to release the resource through a temporary
owner. Replace such calls with the appropriate explicit cleanup operation
(`reset()`, `clear()`, or `Close()`). Discarding `std::move(owner)` does not
release anything.

These types remain noncopyable. `ScopedGeneric` and `ScopedFILE` retain
their existing support for move construction only. `scoped_ptr`,
`ScopedVector`, and `File` also support move assignment. Custom deleters
and stateful traits retain their existing copying behavior.

Downstream types using the removed macro must declare deleted copy
operations and implement native move operations that transfer ownership
and invalidate the source. Merely defaulting a move constructor for a raw
resource handle does not prevent both objects from releasing that handle.

The exported `File` move constructor and move assignment signatures now
take `File&&`. Rebuild downstream binaries when updating the brpc shared
library; the old C++03-emulation symbols are no longer provided.
