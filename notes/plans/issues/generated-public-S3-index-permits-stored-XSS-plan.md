# Plan: Fix F-03 S3 Index XSS

TL;DR: `generate_html_index` and `generate_markdown_index` concatenate `url_prefix + key` into links without URL encoding or quote escaping, so an attacker-controlled S3 key can break out of `href` / Markdown `()` and run script in a publicly hosted index. Percent-encode each key as URL path data, HTML-escape the full attribute and visible labels, apply Markdown-safe label rules, and reject `--url-prefix` unless it is a well-formed `http`/`https` URL that cannot break Markdown destinations. Include every listed key after encoding.

Confirmed decisions:
- Scope is F-03 only. Do not mix F-01 download path logic, F-04 listing/upload exit codes, Content-Type on upload, CSP, host allowlists, browser/CDN link-mapping verification, or style-only churn.
- Harden HTML and Markdown together. The same command publishes both files.
- `--url-prefix` is operator-controlled. Approved origins are **any** `http` or `https` origin with a host. There is no host allowlist and no same-origin constraint against the S3 API endpoint. Reject `javascript:`, `data:`, `file:`, relative prefixes, userinfo, query, fragment, controls, ASCII whitespace, backslash, Markdown/HTML destination delimiters, empty/malformed ports, and malformed `%` escapes. Fail closed. Do **not** silently percent-encode the operator prefix.
- Percent-encode keys and escape labels/attributes; then include every listed key. Do not skip keys and do not abort `indexupload` because a key contains quotes, controls, Unicode, fragments, `.` / `..`, or leading/repeated slashes.
- Encode keys with `urllib.parse.quote(..., safe='/')`. Nested `/` stays a path separator. Do **not** special-case `.` / `..` as `%2E` / `%2E%2E`: the WHATWG URL Standard treats `%2e` / `%2e%2e` (ASCII case-insensitive, including `.%2e` / `%2e.`) as single- and double-dot path segments, so browsers still normalize them and can walk out of the prefix **path**. That cannot change scheme or origin, so it is not the original stored XSS. Exact browser/CDN mapping for those keys is a separate compatibility issue, not an F-03 remedy. Do not encode `/` as `%2F` either: whether the origin decodes `%2F` back to the S3 key is CDN/server-dependent and must not be assumed.
- Residual operator risk is a hostile `--url-prefix`; that is rejected separately. Encoded keys cannot introduce a scheme. A prefix containing `)` is operator-controlled, not the original S3-key XSS path; still reject it so Markdown `](url)` stays structurally closed.
- Validate `--url-prefix` at the start of `command_indexupload`, **before** `boto3.client` construction and before listing.

## Steps

### Phase 1 — URL helpers (no S3 I/O)

1. In scripts/s3_manage.py, add `InvalidIndexUrlPrefixError(ValueError)` next to the index generators (do not reuse `UnsafeDownloadPathError`).
2. Add `validate_index_url_prefix(url_prefix)` that returns the normalized prefix **generators will actually concatenate**. Inspect the **raw string first**, then parse. Do not strip-and-accept.
   - Reject `None`, non-str, and empty with `InvalidIndexUrlPrefixError` (not `TypeError` for `None`/empty).
   - Reject if the raw string contains: C0 controls (`0x00–0x1F`), DEL (`0x7F`), ASCII whitespace (space/tab/LF/VT/FF/CR), `\`, `(`, `)`, `<`, `>`, `"`, `'`.
   - Reject a `%` that is not exactly `%` + two hex digits (`[0-9A-Fa-f]{2}`). Do not decode then re-encode the prefix.
   - Then `urllib.parse.urlsplit`. Catch `ValueError` from `urlsplit`, `.hostname`, and `.port` and turn it into `InvalidIndexUrlPrefixError`.
   - Scheme must be `http` or `https` (already lowercased by `urlsplit`).
   - Require a non-empty hostname. Reject missing host, `//evil.example`, `https:///path`, and protocol-relative URLs.
   - Reject userinfo (`username` or `password` not `None`, or `@` in `netloc`).
   - Reject literal query and fragment delimiters even when their components are empty (for example, trailing `?`, `#`, or `?#`). A `#` would swallow the key and a `?` would turn the key into a query. Percent-encoded `%3F` / `%23` remain valid path data.
   - Force port validation by reading `.port`. Reject nonnumeric and out-of-range ports via that `ValueError`. Reject empty ports (`https://example.com:/path`, `http://[::1]:/path`) even when `.port` is `None`.
   - Allow path, non-empty valid port, and IPv6 literals (`http://[::1]:9000/bucket/`).
   - Rebuild and return `scheme://netloc + path`, lowercasing only the scheme, preserving host/path spelling, and appending `/` if missing. This replaces the duplicated trailing-slash logic in both generators.
