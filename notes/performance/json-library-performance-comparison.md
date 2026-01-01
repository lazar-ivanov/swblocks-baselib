# JSON Library Performance Comparison

**Date:** 2026-01-01
**Platform:** macOS Sequoia (Darwin 24.6.0), ARM64
**Compiler:** Apple clang 17.0.0
**Boost:** 1.90.0
**json-spirit:** 4.08

## Executive Summary

This report compares the performance of Boost.JSON and json-spirit libraries across debug and release builds. The results demonstrate that **Boost.JSON in release mode** provides the best performance across all benchmarks, with particularly significant advantages in parsing operations.

### Key Findings

1. **Boost.JSON Release is the clear winner** across all benchmarks
2. **Debug vs Release impact:**
   - Boost.JSON: Debug builds are 4.8x to 23.5x slower than release
   - json-spirit: Debug builds are 5.4x to 146.6x slower than release
3. **Library comparison (Release builds):**
   - Boost.JSON is 1.1x to 5.5x faster than json-spirit
4. **Library comparison (Debug builds):**
   - Boost.JSON debug is 28.2x to 30.8x faster than json-spirit debug for medium/large JSON

---

## Performance Results

### 1. JSON Parsing Performance

#### Simple JSON (324 bytes) - 1000 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **0.71 ms** | **0.0007 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 3.77 ms | 0.0038 ms | 5.3x slower |
| json-spirit Release | 3.87 ms | 0.0039 ms | 5.5x slower |
| json-spirit Debug | 100.00 ms | 0.1000 ms | 140.8x slower |

#### Medium JSON (29,258 bytes) - 1000 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **55.54 ms** | **0.0555 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 264.40 ms | 0.2644 ms | 4.8x slower |
| json-spirit Release | 297.52 ms | 0.2975 ms | 5.4x slower |
| json-spirit Debug | 8142.76 ms | 8.1428 ms | 146.6x slower |

#### Large JSON (504,286 bytes, 1000 records) - 100 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **86.35 ms** | **0.8635 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 423.83 ms | 4.2383 ms | 4.9x slower |
| json-spirit Release | 418.69 ms | 4.1869 ms | 4.8x slower |
| json-spirit Debug | 11965.92 ms | 119.6592 ms | 138.6x slower |

**Analysis:** Boost.JSON shows the strongest advantage in parsing, especially for larger documents. The json-spirit debug performance is particularly poor, being 30.8x slower than Boost.JSON debug for medium JSON.

---

### 2. JSON Serialization Performance

#### Simple JSON (non-canonical) - 1000 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **0.14 ms** | **0.0001 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 1.50 ms | 0.0015 ms | 10.7x slower |
| json-spirit Release | 1.44 ms | 0.0014 ms | 10.3x slower |
| json-spirit Debug | 4.76 ms | 0.0048 ms | 34.0x slower |

#### Medium JSON (non-canonical) - 1000 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **11.28 ms** | **0.0113 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 112.62 ms | 0.1126 ms | 10.0x slower |
| json-spirit Release | 95.72 ms | 0.0957 ms | 8.5x slower |
| json-spirit Debug | 386.80 ms | 0.3868 ms | 34.3x slower |

#### Simple JSON (canonical) - 1000 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **0.86 ms** | **0.0009 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 5.17 ms | 0.0052 ms | 6.0x slower |
| json-spirit Release | 1.56 ms | 0.0016 ms | 1.8x slower |
| json-spirit Debug | 4.82 ms | 0.0048 ms | 5.6x slower |

#### Medium JSON (canonical) - 1000 iterations

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **62.42 ms** | **0.0624 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 388.64 ms | 0.3886 ms | 6.2x slower |
| json-spirit Release | 93.72 ms | 0.0937 ms | 1.5x slower |
| json-spirit Debug | 384.71 ms | 0.3847 ms | 6.2x slower |

**Analysis:** Serialization performance is more balanced between the two libraries in release mode. For canonical serialization, json-spirit release shows competitive performance (1.5x-1.8x slower) compared to Boost.JSON release (6.0x-6.2x slower in debug).

---

### 3. JSON Object Access Performance

**Iterating 100 items, accessing 4 properties each - 10,000 iterations**

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **19.53 ms** | **0.0020 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 458.69 ms | 0.0459 ms | 23.5x slower |
| json-spirit Release | 46.45 ms | 0.0046 ms | 2.4x slower |
| json-spirit Debug | 892.48 ms | 0.0892 ms | 45.7x slower |

