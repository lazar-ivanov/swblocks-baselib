# Implementing zlib, Brotli, and Zstd as Private Header-Only Dependencies in `swblocks-baselib`

## Original Requirement

`swblocks-baselib` is a **header-only C++ library**.

New functionality being added to `swblocks-baselib` needs compression/decompression support from:

- zlib
- Brotli
- Zstd

These upstream libraries are written primarily in C and are normally compiled into architecture-specific static or shared libraries.

The goal is **not** to expose a provider seam or push dependency management onto the final application.

The desired model is:

> `swblocks-baselib` should completely abstract zlib, Brotli, and Zstd and include them as private implementation details at source level, while remaining header-only from the consumer's point of view.

Specifically:

- Consumers should not have to build zlib, Brotli, or Zstd separately.
- Consumers should not have to install these libraries.
- Consumers should not have to link `-lz`, `-lzstd`, Brotli libraries, DLLs, `.so` files, `.dylib` files, or static archives.
- `swblocks-baselib` should not distribute architecture-specific binaries.
- `swblocks-baselib` should remain source/header-only.
- The same source distribution should work across architectures such as x86-64 and ARM64.
- zlib/Brotli/Zstd should remain private implementation details.
- The final public API should expose only a `swblocks-baselib` compression abstraction.

The desired consumer experience should be close to:

```cpp
#include <swblocks/baselib/Compression.h>

auto compressed =
    bl::Compression::compress(
        bl::CompressionType::Zstd,
        data
    );
```

with no explicit third-party compression-library setup.

---

# Core Conclusion

Yes, this is possible.

The key distinction is that "nobody has to build the libraries" cannot literally mean that no machine code is ever generated.

The C/C++ implementation must eventually be compiled somewhere.

The workable interpretation is:

> zlib, Brotli, and Zstd can be transformed into source-only, header-compatible private implementations that are automatically compiled as part of the normal C++ compilation of code that uses `swblocks-baselib`.

From the consumer's perspective there would be:

- no standalone dependency build,
- no binary distribution,
- no dependency installation,
- no external package discovery,
- no separate link step for the compression libraries.

The compiler simply compiles the embedded implementation as part of normal translation units.

---

# Recommended Architecture

The preferred architecture is to create **generated private header-only versions of the upstream C libraries**.

Do not distribute this:

```text
swblocks-baselib
    |
    +---- libz.a
    +---- libbrotli.a
    +---- libzstd.a
```

Do not require clients to build this:

```text
zlib/*.c
brotli/*.c
zstd/*.c
```

Instead, maintain a dependency-vendorization pipeline inside `swblocks-baselib`:

```text
upstream C source
      |
      | swblocks-baselib vendorization/generation tools
      v
generated C++ header-only implementation
      |
      v
swblocks-baselib/include/swblocks/baselib/detail/compression/...
```

The published/installable `swblocks-baselib` repository can then contain only source/header artifacts such as:

```text
include/
    swblocks/
        baselib/
            Compression.h

            detail/
                compression/
                    zlib_1_3_x.hpp
                    brotli_1_x.hpp
                    zstd_1_5_x.hpp
```

There should be:

- no `.a`
- no `.lib`
- no `.so`
- no `.dylib`
- no `.dll`
- no architecture-specific artifacts
- no `find_package()`
- no `FetchContent()` required by the consumer
- no system dependency requirement

---

# Why Simply Including `.c` Files Is Not Enough

A tempting implementation would be:

```cpp
namespace swblocks::detail
{
#include "zstd.c"
}
```

This is not sufficient by itself.

There are several major problems.

## 1. Duplicate Symbols Across Translation Units

If ten `.cpp` files include a public header that transitively contains implementation code defining symbols such as:

```text
ZSTD_compress
ZSTD_decompress
inflate
deflate
BrotliEncoderCompress
```

the linker may see multiple definitions of those symbols.

Normal C library source assumes each implementation file is compiled once into an object or library.

Header-only C++ code may be instantiated in many translation units.

The implementation must therefore be made **ODR-safe**.

## 2. C Source Included in C++ Is Compiled as C++

If a `.c` file is textually included in a C++ source/header, it is compiled by the C++ compiler as C++.

C and C++ are similar but not identical.

Potential problems include:

