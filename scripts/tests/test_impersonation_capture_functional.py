#!/usr/bin/env python3
#
# This file is part of the swblocks-baselib library.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""
End-to-end tests of scripts/impersonation/capture.py over this host's loopback.

The client is the standard library's: the ssl module, and a minimal HTTP/2 client written here. It
verifies the tool's certificate against the per-session CA the tool's own 'certs' command made with
the openssl command-line tool, so the certificate procedure is exercised too. Nothing leaves
127.0.0.1.

Every wait is on the thing the assertion is about - a frame, an event the tool sets, a process
exiting - and never on a sleep.
"""

import json
import os
import queue
import re
import signal
import socket
import ssl
import subprocess
import sys
import threading
from pathlib import Path

import pytest

REPOSITORY = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY / "scripts" / "impersonation" / "capture.py"

sys.path.insert(0, str(TOOL.parent))

import capture  # noqa: E402

HOST = "capture.test"
WAIT_SECONDS = 20.0


# ========== Fixtures ==========

@pytest.fixture(scope="module")
def certificates(tmp_path_factory):
    if capture.find_openssl() is None:
        pytest.skip("no openssl command on this host")
    return capture.make_certificates(HOST, str(tmp_path_factory.mktemp("certs")))


@pytest.fixture
def start_server(certificates, tmp_path):
    servers = []

    def start(http1=False, linger_seconds=5.0):
        server = capture.CaptureServer(certificates["leaf"], certificates["key"], str(tmp_path / ("run%d" % len(servers))),
                                       "test client", HOST, "127.0.0.1", 0, http1, linger_seconds)
        server.start()
        servers.append(server)
        return server

    yield start
    for server in servers:
        if not server.stop_event.is_set():
            server.stop()


def client_context(certificates, protocols=("h2",)):
    context = ssl.create_default_context(cafile=certificates["ca"])
    context.set_alpn_protocols(list(protocols))
    return context


def connect(port, context, session=None):
    sock = socket.create_connection(("127.0.0.1", port), timeout=WAIT_SECONDS)
    return context.wrap_socket(sock, server_hostname=HOST, session=session)


def read_json(path):
    return json.loads(Path(path).read_text(encoding="ascii"))


# ========== A minimal HTTP/2 client, with HPACK encoding of its own ==========

def huffman(text):
    bits, count = 0, 0
    for byte in text.encode("latin-1"):
        code, length = capture.HUFFMAN_CODES[byte]
        bits, count = (bits << length) | code, count + length
    padding = -count % 8
    return ((bits << padding) | ((1 << padding) - 1)).to_bytes((count + padding) // 8, "big")


def string(text, coded):
    raw = huffman(text) if coded else text.encode("latin-1")
    return capture.encode_integer(len(raw), 7, 0x80 if coded else 0x00) + raw


def indexed(index):
    return capture.encode_integer(index, 7, 0x80)


def literal(kind, name, value, coded=False, name_coded=False):
    """kind is incremental, without or never; name is a table index or a new name."""
    flags, prefix = {"incremental": (0x40, 6), "without": (0x00, 4), "never": (0x10, 4)}[kind]
    if isinstance(name, int):
        return capture.encode_integer(name, prefix, flags) + string(value, coded)
    return capture.encode_integer(0, prefix, flags) + string(name, name_coded) + string(value, coded)


def frame(frame_type, flags, stream, payload):
    return len(payload).to_bytes(3, "big") + bytes([frame_type, flags]) + stream.to_bytes(4, "big") + payload


CLIENT_SETTINGS = [(1, 65536), (2, 0), (4, 6291456), (6, 262144)]
CLIENT_WINDOW_UPDATE = 15663105

# The dynamic table after it, newest first: sec-fetch-mode 62, accept 63, user-agent 64, :authority 65
NAVIGATION_BLOCK = b"".join([
    indexed(2), literal("incremental", 1, HOST, coded=True), indexed(7), indexed(4),
    literal("incremental", 58, "test-agent/1.0", coded=True), literal("incremental", 19, "text/html"),
    literal("incremental", "sec-fetch-mode", "navigate", coded=True, name_coded=True),
    literal("without", 17, "en-US,en;q=0.9")])


def opening(block=NAVIGATION_BLOCK):
    """The preface, SETTINGS, WINDOW_UPDATE, a PRIORITY on idle stream 3, and the navigation on stream 5
    split across HEADERS and CONTINUATION, with priority fields on the HEADERS."""
    settings = b"".join(key.to_bytes(2, "big") + value.to_bytes(4, "big") for key, value in CLIENT_SETTINGS)
    return (capture.H2_PREFACE + frame(4, 0, 0, settings) + frame(8, 0, 0, CLIENT_WINDOW_UPDATE.to_bytes(4, "big"))
            + frame(2, 0, 3, bytes.fromhex("00000000c8"))
            + frame(1, 0x21, 5, bytes.fromhex("80000000ff") + block[:7]) + frame(9, 0x04, 5, block[7:]))


class H2Client:

    def __init__(self, tls):
        self.tls, self.buffer, self.decoder = tls, b"", capture.HpackDecoder()
        self.responses, self.goaway, self.ping_acks = {}, None, []

    def send(self, data):
        self.tls.sendall(data)

    def read_frame(self):
        """The next frame, or None at the tool's close_notify or end of stream."""
        while len(self.buffer) < 9 or len(self.buffer) < 9 + int.from_bytes(self.buffer[:3], "big"):
            try:
                data = self.tls.recv(65536)
            except ssl.SSLZeroReturnError:
                data = b""
            if not data:
                return None
            self.buffer += data
        length = int.from_bytes(self.buffer[:3], "big")
        result = (self.buffer[3], self.buffer[4], int.from_bytes(self.buffer[5:9], "big") & 0x7FFFFFFF,
                  self.buffer[9:9 + length])
        self.buffer = self.buffer[9 + length:]
        return result

    def read_until(self, done):
        while not done():
            frame_type, flags, stream, payload = self.read_frame()
            if frame_type == 4 and not flags & 1:
                self.send(frame(4, 1, 0, b""))
            elif frame_type == 1:
                fields, _ = self.decoder.decode(payload)
                self.responses[stream] = {"headers": [(f["name"], f["value"]) for f in fields], "body": b"",
                                          "ended": bool(flags & 1)}
            elif frame_type == 0:
                self.responses[stream]["body"] += payload
                self.responses[stream]["ended"] = bool(flags & 1)
            elif frame_type == 6 and flags & 1:
                self.ping_acks.append(payload)
            elif frame_type == 7:
                self.goaway = (int.from_bytes(payload[:4], "big"), int.from_bytes(payload[4:8], "big"))

    def ended(self, *streams):
        return lambda: all(self.responses.get(stream, {}).get("ended") for stream in streams)

    def read_to_end(self):
        """After the tool's GOAWAY: everything up to its close_notify and end of stream. A reset raises."""
        while self.read_frame() is not None:
            pass