3. Add `encode_s3_key_for_url(key)`:
   - Require `str` (production S3 keys are `str`).
   - `urllib.parse.quote(key, safe='/', encoding='utf-8', errors='strict')`. Do not use `quote_plus`. Do not encode `/` as `%2F`. Do not rewrite `.` / `..` segments as `%2E` / `%2E%2E`.
   - Empty segments stay empty, so `foo//bar`, `/file`, and `//file` keep their slashes. Document that a leading `/` on the key plus a trailing `/` on the prefix yields a double slash (`…/bucket/` + `/file` → `…/bucket//file`); still include the key.
   - Unreserved characters stay unencoded (including `.`); `"`, `'`, space, `#`, `?`, `&`, `<`, `>`, `(`, `)`, and `%` are encoded. Literal `%` becomes `%25`. A key whose path segments are exactly `.` or `..` therefore remains `.` / `..` in the generated URL. That is intentional for F-03: source encoding is not a browser-normalization fix.
4. Add `build_index_download_url(url_prefix, key)`: validate/normalize the prefix, encode the key, concatenate. Never concatenate the raw key. There is no relative-URL fallback when prefix is missing.

### Phase 2 — HTML and Markdown generators

5. Change `generate_html_index` (scripts/s3_manage.py, currently around the `url_prefix + key` join and the hand-rolled `& < >` replace):
   - Call `validate_index_url_prefix` once up front (empty `objects` still validates).
   - Call `build_index_download_url` once per object.
   - Set `href` to `html.escape(download_url, quote=True)` inside double quotes. Do not keep the manual `.replace('&'/'<'/'>')` path.
   - Set visible link text to `html.escape(key, quote=True)` so labels also escape quotes.
   - Leave size and last-modified as today (not key-controlled).