- implicit conversions that are legal in C but not C++,
- reserved identifiers or keywords,
- C-only constructs,
- differences in initialization rules,
- pointer conversion rules,
- declarations/definitions that assume C linkage,
- compiler-extension assumptions.

Therefore, source inclusion must be validated and possibly patched for C++ compilation.

## 3. File-Scope State and ODR Semantics

The upstream implementation may contain:

```c
static void helper(...);

static const uint32_t table[] = { ... };

static int initialized;

void function(...)
{
    static int once;
}
```

These different forms of `static` do not all mean the same thing and cannot safely be transformed with a trivial macro.

The final generated form must deliberately handle:

- file-scope functions,
- file-scope constant tables,
- writable file-scope state,
- function-local static state,
- externally visible API functions.

---

# Preferred Transformation Model

The goal is to transform an upstream C implementation into an ODR-safe C++ implementation.

For example, upstream code conceptually like:

```c
static int foo(...)
{
    ...
}

static const uint32_t table[] =
{
    ...
};

size_t ZSTD_decompress(...)
{
    return foo(...);
}
```

could become something like:

```cpp
#pragma once

namespace swblocks::baselib::detail::zstd_1_5_x
{

inline constexpr std::uint32_t table[] =
{
    ...
};

inline int foo(...)
{
    ...
}

inline std::size_t decompress(...)
{
    return foo(...);
}

}
```

The broad transformation rules are:

```text
C static/global functions
        ↓
C++ inline functions

C static/global constants
        ↓
inline constexpr / inline const variables

C writable globals
        ↓
C++17 inline variables, where semantically safe

C external public symbols
        ↓
private C++ namespace + inline functions
```

The intent is to rely on normal C++17 ODR behavior.

Every translation unit may see an inline definition, and the linker can coalesce equivalent definitions.

---

# Do Not Preserve the Upstream C ABI Unless Needed

Because these libraries become private implementation details of `swblocks-baselib`, there is no need to preserve the upstream public C symbol names.

There is no requirement for final binaries to expose symbols such as:

```text
inflate
deflate
ZSTD_compress
ZSTD_decompress
BrotliEncoderCompress
BrotliDecoderDecompress
```

Instead, all implementation code should live in private, versioned namespaces such as:

```cpp
namespace swblocks::baselib::detail
{

namespace zstd_1_5_7
{
    ...
}

namespace brotli_1_2_0
{
    ...
}

namespace zlib_1_3_x
{
    ...
}

}
```

Then the public `swblocks-baselib` API can expose something clean and stable:

```cpp
namespace swblocks::baselib
{

class Compression
{
public:

    static Blob compress(
        CompressionType type,
        std::span<const std::byte> input
    );

    static Blob decompress(
        CompressionType type,
        std::span<const std::byte> input
    );
};

}
```

Internally:

```cpp
switch(type)
{
    case CompressionType::Zstd:
        return detail::zstd_1_5_7::compress(...);

    case CompressionType::Brotli:
        return detail::brotli_1_2_0::compress(...);

    case CompressionType::Deflate:
        return detail::zlib_1_3_x::compress(...);
}
```

Consumers should not need to know that zlib, Brotli, or Zstd exist underneath.

---

# Zstd: Best Proof-of-Concept Candidate

Zstd is a good first dependency to prototype because upstream already supports a **single-file / single-compilation-unit integration model**.

The recommended pipeline is conceptually:

```text
official Zstd source
       ↓
official single-file/amalgamation tooling
       ↓
single combined Zstd implementation
       ↓
swblocks-baselib C-to-C++ / header transformation
       ↓
generated zstd header-only implementation
```

A generated file could become:

```text
include/swblocks/baselib/detail/compression/zstd_1_5_x.hpp
```

Zstd is therefore the best first target for validating the whole approach.

If decompression-only support is ever sufficient for a particular feature, upstream Zstd also has a single-file decompressor model that may further reduce size.

---

# zlib: Also a Good Candidate

zlib is relatively small and conventional C.

Its implementation is split across files such as:

```text
adler32.c
compress.c
crc32.c
deflate.c
inflate.c
inffast.c
inftrees.c
trees.c
uncompr.c
zutil.c
```

and optionally the `gz*` file-I/O layer.

