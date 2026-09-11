# ARM64 SVE Capability Reporting: Technical Deep Dive

This document contains the analysis of why importing `cryptography` crashes the Python test suite with `SIGILL` on some aarch64 virtual machines, even though the code and the library are both correct. For operational guidance (what to do about it), see the parent [AGENTS.md](../AGENTS.md).

The guard that works around it lives in `scripts/tests/conftest.py`. The cause is the guest kernel's CPU feature reporting, so it is a property of the machine rather than of the distribution, the Python version, or the repository.

---

## An Impossible CPU

SVE2 is architecturally a superset of SVE: no real processor can implement the former without the latter. Some hypervisors nevertheless report exactly that. Observed under Parallels on Apple Silicon, whose cores implement no SVE at all:

```
$ grep -o 'sve[a-z0-9]*' /proc/cpuinfo | sort -u
sve2
svebf16
svei8mm                      # note: no bare 'sve'

$ LD_SHOW_AUXV=1 /bin/true | grep -i hwcap
AT_HWCAP:   0x2fb3ffff       # bit 22 (HWCAP_SVE)   = 0
AT_HWCAP2:  0x105383         # bit 1  (HWCAP2_SVE2) = 1
```

The kernel derives the base SVE flag from `ID_AA64PFR0_EL1.SVE` and the SVE2/I8MM/BF16 flags from `ID_AA64ZFR0_EL1`. The hypervisor exposes a non-zero `ZFR0` while `PFR0.SVE` is zero, and this kernel does not sanitize the combination before publishing HWCAP.

### It is the kernel, not the distribution

An Ubuntu 24.04 arm64 container on an affected RHEL 9 host reports byte-identical values, because a container shares the host kernel:

```
sve2 svei8mm svebf16
AT_HWCAP:   2fb3ffff
AT_HWCAP2:  0x105383
```

A machine booting its own newer kernel is unaffected, whatever its distribution. Treat "does this kernel report SVE2 without SVE" as the only question that matters; the one-line check above answers it.

---

## Why OpenSSL Trips Over It

`crypto/armcap.c` tests the two capability bits independently — there is no `&&` — so it records SVE2 as present on a machine that has no SVE:

```c
if (getauxval(OSSL_HWCAP)  & OSSL_HWCAP_SVE)    OPENSSL_armcap_P |= ARMV8_SVE;
if (getauxval(OSSL_HWCAP2) & OSSL_HWCAP2_SVE2)  OPENSSL_armcap_P |= ARMV8_SVE2;
```

From OpenSSL 4.0 onwards that bit has consequences. 4.0 added real SVE/SVE2 implementations (`ChaCha20_ctr32_sve`, `poly1305_blocks_sve2`) and, with them, a vector-length probe that `OPENSSL_cpuid_setup` runs from a **library constructor**:

```
Program received signal SIGILL, Illegal instruction.
0x0000ffffe9cf4718 in _armv8_sve_get_vl_bytes () from .../cryptography/hazmat/bindings/_rust.abi3.so
=> 0xffffe9cf4718 <_armv8_sve_get_vl_bytes>:  cntb  x0
#1  OPENSSL_cpuid_setup ()
#2  call_init (dl-init.c:70)
```

`cntb` is an SVE instruction. Because the probe runs during `dlopen`, the process dies the moment the shared object loads — before any of the calling code executes. In the test suite that is during collection, the first time a test module imports `moto`, which imports `cryptography.x509`.

### Why the devenv7 toolchain is not affected

The trap needs an OpenSSL new enough to contain SVE code. The distribution and the Python wheel do not ship the same one:

| Component | OpenSSL | Has `_armv8_sve_get_vl_bytes` |
|---|---|---|
| devenv7 dist (`openssl/3.5.4/...`) | 3.5.4 (30 Sep 2025) | No — symbol absent from `libcrypto.a` |
| `cryptography` 50.0.1 wheel | 4.0.2 (25 Aug 2026) | Yes |

3.5.4 sets the capability bit but never executes an SVE instruction, so every baselib binary runs normally on an affected host — confirmed by `openssl speed -evp aes-128-gcm` reporting 4.0 GB/s, well above the 1.0 GB/s acceleration threshold.

