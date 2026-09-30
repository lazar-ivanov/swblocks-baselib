# Capturing what real browsers send

`capture.py` is a small HTTPS server that a real browser visits by hand. It records exactly what the
browser sends: the TLS ClientHello as it arrived at the socket, the opening of the HTTP/2 connection,
and the headers of four requests - the page (a navigation), a script and an image (two subresources),
and a `fetch`. A second run offers only HTTP/1.1, for the header casing and order over HTTP/1.1.

The browser profiles of the HTTP/2 client's impersonation layer (L7) are derived from these recordings,
never from memory. This page is the whole procedure for capture day. It takes about fifteen minutes to
set up a machine, then about fifteen minutes per browser, most of it waiting.

**Which browsers, where** (the plan's D-L7-1):

| Machine | Browsers |
|---|---|
| Windows | Chrome, Edge, Firefox |
| Mac | Safari, Edge |

For each browser: one **HTTP/2 run** with two visits, then one **HTTP/1.1 run** with one visit.

The tool needs only Python 3.8 or later and its standard library. It never starts or drives a browser,
and it listens only on `127.0.0.1`: nothing it does leaves the machine.

---

## 1. Set up the machine (once, about fifteen minutes)

### 1.1 A working folder, and the tool in it

Use a folder **outside** any git checkout (the tool refuses to write keys inside one), and copy
`capture.py` from `scripts/impersonation/` of a swblocks-baselib checkout into it.

Every Windows command on this page is for **Command Prompt** (`cmd`), not PowerShell: in PowerShell,
`start` means something else and `%USERPROFILE%` does not expand. Windows 11's Terminal opens PowerShell
by default; type `cmd` there first.

- **Windows**, in Command Prompt:
  ```
  mkdir "%USERPROFILE%\capture"
  cd /d "%USERPROFILE%\capture"
  copy "C:\path\to\swblocks-baselib\scripts\impersonation\capture.py" .
  ```
- **Mac**, in Terminal:
  ```
  mkdir -p ~/capture && cd ~/capture
  cp /path/to/swblocks-baselib/scripts/impersonation/capture.py .
  ```

Every command below runs in this folder.

### 1.2 Python

- **Windows:** install Python 3.12 or later from python.org. Then:
  `py -3 capture.py check`
- **Mac:** install Python from python.org, or run `brew install python` with Homebrew. Do not rely on
  Apple's own `/usr/bin/python3`: it may lack what the capture needs, and the check below will say so.
  Then:
  `python3 capture.py check`

It prints the Python and OpenSSL versions, and then `This Python can capture.` If it says
`This Python cannot capture`, run it with the Python you just installed, by its full path - for example
`/Library/Frameworks/Python.framework/Versions/3.12/bin/python3` or `/opt/homebrew/bin/python3` on a Mac.

The rest of this page writes `py -3` (Windows). On a Mac, read `python3` instead.

### 1.3 The name `capture.test`

The browser must visit a name, not `127.0.0.1` or `localhost`: an address sends no server name in the
ClientHello, and browsers treat `localhost` specially. Add one line to the machine's hosts file:

```
127.0.0.1 capture.test
```

- **Windows:** start Notepad with *Run as administrator*. Open
  `C:\Windows\System32\drivers\etc\hosts` (choose *All files* in the dialog). Add the line, save, then
  run `ipconfig /flushdns` in Command Prompt.
- **Mac:** `sudo nano /etc/hosts`, add the line, save. Then run
  `sudo dscacheutil -flushcache; sudo killall -HUP mDNSResponder`.

Check with `ping capture.test`: it must answer from `127.0.0.1`.

### 1.4 A certificate, trusted for this session only

```
py -3 capture.py certs --dir certs
```

This runs the `openssl` command to make a new certificate authority (CA) and a certificate for
`capture.test`. On Windows, `openssl` comes with Git for Windows, and the tool finds it there. If it
does not, add `--openssl "C:\Program Files\Git\usr\bin\openssl.exe"`. On a Mac the built-in one is used;
if it fails, install Homebrew's `openssl@3` and pass `--openssl /opt/homebrew/opt/openssl@3/bin/openssl`
(`/usr/local/opt/openssl@3/bin/openssl` on an Intel Mac).

The CA's private key is deleted as soon as it has signed the certificate. So the CA you trust can never
sign anything else. The command prints the CA's name and SHA-1 fingerprint, which the clean-up needs.
They are also in `certs\ca-info.json`.

Trust the CA:

- **Windows** (Chrome and Edge use this). Windows asks you to confirm; answer *Yes*:
  ```
  certutil -user -addstore Root "%USERPROFILE%\capture\certs\ca.pem"
  ```
- **Mac** (Safari and Edge use this). macOS asks for your password:
  ```
  security add-trusted-cert -r trustRoot -k ~/Library/Keychains/login.keychain-db ~/capture/certs/ca.pem
  ```
- **Firefox** keeps its own list. It is set up per profile, in step 2.1.

### 1.5 Nothing between the browser and the tool

Turn off any VPN, any proxy, and any antivirus HTTPS or SSL scanning ("web shield", "scan encrypted
connections") for the session. Such software can answer the browser itself and send the tool its own
ClientHello instead of the browser's. The report catches some of its signs (section 5), not all of them.

---

## 2. Capture one browser

### 2.1 Start the browser with a fresh profile, and note its exact version

A fresh profile has no extensions, cookies or settings that would add headers. Start the browser as
below; each command makes a new, separate profile and leaves your normal one alone. Dismiss the
first-run screens, **do not sign in**, and change nothing.

| Browser | Start it with a fresh profile | Its exact version |
|---|---|---|
| Chrome (Windows) | `start "" chrome --user-data-dir="%USERPROFILE%\capture\profiles\chrome"` | open `chrome://version`, copy the first line |
| Edge (Windows) | `start "" msedge --user-data-dir="%USERPROFILE%\capture\profiles\edge"` | open `edge://version`, copy the first line |
| Firefox (Windows) | `mkdir "%USERPROFILE%\capture\profiles\firefox"`, then `start "" firefox -no-remote -profile "%USERPROFILE%\capture\profiles\firefox"` | menu > Help > About Firefox |
| Safari (Mac) | Safari > Settings > Profiles > *New Profile*, named `capture`; then File > New Window > *New capture Window*. Safari before 17 has no profiles: clear its history and website data instead | Safari > About Safari |
| Edge (Mac) | `open -na "Microsoft Edge" --args --user-data-dir="$HOME/capture/profiles/edge"` | open `edge://version`, copy the first line |

`start ""` finds each browser wherever it is installed. Never start the browser from the Start menu or the
taskbar for a capture: that opens your own profile.

Write the version down exactly, for example
`Google Chrome 131.0.6778.86 (Official Build) (64-bit)` or `Safari 18.0 (20619.1.26.31.6)`.

**Firefox only:** import the CA into this profile. Go to Settings > Privacy & Security > Certificates >
*View Certificates...* > *Authorities* > *Import...*, choose `certs\ca.pem`, and tick *Trust this CA to
identify websites*.

### 2.2 The HTTP/2 run: two visits

1. Start the tool, with the version you wrote down and a new output folder:
   ```
   py -3 capture.py serve --browser "Google Chrome 131.0.6778.86 (Official Build) (64-bit)" --cert-dir certs --out chrome-h2
   ```
   It prints the address to type. If it cannot listen on port 443, add `--port 8443` and use the
   address it prints then, `https://capture.test:8443/`.
2. **Visit 1.** Click the browser's address bar, **type** `https://capture.test/` and press Enter. Do not
   use a link or a bookmark, and do not reload: how the page was reached changes the headers. Wait until
   the page says **Capture complete**, or *Not all on one connection*, which is still a good capture.
3. Leave the browser open and idle for **about five minutes**. A new profile fetches its field-trial
   settings in that time.
4. **Quit the browser completely**: close every window of this profile, or press Cmd-Q on a Mac. On
   Windows, look in Task Manager: no `msedge.exe` or `chrome.exe` may be left running, so keep your own
   windows of that browser closed during its capture. If Edge keeps running, turn off *Startup boost* at
   `edge://settings/system` and quit again. Leave the tool running. A browser still running at visit 2
   reuses visit 1's connection, and the report then says so.
5. **Visit 2.** Start the browser again with the same command, so the same profile; for Safari, start it
   and open a *New capture Window*. Type the address again, wait for **Capture complete**, then quit the
   browser.
6. In the tool's window, press **Ctrl-C**. It closes its connections gently, which takes up to five
   seconds, and prints its report.

A good report looks like this:

```
Google Chrome 131.0.6778.86 (Official Build) (64-bit): 2 connection(s), 2 ClientHello(s)
JA4 identical across every hello: yes, t13d...(the browser's own value)
Navigation on connection 1, ALPN h2: the script, the image and the fetch on the same connection
Navigation on connection 2, ALPN h2: the script, the image and the fetch on the same connection
Lingering closes: the client closed
```

More connections than visits is normal: browsers open spare ones. Every connection is recorded, and the
report marks the ones that carried a navigation. `the client reset the connection` among the lingering
closes is normal too: a browser that quits may reset its connections. A line beginning **PROBLEM** is
explained in section 5. A line beginning `Note:` is information only. Keep the capture either way.

### 2.3 The HTTP/1.1 run: one visit

```
py -3 capture.py serve --browser "<the same version>" --cert-dir certs --out chrome-http1 --http1
```

Start the browser again with the same command (for Safari, open a *New capture Window*). Type the address,
wait for **Capture complete (http/1.1 pass)**, quit the browser, and press Ctrl-C in the tool's window. Over
HTTP/1.1 a browser spreads its requests across several connections, so the report's "not on this
connection" lines are expected here.

### 2.4 The next browser

Repeat section 2 for each browser in the table at the top, each with its own output folders: for
example `edge-h2` and `edge-http1`, `firefox-h2` and `firefox-http1`, `safari-h2` and `safari-http1`,
`edge-mac-h2` and `edge-mac-http1`.

---

## 3. What to send back

Zip the output folders - `chrome-h2`, `chrome-http1` and the rest - into one file per machine.
Do **not** include `certs` or `profiles`.

Each folder holds only what your browser sent to this tool, and what the tool recorded of the run. The
parts of it that say anything about you or your machine are:
- your browser's own request headers: its user agent, client hints such as `sec-ch-ua-platform`, your
  preferred languages, and the two test cookies the tool set;
- the browser version you typed with `--browser`;
- the operating system version;
- the Python and OpenSSL versions;
- the times of the run.

Every connection's address is this machine's own `127.0.0.1`, and nothing from any other site is in it.
`summary.json` in each folder is readable if you want to look.

---

## 4. Clean up

When every browser on the machine is done:

- **Windows**
  1. Remove the CA: `certutil -user -delstore Root <SHA-1>`, with the SHA-1 printed by `certs` and kept in
     `certs\ca-info.json`.
  2. Remove the `capture.test` line from the hosts file, the same way it was added, and run
     `ipconfig /flushdns`.
  3. Delete `%USERPROFILE%\capture\certs` and `%USERPROFILE%\capture\profiles`. The Firefox profile, and
     the CA imported into it, go with it.
- **Mac**
  1. Remove the CA and its trust setting:
     `security delete-certificate -t -Z <SHA-1> ~/Library/Keychains/login.keychain-db`
  2. Remove the line from `/etc/hosts` with `sudo nano`, and flush as in 1.3.
  3. Delete `~/capture/certs` and `~/capture/profiles`, and delete the Safari profile `capture`
     (Settings > Profiles).

---

## 5. Troubleshooting

When a row says to repeat a run, start the browser with a **new** profile folder (2.1): a new
`--user-data-dir` or `-profile` path, or a new Safari profile, and in Firefox import the CA again. A
profile that has visited `capture.test` can load the page ahead of your Enter key, and a repeat in it can
show the same problem again.

| What you see | What it means, and what to do |
|---|---|
| `This Python cannot capture` | This Python's TLS library lacks TLS 1.3 or ALPN; the message says which. Use a python.org or Homebrew Python (1.2). |
| `no openssl command was found` | On Windows, install Git for Windows, or pass `--openssl` (1.4). |
| `cannot listen on 127.0.0.1 port 443` | Another program has port 443. Add `--port 8443` and type the address the tool prints. |
| `... is not empty: use a new directory` | Every run needs a new `--out` folder. |
| A certificate warning in the browser | That browser does not trust the CA. For Chrome, Edge and Safari, check the trust step in 1.4; for Firefox, check the import in 2.1. Quit and restart the browser, then visit again. Avoid clicking through the warning: that moves the page onto another connection. If you had to, say so when you send the capture. |
| The browser cannot find `capture.test` | Check that `ping capture.test` answers from 127.0.0.1. Turn off any VPN or proxy. In Firefox, turn off DNS over HTTPS for this profile. On a Mac, turn off iCloud Private Relay while capturing. |
| The page stays at *Waiting for the fetch...* | Do not reload. Quit the browser, press Ctrl-C in the tool, and start that run again with a new `--out` folder. |
| Windows asks whether Python may use the network | Either answer works. The tool only uses this machine's own `127.0.0.1`. |
| `PROBLEM: JA4 differs across the hellos` | Two different ClientHellos in one run. The likeliest cause is the one the second visit is there to catch: the browser changed its TLS offer between the visits, after fetching its field-trial settings. Less likely: a proxy or antivirus answered some connections (see 1.5), or two browsers or profiles visited one run. Do not repeat the run to make it go away: send it as it is, with a note of anything unusual. If another row tells you to repeat the run, such as `no navigation was recorded`, send this capture and repeat the run as well. |
| `PROBLEM: hello(s) carrying pre_shared_key` | A resumed connection. Its hello is never used as a profile source; the others are. Send it. |
| `PROBLEM: not every hello named capture.test` | The address typed was not `https://capture.test/`. |
| `PROBLEM: no navigation was recorded` | The page never loaded in this run. Repeat the run. |
| `PROBLEM: only N visit(s) made a new connection` | The browser was still running at visit 2, so visit 2 reused visit 1's connection and sent no new ClientHello. Quit it completely (2.2, step 4), and repeat the HTTP/2 run with a new `--out` folder. N is 0 only when no navigation came with a ClientHello the tool could use as a profile source: then send the capture as it is, with a note. |
| `PROBLEM: more than one navigation on connection(s) ...` | The same, or the page was reloaded. Repeat the run with a new `--out` folder. |
| `PROBLEM: visit 2 (connection ...) disagrees with visit 1 ...` | The browser changed what it sends between the visits, most likely after fetching its field-trial settings. That is a finding, not a mistake: send the capture as it is, with a note. |
| `PROBLEM: on connection ..., the navigation was not typed into the address bar ...` | The page came from a link, or the browser made the navigation without you - a restored tab, or the page loaded ahead of your Enter key. If you did type the address and press Enter, send the capture as it is, with a note of the browser. If this is only on visit 2's connection - the second `Navigation on connection` line of an HTTP/2 run - the capture is still good: visit 1 is the profile source and visit 2 only the check. Send it, with a note. Otherwise repeat the run (see above), typing the whole address and pressing Enter (2.2, step 2). |
| `PROBLEM: on connection ..., the navigation was a reload ...` | The page was reloaded. If this is only on visit 2's connection - the second `Navigation on connection` line of an HTTP/2 run - the capture is still good: visit 1 is the profile source and visit 2 only the check. Send it, with a note. Otherwise repeat the run (see above), without reloading. |
| `PROBLEM: on connection ..., the ... request is a prefetch or a prerender ...` | The browser loaded the page ahead of your Enter key, from its address-bar suggestions. If this is only on visit 2's connection - the second `Navigation on connection` line of an HTTP/2 run - the capture is still good: visit 1 is the profile source and visit 2 only the check. Send it, with a note. Otherwise repeat the run (see above): type the whole address, and press Enter at once. |
| `PROBLEM: something may stand between the browser and the tool on connection ...` | A proxy, a VPN or an antivirus's HTTPS scanning answered the browser; the line names the signs. Turn it off (1.5), and repeat the run. |
| `Note: cannot tell how the navigation ... was reached` | This browser sends no Sec-Fetch headers, so the tool cannot check how the page was reached. Nothing to do. |

---

## 6. Reference

### Commands

| Command | What it does |
|---|---|
| `check` | Prints Python's and OpenSSL's versions and the interpreter's path, and refuses without TLS 1.3 and ALPN. |
| `certs --dir DIR [--host capture.test] [--openssl PATH] [--days 7]` | Makes the session's CA and certificate with the `openssl` command, deletes the CA key, and refuses a folder inside a git checkout. |
| `serve --browser VERSION --cert-dir DIR [--out DIR] [--http1] [--host capture.test] [--listen 127.0.0.1] [--port 443] [--linger-seconds 5]` | The capture. Stop it with Ctrl-C. |
| `analyse DIR` | Derives `summary.json` and every `connection.json` again from a capture's stored bytes. |
| `hello FILE` | Parses one ClientHello and prints it with its JA3 and JA4, as JSON. The file is either a handshake message in the form `AsioSslStreamWrapper::enableClientHelloCapture()` produces, or TLS records. |

### What a capture folder holds

- `session.json`: the run - the browser version given, the host, the port, the pass, the ALPN offered,
  the server's SETTINGS, the linger bound, the Python and OpenSSL versions, and the operating system
  version. No path is recorded; a failure's error and traceback keep file names only.
- `conn-NNNN/` for **every** accepted connection, each holding:
  - `client-raw.bin`: every byte read from the socket, kept before OpenSSL saw any of it;
  - `client-hello.bin`: the ClientHello, reassembled from those bytes across reads and records, as a
    handshake message (type, three-byte length, body);
  - `client-plain.bin`: the decrypted client stream;
  - `meta.json`: what cannot be read from the bytes - timestamps, the negotiated TLS version, cipher and
    ALPN, how the connection ended, and the lingering close's outcome;
  - `connection.json`: everything derived from the bytes. That is:
    - the hello parsed, with GREASE stripped and counted, the extensions in order and as a sorted set,
      the key shares, JA3 and JA4 with their inputs, and whether it may be a profile source;
    - over HTTP/2, every frame in order, the opening (SETTINGS in order, WINDOW_UPDATE, PRIORITY), and
      each request's pseudo-header order and headers;
    - for each header block, every field's HPACK representation (indexed, incremental, without indexing,
      never indexed), its Huffman flags, and any table size update;
    - over HTTP/1.1, each request head exactly as sent.
- `summary.json`: the connections, the requests in arrival order with the connection that carried each,
  and the checks:
  - whether JA4 is identical across every hello, and the hellos refused as a profile source;
  - the navigation connections: visit 1's is the profile source, and each later visit's is compared with
    it on what the browser chose;
  - the visits made on connections of their own, and any connection carrying more than one navigation;
  - how each navigation was reached, and the signs of anything between the browser and the tool;
  - the selected ALPN, the SNI, and the lingering closes.

`analyse` derives all of it again from the stored bytes and gives the same result.

### The controls, and where they are

The plan review's F5 (a)-(k) and the Windows round's G4:

- **(a) Resumption.** The server context sets `OP_NO_TICKET` and `num_tickets = 0`. `OP_NO_TICKET` alone
  still lets TLS 1.3 send tickets; that was measured on OpenSSL 3.0.13. A hello carrying `pre_shared_key`
  (41) is refused as a profile source.
- **(b) Every connection** is recorded, a failed handshake's hello included, and the navigation's
  connection is marked.
- **(c) GREASE** is stripped by the rule `crypto/TlsClientHello.h` uses, and counted.
- **(d) The extension order** is kept, the extension set is derived, and JA4 is checked identical across
  every hello of the run.
- **(e) The host** is a hosts-file name: `--host` refuses IP literals, names ending in a number, and
  `localhost`, and the report checks each hello's SNI.
- **(f) Field trials:** two visits, a few minutes apart and across a restart, in one run. The report
  requires two visits on connections of their own. It compares what the browser chose on each: the
  SETTINGS in order, the connection WINDOW_UPDATE, the PRIORITY frames, the frames before the first
  HEADERS, and the navigation's HEADERS priority and pseudo-header order. HTTP/2 GREASE is treated as JA4
  treats TLS GREASE, and a different header-name order is a Note.
- **(g) ALPN** and which connection carried which request are in the summary.
- **(h) HPACK:** each field's representation and Huffman flags, and table size updates, are recorded.
  The first response sets two cookies, so crumbling shows.
- **(i) Request kinds:** a typed navigation, a script and an image (two destinations), and a `fetch`. The
  report checks from the requests' own headers that the navigation was typed, and that nothing was a
  prefetch, a prerender or a reload.
- **Interception** (1.5): a hello no browser would send, a navigation without h2, proxy headers, or
  Chromium's headers arriving on a hello without GREASE are reported.
- **(j) The HTTP/1.1 pass** is `--http1`.
- **(k) The interpreter:** `check`, and every `serve`, print `ssl.OPENSSL_VERSION` and refuse without
  TLS 1.3 and ALPN.
- **G4, the lingering close.** The tool never closes while the browser may still be sending. Every close
  is GOAWAY (on HTTP/1.1, `Connection: close` on a request answered after Ctrl-C), then close_notify,
  `shutdown( SHUT_WR )`, then reads - recorded, never answered - until the browser's end of stream or the
  bound, then close. On Windows a segment which meets a closed socket draws a reset, and the reset
  discards what the browser has not yet read.

### Tests

```
.venv/bin/pytest scripts/tests/test_impersonation_capture_unit.py scripts/tests/test_impersonation_capture_functional.py
```

- **The unit tests** check:
  - HPACK against RFC 7541 Appendix C, each block also one that `utf_baselib_h2core/TestHpack.h` decodes;
  - the Huffman code and the static table, row by row against that file;
  - JA3 and JA4 equal to `utf_baselib_h2profiles/TestTlsClientHello.h`'s values on its vectors;
  - the record-layer reassembly;
  - a real OpenSSL ClientHello;
  - the checks of a run - the visits and their agreement, interception, and how the navigation was
    reached - on capture folders built in the test;
  - that the tool imports only the standard library.
- **The functional tests** run the tool over loopback against a standard-library client. They check every
  control above; the HTTP/2 server's preface, flow control and table size update; and that two good
  visits give a report with no PROBLEM or Note line.