If `swblocks-baselib` only needs in-memory compression/decompression, it may be unnecessary to vendor all gzip file APIs such as:

```text
gzopen
gzread
gzwrite
```

A first implementation should prefer correctness and updateability over aggressive stripping, but unnecessary subsystems may later be excluded.

zlib has historically been friendly to C/C++ portability, making it a reasonable second target after Zstd.

---

# Brotli: Expected to Require More Work

Brotli is likely the most involved dependency.

Its implementation is spread across many files under areas like:

```text
c/common/
c/dec/
c/enc/
```

Unlike Zstd, Brotli does not normally center its integration model around one blessed single-file implementation.

Therefore, `swblocks-baselib` should likely maintain a deterministic amalgamation step.

Conceptually:

```text
Brotli upstream source tree

        ↓

deterministic ordered source list

        ↓

generated Brotli amalgamation

        ↓

C-to-C++ / namespace / ODR transformation

        ↓

brotli-header-only.hpp
```

The generator should contain a canonical ordered source-file list, for example:

```python
sources = [
    "c/common/constants.c",
    "c/common/context.c",
    "c/common/dictionary.c",
    # ...
    "c/dec/decode.c",
    # ...
    "c/enc/encode.c",
    # ...
]
```

The exact list should be derived from and validated against the upstream Brotli build/package configuration.

---

# Two Possible Headerization Strategies

There are two broad implementation strategies.

## Strategy A: Proper C++ `inline` Headerization

This is the preferred long-term solution.

Example:

```cpp
namespace swblocks::baselib::detail::embedded_zstd
{

inline int helper(...)
{
    ...
}

inline constexpr unsigned table[] =
{
    ...
};

inline std::size_t decompress(...)
{
    ...
}

}
```

Advantages:

- no duplicate-symbol errors,
- normal C++ ODR semantics,
- usually only one logical implementation in the final binary,
- no platform-specific linker assumptions,
- clean namespace isolation,
- natural fit for a true header-only library.

This should be the target architecture.

---

## Strategy B: Per-Translation-Unit Private Copies

A simpler proof-of-concept is to force everything to internal linkage:

```cpp
static size_t zstd_decompress(...)
{
    ...
}
```

Then each translation unit gets its own private implementation.

This is easy to make link correctly because no implementation symbol is externally visible.

However, potential costs include:

```text
compile-time bloat
object-file bloat
executable-size bloat
duplicate lookup tables
duplicate machine code
duplicate writable state
```

Some linkers may perform identical-code folding, but the design should not depend on that.

This strategy may be useful for the first technical spike, but the preferred production implementation is the proper C++ inline/ODR-safe transformation.

---

# Do Not Use a Naive `#define static inline` Trick

A textual macro such as:

```cpp
#define static inline
#include "zstd.c"
```

is not robust.

Consider:

```c
static void foo();

static const int table[] = { ... };

static int initialized;

void foo()
{
    static int once;
}
```

The `static` keyword has different semantics in each case.

A correct transformation must distinguish at least:

1. file-scope function with internal linkage,
2. file-scope immutable table,
3. file-scope writable global,
4. function-local static storage.

Function-local statics, for example, often should remain function-local statics.

Therefore, use one of:

- a source-aware transformation tool,
- a lightweight parser,
- an AST-based transformer,
- a deterministic explicit patch set against pinned upstream versions,
- a combination of amalgamation + carefully maintained patches.

For a small set of pinned third-party releases, a deterministic patch-based approach may be easier to audit than a generalized C-to-C++ translator.

---

# Recommended Repository Layout

A practical repository structure would be:

```text
swblocks-baselib/

    include/
        swblocks/
            baselib/
                Compression.h

                detail/
                    compression/
                        zlib.hpp
                        brotli.hpp
                        zstd.hpp

    dependencies/
        compression/
            versions.json

            generate-zlib.py
            generate-brotli.py
            generate-zstd.py

            patches/
                zlib/
                brotli/
                zstd/

    tests/
        compression/
```

The installable/public headers should contain the generated implementation.

The dependency-generation tooling exists only for maintainers and dependency upgrades.

---

# Version Pinning

Maintain explicit pinned versions/commits, for example:

```json
{
    "zlib": "<version-or-commit>",
    "brotli": "<version-or-commit>",
    "zstd": "<version-or-commit>"
}
```