**Analysis:** Boost.JSON demonstrates strong performance for object access operations, with a 2.4x advantage in release mode.

---

### 4. Nested JSON Access Performance

**Deeply nested JSON (20 levels) - 100,000 iterations**

#### Parse Time

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **179.89 ms** | **0.0018 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 774.43 ms | 0.0077 ms | 4.3x slower |
| json-spirit Release | 809.68 ms | 0.0081 ms | 4.5x slower |
| json-spirit Debug | 20470.05 ms | 0.2047 ms | 113.8x slower |

#### Access Time

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **33.29 ms** | **0.0003 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 253.76 ms | 0.0025 ms | 7.6x slower |
| json-spirit Release | 36.17 ms | 0.0004 ms | 1.1x slower |
| json-spirit Debug | 264.49 ms | 0.0026 ms | 7.9x slower |

**Analysis:** The two libraries show similar performance for deeply nested access operations in release mode. Parse time shows a 4.5x difference favoring Boost.JSON.

---

### 5. Object Construction Performance

**Constructing object with nested array (10 items) - 10,000 iterations**

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **28.82 ms** | **0.0029 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 139.25 ms | 0.0139 ms | 4.8x slower |
| json-spirit Release | 131.55 ms | 0.0132 ms | 4.6x slower |
| json-spirit Debug | 713.74 ms | 0.0714 ms | 24.8x slower |

**Analysis:** Boost.JSON shows strong performance for object construction with a 4.6x advantage in release mode.

---

### 6. Round-Trip Performance

**Parse + Access + Serialize (medium JSON) - 500 iterations**

| Configuration | Total Time | Avg/Iteration | vs Boost.JSON Release |
|--------------|------------|---------------|----------------------|
| **Boost.JSON Release** | **32.63 ms** | **0.0653 ms** | **1.0x (baseline)** |
| Boost.JSON Debug | 189.53 ms | 0.3791 ms | 5.8x slower |
| json-spirit Release | 195.79 ms | 0.3916 ms | 6.0x slower |
| json-spirit Debug | 4371.22 ms | 8.7424 ms | 134.0x slower |

**Analysis:** This composite benchmark reflects real-world usage patterns and shows Boost.JSON's consistent performance advantage.

---

## Recommendations

### 1. Default Library Choice

**Use Boost.JSON** as the default JSON library for optimal performance across all operations.

### 2. Build Configuration

**Always use release builds in production environments.** The performance difference is significant:
- Boost.JSON: 4.8x to 23.5x improvement
- json-spirit: 5.4x to 146.6x improvement

### 3. When to Use json-spirit

json-spirit compatibility mode should only be used when:
- Migrating legacy code with heavy json-spirit API dependencies
- Performance is not critical for the application
- **Always in release mode** (debug mode performance is unacceptable)

### 4. Development Considerations

- Boost.JSON debug builds offer reasonable performance for testing and development
- Avoid json-spirit debug builds when working with medium-to-large JSON documents
- For performance-critical development, consider using release builds even during testing

---

## Test Configuration Details

### Build Commands

```bash
# Boost.JSON Debug
rm -rf ./bld && make -k -j1 utf_baselib_data VARIANT=debug

# Boost.JSON Release
rm -rf ./bld && make -k -j1 utf_baselib_data VARIANT=release

# json-spirit Debug
rm -rf ./bld && make -k -j1 utf_baselib_data VARIANT=debug BL_USE_JSON_SPIRIT=1

# json-spirit Release
rm -rf ./bld && make -k -j1 utf_baselib_data VARIANT=release BL_USE_JSON_SPIRIT=1
```

### Test Execution

```bash
./bld/d25-a64-clang1700-{debug|release}/utests/utf_baselib_data/utf-baselib-data \
  --run_test="JsonPerformance*" --log_level=all -- --is-client
```

### Test Data Sizes

- **Simple JSON:** 324 bytes
- **Medium JSON:** 29,258 bytes
- **Large JSON:** 504,286 bytes (1000 records)
- **Nested JSON:** 20 levels deep

---

## Conclusion

The performance testing conclusively demonstrates that Boost.JSON provides superior performance across all benchmark categories. The abstraction layer implemented in the codebase allows switching between libraries via the `BL_USE_JSON_SPIRIT` flag, but the performance data strongly favors **Boost.JSON as the default choice** for production use.

The particularly poor performance of json-spirit in debug builds (up to 146.6x slower) makes it unsuitable for development work with anything beyond trivial JSON documents. Boost.JSON maintains acceptable performance even in debug builds, making it the better choice for both development and production environments.
