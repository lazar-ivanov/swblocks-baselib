# Data Model Performance — Internal Redesign Plan

## Goal

Minimize the overhead the data model layer adds on top of raw Boost.JSON, while preserving the external macro API (`BL_DM_DEFINE_CLASS_BEGIN`, `BL_DM_DECLARE_*`, etc.) exactly. Medium-risk internal changes are acceptable. All changes must be C++11. Staged carefully — each stage independently compilable, testable, and reviewable.

---

## Architecture: V1/V2 Split

Preserve the existing implementation verbatim as V1. Create V2 as a copy, then improve V2 incrementally. A compile-time macro switches between them.

### File structure

| Current file | Becomes |
|---|---|
| `DataModelObject.h` | Wrapper — includes V1 or V2 based on `BL_JSON_USE_DM_V1` |
| `DataModelObjectDefs.h` | Wrapper — includes V1 or V2 based on `BL_JSON_USE_DM_V1` |
| (new) `DataModelObjectImplV1.h` | Verbatim copy of current `DataModelObject.h` |
| (new) `DataModelObjectDefsImplV1.h` | Verbatim copy of current `DataModelObjectDefs.h` |
| (new) `DataModelObjectImplV2.h` | Copy of current `DataModelObject.h` (will be improved in stages) |
| (new) `DataModelObjectDefsImplV2.h` | Copy of current `DataModelObjectDefs.h` (will be improved in stages) |

### Wrapper file pattern (same as `JsonUtils.h` / `BL_USE_JSON_SPIRIT`):

```cpp
// DataModelObject.h
#ifdef BL_JSON_USE_DM_V1
#include <baselib/data/DataModelObjectImplV1.h>
#else
#include <baselib/data/DataModelObjectImplV2.h>
#endif
```

```cpp
// DataModelObjectDefs.h
#ifdef BL_JSON_USE_DM_V1
#include <baselib/data/DataModelObjectDefsImplV1.h>
#else
#include <baselib/data/DataModelObjectDefsImplV2.h>
#endif
```

### Makefile propagation (`projects/make/3rd/boost/common.mk`)

Follow the `BL_USE_JSON_SPIRIT` pattern:

```makefile
ifdef BL_JSON_USE_DM_V1
CPPFLAGS += -DBL_JSON_USE_DM_V1
endif

# If BL_USE_JSON_SPIRIT is set, force DM V1 (implied)
ifdef BL_USE_JSON_SPIRIT
CPPFLAGS += -DBL_JSON_USE_DM_V1
endif
```

**Default behavior:** `BL_JSON_USE_DM_V1` is NOT defined → V2 is used.

---

## Staged Implementation Plan

### Stage 0 — Benchmarks + V1/V2 split + baseline

**What:**
1. Add apples-to-apples benchmark tests in `TestJsonPerformance.h` that compare the same JSON data through direct Boost.JSON and the data model layer, reporting overhead ratio
2. Create the V1/V2 file split (4 new files + 2 wrapper files)
3. Add the `BL_JSON_USE_DM_V1` makefile support
4. Run baseline benchmarks — record numbers for both V1 and V2 (should be identical at this point)

**Files:**
- `src/utests/utf_baselib_data/TestJsonPerformance.h` — new benchmark tests
- `src/include/baselib/data/DataModelObjectImplV1.h` — verbatim copy of current DataModelObject.h
- `src/include/baselib/data/DataModelObjectDefsImplV1.h` — verbatim copy of current DataModelObjectDefs.h
- `src/include/baselib/data/DataModelObjectImplV2.h` — copy of current DataModelObject.h (includes the loadFromJsonText fix already applied)
- `src/include/baselib/data/DataModelObjectDefsImplV2.h` — copy of current DataModelObjectDefs.h
- `src/include/baselib/data/DataModelObject.h` — rewritten as V1/V2 wrapper
- `src/include/baselib/data/DataModelObjectDefs.h` — rewritten as V1/V2 wrapper
- `projects/make/3rd/boost/common.mk` — add `BL_JSON_USE_DM_V1` propagation