The generation process should record:

- upstream version,
- upstream commit hash where applicable,
- expected source checksum,
- generation-tool version,
- local patch-set version.

This makes the generated source reproducible and auditable.

---

# Generated File Metadata

Every generated implementation header should contain a prominent banner such as:

```text
DO NOT EDIT MANUALLY.

Generated from upstream Zstd <version>.

Upstream source:
    <project/source reference>

Upstream commit:
    <commit>

Generated by:
    dependencies/compression/generate-zstd.py

Local transformation:
    - amalgamated upstream implementation
    - adjusted for C++ compilation
    - moved into private swblocks-baselib namespace
    - transformed for header-only ODR-safe linkage

See:
    dependencies/compression/README.md
```

This is important both for maintainability and licensing/source-attribution clarity.

---

# Consumer Experience

The final consumer experience should require only `swblocks-baselib`.

For example:

```cpp
#include <swblocks/baselib/Compression.h>

auto compressed =
    bl::Compression::compress(
        bl::CompressionType::Zstd,
        data
    );
```

The consumer should **not** need to do:

```bash
cmake zstd
make zstd

cmake brotli
make brotli

cmake zlib
make zlib

c++ myapp.cpp \
    -lz \
    -lzstd \
    -lbrotlienc \
    -lbrotlidec
```

The consumer should also not need:

```text
LD_LIBRARY_PATH
DYLD_LIBRARY_PATH
Windows DLL search-path configuration
package-manager setup
architecture-specific artifact selection
```

The compiler should generate native machine code automatically from the vendored header implementation.

---

# Cross-Architecture Benefit

This architecture eliminates most architecture-specific distribution work.

The same `swblocks-baselib` source tree can be consumed by:

```text
Linux x86-64
Linux ARM64
macOS x86-64
macOS ARM64
Windows x64
Windows ARM64
```

The target compiler generates machine code for the active target.

This avoids maintaining artifacts such as:

```text
libzstd-linux-x86_64.a
libzstd-linux-aarch64.a
libzstd-macos-arm64.a
libzstd-windows-x64.lib
...
```

This is one of the strongest reasons to use source-level embedding.

---

# Major Cost: Compilation Time

The biggest practical downside is likely compile time.

Zstd and especially Brotli include:

- substantial implementation code,
- large constant tables,
- many helper functions.

It would be a mistake for a generic umbrella header such as:

```cpp
#include <swblocks/baselib/Baselib.h>
```

to unconditionally include all three complete implementations into every translation unit.

Compression must therefore be isolated carefully.

Recommended rule:

```text
Baselib.h
    DOES NOT automatically pull in all compression implementation
```

Instead:

```text
Compression.h
    includes only the compression implementation needed for this API
```

Potentially split further into:

```text
CompressionZlib.h
CompressionBrotli.h
CompressionZstd.h
```

or use implementation-detail headers so only consumers using compression pay the parsing/compile-time cost.

The public runtime API can still remain unified.

---

# Possible Lazy Inclusion Model

One possible design is:

```text
Compression.h
    |
    +-- public enums/types/common dispatch
    |
    +-- detail/CompressionZstd.h
    +-- detail/CompressionBrotli.h
    +-- detail/CompressionZlib.h
```

If the project can determine algorithms at compile time, it may be possible to include only selected implementations.

If the API chooses the compression type dynamically at runtime, all enabled implementations may need to be visible in the relevant translation units.

Compile-time feature flags could still help:

```cpp
#define SWBLOCKS_BASELIB_ENABLE_ZSTD   1
#define SWBLOCKS_BASELIB_ENABLE_BROTLI 1
#define SWBLOCKS_BASELIB_ENABLE_ZLIB   1
```

These should be configuration controls, not external dependency seams.

---

# Keep the Embedded API Smaller Than the Upstream API Where Practical

Because these implementations are private, `swblocks-baselib` does not need to preserve every upstream API.

The internal interface could be reduced to a small set such as:

```cpp
detail::zstd::compress(...)
detail::zstd::decompress(...)

detail::brotli::compress(...)
detail::brotli::decompress(...)

detail::deflate::compress(...)
detail::deflate::decompress(...)
```

Potentially unnecessary functionality may include:

```text
Zstd dictionary-training APIs
advanced streaming APIs
gzip file I/O
Brotli CLI support
custom dictionary-management features
advanced custom allocator surfaces
command-line utilities
test/demo tools
```

However:

> Do not aggressively strip functionality in the initial implementation if that makes upstream upgrades much harder.

The first priority should be:

1. correctness,
2. reproducibility,
3. maintainability,
4. simple upstream refreshes.

Optimization and dead-code reduction can happen later.

---

# State and Thread-Safety Must Be Audited

When transforming C code into header-only C++, carefully audit mutable global state.

Possible categories include:

- writable file-scope variables,
- lazily initialized tables,
- cached CPU-feature detection,
- global allocator state,
- static scratch buffers,
- one-time initialization state.

The transformation must preserve intended semantics across translation units.

A transformation from:

```c
static int initialized;
```

to one copy per translation unit may change behavior.

A C++17 inline variable:

```cpp
inline int initialized = 0;
```

may be appropriate when one shared program-wide object is intended.

For immutable tables:

```cpp
inline constexpr ...
```

is often preferable.

Function-local statics should normally preserve their function-local semantics.

Thread-safety of initialization and upstream assumptions should be tested explicitly.

---

# Namespace Isolation

All embedded implementation symbols should be isolated from the application and from system-installed copies of the same libraries.

For example:

```cpp
namespace swblocks::baselib::detail::embedded
{

namespace zstd_1_5_x
{
    ...
}

namespace brotli_1_x
{
    ...
}

namespace zlib_1_3_x
{
    ...
}

}
```

The generated source should avoid exporting ordinary global symbols such as:

```text
ZSTD_decompress
deflate
inflate
BrotliEncoderCompress
```

This minimizes collision risk if the final process also happens to use a normal copy of one of these libraries elsewhere.

---

# Macro Isolation

Upstream C libraries may define many macros.

Because the implementation is now included into consumer translation units, macro leakage becomes especially dangerous.

The generated implementation should avoid polluting downstream code.

Possible techniques:

- rename private macros with a `SWBLOCKS_BASELIB_...` prefix,
- `#undef` implementation macros after use,
- avoid exposing upstream configuration macros,
- encapsulate generated configuration in detail headers,
- use compiler push/pop macro pragmas where useful and portable,
- generate a clean private config header.

The final public `Compression.h` should not leave arbitrary zlib/Zstd/Brotli macros defined.

---

# Warning Isolation

Third-party C code may trigger warnings under consumer compiler settings such as:

```text
-Wall
-Wextra
-Wconversion
-Wsign-conversion
-Wshadow
-Wold-style-cast
/pedantic
/W4
/WX
```

Because the third-party code is compiled as part of user translation units, warnings may become errors.

The generated headers should therefore locally isolate warnings where needed.

Conceptually:

```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "..."
...
#pragma GCC diagnostic pop
```

with equivalent handling for Clang/MSVC when necessary.

Do not globally disable warnings for consumer code.

---

# Compiler Compatibility

The generated implementation should be tested at minimum with:

- GCC
- Clang
- MSVC

and ideally both:

- x86-64
- ARM64

Potential differences to test include:

- C vs C++ conversion behavior,
- intrinsics,
- endian handling,
- alignment,
- `restrict` handling,
- compiler attributes,
- builtins,
- SIMD paths,
- architecture detection,
- calling conventions,
- preprocessor configuration.

---

# SIMD and CPU-Specific Optimizations

Zstd/Brotli may contain architecture-specific optimized code.

The implementation must verify that those paths still compile correctly when embedded into C++ headers.

Possible approaches:

1. preserve upstream compiler-feature detection;
2. disable problematic SIMD paths initially;
3. re-enable optimized paths after baseline correctness;
4. ensure no architecture-specific generated binary artifacts are introduced.

The source-level model should still allow the compiler to emit optimized target-specific code.

---

# Testing Requirements

The embedded implementations require strong equivalence testing.

## Round-Trip Tests

For each format:

```text
input
  ↓
compress
  ↓
decompress
  ↓
compare to original
```

Test:

- empty input,
- tiny input,
- text,
- binary data,
- highly compressible data,
- incompressible/random-looking data,
- large buffers.

## Interoperability Tests

Verify:

```text
swblocks-baselib compress
        ↓
upstream command/tool/library decompress
```

and:

```text
upstream command/tool/library compress
        ↓
swblocks-baselib decompress
```

This ensures the private implementation still produces standard-compliant streams.

## Error Tests

Test:

- truncated streams,
- corrupt streams,
- invalid headers,
- unsupported parameters,
- oversized expected output,
- malformed input.

## Multi-Translation-Unit Tests

This is essential.

Create a test executable where many source files all include and use compression:

```text
test1.cpp
test2.cpp
test3.cpp
...
```

The test should verify:

- no duplicate symbols,
- no ODR violations,
- correct shared/internal state behavior,
- successful linking,
- acceptable binary size.

## Concurrency Tests

Use compression/decompression concurrently from multiple threads.

This is especially important after transforming file-scope state.

---

# Binary-Size Tests

Compare:

```text
baseline executable
vs
zlib enabled
vs
Zstd enabled
vs
Brotli enabled
vs
all enabled
```

Test both:

- debug builds,
- optimized release builds.

Also test:

- LTO enabled,
- LTO disabled,
- function/data section garbage collection where available.

Ensure unused algorithms can be discarded when practical.

---

# Compile-Time Tests

Because compile-time cost is one of the major risks, measure it.

Suggested CI metrics:

```text
time to compile one TU with no compression
time to compile one TU using zlib
time to compile one TU using Zstd
time to compile one TU using Brotli
time to compile many TUs including Compression.h
```

Watch for pathological increases.

---

# Dependency Generation Workflow

The intended maintainer workflow should be:

```text
             pinned upstream release
                      │
             verify tag/checksum
                      │
              acquire source
                      │
              amalgamate source
                      │
          apply compatibility patches
                      │
          C → C++ compatibility fixes
                      │
       namespace all implementation symbols
                      │
     convert file-scope entities for ODR
                      │
           preserve license notices
                      │
          generate *.hpp artifact
                      │
          compile/test GCC
                      │
          compile/test Clang
                      │
          compile/test MSVC
                      │
       x86-64 + ARM64 CI validation
                      │
       interoperability test suite
                      │
            commit generated file
```

Consumers do not run this pipeline.

Only `swblocks-baselib` maintainers run it when upgrading dependencies.

---

# Recommended Implementation Order

The best implementation order is:

## Phase 1: Zstd Proof of Concept

Reason:

- upstream already supports single-file integration,
- good test of C-to-C++ compatibility,
- good test of ODR/headerization strategy.

Goals:

- get a generated private Zstd implementation compiling as C++,
- namespace it,
- make it safe across multiple translation units,
- expose a minimal baselib wrapper,
- validate compatibility with upstream Zstd streams.

## Phase 2: zlib

Reason:

- smaller and simpler C codebase,
- conventional implementation,
- good second validation.

Goals:

- embed deflate/inflate,
- omit `gz*` file API if unnecessary,
- verify gzip/zlib/raw-deflate semantics required by baselib.

## Phase 3: Brotli

Reason:

- likely largest source-integration effort,
- more source files,
- custom amalgamation likely necessary.

Goals:

- deterministic source-file amalgamation,
- namespace and ODR transformation,
- compile-time/binary-size assessment,
- full interoperability tests.

---

# Licensing / Attribution

These libraries are generally compatible with source vendoring, but the generated files must preserve the relevant upstream license and attribution requirements.

Important principles:

- preserve upstream copyright notices,
- preserve required license text,
- clearly identify locally transformed/generated source,
- do not imply transformed files are pristine upstream files,
- document the upstream version/commit.

The dependency-generation pipeline should automatically inject or preserve attribution headers.

A `THIRD_PARTY_NOTICES.md` file may also be appropriate.

The coding agent should verify the exact current upstream licenses and notices when implementing or upgrading each dependency.

---

# Security / Upgrade Considerations

Source vendoring means `swblocks-baselib` becomes responsible for tracking upstream security fixes.

Therefore:

- pin exact upstream versions,
- document them clearly,
- make dependency refresh reproducible,
- minimize local patch divergence,
- keep generated code mechanically reproducible,
- periodically check upstream releases/CVEs,
- make upgrade diffs reviewable.

Avoid manually editing generated implementation headers.

Any local modification should live in:

```text
dependencies/compression/patches/
```

or equivalent reproducible transformation logic.

---

# Reproducibility

Given the same:

```text
upstream source version
generator version
patch set
configuration
```

the generated header should be byte-for-byte reproducible if feasible.

CI can verify this by regenerating and checking that:

```bash
git diff --exit-code
```

is clean.

This guards against stale generated code.

---

# Potential Tooling Approaches

The implementation generator may use one of several approaches.

## Option 1: Deterministic textual transformation

Suitable if changes are limited and well understood.

Pipeline:

```text
amalgamate
→ regex/token-aware transformations
→ apply patches
→ validate output
```

This is simpler but must avoid unsafe global substitutions.

## Option 2: Parser/AST-based rewriting

Use a C/C++ parser or Clang tooling to classify:

- file-scope functions,
- variables,
- constants,
- declarations,
- definitions.

This can be more robust but is more tooling-heavy.

## Option 3: Explicit patch set

For pinned releases:

```text
upstream amalgamation
+
small checked-in patch
=
header-compatible implementation
```

This may be the easiest model to audit.

A new upstream release is:

1. regenerated,
2. patch reapplied/updated,
3. tests run,
4. resulting diff reviewed.

## Recommended practical starting point

Start simple.

For Zstd:

```text
upstream single-file generator
+
small compatibility patch
+
namespace wrapper/transformation
+
multi-TU tests
```

Do not build a generalized C compiler transformation framework unless the real source proves that it is necessary.

---

# Important Design Constraint: Keep This Private

Do not expose generated upstream implementation headers as public APIs.

Consumers should include:

```cpp
#include <swblocks/baselib/Compression.h>
```

not:

```cpp
#include <swblocks/baselib/detail/compression/zstd.hpp>
```

The detail path is implementation-only and can change between releases.

No compatibility guarantees should be made for the private generated interfaces.

---

# Potential Public API Shape

An example public shape:

```cpp
namespace swblocks::baselib
{

enum class CompressionType
{
    Zlib,
    Brotli,
    Zstd
};

struct CompressionOptions
{
    int level = 0;
};

class Compression
{
public:
    static Blob compress(
        CompressionType type,
        std::span<const std::byte> input,
        const CompressionOptions& options = {}
    );

    static Blob decompress(
        CompressionType type,
        std::span<const std::byte> input
    );
};

}
```

The implementation may dispatch internally to:

```cpp
detail::compression::zlib::compress(...)
detail::compression::brotli::compress(...)
detail::compression::zstd::compress(...)
```

The exact baselib API should follow existing `swblocks-baselib` conventions.

---

# Memory Allocation

The coding agent should inspect how the three libraries allocate memory.

The ideal final abstraction should be consistent with `swblocks-baselib` memory/error conventions.

Possible concerns:

- `malloc` / `free`,
- custom allocator hooks,
- exception boundaries,
- allocation failures,
- large output buffers,
- decompression bombs / untrusted input.

Do not blindly expose upstream allocation behavior if baselib has stronger conventions.

However, avoid heavily rewriting allocator internals during the initial headerization proof of concept.

---

# Error Handling

The public API should translate upstream error models into normal `swblocks-baselib` errors.

Do not leak raw implementation details such as:

```text
ZSTD_ErrorCode
BrotliDecoderResult
z_stream
```

unless deliberately part of a private implementation layer.

The public abstraction should report errors in the style already used by `swblocks-baselib`.

---

# Compression Format vs Library Name

Be precise about the format semantics.

For example, zlib can support:

- raw DEFLATE,
- zlib-wrapped DEFLATE,
- gzip-wrapped DEFLATE.

The baselib API should define whether a value named:

```text
Zlib
Deflate
Gzip
```

means one of those specific wire formats.

Do not let the internal library choice ambiguously define the public format.

---

# Avoiding Unnecessary API Surface

The implementation goal is not to reproduce entire third-party SDKs.

The goal is to support the compression functionality `swblocks-baselib` actually needs.

Therefore, public baselib APIs should remain narrow.

The private generated implementation can initially remain broader for ease of upstream upgrades, but only a small wrapper surface should be used by baselib.

---

# Build-System Expectations

A consuming project should not need special third-party compression configuration.