def navigate(port, context, session=None):
    tls = connect(port, context, session)
    client = H2Client(tls)
    client.send(opening())
    client.read_until(client.ended(5))
    return tls, client


def lingering_client_close(tls):
    """Our own orderly close: close_notify, the tool's close_notify back, then the socket."""
    try:
        tls.unwrap().close()
    except (OSError, ValueError):
        tls.close()


# ========== HTTP/2: the three request kinds, the record, the lingering close ==========

class TestHttp2Capture:

    def test_three_request_kinds_on_one_connection_and_a_lingering_close(self, start_server, certificates):
        server = start_server()
        tls, client = navigate(server.port, client_context(certificates))
        navigation = client.responses[5]
        assert (":status", "200") in navigation["headers"]
        assert [value for name, value in navigation["headers"] if name == "set-cookie"] == list(capture.COOKIES)

        # The script, the image and the fetch, on the same connection, reusing the dynamic table: a cookie
        # of two crumbs, the second never indexed, and a table size update opening the fetch's block
        script = b"".join([indexed(2), indexed(65), indexed(7), literal("without", 4, "/capture.js"), indexed(64),
                           literal("without", 19, "*/*"), literal("incremental", 32, "capture_a=1"),
                           literal("never", 32, "capture_b=2")])
        image = b"".join([indexed(2), indexed(66), indexed(7), literal("without", 4, "/capture.png"), indexed(65),
                          literal("without", 19, "image/png"), indexed(62), literal("never", 32, "capture_b=2")])
        fetch = b"".join([capture.encode_integer(2048, 5, 0x20), indexed(2), indexed(66), indexed(7),
                          literal("without", 4, "/fetch"), indexed(65), literal("without", 19, "*/*"), indexed(62),
                          literal("never", 32, "capture_b=2")])
        client.send(frame(1, 0x05, 7, script) + frame(1, 0x05, 9, image) + frame(1, 0x05, 11, fetch)
                    + frame(6, 0, 0, b"pingpong"))
        client.read_until(lambda: client.ended(7, 9, 11)() and client.ping_acks)
        assert client.ping_acks == [b"pingpong"]
        assert json.loads(client.responses[11]["body"]) == {
            "connection": 1, "protocol": "h2", "missing": [],
            "kinds": ["fetch", "navigation", "subresource-image", "subresource-script"]}

        # The tool is stopped while the connection is open: GOAWAY first. Frames sent after it - as a
        # browser's SETTINGS ACK or WINDOW_UPDATE would be - must reach the tool, and draw no reset
        server.request_stop()
        client.read_until(lambda: client.goaway is not None)
        assert client.goaway == (11, 0)
        client.send(frame(8, 0, 0, (1000).to_bytes(4, "big")) + frame(6, 0, 0, b"latelate"))
        client.read_to_end()
        tls.close()
        summary = server.wait()

        checks = summary["checks"]
        assert checks["connections_recorded"] == 1 and checks["ja4_identical_across_hellos"]
        assert checks["navigation_connections"] == [1] and checks["profile_source_connection"] == 1
        assert checks["kinds_missing_from_navigation_connection"] == {"1": []}
        assert checks["alpn_selected"] == {"1": "h2"} and checks["sni_is_the_capture_host"]
        assert checks["hellos_refused_as_profile_source"] == []
        assert checks["lingering_close_outcomes"] == {"1": "the client closed"}

        record = read_json(Path(server.out_dir) / "conn-0001" / "connection.json")
        h2 = record["http2"]
        assert h2["error"] is None
        assert h2["opening"] == ["SETTINGS", "WINDOW_UPDATE", "PRIORITY", "HEADERS"]
        assert [(s["id"], s["value"]) for s in h2["settings"]] == CLIENT_SETTINGS
        assert h2["connection_window_update"] == CLIENT_WINDOW_UPDATE
        assert [(f["stream"], f["priority"]) for f in h2["priority_frames"]] == [
            (3, {"exclusive": False, "depends_on": 0, "weight": 201, "weight_on_wire": 200})]
        requests = h2["requests"]
        assert [(r["stream"], r["kind"], r["pseudo_header_short"]) for r in requests] == [
            (5, "navigation", "m,a,s,p"), (7, "subresource-script", "m,a,s,p"), (9, "subresource-image", "m,a,s,p"),
            (11, "fetch", "m,a,s,p")]
        first = requests[0]["header_block"]
        assert first["frames"] == ["HEADERS", "CONTINUATION"]
        assert first["priority"] == {"exclusive": True, "depends_on": 0, "weight": 256, "weight_on_wire": 255}
        assert [(f["name"], f["representation"], f.get("value_huffman")) for f in first["fields"]] == [
            (":method", "indexed", None), (":authority", "incremental", True), (":scheme", "indexed", None),
            (":path", "indexed", None), ("user-agent", "incremental", True), ("accept", "incremental", False),
            ("sec-fetch-mode", "incremental", True), ("accept-language", "without_indexing", False)]
        assert first["fields"][6]["name_huffman"] is True
        assert requests[0]["headers"][:2] == [["user-agent", "test-agent/1.0"], ["accept", "text/html"]]
        crumbs = [(f["value"], f["representation"]) for f in requests[1]["header_block"]["fields"] if f["name"] == "cookie"]
        assert crumbs == [("capture_a=1", "incremental"), ("capture_b=2", "never_indexed")]
        assert requests[1]["cookie_fields"] == 2
        assert [(f["name"], f["index"], f["table"]) for f in requests[2]["header_block"]["fields"]
                if f["representation"] == "indexed" and f["table"] == "dynamic"] == [
            (":authority", 66, "dynamic"), ("user-agent", 65, "dynamic"), ("cookie", 62, "dynamic")]
        assert requests[3]["header_block"]["table_size_updates"] == [{"size": 2048, "after_fields": 0}]

        late = [(f["type"], f.get("increment")) for f in h2["frames"] if f["after_close_began"]]
        assert late == [("WINDOW_UPDATE", 1000), ("PING", None)]
        meta = record["meta"]
        assert meta["goaway"] == {"last_stream": 11, "error_code": 0}
        assert meta["ended_by"] == "the tool was stopped"
        assert meta["linger"]["outcome"] == "the client closed" and meta["linger"]["plain_bytes"] > 0

        directory = Path(server.out_dir) / "conn-0001"
        hello = (directory / "client-hello.bin").read_bytes()
        assert hello[0] == 1 and capture.summarize_hello(hello) == record["hello"]
        assert (directory / "client-plain.bin").read_bytes().startswith(capture.H2_PREFACE)
        assert record["hello"]["sni"] == {"present": True, "host_name": HOST}
        assert record["hello"]["alpn"] == ["h2"]

        # Everything in the summary is derived from the stored bytes: deriving it again gives the same
        assert capture.analyse_session(server.out_dir) == summary

    def test_negative_control_without_the_linger_the_late_frames_are_lost(self, start_server, certificates):
        """The same close with the bound at 0: the tool closes without reading, so the assertion above
        about late frames is one that can fail."""
        server = start_server(linger_seconds=0)
        tls, client = navigate(server.port, client_context(certificates))
        server.request_stop()
        client.read_until(lambda: client.goaway is not None)
        try:
            client.send(frame(8, 0, 0, (1000).to_bytes(4, "big")) + frame(6, 0, 0, b"latelate"))
            client.read_to_end()
        except OSError:
            pass                                    # a reset is what this control is allowed to draw
        tls.close()
        server.wait()
        record = read_json(Path(server.out_dir) / "conn-0001" / "connection.json")
        assert [f["type"] for f in record["http2"]["frames"] if f["after_close_began"]] == []
        assert record["meta"]["linger"]["outcome"] == "the bound was reached"
        assert record["meta"]["linger"]["raw_bytes"] == 0

    def test_a_resumed_hello_is_refused_and_the_tool_issues_no_ticket(self, start_server, certificates):
        """F5(a). The positive control: a server with tickets on does give this client a ticket."""
        context = client_context(certificates)
        issuing = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        issuing.load_cert_chain(certificates["leaf"], certificates["key"])
        listener = socket.create_server(("127.0.0.1", 0))

        def serve_one():
            sock, _ = listener.accept()
            with issuing.wrap_socket(sock, server_side=True) as tls:
                tls.sendall(b"x")
                tls.recv(1)

        helper = threading.Thread(target=serve_one)
        helper.start()
        with connect(listener.getsockname()[1], context) as tls:
            tls.recv(1)
            ticket = tls.session
            tls.sendall(b"y")
        helper.join(WAIT_SECONDS)
        listener.close()
        assert ticket.has_ticket

        server = start_server()
        fresh, _ = navigate(server.port, context)
        assert not fresh.session.has_ticket
        lingering_client_close(fresh)
        resumed, _ = navigate(server.port, context, session=ticket)
        assert not resumed.session_reused
        lingering_client_close(resumed)
        summary = server.stop()

        checks = summary["checks"]
        assert checks["hellos_refused_as_profile_source"] == [2]
        assert checks["navigation_connections"] == [1, 2] and checks["profile_source_connection"] == 1
        assert not checks["ja4_identical_across_hellos"] and len(checks["ja4_values"]) == 2
        hello = read_json(Path(server.out_dir) / "conn-0002" / "connection.json")["hello"]
        assert hello["pre_shared_key"] and not hello["profile_source"]["eligible"]
        assert "PROBLEM: JA4 differs" in capture.format_report(summary)

    def test_every_connection_is_recorded_and_the_navigation_is_marked(self, start_server, certificates):
        """F5(b): a connection with no TLS at all, one whose client refused the certificate, one that
        navigated - three records, and the navigation's is the one marked."""
        server = start_server()
        socket.create_connection(("127.0.0.1", server.port), timeout=WAIT_SECONDS).close()
        untrusting = ssl.create_default_context()
        untrusting.set_alpn_protocols(["h2"])
        with pytest.raises(ssl.SSLCertVerificationError):
            connect(server.port, untrusting)
        tls, _ = navigate(server.port, client_context(certificates))
        lingering_client_close(tls)
        summary = server.stop()
        checks = summary["checks"]
        assert checks["connections_recorded"] == 3 and checks["hellos_recorded"] == 2
        assert checks["handshakes_not_complete"] == [1, 2]
        assert checks["navigation_connections"] == [3] and checks["ja4_identical_across_hellos"]
        rows = {row["connection"]: row for row in summary["connections"]}
        assert rows[1]["handshake"] == "incomplete: the client closed the connection" and rows[1]["ja4"] is None
        assert rows[2]["handshake"].startswith("failed") and rows[2]["ja4"] == rows[3]["ja4"]
        assert rows[3]["kinds"] == ["navigation"]