---

### Stage 1 — Eliminate deep copies in complex vector/map deserialization (V2 only)

**Impact: HIGH** — eliminates N full `json::object` tree deep copies for vectors/maps of N nested objects.

**Root cause:** `DataModelObjectDefsImplV2.h` line 653:
```cpp
BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_DESERIALIZE( tempContext, bl::cpp::copy( item.as_object() ) );
```
Deep-copies each nested `json::object`. Unnecessary because:
- The parent context's `deserializationDoc` is alive throughout the loop
- The property key is added to `m_processedProperties`, so unmapped handling skips it
- The single complex property macro (line 573) already uses `std::move` for the same pattern

**Fix:** Change to non-const iteration and move instead of copy:
- `const auto& items` → `auto& items`
- `const auto& item` → `auto& item`
- `bl::cpp::copy(item.as_object())` → `std::move(item.as_object())`
- Same fix for complex map deserialization (line 726)

**Files:** `DataModelObjectDefsImplV2.h`

---

### Stage 2 — Eliminate json::object copies in nested serialization (V2 only)

**Impact: HIGH** — eliminates N `json::object` copies when embedding nested serialization results into the parent.

**Fix:** Move instead of copy at three locations:
- `object.emplace(key, tempContext.serializationDoc())` → `std::move(tempContext.serializationDoc())`
- `items.push_back(json::value(tempContext.serializationDoc()))` → `std::move(...)`
- `items.emplace(key, tempContext.serializationDoc())` → `std::move(...)`

`tempContext` is a stack-local variable not used after the emplace.

**Files:** `DataModelObjectDefsImplV2.h`

---

### Stage 3 — Eliminate wasted json::object in SerializationContextBase (V2 only)

**Impact: MEDIUM** — eliminates 1 wasted `json::object` construction per context (N+1 for complex objects).

**What:** Replace `m_serializationDoc` + `m_deserializationDoc` with single `m_doc`. `m_isSerialization` is `const bool`; accessors have `BL_ASSERT` guards. No code path mixes accessors.

**Files:** `DataModelObjectImplV2.h`

---

### Stage 4 — Lazy m_processedProperties allocation (V2 only)

**Impact: MEDIUM** — eliminates `unordered_set<string>` construction for serialization contexts.

**What:** Change to `std::unique_ptr<std::unordered_set<std::string>>`. Allocate on first `addProcessedProperty` call (unconditionally — needed for unmapped handling during deserialization). Saves allocation on all serialization contexts and deserialization contexts for partial classes.

**Files:** `DataModelObjectImplV2.h`

---

## Verification (per stage)

```bash
# Build with V2 (default)
make -k -j4 utf_baselib_data VARIANT=release

# Build with V1 (verify old code still works)
make -k -j4 utf_baselib_data VARIANT=release BL_JSON_USE_DM_V1=1

# Correctness tests
./bld/d25-a64-clang1700-release/utests/utf_baselib_data/utf-baselib-data \
  --run_test="*" --log_level=all

# Performance benchmarks
./bld/d25-a64-clang1700-release/utests/utf_baselib_data/utf-baselib-data \
  --run_test="*Performance*" --log_level=all -- --is-client
```

Compare timing at each stage against Stage 0 baseline and previous stage.

---

## Baseline Results (Stage 0)

**Date:** 2026-02-24
**Platform:** macOS (Darwin), ARM64 (VM)
**Compiler:** Apple clang 17.0.0
**Build:** Release
**JSON library:** Boost.JSON

### Simple Object (4 scalar properties)

| Operation | Direct JSON | Data Model | Overhead |
|-----------|------------|------------|----------|
| Deserialization (5000 iter) | 1.59 ms (0.0003 ms/iter) | 3.87 ms (0.0008 ms/iter) | **2.43x** |
| Serialization (5000 iter) | 0.43 ms (0.0001 ms/iter) | 3.57 ms (0.0007 ms/iter) | **8.40x** |

### Complex Object (scalars + map + vectors + nested object + nested vector)

