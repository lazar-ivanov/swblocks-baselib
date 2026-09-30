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
import struct
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
        self.responses, self.goaway, self.ping_acks, self.log = {}, None, [], []

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
        self.log.append(result)
        return result

    def read_until(self, done):
        while not done():
            frame_type, flags, stream, payload = self.read_frame()
            if frame_type == 4 and not flags & 1:
                self.send(frame(4, 1, 0, b""))
            elif frame_type == 1:
                fields, _ = self.decoder.decode(payload)
                self.responses[stream] = {"headers": [(f["name"], f["value"]) for f in fields], "body": b"",
                                          "ended": bool(flags & 1), "block": payload}
            elif frame_type == 0:
                self.responses[stream]["body"] += payload
                self.responses[stream]["ended"] = bool(flags & 1)
            elif frame_type == 6 and flags & 1:
                self.ping_acks.append(payload)
            elif frame_type == 7:
                self.goaway = (int.from_bytes(payload[:4], "big"), int.from_bytes(payload[4:8], "big"))

    def ended(self, *streams):
        return lambda: all(self.responses.get(stream, {}).get("ended") for stream in streams)

    def barrier(self, before=b""):
        """Sends 'before' and a PING in one write, and reads until the PING's ACK. The tool answers frames in
        order, so by then everything the frames before the PING allowed it to send has arrived."""
        self.barriers = getattr(self, "barriers", 0) + 1
        payload = b"barrier%d" % (self.barriers % 10)
        self.send(before + frame(6, 0, 0, payload))
        self.read_until(lambda: payload in self.ping_acks)
        self.ping_acks.remove(payload)

    def received(self, stream):
        return len(self.responses.get(stream, {}).get("body", b""))

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

    def test_a_ping_is_answered_after_the_data_of_the_requests_before_it(self, start_server, certificates):
        """The tool answers frames in order: the DATA a request is owed goes out before the ACK of a PING sent
        after it, in the same write. That makes a PING the barrier the flow-control cases wait on."""
        server = start_server()
        tls = connect(server.port, client_context(certificates))
        client = H2Client(tls)
        client.barrier(before=opening())
        assert client.ended(5)() and client.responses[5]["body"] == capture.PAGE
        lingering_client_close(tls)
        server.stop()

    def test_a_stream_window_update_before_the_request_ends_is_kept(self, start_server, certificates):
        """A1-17: a stream's send window starts when the stream opens, so a WINDOW_UPDATE the client sends
        before its request's END_STREAM counts: the page's 100-byte window is opened wide before it is due."""
        server = start_server()
        tls = connect(server.port, client_context(certificates))
        client = H2Client(tls)
        block = b"".join([indexed(3), literal("without", 1, HOST), indexed(7), indexed(4)])     # POST https /
        client.barrier(before=capture.H2_PREFACE + frame(4, 0, 0, bytes.fromhex("000400000064"))
                       + frame(1, 0x04, 1, block) + frame(8, 0, 1, (10000).to_bytes(4, "big"))
                       + frame(0, 0x01, 1, b"x"))
        assert client.ended(1)() and client.responses[1]["body"] == capture.PAGE
        lingering_client_close(tls)
        server.stop()

    def test_a_stop_lands_while_the_client_keeps_sending(self, start_server, certificates):
        """A1-14: the stop check between batches ends a connection whose client never leaves it idle for a
        poll. Every PING here is sent as soon as the one before is answered, and GOAWAY must still come."""
        server = start_server()
        tls, client = navigate(server.port, client_context(certificates))
        server.request_stop()
        for _ in range(20):
            client.send(frame(6, 0, 0, b"keepbusy"))
            client.read_until(lambda: client.goaway is not None or b"keepbusy" in client.ping_acks)
            if client.goaway is not None:
                break
            client.ping_acks.remove(b"keepbusy")
        assert client.goaway == (5, 0)
        client.read_to_end()
        tls.close()
        server.wait()

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

    def test_a_client_that_resets_is_recorded_as_such_and_not_as_an_error(self, start_server, certificates):
        """A browser quitting may reset its sockets. That is the client going away, not a fault of the tool:
        no error and no traceback in the record, and nothing more sent to a peer that is gone."""
        server = start_server()
        tls, _ = navigate(server.port, client_context(certificates))
        tls.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH" if sys.platform == "win32" else "ii", 1, 0))
        tls.close()                                 # SO_LINGER 0: the close is a reset
        connection = server.connections[0]
        connection.join(WAIT_SECONDS)               # the tool finishes with it on its own, before any stop
        assert not connection.is_alive()
        summary = server.stop()
        meta = read_json(Path(server.out_dir) / "conn-0001" / "meta.json")
        assert "error" not in meta and "traceback" not in meta
        assert meta["ended_by"] == "the client reset the connection"
        assert meta["linger"]["outcome"] == "the client reset the connection"
        assert "send_error" not in meta["linger"] and "shutdown_error" not in meta["linger"]
        assert summary["checks"]["navigation_connections"] == [1]

    def test_a_hello_that_names_no_host_is_a_problem(self, start_server, certificates):
        """A1-4, F5(e) on the bytes: what an address typed on capture day looks like - no server name in the
        hello - is reported, whatever --host was."""
        server = start_server()
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.set_alpn_protocols(["h2"])
        tls = context.wrap_socket(socket.create_connection(("127.0.0.1", server.port), timeout=WAIT_SECONDS),
                                  server_hostname=None)
        client = H2Client(tls)
        client.send(opening())
        client.read_until(client.ended(5))
        lingering_client_close(tls)
        summary = server.stop()
        assert summary["checks"]["sni_is_the_capture_host"] is False
        assert summary["connections"][0]["sni"] is None
        assert "PROBLEM: not every hello named capture.test in its SNI" in capture.format_report(summary)

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