Ideally, this should work with ordinary inclusion of `swblocks-baselib`.

No new consumer-facing requirements such as:

```cmake
find_package(ZLIB)
find_package(zstd)
find_package(Brotli)
```

should be introduced.

Likewise, avoid making the consuming project run the dependency generator.

The generated source should already be committed/distributed with `swblocks-baselib`.

---

# What "Header-Only" Means in This Design

This design preserves the practical meaning of header-only:

- no baselib static/shared library is required,
- no compression static/shared library is required,
- no architecture-specific native artifact is distributed,
- implementation source is provided through headers,
- compilation happens naturally in the consumer build.

The price is:

- more compiler work,
- potentially larger intermediate objects,
- careful ODR engineering,
- maintenance of the generation pipeline.

---

# Final Recommended Design

The intended architecture is:

```text
                 swblocks-baselib
                       │
              public Compression API
                       │
        ┌──────────────┼──────────────┐
        │              │              │
   generated       generated      generated
 zlib header     Brotli header   Zstd header
        │              │              │
        └──────────────┴──────────────┘
                 C++17 inline
                 implementation
```

The preferred generation flow is:

```text
             pinned upstream release
                      │
             verify source identity
                      │
              amalgamate source
                      │
          C++ compatibility patch
                      │
       private versioned namespace
                      │
     ODR-safe inline transformation
                      │
           preserve licenses
                      │
         generated *.hpp files
                      │
      GCC / Clang / MSVC validation
                      │
       x86-64 / ARM64 validation
                      │
         multi-TU/linking tests
                      │
      interoperability test suite
```

The dependencies become:

> private source-level implementation details of `swblocks-baselib`.

No separate compression-library builds or architecture-specific binary packages are required.

---

# Implementation Priorities for the Coding Agent

1. **Do not introduce a provider seam that delegates compression dependency management to applications.**
2. **Keep `swblocks-baselib` header-only from the consumer's perspective.**
3. **Vendor source, not binaries.**
4. **Prototype Zstd first.**
5. **Use upstream single-file/amalgamation support where available.**
6. **Compile generated implementation as C++, not as external C objects.**
7. **Move implementation symbols into private versioned namespaces.**
8. **Make every definition safe across multiple translation units.**
9. **Do not use naive `#define static inline` transformations.**
10. **Audit immutable vs mutable globals carefully.**
11. **Prevent macro and warning leakage into consumer code.**
12. **Keep compression out of broad umbrella headers when possible.**
13. **Measure compile-time and binary-size impact.**
14. **Add multi-TU tests specifically designed to catch ODR/linkage problems.**
15. **Verify interoperability against normal upstream libraries/tools.**
16. **Make generated files reproducible and never hand-edit them.**
17. **Keep local patches minimal to simplify upstream upgrades.**
18. **Preserve upstream licenses and clearly mark transformed source.**
19. **Test GCC, Clang, and MSVC.**
20. **Test x86-64 and ARM64 at minimum.**

---

# Suggested First Milestone

Implement a Zstd-only proof of concept that proves all core assumptions.

Success criteria:

```text
[ ] Zstd upstream version is pinned.
[ ] Upstream single-file implementation is generated reproducibly.
[ ] Generated implementation compiles as C++.
[ ] Implementation lives in a private swblocks namespace.
[ ] Public Zstd symbols are not leaked globally.
[ ] Header can be included from multiple translation units.
[ ] Final executable links without an external Zstd library.
[ ] Compression works.
[ ] Decompression works.
[ ] Output interoperates with upstream Zstd.
[ ] Upstream-generated Zstd data can be decompressed.
[ ] GCC build passes.
[ ] Clang build passes.
[ ] MSVC build passes.
[ ] x86-64 build passes.
[ ] ARM64 build passes.
[ ] Compile-time cost is measured.
[ ] Binary-size cost is measured.
```

Once this works cleanly, repeat the pattern for zlib and then Brotli.

---

# Key Architectural Principle

The main idea can be summarized as:

> `swblocks-baselib` owns both the abstraction and the source-level implementation of its compression dependencies. Upstream C libraries are treated as source inputs to a reproducible generation pipeline that produces private, ODR-safe C++ header implementations committed with baselib. Consumers neither build nor link those third-party libraries explicitly.

That is the architecture to implement.