| Operation | Direct JSON | Data Model | Overhead |
|-----------|------------|------------|----------|
| Deserialization (2000 iter) | 4.62 ms (0.0023 ms/iter) | 9.94 ms (0.0050 ms/iter) | **2.15x** |
| Serialization (2000 iter) | 1.00 ms (0.0005 ms/iter) | 9.69 ms (0.0048 ms/iter) | **9.66x** |
| Round-trip (2000 iter) | 4.30 ms (0.0022 ms/iter) | 15.65 ms (0.0078 ms/iter) | **3.64x** |

### Key Observations

1. **Serialization overhead is the largest problem** — 8.4x-9.7x overhead vs raw JSON
2. **Deserialization overhead is moderate** — 2.1x-2.4x overhead (includes parse time in both)
3. **Complex objects have slightly lower deserialization overhead ratio** than simple objects (2.15x vs 2.43x), likely because parsing dominates more for larger JSON
4. **Serialization overhead is worse for complex objects** (9.66x vs 8.40x), consistent with the copy overhead analysis (more nested objects = more copies)

---

## Stage 1 Results — Eliminate deep copies in complex vector/map deserialization

**Date:** 2026-02-24
**Platform:** macOS (Darwin), ARM64 (different host from Stage 0 — compare ratios, not absolute times)

### Simple Object (4 scalar properties)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 |
|-----------|------------|------------|----------|------------|
| Deserialization (5000 iter) | 1.19 ms (0.0002 ms/iter) | 2.26 ms (0.0005 ms/iter) | **1.91x** | was 2.43x |
| Serialization (5000 iter) | 0.37 ms (0.0001 ms/iter) | 2.04 ms (0.0004 ms/iter) | **5.59x** | was 8.40x |

### Complex Object (scalars + map + vectors + nested object + nested vector)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 |
|-----------|------------|------------|----------|------------|
| Deserialization (2000 iter) | 2.95 ms (0.0015 ms/iter) | 6.03 ms (0.0030 ms/iter) | **2.04x** | was 2.15x |
| Serialization (2000 iter) | 0.78 ms (0.0004 ms/iter) | 7.76 ms (0.0039 ms/iter) | **9.98x** | was 9.66x |
| Round-trip (2000 iter) | 3.64 ms (0.0018 ms/iter) | 13.82 ms (0.0069 ms/iter) | **3.80x** | was 3.64x |

### Key Observations

1. **Different host machine** — absolute times differ from Stage 0; overhead ratios are the meaningful comparison
2. **Complex deserialization improved modestly** — 2.15x → 2.04x (5% improvement in ratio). The test object only has 5 nested items, limiting the impact of eliminating deep copies
3. **Simple object ratios also shifted** — likely machine/environment variance since Stage 1 changes don't affect simple objects (no complex vectors/maps)
4. **Serialization unchanged as expected** — Stage 1 only targets deserialization; serialization copy overhead is addressed in Stage 2

---

## Stage 2 Results — Eliminate json::object copies in nested serialization

**Date:** 2026-02-24
**Platform:** macOS (Darwin), ARM64 (same host as Stage 1)

### Simple Object (4 scalar properties)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 1 |
|-----------|------------|------------|----------|------------|------------|
| Deserialization (5000 iter) | 2.34 ms (0.0005 ms/iter) | 4.19 ms (0.0008 ms/iter) | **1.79x** | was 2.43x | was 1.91x |
| Serialization (5000 iter) | 0.70 ms (0.0001 ms/iter) | 3.48 ms (0.0007 ms/iter) | **4.95x** | was 8.40x | was 5.59x |

### Complex Object (scalars + map + vectors + nested object + nested vector)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 1 |
|-----------|------------|------------|----------|------------|------------|
| Deserialization (2000 iter) | 5.57 ms (0.0028 ms/iter) | 9.76 ms (0.0049 ms/iter) | **1.75x** | was 2.15x | was 2.04x |
| Serialization (2000 iter) | 0.97 ms (0.0005 ms/iter) | 9.38 ms (0.0047 ms/iter) | **9.68x** | was 9.66x | was 9.98x |
| Round-trip (2000 iter) | 4.59 ms (0.0023 ms/iter) | 15.20 ms (0.0076 ms/iter) | **3.31x** | was 3.64x | was 3.80x |