# ========== The HTTP/2 server a browser needs: preface, ACK, size update, flow control ==========

def settings_frame(*pairs):
    return frame(4, 0, 0, b"".join(key.to_bytes(2, "big") + value.to_bytes(4, "big") for key, value in pairs))


def get(path):
    """A GET for path on capture.test, as a header block of literals and static indices."""
    return b"".join([indexed(2), literal("without", 1, HOST), indexed(7), literal("without", 4, path)])


class TestHttp2Server:
    """A1-3: what every browser relies on the server for. Each case turns red when its behaviour is removed
    (the reviewer's mutants, logs/l7/a/review/mutate.py)."""

    def test_the_server_speaks_first_acks_the_clients_settings_and_answers_a_small_table(self, start_server,
                                                                                          certificates):
        server = start_server()
        tls = connect(server.port, client_context(certificates))
        client = H2Client(tls)
        client.barrier(before=capture.H2_PREFACE + settings_frame((1, 0)) + frame(1, 0x05, 1, get("/")))
        first = client.log[0]
        assert (first[0], first[1], first[3]) == (4, 0, bytes.fromhex("000300000064"))     # MAX_CONCURRENT_STREAMS
        assert [entry for entry in client.log if entry[0] == 4 and entry[1] & 1] == [(4, 1, 0, b"")]
        assert client.responses[1]["block"][:1] == b"\x20"           # a table size update to 0 opens the block
        assert client.ended(1)()
        lingering_client_close(tls)
        server.stop()

    def test_the_stream_window_stops_data_and_a_window_update_resumes_it(self, start_server, certificates):
        server = start_server()
        tls = connect(server.port, client_context(certificates))
        client = H2Client(tls)
        client.barrier(before=capture.H2_PREFACE + settings_frame((4, 100)) + frame(1, 0x05, 1, get("/")))
        assert client.received(1) == 100 and not client.ended(1)()
        client.barrier(before=frame(8, 0, 1, (10000).to_bytes(4, "big")))
        assert client.ended(1)() and client.responses[1]["body"] == capture.PAGE
        lingering_client_close(tls)
        server.stop()

    def test_the_connection_window_stops_data_and_a_window_update_resumes_it(self, start_server, certificates):
        server = start_server()
        tls = connect(server.port, client_context(certificates))
        client = H2Client(tls)
        count = 65535 // len(capture.SCRIPT) + 3
        streams = [1 + 2 * index for index in range(count)]
        client.barrier(before=capture.H2_PREFACE + settings_frame((4, 1 << 20))
                       + b"".join(frame(1, 0x05, stream, get("/capture.js")) for stream in streams))
        assert sum(client.received(stream) for stream in streams) == 65535
        assert not client.ended(*streams)()
        client.barrier(before=frame(8, 0, 0, (1 << 20).to_bytes(4, "big")))
        assert client.ended(*streams)()
        assert all(client.responses[stream]["body"] == capture.SCRIPT for stream in streams)
        lingering_client_close(tls)
        server.stop()

    def test_a_new_initial_window_size_applies_to_a_stream_already_sending(self, start_server, certificates):
        """RFC 9113 6.9.2: a second SETTINGS, mid-response, moves the open stream's window by the difference;
        the rest of the page arrives with no WINDOW_UPDATE at all."""
        server = start_server()
        tls = connect(server.port, client_context(certificates))
        client = H2Client(tls)
        client.barrier(before=capture.H2_PREFACE + settings_frame((4, 100)) + frame(1, 0x05, 1, get("/")))
        assert client.received(1) == 100 and not client.ended(1)()
        client.barrier(before=settings_frame((4, 1000)))
        assert client.ended(1)() and client.responses[1]["body"] == capture.PAGE
        lingering_client_close(tls)
        server.stop()


