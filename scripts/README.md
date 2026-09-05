# Scripts

## Compiler Wrappers: cl.py and clang-cl.py

These scripts wrap MSVC `cl.exe` and `clang-cl.exe` to parse `/showIncludes` output and
generate make-compatible `.d` dependency files. They must support both Python 2.7 and
Python 3.6+.

### Non-ASCII Encoding in Compiler Output

**Status:** Fixed (Option C). Option B below is retained for historical context.

#### The Problem

When `cl.exe` outputs file paths containing non-ASCII characters (e.g., accented usernames
like `C:\Users\Jose\header.h`), Python's `subprocess.Popen` can fail with:

```
UnicodeDecodeError: 'ascii' codec can't decode byte 0xe9 in position 42: ordinal not in range(128)
```

**Root cause:** `Popen(universal_newlines=True)` decodes subprocess output using
`locale.getpreferredencoding(False)`. In MSYS2/make environments on Windows, this often
resolves to ASCII. Any byte > 127 from the compiler triggers a `UnicodeDecodeError`.

On Python 2.7, the same error can occur when byte strings containing values > 127 are
implicitly converted to unicode through string operations or piped stdout.

See also:
- [CPython issue #27179](https://bugs.python.org/issue27179) - subprocess uses wrong
  encoding on Windows
- [CPython issue #6135](https://bugs.python.org/issue6135) - subprocess local encoding

#### Superseded Fix (Option B): Decode with `errors='replace'`

The first fix removed `universal_newlines=True` and decoded bytes with replacement:

```python
p = Popen(args, stdout=PIPE, stderr=STDOUT)
for raw_line in p.stdout:
    if isinstance(raw_line, bytes):
        line = raw_line.decode('utf-8', errors='replace')
    else:
        line = raw_line
    line = line.rstrip()
```

**Why it was replaced:**
- It prevented decode crashes, but non-ASCII characters became U+FFFD (replacement
  character), so dependency file paths containing non-ASCII characters were written
  incorrectly and `make` could not resolve those headers
- Stale objects could therefore be reused because the dependency edge was missing

#### Current Fix (Option C): Lossless Decode with `latin-1`

For lossless byte preservation, `latin-1` encoding maps bytes 0-255 to unicode code points
0-255 bijectively. This means round-tripping (decode then encode) preserves the original
bytes exactly, so dependency file paths remain correct even with non-ASCII characters.

The implementation requires:

1. Decode with `latin-1` instead of `utf-8`:
   ```python
   p = Popen(args, stdout=PIPE, stderr=STDOUT)
   for raw_line in p.stdout:
       if isinstance(raw_line, bytes):
           line = raw_line.decode('latin-1')
       else:
           line = raw_line
       line = line.rstrip()
   ```

2. Change dependency file writing to binary mode and encode back to `latin-1`:
   ```python
   f = open('%s.d' % splitext(options.target)[0], 'wb')
   f.write((options.target + ': \\\n').encode('latin-1'))
   for dep in deps:
       f.write((' %s \\\n' % dep).encode('latin-1'))
   f.write(b'\n\n')
   for dep in deps:
       f.write(('%s:\n' % dep).encode('latin-1'))
   f.close()
   ```

**Trade-offs of Option C:**
- Lossless: dependency file paths are byte-exact, `make` can find all headers
- Works in both Python 2.7 and Python 3.6+
- Display of non-ASCII paths may appear garbled (e.g., CJK characters displayed as
  latin-1 code points), but the `.d` file content is correct
- More code changes than Option B (file open mode, encode on write)

The wrappers also reconfigure `stdout` to UTF-8 with `errors='replace'` before running the
compiler (the same approach `debug_harness.py` uses), so echoing a latin-1 decoded line to an
ASCII stdout under MSYS2 or make cannot raise `UnicodeEncodeError`.

For pure-ASCII compiler output `latin-1` and `utf-8` produce byte-identical results, so this
change has no effect on builds whose include paths are all ASCII.

**`debug_harness.py` decodes differently, deliberately.** It tries `utf-8` first and falls back to
`latin-1`, rather than using `latin-1` unconditionally. It writes no `.d` file, so it has no
byte-exact round-trip to protect; its output only ever reaches a UTF-8 stdout. Decoding genuine
UTF-8 test output as `latin-1` would mojibake it, which on Linux and macOS is the common case rather
than the exceptional one. Both branches are lossless and neither can raise, so nothing is replaced
with U+FFFD in either script.