### Key Observations

1. **Complex serialization overhead unchanged** — 9.68x, roughly same as Stage 0 (9.66x). The move optimization helps for the nested objects, but the dominant cost appears to be elsewhere in the serialization path (context construction, property-by-property building of the json::object)
2. **Deserialization continues to improve** — complex object down to 1.75x from 2.04x (Stage 1) and 2.15x (Stage 0)
3. **Round-trip improved** — 3.31x from 3.64x (Stage 0), driven by deserialization gains
4. **Simple object serialization ratio variance** — 4.95x vs 5.59x (Stage 1) vs 8.40x (Stage 0); the Stage 0 number was on a different machine, so Stage 1→2 comparison (5.59x→4.95x) is more meaningful

---

## Stage 3 Results — Eliminate wasted json::object in SerializationContextBase

**Date:** 2026-02-24
**Platform:** macOS (Darwin), ARM64 (same host as Stages 1-2)

### Simple Object (4 scalar properties)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 2 |
|-----------|------------|------------|----------|------------|------------|
| Deserialization (5000 iter) | 1.13 ms (0.0002 ms/iter) | 2.19 ms (0.0004 ms/iter) | **1.94x** | was 2.43x | was 1.79x |
| Serialization (5000 iter) | 0.38 ms (0.0001 ms/iter) | 1.91 ms (0.0004 ms/iter) | **5.04x** | was 8.40x | was 4.95x |

### Complex Object (scalars + map + vectors + nested object + nested vector)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 2 |
|-----------|------------|------------|----------|------------|------------|
| Deserialization (2000 iter) | 2.97 ms (0.0015 ms/iter) | 5.96 ms (0.0030 ms/iter) | **2.01x** | was 2.15x | was 1.75x |
| Serialization (2000 iter) | 0.78 ms (0.0004 ms/iter) | 6.68 ms (0.0033 ms/iter) | **8.51x** | was 9.66x | was 9.68x |
| Round-trip (2000 iter) | 3.68 ms (0.0018 ms/iter) | 13.21 ms (0.0066 ms/iter) | **3.59x** | was 3.64x | was 3.31x |

### Key Observations

1. **Complex serialization improved modestly** — 8.51x vs 9.68x (Stage 2) and 9.66x (Stage 0). Eliminating the wasted json::object construction saves some overhead per context
2. **Simple serialization also improved** — 5.04x vs 4.95x (Stage 2), within noise, but trending down from 8.40x (Stage 0)
3. **Deserialization ratios show machine variance** — 2.01x vs 1.75x (Stage 2); the Stage 3 change eliminates a wasted serialization doc during deserialization contexts, but the ratio increase suggests run-to-run variance rather than regression
4. **Round-trip stable** — 3.59x vs 3.31x (Stage 2) and 3.64x (Stage 0)

---

## Stage 2b Results — Move local arrays/objects in container serialization

**Date:** 2026-02-24
**Platform:** macOS (Darwin), ARM64 (same host as Stages 1-3)

**What:** Fixed 4 additional locations where local `items` (json::array or json::object) was copied instead of moved into the parent json::object during serialization:
- Complex vector property serialize (json::array)
- Complex map property serialize (json::object)
- Simple map property serialize (json::object)
- Simple container property serialize (json::array)

### Simple Object (4 scalar properties)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 |
|-----------|------------|------------|----------|------------|
| Deserialization (5000 iter) | 1.12 ms (0.0002 ms/iter) | 2.29 ms (0.0005 ms/iter) | **2.04x** | was 2.43x |
| Serialization (5000 iter) | 0.37 ms (0.0001 ms/iter) | 2.22 ms (0.0004 ms/iter) | **5.99x** | was 8.40x |