# ========== HTTP/1.1: the second pass ==========

def read_http1_response(tls, buffer=b""):
    def more():
        data = tls.recv(65536)
        assert data, "the tool closed before its response was complete"
        return data

    while b"\r\n\r\n" not in buffer:
        buffer += more()
    head, _, rest = buffer.partition(b"\r\n\r\n")
    lines = head.decode("latin-1").split("\r\n")
    headers = [tuple(line.split(": ", 1)) for line in lines[1:]]
    length = int(dict(headers)["Content-Length"])
    while len(rest) < length:
        rest += more()
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

        # A request in flight when the tool is stopped is answered, with Connection: close, then lingered.
        # The stop comes once the tool holds the request part way in, and the rest once the stopped tool
        # is waiting for it, so each step is certain
        second_head, second_tail = b"GET /fetch HTTP/1.1\r\nHost: capture.test\r\n", b"Cookie: capture_a=1\r\n\r\n"
        tls.sendall(second_head)
        assert server.connections[0].partial_request.wait(WAIT_SECONDS)
        server.request_stop()
        assert server.connections[0].finishing_request.wait(WAIT_SECONDS)
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

    def test_a_stop_ends_a_connection_that_trickles_an_unfinished_request(self, start_server, certificates):
        """A1-14: the loop checks the stop and its deadline on every pass, so a client keeping its request
        unfinished by sending a line every 50 ms cannot hold the connection past the bound."""
        server = start_server(http1=True, linger_seconds=0.5)
        tls = connect(server.port, client_context(certificates, ("http/1.1",)))
        tls.sendall(b"GET / HTTP/1.1\r\nHost: capture.test\r\n")
        connection = server.connections[0]
        assert connection.partial_request.wait(WAIT_SECONDS)
        server.request_stop()
        for _ in range(400):                        # 20 s of trickle at most, against a 0.5 s deadline
            try:
                tls.sendall(b"X-Trickle: 1\r\n")
            except OSError:
                break                               # the tool has closed
            connection.join(0.05)                   # the client's cadence, and the wait on what is asserted
            if not connection.is_alive():
                break
        assert not connection.is_alive()
        tls.close()
        server.wait()
        meta = read_json(Path(server.out_dir) / "conn-0001" / "meta.json")
        assert meta["ended_by"] == "the tool was stopped with a request incomplete"

    def test_a_malformed_request_is_answered_and_recorded_and_the_summary_is_written(self, start_server, certificates):
        """A1-5, end to end: a Content-Length that passes isdigit() but is no number gets a 400 with
        Connection: close and a lingering close, and the run still ends in a summary.json holding the error."""
        server = start_server(http1=True)
        tls = connect(server.port, client_context(certificates, ("h2", "http/1.1")))
        tls.sendall("GET / HTTP/1.1\r\nHost: capture.test\r\nContent-Length: ²\r\n\r\n".encode("latin-1"))
        status, headers, _, _ = read_http1_response(tls)
        assert status == "HTTP/1.1 400 Bad Request" and dict(headers)["Connection"] == "close"
        while tls.recv(65536):
            pass
        tls.close()
        summary = server.stop()
        assert (Path(server.out_dir) / "summary.json").is_file()
        record = read_json(Path(server.out_dir) / "conn-0001" / "connection.json")
        assert record["http1"]["error"] == "a malformed Content-Length" and record["http1"]["requests"] == []
        assert record["meta"]["ended_by"] == "an HTTP/1.1 error from the client: a malformed Content-Length"
        assert "error" not in record["meta"] and summary["checks"]["connections_recorded"] == 1


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