6. Change `generate_markdown_index`:
   - Same up-front prefix validation and the same `build_index_download_url` so HTML and Markdown URLs match for a given key.
   - Keep the existing `[label](url)` shape. Do not wrap destinations in `<>`.
   - After encoding, `)` / `(` in the **key** are `%29` / `%28`. Combined with prefix rejection of `()`, the complete Markdown destination cannot close early.
   - Build the visible label by: replacing CR/LF/TAB with a single space (one key stays one table row); `html.escape(..., quote=True)` so HTML-capable Markdown viewers do not interpret `<script>`; then escape `\`, `[`, `]`, and `|` for Markdown/table syntax (`\` first). Keep today’s `\|` behavior for pipes.
7. Both generators must raise `InvalidIndexUrlPrefixError` for a bad prefix even when `objects` is empty.

### Phase 3 — Command boundary

8. In `command_indexupload`, call `validate_index_url_prefix(args.url_prefix)` **before** the `if s3_client is None: boto3.client(...)` block (scripts/s3_manage.py currently constructs the client at ~1071) and before listing. On `InvalidIndexUrlPrefixError`, print `[ERROR]` with the reason, then `sys.exit(1)`. Do not construct a client, list, generate, or upload.
9. Do not change listing-error handling, empty-bucket behavior, index.html/index.md exclusion, or upload failure handling (those are F-04). Empty bucket with a *valid* prefix still prints “No index files will be generated” and returns.
10. Do not reuse F-01 `validate_s3_key_components` to filter index keys (including `../` keys). Do not set `ContentType` on `upload_file` in this change.
11. Leave argparse `--url-prefix` required; do not add an argparse `type=` wrapper. Shared validator stays unit-testable.

### Phase 4 — Tests

12. Extend `TestIndexGeneration` in scripts/tests/test_s3_manage_unit.py (pure, no moto). Keep existing safe-key assertions: `href="https://example.com/file1.txt"` and `[file1.txt](https://example.com/file1.txt)` must still hold.
13. Replace `test_generate_html_special_characters`. The current `assert "file<test>.txt" not in html or "<td>" in html` is vacuous because the table always contains `<td>`.
14. Add helper tests (new class next to index generation is fine):
    - Prefix accept: `http` and `https`, with and without trailing slash, with port, with path, scheme case `HTTP`, IPv6 literal with port, and percent-encoded `%3F` / `%23` path data.
    - Prefix reject: `javascript:alert(1)`, `data:text/html,...`, `file:///tmp`, empty, None, whitespace-only, leading space before `https://`, space in the path, relative `/bucket/`, protocol-relative `//cdn.example/`, userinfo `https://u:p@host/`, non-empty query `?x=1`, trailing empty query `?`, non-empty fragment `#x`, trailing empty fragment `#`, combined empty `?#`, backslash, NUL, `)`, `(`, `<`, `>`, `"`, `'`, malformed `%` (`https://example.com/%zz/`, `https://example.com/%2`), empty port, nonnumeric port.
    - Returned prefix is the exact string generators concatenate (scheme lowercased, trailing `/` present).
    - `build_index_download_url` concatenates that normalized prefix + encoded key only.
15. Hostile-key HTML tests (include the key; never omit). For **single-object** hostile cases, parse with stdlib `html.parser.HTMLParser`:
    - Exactly one `<a>` in that document.
    - Its only attribute is `href`.
    - The parsed `href` equals the exact percent-encoded URL (`HTMLParser` returns the unescaped attribute value).
    - Visible text is the escaped/original label as specified — do **not** assert that substrings such as `onclick` are absent; they may legitimately appear in escaped visible text.
    - Quote breakout: key `foo"><script>alert(1)</script>` — parsed href has `%22`; raw `<script>` is not an extra tag.
    - `& < >` in the label become entities; href uses `%26` `%3C` `%3E`.
    - Fragment: key `file.txt#xss` → path contains `%23xss`, not a URL fragment.
    - Query-like: key `file.txt?x=1` → `%3F`, not a query string.
    - Scheme-like key: `javascript:alert(1)` → `javascript%3A...`, not a `javascript:` URL.
    - Controls in the key: NUL, tab, CR, LF, form feed, DEL — encoded in href (`%00`, `%09`, `%0D`, `%0A`, `%0C`, `%7F`); still one well-formed `href`.
    - Unicode: `café.txt` percent-encoded as UTF-8 in href; visible label keeps the original characters after HTML escaping (é does not need entities).
    - Nested path `folder/file.txt` keeps the slash unencoded.
    - `%` in a key is `%25` (no double-decode trap).
    - Dot-segments and slashes (included, not omitted). Assert **generated source** only — these tests do **not** claim browser navigation stays under the prefix path, and they must not encode `.`/`..` as `%2E`/`%2E%2E` or `/` as `%2F`:
      - `../file` → `../file`
      - `a/../b` → `a/../b`
      - `./file` → `./file`
      - `/file` → `/file` (double slash after the prefix)
      - `//file` → `//file`
      - `foo//bar` → `foo//bar`
    - Do not add browser-level or live-origin tests in this change. Exact click-through mapping for `.`/`..`/`%2F` keys is out of F-03 scope.
16. Hostile-key Markdown tests: parameterize the same hostile-key matrix used for HTML so coverage cannot drift; every destination URL **equals** the HTML encoded URL. Verify script-like labels are HTML-escaped, `\` / `|` / `[` / `]` are Markdown-escaped, newline/CR/tab do not split the table row, a `javascript:` key does not produce a javascript URL, and `)` in a key is `%29`. Prefix-reject cases above must not appear as a Markdown breakout (`[file](https://example.com/)<img...>/file)`).
17. Generator-level invalid prefix: `generate_html_index` and `generate_markdown_index` raise `InvalidIndexUrlPrefixError` for `javascript:...` even with `objects=[]`.
18. Update `TestCommandIndexupload` cases that pass `url_prefix: None` to pass a valid `https://...` prefix (CLI already requires it). Command-level invalid prefix:
    - Prints `[ERROR]`, `SystemExit` code 1.
    - With injected `s3_client`: does not list, does not call either generator, does not upload.
    - With `s3_client is None`: does not call `boto3.client` (patch it).
    - Do not change empty-bucket / listing-error exit contracts beyond the prefix check.

### Phase 5 — Docs

19. In scripts/s3_manage.md:
    - Replace the “Character Escaping” claim that HTML only maps `& < >` and that this “Prevents XSS”. Document percent-encoding of keys, `html.escape(..., quote=True)` for href and labels, Markdown label/table escaping, and that every listed key is included. Document that `.` / `..` path segments are left as-is (WHATWG still treats `%2e` as a dot-segment); browsers may normalize those links out of the prefix path without changing scheme/origin. Do not claim that encoding prevents that.
    - Document `--url-prefix` policy under URL Prefix Handling: any operator-selected `http`/`https` origin with a host; no host allowlist; no userinfo/query/fragment; no destination-delimiter/whitespace/malformed-`%` characters; trailing slash still added; invalid prefix fails **before client construction and listing** with exit 1.
    - Add a Security Notes bullet: index files treat S3 keys as untrusted; encoded keys cannot introduce a scheme or HTML/Markdown breakout; operators must not pass a hostile prefix (rejected if they do); documented static-hosting use is why this is stored XSS, not a console-only issue. Note that `.`/`..` keys can still be path-normalized by browsers; that is link correctness, not XSS.
    - Features table line that says HTML (`&<>`) and Markdown (`|`) only: update to match.

## Relevant files

- scripts/s3_manage.py — `generate_html_index` (~960–1023), `generate_markdown_index` (~1025–1059), `command_indexupload` (~1061–1163, client at ~1071), `--url-prefix` argparse (~1447–1448). Add `InvalidIndexUrlPrefixError`, `validate_index_url_prefix`, `encode_s3_key_for_url`, `build_index_download_url`. Import `html` and `urllib.parse` at module level (logic change, not a drive-by import reorder of unrelated names).
- scripts/tests/test_s3_manage_unit.py — `TestIndexGeneration` (~582–745), including vacuous `test_generate_html_special_characters` (~612–624); `TestCommandIndexupload` (~2509–2734) `url_prefix: None` fixtures.
- scripts/s3_manage.md — indexupload usage (~413+), Character Escaping (~561–571), URL Prefix Handling (~546–552), Security Notes (~1043+), Features escaping bullet (~437).

## Verification

1. Interpreter: project `.venv`, i.e. `.venv/bin/python` relative to the repository root. If missing, `make pytest-install`.
2. `.venv/bin/python -m pytest scripts/tests/test_s3_manage_unit.py -v` — the **complete** module, not only `TestIndexGeneration` / `TestCommandIndexupload`.
3. Single-object HTMLParser assertion: key `x" onclick="alert(1)` yields exactly one `<a>`, `href` only, parsed href is the percent-encoded URL, no extra tags.
4. `generate_html_index` / `generate_markdown_index` with prefix `javascript:alert(1)` and with prefix `https://example.com/)<img src=x>` raise `InvalidIndexUrlPrefixError` even when `objects` is empty.
5. Existing safe nested-path and trailing-slash prefix tests still pass.

## Decisions

- F-03 only; HTML and Markdown together.
- Approved origins = any operator-selected `http`/`https` origin with a host. No allowlist. No same-origin check against `--endpoint-url`.
- Reject unsafe prefix characters rather than percent-encoding the operator prefix.
- Encode and include every listed key with `quote(..., safe='/')`. Do not rewrite `.`/`..` as `%2E`/`%2E%2E` and do not encode `/` as `%2F`. Do not filter with the F-01 download validator. Source-string tests for those keys prove inclusion/encoding only, not browser navigation.
- `html.escape(..., quote=True)` for the full href and HTML labels. No hand-rolled entity tables.
- Raw-string prefix checks **before** `urlsplit`; catch `ValueError` from `urlsplit` / `.hostname` / `.port`.
- Invalid prefix: command `sys.exit(1)` before client construction and listing. Empty bucket with a valid prefix stays a non-error no-op.
- Hostile HTML tests parse with `HTMLParser`; do not use “substring absent” as the XSS oracle.
- Do not add Content-Type, CSP, or argparse type converters.

## Out of scope (review items not adopted)

- Host/origin allowlist or same-origin constraint against the S3 endpoint. The original “approved schemes/origins” requirement is met by operator-selected `http`/`https` with a host; document that threat model instead of adding allowlist flags.
- Silently percent-encoding / canonicalizing the operator prefix path while “preserving valid `%HH`”. Reject delimiters, whitespace, controls, and malformed `%` instead so operator URLs are not rewritten.
- Skipping, unlinking, or aborting on keys with `.`, `..`, leading `/`, or repeated slashes. Those are not the original quote-XSS; include every key.
- Encoding `.`/`..` as `%2E`/`%2E%2E` as an F-03 remedy. WHATWG still classifies those as dot-segments; the claim that this stops `remove_dot_segments` is false.
- Encoding `/` as `%2F` (`..%2Ffile`) to dodge browser dot-segment recognition. Origin/CDN decoding of `%2F` is deployment-dependent and must not be assumed.
- Browser-level or live-origin verification that every generated href fetches the intended S3 object. Classify exact `.`/`..` click-through mapping as a separate compatibility issue.
- Using F-01 `validate_s3_key_components` inside index generation.
- Requiring `HTMLParser` “exactly one anchor” on empty or multi-file indexes.
- F-04 false-success listing/upload exits (except the new invalid-prefix exit).
- Setting `ContentType` on index upload, CSP, argparse `type=` wrappers, third-party sanitizers.