# ========== HTTP/1.1: the second pass ==========

def read_http1_response(tls, buffer=b""):
    while b"\r\n\r\n" not in buffer:
        buffer += tls.recv(65536)
    head, _, rest = buffer.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    headers = [tuple(line.split(": ", 1)) for line in lines[1:]]
    length = int(dict(headers)["Content-Length"])
    while len(rest) < length:
        rest += tls.recv(65536)
    return lines[0], headers, rest[:length], rest[length:]


class TestHttp1Capture:

    def test_the_raw_request_bytes_are_recorded_and_the_close_says_connection_close(self, start_server, certificates):
        server = start_server(http1=True)
        tls = connect(server.port, client_context(certificates, ("h2", "http/1.1")))
        assert tls.selected_alpn_protocol() == "http/1.1"
        first = (b"GET / HTTP/1.1\r\nHost: capture.test\r\nConnection: keep-alive\r\nUser-Agent: test-agent/1.0\r\n"
                 b"Accept: text/html\r\nAccept-Language: en-US\r\n\r\n")
        tls.sendall(first)
        status, headers, body, _ = read_http1_response(tls)
        assert status == "HTTP/1.1 200 OK" and body == capture.PAGE
        assert [value for name, value in headers if name == "Set-Cookie"] == list(capture.COOKIES)
        assert "Connection" not in dict(headers)

        # A request in flight when the tool is stopped is answered, with Connection: close, then lingered
        second_head, second_tail = b"GET /fetch HTTP/1.1\r\nHost: capture.test\r\n", b"Cookie: capture_a=1\r\n\r\n"
        tls.sendall(second_head)
        server.request_stop()
        connection = server.connections[0]
        assert connection.stopping.wait(WAIT_SECONDS)
        tls.sendall(second_tail)
        status, headers, body, _ = read_http1_response(tls)
        assert dict(headers)["Connection"] == "close" and json.loads(body)["protocol"] == "http/1.1"
        while tls.recv(65536):
            pass                                    # the tool's close_notify, then its end of stream
        late = b"GET /late HTTP/1.1\r\nHost: capture.test\r\n\r\n"
        tls.sendall(late)                           # crossing the tool's close, as a browser's may
        tls.close()
        summary = server.wait()

        directory = Path(server.out_dir) / "conn-0001"
        assert (directory / "client-plain.bin").read_bytes() == first + second_head + second_tail + late
        record = read_json(directory / "connection.json")
        requests = record["http1"]["requests"]
        assert [(r["target"], r["kind"], r["after_close_began"]) for r in requests] == [
            ("/", "navigation", False), ("/fetch", "fetch", False), ("/late", "other", True)]
        assert requests[0]["raw_head"] == first.decode("latin-1")
        assert [name for name, _ in requests[0]["headers"]] == ["Host", "Connection", "User-Agent", "Accept",
                                                                "Accept-Language"]
        assert record["meta"]["ended_by"] == "the tool was stopped (Connection: close)"
        assert record["meta"]["linger"]["outcome"] == "the client closed"
        assert summary["checks"]["alpn_selected"] == {"1": "http/1.1"}
        assert summary["session"]["alpn_offered"] == ["http/1.1"]


