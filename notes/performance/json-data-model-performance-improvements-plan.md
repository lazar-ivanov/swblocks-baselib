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
