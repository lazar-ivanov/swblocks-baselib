# Scripts

## Compiler Wrappers: cl.py and clang-cl.py

These scripts wrap MSVC `cl.exe` and `clang-cl.exe` to parse `/showIncludes` output and
generate make-compatible `.d` dependency files. They must support both Python 2.7 and
Python 3.6+.

### Known Limitation: Non-ASCII Encoding in Compiler Output

**Status:** Partial fix implemented (Option B). See "Future Improvement" below for the
complete fix (Option C).

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

#### Current Fix (Option B): Decode with `errors='replace'`

The current implementation removes `universal_newlines=True` and decodes bytes manually:

```python
p = Popen(args, stdout=PIPE, stderr=STDOUT)
for raw_line in p.stdout:
    if isinstance(raw_line, bytes):
        line = raw_line.decode('utf-8', errors='replace')
    else:
        line = raw_line
    line = line.rstrip()
```

**Trade-offs:**
- Works in both Python 2.7 and Python 3.6+
- Prevents crashes on non-ASCII output
- Non-ASCII characters are replaced with U+FFFD (replacement character), so dependency
  file paths containing non-ASCII characters become incorrect and `make` cannot find those
  headers
- This is acceptable because non-ASCII paths in compiler include directories are rare

#### Future Improvement (Option C): Lossless Decode with `latin-1`

For lossless byte preservation, `latin-1` encoding maps bytes 0-255 to unicode code points
0-255 bijectively. This means round-tripping (decode then encode) preserves the original
bytes exactly, so dependency file paths remain correct even with non-ASCII characters.

The implementation would require:

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

This improvement should be implemented if non-ASCII paths in include directories become
a requirement.