# ========== Certificates, and the command line ==========

def memory_handshake(certificates, hostname):
    """A handshake between the tool's context and a verifying client, in memory: no socket."""
    server = capture.make_server_context(certificates["leaf"], certificates["key"], False)
    client = ssl.create_default_context(cafile=certificates["ca"])
    pipes = [ssl.MemoryBIO() for _ in range(4)]
    ends = [(client.wrap_bio(pipes[0], pipes[1], server_hostname=hostname), pipes[1], pipes[2]),
            (server.wrap_bio(pipes[2], pipes[3], server_side=True), pipes[3], pipes[0])]
    for _ in range(8):
        finished = 0
        for tls, outgoing, peer in ends:
            try:
                tls.do_handshake()
                finished += 1
            except ssl.SSLWantReadError:
                pass
            peer.write(outgoing.read())
        if finished == 2:
            return
    raise AssertionError("the handshake did not finish")


class TestCertificatesAndCommandLine:

    def test_the_certificates_verify_for_the_capture_host_only_and_no_ca_key_remains(self, certificates):
        directory = Path(certificates["ca"]).parent
        assert sorted(path.name for path in directory.iterdir()) == ["ca-info.json", "ca.pem", "leaf.key", "leaf.pem"]
        memory_handshake(certificates, HOST)
        with pytest.raises(ssl.SSLCertVerificationError):
            memory_handshake(certificates, "other.test")
        with pytest.raises(capture.CaptureError):
            capture.make_certificates(HOST, str(directory))

    def test_the_certs_command(self, tmp_path, capsys):
        assert capture.main(["certs", "--dir", str(tmp_path / "certs")]) == 0
        output = capsys.readouterr().out
        assert ssl.OPENSSL_VERSION in output and "The CA's private key is already deleted" in output
        assert not (tmp_path / "certs" / "ca.key").exists()

    def test_serve_runs_from_the_command_line_and_stops_on_ctrl_c(self, certificates, tmp_path):
        out = tmp_path / "cli"
        command = [sys.executable, str(TOOL), "serve", "--browser", "Test Client 1.0", "--cert-dir",
                   str(Path(certificates["leaf"]).parent), "--out", str(out), "--port", "0", "--linger-seconds", "5"]
        windows = sys.platform == "win32"
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                   creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if windows else 0)
        lines = queue.Queue()
        threading.Thread(target=lambda: [lines.put(line) for line in process.stdout] + [lines.put(None)],
                         daemon=True).start()
        output, port = [], None
        try:
            while port is None:
                output.append(lines.get(timeout=WAIT_SECONDS))
                match = re.search(r"port (\d+);", output[-1] or "")
                port = int(match.group(1)) if match else None
            tls, client = navigate(port, client_context(certificates))
            process.send_signal(signal.CTRL_BREAK_EVENT if windows else signal.SIGINT)
            client.read_until(lambda: client.goaway is not None)
            client.read_to_end()
            tls.close()
            assert process.wait(timeout=WAIT_SECONDS) == 0
        finally:
            if process.poll() is None:
                process.kill()
        while output[-1] is not None:
            output.append(lines.get(timeout=WAIT_SECONDS))
        text = "".join(output[:-1])
        assert ssl.OPENSSL_VERSION in text and "https://capture.test:%d/" % port in text
        assert "JA4 identical across every hello: yes" in text
        assert "Navigation on connection 1, ALPN h2: not on this connection: subresource-script" in text
        summary = read_json(out / "summary.json")
        assert summary["session"]["browser"] == "Test Client 1.0" and summary["checks"]["navigation_connections"] == [1]