**This is a deferred problem, not an absent one.** When the devenv7 distribution moves to OpenSSL 4.x, `libcrypto`'s constructor will run `cntb` inside *every* baselib test binary on an affected host, and the whole build matrix will fail at process start rather than only the Python suite. Re-test on an affected machine before that uplift.

### Platforms that cannot be affected

| Platform | Why |
|---|---|
| Windows (ARM64 and x64) | `armcap.c` compiles a separate `_WIN32` implementation that sets only NEON/AES/PMULL/SHA1/SHA256 via `IsProcessorFeaturePresent`. It never sets an SVE bit, never calls `getauxval`, and never reads the environment variable below. |
| macOS (Apple Silicon and Intel) | Detection goes through `sysctl`, not HWCAP, so the phantom flag never appears. |
| Linux x86_64 | `armcap.c` is not compiled at all; the analogous knob is `OPENSSL_ia32cap`. |

---

## The Workaround, and Why `0`

Setting `OPENSSL_armcap` makes `OPENSSL_cpuid_setup` skip detection entirely and take the value as given:

```c
if ((e = getenv("OPENSSL_armcap"))) {
    OPENSSL_armcap_P = (unsigned int)strtoul(e, NULL, 0);
    return;
}
```

The value is read when `libcrypto` is `dlopen`ed rather than at process start, so setting it from `conftest.py` — before any test module imports `cryptography` — is early enough. Measured on an affected host, through the wheel:

| `OPENSSL_armcap` | Result | AES-256-GCM |
|---|---|---|
| unset | **SIGILL** | — |
| `0` | works | 0.09 GB/s |
| `0x1fff` (all but SVE/SVE2) | works | 3.77 GB/s |
| `0x3fff` | works | 3.98 GB/s |

The masks are faster, and that is deliberately not the point. `0` means "no acceleration" in every OpenSSL version, forever. A hardcoded non-zero mask asserts a specific bit layout from `arm_arch.h`, and if those bits are ever renumbered the mask can switch **on** a feature the CPU lacks — reintroducing the exact crash it was meant to prevent. The Python suite asserts nothing about crypto throughput and completes in roughly 30 seconds either way, so the mask would trade a measurable-in-principle, irrelevant-in-practice speedup for a latent version-coupled failure.

Two related footguns:

- **`OPENSSL_armcap=` (empty) is not "unset".** `getenv` returns a non-NULL empty string, `strtoul("")` yields 0, and detection is skipped — so an empty assignment silently disables all acceleration.
- **Setting the variable skips everything after the early return,** including any derived-capability logic, which is a second reason not to hand-compute a replacement value.

---

## Diagnosis

Confirm the machine is affected:

```bash
grep -o 'sve[a-z0-9]*' /proc/cpuinfo | sort -u        # 'sve2' without 'sve' == affected
```

Confirm the crash and the fix in isolation:

```bash
.venv/bin/python -c "import cryptography.x509"                      # SIGILL on an affected host
OPENSSL_armcap=0 .venv/bin/python -c "import cryptography.x509"     # succeeds
```

Identify which OpenSSL a wheel actually bundles:

```bash
strings .venv/lib*/python3*/site-packages/cryptography/hazmat/bindings/_rust.abi3.so \
    | grep -E '^OpenSSL [0-9]'
nm .venv/lib*/python3*/site-packages/cryptography/hazmat/bindings/_rust.abi3.so \
    | grep sve_get_vl_bytes                                          # present == vulnerable to this
```

Note that the Python version is not a variable here: the wheel is tagged `cp39-abi3`, so every Python 3.9+ interpreter installs the same binary with the same bundled OpenSSL.

---

## Upstream

The underlying defect belongs to the hypervisor, which should not advertise SVE2 on a core with no SVE. The guard in `conftest.py` is self-retiring: if the hypervisor is fixed, or the guest kernel starts sanitizing `ID_AA64ZFR0_EL1`, the condition stops matching and the variable stops being set, with no code change required.