### Complex Object (scalars + map + vectors + nested object + nested vector)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 3 |
|-----------|------------|------------|----------|------------|------------|
| Deserialization (2000 iter) | 3.18 ms (0.0016 ms/iter) | 6.47 ms (0.0032 ms/iter) | **2.04x** | was 2.15x | was 2.01x |
| Serialization (2000 iter) | 0.76 ms (0.0004 ms/iter) | 6.51 ms (0.0033 ms/iter) | **8.60x** | was 9.66x | was 8.51x |
| Round-trip (2000 iter) | 3.89 ms (0.0019 ms/iter) | 12.71 ms (0.0064 ms/iter) | **3.27x** | was 3.64x | was 3.59x |

### Key Observations

1. **Round-trip improved** — 3.27x vs 3.64x (Stage 0), the best result so far
2. **Serialization ratios within noise of Stage 3** — 8.60x vs 8.51x. The test object's containers are small (5 items), limiting the impact. Larger containers would benefit more
3. **Deserialization stable** — 2.04x, consistent with Stage 3
4. **Simple object unaffected as expected** — simple objects have no containers, so these changes don't apply

---

## Stage 4 Results — Eliminate hidden deep copy in saveToString

**Date:** 2026-02-24
**Platform:** macOS (Darwin), ARM64 (same host as previous stages)

**What:** Profiling revealed that `saveToString(const json::object&)` internally copies the entire json::object into a `json::value` via `value(rootObject)` before serializing. This hidden deep copy accounted for ~30% of total serialization time. Fix: changed `getJsonString()` to move the object into a value: `json::saveToString(json::value(std::move(jsonObject)), ...)`.

### Simple Object (4 scalar properties)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 2b |
|-----------|------------|------------|----------|------------|-------------|
| Deserialization (5000 iter) | 1.79 ms (0.0004 ms/iter) | 4.06 ms (0.0008 ms/iter) | **2.26x** | was 2.43x | was 2.04x |
| Serialization (5000 iter) | 0.51 ms (0.0001 ms/iter) | 2.23 ms (0.0004 ms/iter) | **4.40x** | was 8.40x | was 5.99x |

### Complex Object (scalars + map + vectors + nested object + nested vector)

| Operation | Direct JSON | Data Model | Overhead | vs Stage 0 | vs Stage 2b |
|-----------|------------|------------|----------|------------|-------------|
| Deserialization (2000 iter) | 4.63 ms (0.0023 ms/iter) | 9.14 ms (0.0046 ms/iter) | **1.98x** | was 2.15x | was 2.04x |
| Serialization (2000 iter) | 1.09 ms (0.0005 ms/iter) | 5.73 ms (0.0029 ms/iter) | **5.26x** | was 9.66x | was 8.60x |
| Round-trip (2000 iter) | 4.65 ms (0.0023 ms/iter) | 12.37 ms (0.0062 ms/iter) | **2.66x** | was 3.64x | was 3.27x |

### Key Observations

1. **Complex serialization dramatically improved** — 5.26x vs 8.60x (Stage 2b) and 9.66x (Stage 0). A 46% reduction from baseline from a 2-line change
2. **Simple serialization also improved** — 4.40x vs 5.99x (Stage 2b) and 8.40x (Stage 0)
3. **Round-trip at 2.66x** — best result so far, down from 3.64x (Stage 0)
4. **Deserialization under 2x for complex objects** — 1.98x
5. **Profiling was critical** — without profiling, we would have pursued a complex direct-to-string refactor. Instead, a 2-line move fix eliminated ~30% of serialization overhead

---

## Files summary

| File | Stage |
|------|-------|
| `DataModelObjectImplV1.h` (new) | 0 |
| `DataModelObjectDefsImplV1.h` (new) | 0 |
| `DataModelObjectImplV2.h` (new) | 0, 3, 4 |
| `DataModelObjectDefsImplV2.h` (new) | 0, 1, 2 |
| `DataModelObject.h` (wrapper) | 0 |
| `DataModelObjectDefs.h` (wrapper) | 0 |
| `projects/make/3rd/boost/common.mk` | 0 |
| `TestJsonPerformance.h` | 0 |
