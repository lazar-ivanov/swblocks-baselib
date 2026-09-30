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
The capture endpoint for browser impersonation: what a real browser sends, recorded at the socket.

A local HTTPS server which a real browser is pointed at by hand (notes/plans/http2-l7-execution-plan.md,
D-L7-1 and D-L7-2). For every connection it keeps:

- every byte read from the socket, before any of it reaches OpenSSL, and the ClientHello reassembled
  from those bytes - the ssl module never exposes the hello, so it is taken from the record layer;
- the decrypted client stream, decoded frame by frame over HTTP/2 with one HPACK table per connection,
  and each field's representation;
- a summary derived from those bytes alone: the hello parsed with GREASE stripped, JA3 and JA4, the
  HTTP/2 opening, and which connection carried which request.

The page it serves yields a navigation, two subresources (a script and an image) and a fetch, on one
connection, and sets two cookies on the first response so that the later requests carry a cookie.
Run with --http1 it offers only http/1.1, for the second pass.

It never closes a connection while the browser may still be sending. Every close is GOAWAY (on
HTTP/1.1, Connection: close), close_notify, shutdown( SHUT_WR ), then reads until the browser's end of
stream or a short bound, then close: on Windows a segment which meets a closed socket draws a reset,
and the reset discards what the browser has not yet read.

Standard library only. README.md beside this file is the capture procedure.
"""

import argparse
import datetime
import hashlib
import ipaddress
import json
import os
import platform
import re
import select
import shutil
import signal
import socket
import ssl
import struct
import subprocess
import sys
import threading
import time
import traceback
import zlib

TOOL_VERSION = "1"

DEFAULT_HOST = "capture.test"
DEFAULT_LINGER_SECONDS = 5.0
POLL_SECONDS = 0.25
SEND_TIMEOUT_SECONDS = 30.0
MAX_HELLO_BYTES = 1 << 16
MAX_REQUEST_HEAD_BYTES = 1 << 16

RAW_FILE = "client-raw.bin"
HELLO_FILE = "client-hello.bin"
PLAIN_FILE = "client-plain.bin"
META_FILE = "meta.json"
CONNECTION_FILE = "connection.json"
SESSION_FILE = "session.json"
SUMMARY_FILE = "summary.json"


class CaptureError(Exception):
    """A problem the operator can fix; reported without a traceback."""


class ParseError(ValueError):
    """Bytes which are not what the protocol says they must be."""


class HpackError(ValueError):
    """An HPACK decoding error (RFC 7541), which HTTP/2 makes a COMPRESSION_ERROR."""


class ProtocolError(Exception):
    """A violation by the client; 'code' is the HTTP/2 error code a GOAWAY answers it with."""

    def __init__(self, code, text):
        super().__init__(text)
        self.code = code
        self.text = text


def now_text():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")


def write_json(path, value):
    with open(path, "w", encoding="ascii") as handle:
        json.dump(value, handle, indent=2)
        handle.write("\n")


def read_json(path):
    if not os.path.isfile(path):
        return None
    with open(path, encoding="ascii") as handle:
        return json.load(handle)


def read_bytes(path):
    if not os.path.isfile(path):
        return b""
    with open(path, "rb") as handle:
        return handle.read()


# ==================================================================================================
# The interpreter check (the plan review's F5(k)): Apple's bundled python3 cannot do this
# ==================================================================================================

def without_paths(text):
    """Every quoted absolute path reduced to its last component. A path can name the operator's account
    (C:\\Users\\<account>\\...), and a capture's records leave the machine."""
    return re.sub(r"""(["'])(?:[A-Za-z]:[\\/]|/)[^"']*\1""",
                  lambda match: match.group(1) + re.split(r"[\\/]+", match.group(0)[1:-1])[-1] + match.group(1),
                  text)


def interpreter_report():
    """What session.json records of the interpreter: versions, and no path."""
    return {
        "python": sys.version.split()[0],
        "openssl": ssl.OPENSSL_VERSION,
        "tls13": bool(getattr(ssl, "HAS_TLSv1_3", False)),
        "alpn": bool(getattr(ssl, "HAS_ALPN", False)),
        "platform": platform.platform(),
    }


def interpreter_problems():
    """Why this interpreter cannot capture, or an empty list."""
    problems = []
    if sys.version_info < (3, 8):
        problems.append("Python 3.8 or later is needed; this is %s" % sys.version.split()[0])
    if not getattr(ssl, "HAS_TLSv1_3", False):
        problems.append("its ssl module has no TLS 1.3")
    if not getattr(ssl, "HAS_ALPN", False):
        problems.append("its ssl module has no ALPN, so it cannot negotiate h2")
    if not hasattr(ssl.SSLContext, "num_tickets"):
        problems.append("its ssl module cannot turn TLS 1.3 session tickets off")
    return problems


# ==================================================================================================
# The ClientHello, from the record layer (RFC 8446 5.1), and its fingerprints
# ==================================================================================================

class RecordStream:
    """
    Follows the client's TLS records in the bytes read from the socket, as they arrive, and
    reassembles the ClientHello across reads and across records.

    It stops at the first application_data record, after which everything is encrypted.
    'record_types' keeps the content types up to there: a second handshake record after the hello
    of a TLS 1.3 connection is the second ClientHello of a HelloRetryRequest.
    """

    def __init__(self):
        self.buffer = bytearray()
        self.handshake = bytearray()
        self.hello = None
        self.error = None
        self.record_types = []
        self.hello_records = None                   # how many records the hello itself arrived in
        self.done = False

    def feed(self, data):
        if self.done:
            return
        self.buffer += data
        while not self.done and len(self.buffer) >= 5:
            content_type, _, length = struct.unpack(">BHH", self.buffer[:5])
            if not self.record_types and content_type != 22:
                return self._fail("the first record is not a TLS handshake record (type %d)" % content_type)
            if length > (1 << 14) + 2048:
                return self._fail("a record of %d bytes is longer than TLS allows" % length)
            if len(self.buffer) < 5 + length:
                return
            fragment = bytes(self.buffer[5:5 + length])
            del self.buffer[:5 + length]
            self.record_types.append(content_type)
            if self.hello is None:
                if content_type != 22:
                    return self._fail("a record of type %d arrived inside the ClientHello" % content_type)
                self.handshake += fragment
                self._take_hello()
                if self.hello is not None and self.hello_records is None:
                    self.hello_records = len(self.record_types)
            elif content_type == 23:
                self.done = True

    def _take_hello(self):
        if len(self.handshake) < 4:
            return
        if self.handshake[0] != 1:
            return self._fail("the first handshake message is not a ClientHello (type %d)" % self.handshake[0])
        length = int.from_bytes(self.handshake[1:4], "big")
        if length > MAX_HELLO_BYTES:
            return self._fail("a ClientHello of %d bytes" % length)
        if len(self.handshake) >= 4 + length:
            self.hello = bytes(self.handshake[:4 + length])

    def _fail(self, text):
        self.error = text
        self.done = True


EXTENSION_NAMES = {
    0: "server_name", 5: "status_request", 10: "supported_groups", 11: "ec_point_formats",
    13: "signature_algorithms", 16: "application_layer_protocol_negotiation",
    18: "signed_certificate_timestamp", 21: "padding", 22: "encrypt_then_mac",
    23: "extended_master_secret", 27: "compress_certificate", 28: "record_size_limit",
    34: "delegated_credentials", 35: "session_ticket", 41: "pre_shared_key", 42: "early_data",
    43: "supported_versions", 44: "cookie", 45: "psk_key_exchange_modes", 49: "post_handshake_auth",
    50: "signature_algorithms_cert", 51: "key_share", 17513: "application_settings (0x4469)",
    17613: "application_settings", 65037: "encrypted_client_hello", 65281: "renegotiation_info",
}

GROUP_NAMES = {
    23: "secp256r1", 24: "secp384r1", 25: "secp521r1", 29: "x25519", 30: "x448", 256: "ffdhe2048",
    257: "ffdhe3072", 4587: "SecP256r1MLKEM768", 4588: "X25519MLKEM768", 25497: "X25519Kyber768Draft00",
}

EXTENSION_PRE_SHARED_KEY = 41


def is_grease(value):
    """RFC 8701's sixteen reserved values, by the rule crypto/TlsClientHello.h uses."""
    return (value >> 8) == (value & 0xFF) and (value & 0x0F) == 0x0A


def without_grease(values):
    return [value for value in values if not is_grease(value)]


class Cursor:
    """A bounds-checked reader: no length field can make it read outside what it was given."""

    def __init__(self, data):
        self.data = bytes(data)
        self.pos = 0

    def at_end(self):
        return self.pos >= len(self.data)

    def take(self, count):
        if count < 0 or len(self.data) - self.pos < count:
            raise ParseError("truncated")
        chunk = self.data[self.pos:self.pos + count]
        self.pos += count
        return chunk

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return int.from_bytes(self.take(2), "big")

    def u32(self):
        return int.from_bytes(self.take(4), "big")

    def sub(self, count):
        return Cursor(self.take(count))

    def u16_list(self, length_size):
        items = self.sub(self.u8() if length_size == 1 else self.u16())
        values = []
        while not items.at_end():
            values.append(items.u16())
        return values


def _parse_server_name(info, extension, _):
    # JA4 reads the presence only, as crypto/TlsClientHello.h does; the name is decoded for the record
    info["has_server_name"] = True
    try:
        names = extension.sub(extension.u16())
        while not names.at_end():
            name_type, name = names.u8(), names.take(names.u16())
            if name_type == 0 and info["server_name"] is None:
                info["server_name"] = name.decode("latin-1")
    except ParseError:
        pass


def _parse_alpn(info, extension, _):
    protocols = extension.sub(extension.u16())
    while not protocols.at_end():
        info["alpn"].append(protocols.take(protocols.u8()))


def _parse_supported_versions(info, extension, _):
    versions = extension.u16_list(1)
    info["supported_versions"] += versions
    highest = max(without_grease(versions), default=0)
    if highest:
        info["highest_version"] = highest


def _parse_key_share(info, extension, _):
    shares = extension.sub(extension.u16())
    while not shares.at_end():
        group = shares.u16()
        info["key_shares"].append((group, len(shares.take(shares.u16()))))


def _parse_names(info, extension, kind):
    protocols, names = extension.sub(extension.u16()), []
    while not protocols.at_end():
        names.append(protocols.take(protocols.u8()).decode("latin-1"))
    info["other"][EXTENSION_NAMES[kind]] = names


def _parse_psk_identities(info, extension, _):
    identities, count = extension.sub(extension.u16()), 0
    while not identities.at_end():
        identities.take(identities.u16())
        identities.u32()
        count += 1
    info["other"]["pre_shared_key_identities"] = count


def _hex_list(values):
    return ["%04x" % value for value in values]


# The six extensions crypto/TlsClientHello.h parses, parsed the way it parses them: a malformed body
# fails the whole hello, exactly as it does there, so the two never disagree on what is a hello
_STRICT_PARSERS = {
    0: _parse_server_name,
    10: lambda info, extension, _: info["supported_groups"].extend(extension.u16_list(2)),
    11: lambda info, extension, _: info["ec_point_formats"].extend(extension.take(extension.u8())),
    13: lambda info, extension, _: info["signature_algorithms"].extend(extension.u16_list(2)),
    16: _parse_alpn,
    43: _parse_supported_versions,
}

# What a profile needs beyond JA3 and JA4. A malformed body here is recorded, not fatal
_LENIENT_PARSERS = {
    5: lambda info, extension, _: info["other"].__setitem__("status_request_type", extension.u8()),
    27: lambda info, extension, _: info["other"].__setitem__(
        "compress_certificate", _hex_list(extension.u16_list(1))),
    28: lambda info, extension, _: info["other"].__setitem__("record_size_limit", extension.u16()),
    34: lambda info, extension, _: info["other"].__setitem__(
        "delegated_credentials", _hex_list(extension.u16_list(2))),
    41: _parse_psk_identities,
    45: lambda info, extension, _: info["other"].__setitem__(
        "psk_key_exchange_modes", list(extension.take(extension.u8()))),
    50: lambda info, extension, _: info["other"].__setitem__(
        "signature_algorithms_cert", _hex_list(extension.u16_list(2))),
    51: _parse_key_share,
    17513: _parse_names,
    17613: _parse_names,
}


def parse_client_hello(message):
    """
    Parses a ClientHello handshake message - type, three-byte length, body, the form
    AsioSslStreamWrapper::enableClientHelloCapture() produces - exactly as crypto/TlsClientHello.h
    does for every field JA3 and JA4 read, and also the fields a profile needs. Everything is kept as
    it was sent, GREASE included.
    """
    cursor = Cursor(message)
    if cursor.u8() != 1:
        raise ParseError("the handshake message is not a ClientHello")
    body = cursor.sub(int.from_bytes(cursor.take(3), "big"))
    info = {
        "legacy_version": body.u16(), "cipher_suites": [], "extensions": [], "extension_lengths": [],
        "supported_groups": [], "ec_point_formats": [], "signature_algorithms": [], "alpn": [],
        "supported_versions": [], "key_shares": [], "has_server_name": False, "server_name": None,
        "other": {},
    }
    info["highest_version"] = info["legacy_version"]
    body.take(32)
    info["session_id_length"] = len(body.take(body.u8()))
    suites = body.sub(body.u16())
    while not suites.at_end():
        info["cipher_suites"].append(suites.u16())
    info["compression_methods"] = list(body.take(body.u8()))
    if body.at_end():
        return info
    extensions = body.sub(body.u16())
    while not extensions.at_end():
        kind = extensions.u16()
        extension = extensions.sub(extensions.u16())
        info["extensions"].append(kind)
        info["extension_lengths"].append(len(extension.data))
        if kind in _STRICT_PARSERS:
            _STRICT_PARSERS[kind](info, extension, kind)
        elif kind in _LENIENT_PARSERS:
            try:
                _LENIENT_PARSERS[kind](info, extension, kind)
            except ParseError:
                info["other"]["malformed %s" % EXTENSION_NAMES.get(kind, kind)] = True
    return info


def ja3_string(info):
    fields = [str(info["legacy_version"])]
    for key in ("cipher_suites", "extensions", "supported_groups"):
        fields.append("-".join(str(value) for value in without_grease(info[key])))
    fields.append("-".join(str(value) for value in info["ec_point_formats"]))
    return ",".join(fields)


def ja3_hash(text):
    try:
        digest = hashlib.md5(text.encode("ascii"), usedforsecurity=False)
    except TypeError:
        digest = hashlib.md5(text.encode("ascii"))
    return digest.hexdigest()


_JA4_VERSIONS = {0x0304: "13", 0x0303: "12", 0x0302: "11", 0x0301: "10", 0x0300: "s3", 0x0200: "s2", 0x0100: "s1"}


def _is_alphanumeric(byte):
    return 0x30 <= byte <= 0x39 or 0x41 <= byte <= 0x5A or 0x61 <= byte <= 0x7A


def ja4_prefix(info):
    text = "t" + _JA4_VERSIONS.get(info["highest_version"], "00") + ("d" if info["has_server_name"] else "i")
    text += "%02d%02d" % (
        min(99, len(without_grease(info["cipher_suites"]))), min(99, len(without_grease(info["extensions"]))))
    if not info["alpn"] or not info["alpn"][0]:
        return text + "00"
    first, last = info["alpn"][0][0], info["alpn"][0][-1]
    if _is_alphanumeric(first) and _is_alphanumeric(last):
        return text + chr(first) + chr(last)
    return text + ("%02x" % first)[0] + ("%02x" % last)[1]


def ja4_cipher_input(info):
    return ",".join(_hex_list(sorted(without_grease(info["cipher_suites"]))))


def ja4_extension_input(info):
    # server_name and ALPN are counted in the prefix but left out here, and the signature
    # algorithms keep the order they were sent in
    values = sorted(value for value in without_grease(info["extensions"]) if value not in (0x0000, 0x0010))
    text = ",".join(_hex_list(values))
    signatures = without_grease(info["signature_algorithms"])
    if signatures:
        text += "_" + ",".join(_hex_list(signatures))
    return text


def ja4(info):
    def section(text):
        return hashlib.sha256(text.encode("ascii")).hexdigest()[:12] if text else "0" * 12
    return "_".join([ja4_prefix(info), section(ja4_cipher_input(info)), section(ja4_extension_input(info))])


def summarize_hello(message):
    """The hello as a profile may use it: GREASE stripped, the extension set, JA3 and JA4 (F5(c), (d))."""
    info = parse_client_hello(message)
    extensions = without_grease(info["extensions"])
    groups = without_grease(info["supported_groups"])
    shares = [share for share in info["key_shares"] if not is_grease(share[0])]
    grease = {key: len(info[key]) - len(without_grease(info[key])) for key in (
        "cipher_suites", "extensions", "supported_groups", "signature_algorithms", "supported_versions")}
    grease["key_shares"] = len(info["key_shares"]) - len(shares)
    ja3 = ja3_string(info)
    resumed = EXTENSION_PRE_SHARED_KEY in info["extensions"]
    return {
        "length": len(message),
        "legacy_version": "%04x" % info["legacy_version"],
        "supported_versions": _hex_list(without_grease(info["supported_versions"])),
        "highest_version": "%04x" % info["highest_version"],
        "cipher_suites": _hex_list(without_grease(info["cipher_suites"])),
        "extensions": _hex_list(extensions),
        "extension_names": [EXTENSION_NAMES.get(value, "unknown") for value in extensions],
        "extension_lengths": [length for value, length in zip(info["extensions"], info["extension_lengths"])
                              if not is_grease(value)],
        "extension_set": _hex_list(sorted(set(extensions))),
        "supported_groups": _hex_list(groups),
        "supported_group_names": [GROUP_NAMES.get(value, "unknown") for value in groups],
        "key_shares": [{"group": "%04x" % group, "name": GROUP_NAMES.get(group, "unknown"), "key_length": length}
                       for group, length in shares],
        "signature_algorithms": _hex_list(without_grease(info["signature_algorithms"])),
        "ec_point_formats": info["ec_point_formats"],
        "alpn": [protocol.decode("latin-1") for protocol in info["alpn"]],
        "sni": {"present": info["has_server_name"], "host_name": info["server_name"]},
        "session_id_length": info["session_id_length"],
        "compression_methods": info["compression_methods"],
        "other_extensions": info["other"],
        "grease_stripped": grease,
        "ja3": {"string": ja3, "hash": ja3_hash(ja3)},
        "ja4": {"fingerprint": ja4(info), "prefix": ja4_prefix(info), "cipher_input": ja4_cipher_input(info),
                "extension_input": ja4_extension_input(info)},
        "pre_shared_key": resumed,
        "profile_source": {
            "eligible": not resumed,
            "reason": ("carries pre_shared_key (41): a resumed connection's hello, refused as a profile source"
                       if resumed else "a full handshake's hello"),
        },
    }


# ==================================================================================================
# HPACK (RFC 7541): the decoder, with the Huffman code and the static table, and a literal-only encoder
# ==================================================================================================

# Appendix B's code is canonical: the codes ascend with (length, symbol), so the lengths alone fix
# every code. Symbol 256 is EOS. _build_huffman_codes() proves the result is a complete code
_HUFFMAN_LENGTHS = (
    (5, (48, 49, 50, 97, 99, 101, 105, 111, 115, 116)),
    (6, (32, 37, 45, 46, 47, 51, 52, 53, 54, 55, 56, 57, 61, 65, 95, 98, 100, 102, 103, 104, 108, 109, 110,
         112, 114, 117)),
    (7, (58, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 89, 106,
         107, 113, 118, 119, 120, 121, 122)),
    (8, (38, 42, 44, 59, 88, 90)),
    (10, (33, 34, 40, 41, 63)),
    (11, (39, 43, 124)),
    (12, (35, 62)),
    (13, (0, 36, 64, 91, 93, 126)),
    (14, (94, 125)),
    (15, (60, 96, 123)),
    (19, (92, 195, 208)),
    (20, (128, 130, 131, 162, 184, 194, 224, 226)),
    (21, (153, 161, 167, 172, 176, 177, 179, 209, 216, 217, 227, 229, 230)),
    (22, (129, 132, 133, 134, 136, 146, 154, 156, 160, 163, 164, 169, 170, 173, 178, 181, 185, 186, 187, 189,
          190, 196, 198, 228, 232, 233)),
    (23, (1, 135, 137, 138, 139, 140, 141, 143, 147, 149, 150, 151, 152, 155, 157, 158, 165, 166, 168, 174, 175,
          180, 182, 183, 188, 191, 197, 231, 239)),
    (24, (9, 142, 144, 145, 148, 159, 171, 206, 215, 225, 236, 237)),
    (25, (199, 207, 234, 235)),
    (26, (192, 193, 200, 201, 202, 205, 210, 213, 218, 219, 238, 240, 242, 243, 255)),
    (27, (203, 204, 211, 212, 214, 221, 222, 223, 241, 244, 245, 246, 247, 248, 250, 251, 252, 253, 254)),
    (28, (2, 3, 4, 5, 6, 7, 8, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 127,
          220, 249)),
    (30, (10, 13, 22, 256)),
)


def _build_huffman_codes():
    codes, code, previous = [None] * 257, 0, None
    for length, symbols in _HUFFMAN_LENGTHS:
        if previous is not None:
            code <<= length - previous
        for symbol in symbols:
            if codes[symbol] is not None:
                raise AssertionError("Huffman symbol %d is listed twice" % symbol)
            codes[symbol] = (code, length)
            code += 1
        previous = length
    if None in codes or code != 1 << 30:
        raise AssertionError("the Huffman code is not complete")
    return tuple(codes)


HUFFMAN_CODES = _build_huffman_codes()
HUFFMAN_EOS = 256
_HUFFMAN_DECODE = {(length, code): symbol for symbol, (code, length) in enumerate(HUFFMAN_CODES)}

STATIC_TABLE = (
    (":authority", ""), (":method", "GET"), (":method", "POST"), (":path", "/"), (":path", "/index.html"),
    (":scheme", "http"), (":scheme", "https"), (":status", "200"), (":status", "204"), (":status", "206"),
    (":status", "304"), (":status", "400"), (":status", "404"), (":status", "500"), ("accept-charset", ""),
    ("accept-encoding", "gzip, deflate"), ("accept-language", ""), ("accept-ranges", ""), ("accept", ""),
    ("access-control-allow-origin", ""), ("age", ""), ("allow", ""), ("authorization", ""),
    ("cache-control", ""), ("content-disposition", ""), ("content-encoding", ""), ("content-language", ""),
    ("content-length", ""), ("content-location", ""), ("content-range", ""), ("content-type", ""),
    ("cookie", ""), ("date", ""), ("etag", ""), ("expect", ""), ("expires", ""), ("from", ""), ("host", ""),
    ("if-match", ""), ("if-modified-since", ""), ("if-none-match", ""), ("if-range", ""),
    ("if-unmodified-since", ""), ("last-modified", ""), ("link", ""), ("location", ""), ("max-forwards", ""),
    ("proxy-authenticate", ""), ("proxy-authorization", ""), ("range", ""), ("referer", ""), ("refresh", ""),
    ("retry-after", ""), ("server", ""), ("set-cookie", ""), ("strict-transport-security", ""),
    ("transfer-encoding", ""), ("user-agent", ""), ("vary", ""), ("via", ""), ("www-authenticate", ""),
)


def huffman_decode(data):
    out, code, length = bytearray(), 0, 0
    for byte in data:
        for shift in range(7, -1, -1):
            code, length = (code << 1) | ((byte >> shift) & 1), length + 1
            symbol = _HUFFMAN_DECODE.get((length, code))
            if symbol == HUFFMAN_EOS:
                raise HpackError("EOS inside a Huffman string")
            if symbol is not None:
                out.append(symbol)
                code, length = 0, 0
    # section 5.2: at most seven bits of padding, and they must be the most significant bits of EOS
    if length > 7 or code != (1 << length) - 1:
        raise HpackError("invalid Huffman padding")
    return bytes(out)


def decode_integer(data, pos, prefix_bits):
    if pos >= len(data):
        raise HpackError("a truncated integer")
    limit = (1 << prefix_bits) - 1
    value, pos = data[pos] & limit, pos + 1
    if value < limit:
        return value, pos
    shift = 0
    while True:
        if pos >= len(data):
            raise HpackError("a truncated integer")
        byte, pos = data[pos], pos + 1
        value += (byte & 0x7F) << shift
        shift += 7
        if value >= 1 << 32:
            raise HpackError("an integer beyond 32 bits")
        if not byte & 0x80:
            return value, pos


def decode_string(data, pos):
    if pos >= len(data):
        raise HpackError("a truncated string")
    huffman = bool(data[pos] & 0x80)
    length, pos = decode_integer(data, pos, 7)
    if pos + length > len(data):
        raise HpackError("a string longer than its header block")
    raw = bytes(data[pos:pos + length])
    return (huffman_decode(raw) if huffman else raw).decode("latin-1"), huffman, pos + length


class HpackDecoder:
    """
    One connection's decoder: its dynamic table is kept across every header block, in order, and
    each field comes back with how it was represented - which is what a profile's indexing policy and
    cookie crumbling are derived from (F5(h)).
    """

    def __init__(self, limit=4096):
        self.limit = limit          # SETTINGS_HEADER_TABLE_SIZE as this side announced it
        self.max_size = limit
        self.entries = []           # newest first
        self.size = 0

    def _entry(self, index):
        if index == 0:
            raise HpackError("index 0")
        if index <= len(STATIC_TABLE):
            return STATIC_TABLE[index - 1], "static"
        if index - len(STATIC_TABLE) > len(self.entries):
            raise HpackError("index %d is beyond the dynamic table" % index)
        return self.entries[index - len(STATIC_TABLE) - 1], "dynamic"

    def _evict_to(self, size):
        while self.entries and self.size > size:
            name, value = self.entries.pop()
            self.size -= len(name) + len(value) + 32

    def _add(self, name, value):
        size = len(name) + len(value) + 32
        self._evict_to(self.max_size - size)
        if size <= self.max_size:
            self.entries.insert(0, (name, value))
            self.size += size

    def decode(self, block):
        fields, updates, pos = [], [], 0
        while pos < len(block):
            first = block[pos]
            if first & 0x80:
                index, pos = decode_integer(block, pos, 7)
                (name, value), table = self._entry(index)
                fields.append({"name": name, "value": value, "representation": "indexed", "index": index,
                               "table": table})
            elif first & 0x20 and not first & 0x40:
                size, pos = decode_integer(block, pos, 5)
                if size > self.limit:
                    raise HpackError("a table size update to %d, above the %d allowed" % (size, self.limit))
                self.max_size = size
                self._evict_to(size)
                updates.append({"size": size, "after_fields": len(fields)})
            else:
                if first & 0x40:
                    prefix, representation = 6, "incremental"
                else:
                    prefix, representation = 4, "never_indexed" if first & 0x10 else "without_indexing"
                index, pos = decode_integer(block, pos, prefix)
                field = {"representation": representation, "name_index": index}
                if index:
                    (field["name"], _), field["name_table"] = self._entry(index)
                    field["name_huffman"] = None
                else:
                    field["name"], field["name_huffman"], pos = decode_string(block, pos)
                field["value"], field["value_huffman"], pos = decode_string(block, pos)
                if representation == "incremental":
                    self._add(field["name"], field["value"])
                fields.append(field)
        return fields, updates


def encode_integer(value, prefix_bits, flags):
    limit = (1 << prefix_bits) - 1
    if value < limit:
        return bytes([flags | value])
    out, value = bytearray([flags | limit]), value - limit
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def encode_header_block(fields, size_update=None):
    """Literals without indexing, new names, no Huffman: the responses never touch the browser's table."""
    out = bytearray()
    if size_update is not None:
        out += encode_integer(size_update, 5, 0x20)
    for name, value in fields:
        out.append(0x00)
        for text in (name, value):
            raw = text.encode("latin-1")
            out += encode_integer(len(raw), 7, 0x00) + raw
    return bytes(out)


# ==================================================================================================
# HTTP/2 (RFC 9113) and HTTP/1.1, followed from the client's decrypted bytes
# ==================================================================================================

H2_PREFACE = b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
DATA, HEADERS, PRIORITY, RST_STREAM, SETTINGS, PUSH_PROMISE, PING, GOAWAY, WINDOW_UPDATE, CONTINUATION = range(10)
PRIORITY_UPDATE = 0x10
FRAME_NAMES = {DATA: "DATA", HEADERS: "HEADERS", PRIORITY: "PRIORITY", RST_STREAM: "RST_STREAM",
               SETTINGS: "SETTINGS", PUSH_PROMISE: "PUSH_PROMISE", PING: "PING", GOAWAY: "GOAWAY",
               WINDOW_UPDATE: "WINDOW_UPDATE", CONTINUATION: "CONTINUATION", PRIORITY_UPDATE: "PRIORITY_UPDATE"}
FLAG_ACK = FLAG_END_STREAM = 0x01
FLAG_END_HEADERS, FLAG_PADDED, FLAG_PRIORITY = 0x04, 0x08, 0x20
NO_ERROR, PROTOCOL_ERROR, FRAME_SIZE_ERROR, COMPRESSION_ERROR = 0x0, 0x1, 0x6, 0x9
SETTING_NAMES = {1: "HEADER_TABLE_SIZE", 2: "ENABLE_PUSH", 3: "MAX_CONCURRENT_STREAMS", 4: "INITIAL_WINDOW_SIZE",
                 5: "MAX_FRAME_SIZE", 6: "MAX_HEADER_LIST_SIZE", 8: "ENABLE_CONNECT_PROTOCOL",
                 9: "NO_RFC7540_PRIORITIES"}
PSEUDO_SHORT = {":method": "m", ":authority": "a", ":scheme": "s", ":path": "p"}
MAX_FRAME_SIZE = 16384
SERVER_SETTINGS = ((3, 100),)       # MAX_CONCURRENT_STREAMS; nothing else, so every other value is the default


_FLAGS = {
    DATA: ((0x01, "END_STREAM"), (0x08, "PADDED")),
    HEADERS: ((0x01, "END_STREAM"), (0x04, "END_HEADERS"), (0x08, "PADDED"), (0x20, "PRIORITY")),
    SETTINGS: ((0x01, "ACK"),), PUSH_PROMISE: ((0x04, "END_HEADERS"), (0x08, "PADDED")), PING: ((0x01, "ACK"),),
    CONTINUATION: ((0x04, "END_HEADERS"),),
}


def _flag_names(frame_type, flags):
    """The flags by name; bits the frame type does not define are kept as a number, since a browser set them."""
    defined = _FLAGS.get(frame_type, ())
    names = [name for bit, name in defined if flags & bit]
    undefined = flags & ~sum(bit for bit, _ in defined)
    return names + (["0x%02x" % undefined] if undefined else [])


def _priority_fields(data):
    dependency = int.from_bytes(data[:4], "big")
    # the wire carries weight - 1; baselib's Http2Profile holds the wire byte
    return {"exclusive": bool(dependency >> 31), "depends_on": dependency & 0x7FFFFFFF,
            "weight": data[4] + 1, "weight_on_wire": data[4]}


def _without_padding(payload, flags):
    if not flags & FLAG_PADDED:
        return payload, 0
    if not payload or payload[0] >= len(payload):
        raise ProtocolError(PROTOCOL_ERROR, "padding longer than its frame")
    return payload[1:len(payload) - payload[0]], payload[0]


class H2Follower:
    """
    Follows the client's half of an HTTP/2 connection: every frame in order, every header block
    decoded on the connection's one HPACK table, and the requests. The live server answers from it
    and the analysis re-runs it over client-plain.bin, so what is served and what is recorded come
    from the same code and the same bytes. feed() returns the events the server acts on.
    """

    def __init__(self):
        self.buffer = bytearray()
        self.consumed = 0
        self.preface_seen = False
        self.decoder = HpackDecoder()
        self.frames, self.requests, self.streams = [], [], {}
        self.block = None
        self.highest_stream = 0

    def feed(self, data):
        self.buffer += data
        events = []
        if not self.preface_seen:
            if len(self.buffer) < len(H2_PREFACE):
                if not H2_PREFACE.startswith(bytes(self.buffer)):
                    raise ProtocolError(PROTOCOL_ERROR, "the client did not send the HTTP/2 preface")
                return events
            if bytes(self.buffer[:len(H2_PREFACE)]) != H2_PREFACE:
                raise ProtocolError(PROTOCOL_ERROR, "the client did not send the HTTP/2 preface")
            del self.buffer[:len(H2_PREFACE)]
            self.consumed, self.preface_seen = len(H2_PREFACE), True
        while len(self.buffer) >= 9:
            length = int.from_bytes(self.buffer[:3], "big")
            if length > MAX_FRAME_SIZE:
                raise ProtocolError(FRAME_SIZE_ERROR, "a frame of %d bytes; this side allows 16384" % length)
            if len(self.buffer) < 9 + length:
                break
            frame_type, flags = self.buffer[3], self.buffer[4]
            stream = int.from_bytes(self.buffer[5:9], "big") & 0x7FFFFFFF
            payload, offset = bytes(self.buffer[9:9 + length]), self.consumed
            del self.buffer[:9 + length]
            self.consumed += 9 + length
            events += self._frame(offset, frame_type, flags, stream, payload)
        return events

    def _frame(self, offset, frame_type, flags, stream, payload):
        entry = {"offset": offset, "type": FRAME_NAMES.get(frame_type, "UNKNOWN_0x%02x" % frame_type),
                 "stream": stream, "flags": _flag_names(frame_type, flags), "length": len(payload)}
        self.frames.append(entry)
        if self.block is not None and frame_type != CONTINUATION:
            raise ProtocolError(PROTOCOL_ERROR, "a %s frame inside a header block" % entry["type"])
        # The follower is a recorder: it refuses only what would stop it from following the stream. So a
        # SETTINGS ACK that carries a payload, or a WINDOW_UPDATE of 0, is recorded as it came, although
        # RFC 9113 6.5 and 6.9 make them errors
        sizes = {PRIORITY: 5, RST_STREAM: 4, PING: 8, WINDOW_UPDATE: 4}
        if frame_type in sizes and len(payload) != sizes[frame_type]:
            raise ProtocolError(FRAME_SIZE_ERROR, "a %s frame of %d bytes" % (entry["type"], len(payload)))
        if frame_type == SETTINGS:
            if stream != 0:
                raise ProtocolError(PROTOCOL_ERROR, "SETTINGS on stream %d" % stream)
            if len(payload) % 6:
                raise ProtocolError(FRAME_SIZE_ERROR, "a SETTINGS frame of %d bytes" % len(payload))
            if flags & FLAG_ACK:
                return []
            entry["settings"] = [{"id": key, "name": SETTING_NAMES.get(key, "UNKNOWN"), "value": value}
                                 for key, value in struct.iter_unpack(">HI", payload)]
            return [("settings", entry["settings"])]
        if frame_type == WINDOW_UPDATE:
            entry["increment"] = int.from_bytes(payload, "big") & 0x7FFFFFFF
            return [("window_update", stream, entry["increment"])]
        if frame_type == PRIORITY:
            entry["priority"] = _priority_fields(payload)
            return []
        if frame_type == HEADERS:
            return self._headers(entry, flags, stream, payload)
        if frame_type == CONTINUATION:
            if self.block is None or stream != self.block["stream"]:
                raise ProtocolError(PROTOCOL_ERROR, "a CONTINUATION frame without its header block")
            self.block["fragments"].append(payload)
            self.block["frames"].append("CONTINUATION")
            return self._finish_block() if flags & FLAG_END_HEADERS else []
        if frame_type == DATA:
            data, _ = _without_padding(payload, flags)
            request = self.streams.get(stream)
            if request is None:
                return []
            request["data_bytes"] += len(data)
            if flags & FLAG_END_STREAM and not request["ended"]:
                request["ended"] = True
                return [("request", request)]
            return []
        if frame_type == PING:
            return [] if flags & FLAG_ACK else [("ping", payload)]
        if frame_type == GOAWAY and len(payload) >= 8:
            entry["last_stream"] = int.from_bytes(payload[:4], "big") & 0x7FFFFFFF
            entry["error_code"] = int.from_bytes(payload[4:8], "big")
            return []
        if frame_type == RST_STREAM:
            entry["error_code"] = int.from_bytes(payload, "big")
            return [("rst_stream", stream)]
        if frame_type == PRIORITY_UPDATE and len(payload) >= 4:
            entry["prioritized_stream"] = int.from_bytes(payload[:4], "big") & 0x7FFFFFFF
            entry["priority_field_value"] = payload[4:].decode("latin-1")
            return []
        entry["payload_hex"] = payload[:64].hex()
        return []

    def _headers(self, entry, flags, stream, payload):
        if stream == 0:
            raise ProtocolError(PROTOCOL_ERROR, "HEADERS on stream 0")
        fragment, _ = _without_padding(payload, flags)
        priority = None
        if flags & FLAG_PRIORITY:
            if len(fragment) < 5:
                raise ProtocolError(FRAME_SIZE_ERROR, "a HEADERS frame too short for its priority")
            priority, fragment = _priority_fields(fragment[:5]), fragment[5:]
        entry["priority"] = priority
        self.block = {"stream": stream, "flags": flags, "priority": priority, "fragments": [fragment],
                      "frames": ["HEADERS"], "offset": entry["offset"]}
        return self._finish_block() if flags & FLAG_END_HEADERS else []

    def _finish_block(self):
        block, self.block = self.block, None
        data = b"".join(block["fragments"])
        try:
            fields, updates = self.decoder.decode(data)
        except HpackError as error:
            raise ProtocolError(COMPRESSION_ERROR, "HPACK: %s" % error)
        header_block = {"offset": block["offset"], "frames": block["frames"], "length": len(data),
                        "priority": block["priority"], "end_stream": bool(block["flags"] & FLAG_END_STREAM),
                        "table_size_updates": updates, "fields": fields}
        stream = block["stream"]
        request = self.streams.get(stream)
        if request is not None:
            request["trailers"] = header_block
            if header_block["end_stream"] and not request["ended"]:
                request["ended"] = True
                return [("request", request)]
            return []
        pseudo = [field["name"] for field in fields if field["name"].startswith(":")]
        values = {field["name"]: field["value"] for field in fields if field["name"].startswith(":")}
        request = {
            "stream": stream, "kind": classify(values.get(":path")), "method": values.get(":method"),
            "scheme": values.get(":scheme"), "authority": values.get(":authority"), "path": values.get(":path"),
            "pseudo_header_order": pseudo, "pseudo_header_short": ",".join(PSEUDO_SHORT.get(n, n) for n in pseudo),
            "headers": [[field["name"], field["value"]] for field in fields if not field["name"].startswith(":")],
            "cookie_fields": sum(1 for field in fields if field["name"] == "cookie"),
            "header_block": header_block, "data_bytes": 0, "ended": header_block["end_stream"],
        }
        self.streams[stream] = request
        self.requests.append(request)
        self.highest_stream = max(self.highest_stream, stream)
        return [("stream_open", stream)] + ([("request", request)] if request["ended"] else [])


class H1Follower:
    """
    Follows the client's HTTP/1.1 requests, keeping each head exactly as it was sent: the names' case
    and order are what the second pass is for.
    """

    def __init__(self):
        self.buffer = bytearray()
        self.consumed = 0
        self.requests = []
        self.pending = None

    def partial(self):
        return bool(self.buffer) or self.pending is not None

    def feed(self, data):
        self.buffer += data
        complete = []
        while True:
            if self.pending is None:
                end = self.buffer.find(b"\r\n\r\n")
                if end < 0:
                    if len(self.buffer) > MAX_REQUEST_HEAD_BYTES:
                        raise ProtocolError(PROTOCOL_ERROR, "a request head over 64 KiB")
                    return complete
                head = bytes(self.buffer[:end + 4])
                del self.buffer[:end + 4]
                self.pending = self._parse_head(head)
                self.consumed += len(head)
            request = self.pending
            if len(self.buffer) < request["body_length"]:
                return complete
            del self.buffer[:request["body_length"]]
            self.consumed += request["body_length"]
            self.pending, request["index"] = None, len(self.requests)
            self.requests.append(request)
            complete.append(request)

    def _parse_head(self, head):
        lines = head[:-4].decode("latin-1").split("\r\n")
        parts = lines[0].split(" ")
        if len(parts) != 3:
            raise ProtocolError(PROTOCOL_ERROR, "a malformed request line")
        headers = []
        for line in lines[1:]:
            name, colon, value = line.partition(":")
            if not colon or not name or name != name.strip():
                raise ProtocolError(PROTOCOL_ERROR, "a malformed header line")
            headers.append([name, value.strip(" \t")])
        if any(name.lower() == "transfer-encoding" for name, _ in headers):
            raise ProtocolError(PROTOCOL_ERROR, "a request body with Transfer-Encoding")
        lengths = [value for name, value in headers if name.lower() == "content-length"]
        if lengths and not re.fullmatch(r"[0-9]{1,15}", lengths[0]):
            raise ProtocolError(PROTOCOL_ERROR, "a malformed Content-Length")
        return {"offset": self.consumed, "raw_head": head.decode("latin-1"), "request_line": lines[0],
                "method": parts[0], "target": parts[1], "version": parts[2], "headers": headers,
                "body_length": int(lengths[0]) if lengths else 0, "kind": classify(parts[1])}


# ==================================================================================================
# The page: a navigation, a script and an image (two subresource destinations), and a fetch
# ==================================================================================================

KINDS = {"/": "navigation", "/capture.js": "subresource-script", "/capture.png": "subresource-image",
         "/fetch": "fetch", "/favicon.ico": "favicon"}
REQUIRED_KINDS = ("navigation", "subresource-script", "subresource-image", "fetch")
COOKIES = ("capture_a=1; Path=/; Secure; SameSite=Lax", "capture_b=2; Path=/; Secure; SameSite=Lax")
REASONS = {200: "OK", 400: "Bad Request", 404: "Not Found"}

PAGE = b"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>capture</title>
<script src="/capture.js"></script>
</head>
<body>
<h1>Capture in progress</h1>
<p><img src="/capture.png" width="32" height="32" alt="image"> The green square is the image.</p>
<p id="status">Waiting for the fetch...</p>
<p>Do not reload this page and do not click on it. Go back to the README once the line above says so.</p>
</body>
</html>
"""

SCRIPT = b"""window.addEventListener("load", function () {
  var status = document.getElementById("status");
  fetch("/fetch").then(function (response) {
    return response.json();
  }).then(function (result) {
    if (result.protocol !== "h2") {
      status.textContent = "Capture complete (" + result.protocol + " pass).";
    } else if (result.missing.length === 0) {
      status.textContent = "Capture complete: the navigation, the script, the image and the fetch all " +
        "arrived on connection " + result.connection + ".";
    } else {
      status.textContent = "Not all on one connection: " + result.missing.join(", ") +
        " did not arrive on connection " + result.connection + ". The capture still has them all.";
    }
  }, function (error) {
    status.textContent = "The fetch failed: " + error;
  });
});
"""


def _png_square():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)                   # 1x1, 8-bit RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"\x00\x00\xa0\x00"))
            + chunk(b"IEND", b""))


IMAGE = _png_square()


def classify(path):
    return KINDS.get((path or "").split("?", 1)[0], "other")


def build_response(kind, connection, kinds_seen, protocol):
    """The status, the headers (lower case, in order) and the body for one request."""
    headers = []
    if kind == "navigation":
        status, content_type, body = 200, "text/html; charset=utf-8", PAGE
        headers = [("set-cookie", cookie) for cookie in COOKIES]
    elif kind == "subresource-script":
        status, content_type, body = 200, "text/javascript; charset=utf-8", SCRIPT
    elif kind == "subresource-image":
        status, content_type, body = 200, "image/png", IMAGE
    elif kind == "fetch":
        missing = [required for required in REQUIRED_KINDS if required not in kinds_seen]
        status, content_type = 200, "application/json"
        body = json.dumps({"connection": connection, "protocol": protocol, "kinds": sorted(set(kinds_seen)),
                           "missing": missing}).encode("ascii")
    else:
        status, content_type, body = 404, "text/plain; charset=utf-8", b"not found\n"
    return status, [("content-type", content_type), ("content-length", str(len(body))),
                    ("cache-control", "no-store")] + headers, body


# ==================================================================================================
# The live server
# ==================================================================================================

def validate_host(host):
    """The capture host name (F5(e)): never an IP literal, which sends no SNI, nor localhost."""
    name = host.strip().rstrip(".").lower()
    try:
        ipaddress.ip_address(name.strip("[]"))
    except ValueError:
        pass
    else:
        raise CaptureError("%s is an IP literal, and a browser sends no SNI to one: use a hosts-file name "
                           "such as %s" % (host, DEFAULT_HOST))
    if re.fullmatch(r"0x[0-9a-f]*|[0-9]+", name.rsplit(".", 1)[-1]):
        raise CaptureError("%s ends in a number, which a browser reads as an IPv4 address: use a hosts-file "
                           "name such as %s" % (host, DEFAULT_HOST))
    if name == "localhost" or name.endswith(".localhost") or "." not in name:
        raise CaptureError("%s is special-cased by browsers or not a full name: use a hosts-file name such as "
                           "%s" % (host, DEFAULT_HOST))
    return name


def make_server_context(cert_file, key_file, http1):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert_file, key_file)
    context.set_alpn_protocols(["http/1.1"] if http1 else ["h2", "http/1.1"])
    # F5(a): no session ticket, so no later hello of the session carries pre_shared_key. Over TLS 1.3
    # OP_NO_TICKET alone only makes the tickets stateful; num_tickets = 0 is what stops them
    context.options |= ssl.OP_NO_TICKET
    context.num_tickets = 0
    return context


class Connection(threading.Thread):
    """One accepted connection: the capture, TLS, the conversation, the record, and a lingering close."""

    def __init__(self, server, sock, peer, number):
        super().__init__(name="capture-connection-%d" % number, daemon=True)
        self.server, self.sock, self.number = server, sock, number
        self.directory = os.path.join(server.out_dir, "conn-%04d" % number)
        os.makedirs(self.directory)
        self.raw = open(os.path.join(self.directory, RAW_FILE), "wb")
        self.plain = open(os.path.join(self.directory, PLAIN_FILE), "wb")
        self.plain_length = 0
        self.records = RecordStream()
        self.partial_request = threading.Event()   # set while an HTTP/1.1 request is part way in
        self.finishing_request = threading.Event()  # set once stopped, while waiting for such a request's rest
        self.meta = {"connection": number, "peer": "%s:%s" % peer[:2], "accepted_at": now_text(),
                     "request_times": {}}
        self.tls = self.incoming = self.outgoing = self.follower = None
        self.client_closed = self.goaway_sent = self.size_update_due = False
        self.client_gone = None                     # "closed" at its end of stream, "reset" at a reset or abort
        self.peer_initial_window, self.peer_max_frame, self.connection_window = 65535, MAX_FRAME_SIZE, 65535
        self.windows, self.pending = {}, {}

    def run(self):
        try:
            if self._handshake():
                if self.meta["alpn"] == "h2":
                    self.follower = H2Follower()
                    self._serve_h2()
                else:
                    self.follower = H1Follower()
                    self._serve_h1()
        except Exception as error:
            self.meta.setdefault("error", without_paths("%s: %s" % (type(error).__name__, error)))
            self.meta.setdefault("traceback", without_paths(traceback.format_exc()))
        finally:
            try:
                self._linger_close()
            except Exception as error:
                self.meta.setdefault("error", without_paths("%s: %s" % (type(error).__name__, error)))
                self.sock.close()
            finally:
                self._finish()

    # ---- bytes in and out ----

    def _read(self, timeout):
        """What the socket has, b"" at the client's end of stream, None when nothing came in time.
        Every byte is kept, and fed to the hello's reassembly, before OpenSSL sees any of it."""
        ready, _, _ = select.select([self.sock], [], [], timeout)
        if not ready:
            return None
        try:
            data = self.sock.recv(65536)
        except (ConnectionResetError, ConnectionAbortedError):
            # a browser quitting may reset its sockets; on Windows a close can also arrive as an abort
            self.client_gone = "reset"
            return b""
        if not data:
            self.client_gone = self.client_gone or "closed"
        else:
            self.raw.write(data)
            self.raw.flush()
            had_hello = self.records.hello is not None
            self.records.feed(data)
            if not had_hello and self.records.hello is not None:
                with open(os.path.join(self.directory, HELLO_FILE), "wb") as handle:
                    handle.write(self.records.hello)
        return data

    def _gone_text(self):
        return "the client reset the connection" if self.client_gone == "reset" else "the client closed the connection"

    def _next_data(self):
        while True:
            data = self._read(POLL_SECONDS)
            if data is not None:
                return data
            if self.server.stop_requested:
                return None

    def _decrypt(self):
        chunks = []
        while True:
            try:
                chunk = self.tls.read(65536)
            except ssl.SSLWantReadError:
                break
            except ssl.SSLZeroReturnError:
                chunk = b""
            if not chunk:
                self.client_closed = True           # the client's close_notify
                break
            chunks.append(chunk)
        data = b"".join(chunks)
        if data:
            self.plain.write(data)
            self.plain.flush()
            self.plain_length += len(data)
        return data

    def _flush(self):
        data = self.outgoing.read()
        if data and self.client_gone is None:
            try:
                self.sock.sendall(data)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                self.client_gone = "reset"          # the client is gone; nothing more is sent

    def _handshake(self):
        self.incoming, self.outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
        self.tls = self.server.context.wrap_bio(self.incoming, self.outgoing, server_side=True)
        while True:
            try:
                self.tls.do_handshake()
                break
            except ssl.SSLWantReadError:
                self._flush()
                data = self._next_data()
                if not data:
                    self.meta["handshake"] = "incomplete: " + (
                        "the tool was stopped" if data is None else self._gone_text())
                    return False
                self.incoming.write(data)
            except ssl.SSLError as error:
                self._flush()                       # the alert OpenSSL wrote, if any
                self.meta["handshake"] = "failed: %s" % error
                return False
        self._flush()
        self.meta.update(handshake="complete", tls_version=self.tls.version(), cipher=self.tls.cipher()[0],
                         alpn=self.tls.selected_alpn_protocol())
        return True

    def _kinds(self):
        return [request["kind"] for request in self.follower.requests]

    # ---- HTTP/2 ----

    def _send_frame(self, frame_type, flags, stream, payload):
        self.tls.write(len(payload).to_bytes(3, "big") + bytes((frame_type, flags)) + stream.to_bytes(4, "big")
                       + payload)

    def _serve_h2(self):
        self._send_frame(SETTINGS, 0, 0, b"".join(struct.pack(">HI", key, value) for key, value in SERVER_SETTINGS))
        while True:
            plaintext = self._decrypt()
            if plaintext:
                try:
                    events = self.follower.feed(plaintext)
                except ProtocolError as error:
                    self.meta["ended_by"] = "an HTTP/2 error from the client: %s" % error.text
                    return self._goaway(error.code, error.text)
                for event in events:
                    self._on_h2_event(event)
                    self._pump()                    # in order: DATA a frame allows goes before a later PING's ACK
            self._flush()
            if self.client_gone is not None:
                self.meta["ended_by"] = self._gone_text()
                return
            if self.client_closed:
                self.meta["ended_by"] = "the client sent close_notify"
                return
            if self.server.stop_requested:
                self.meta["ended_by"] = "the tool was stopped"
                return self._goaway(NO_ERROR)
            data = self._next_data()
            if data is None:
                self.meta["ended_by"] = "the tool was stopped"
                return self._goaway(NO_ERROR)
            if not data:
                self.meta["ended_by"] = self._gone_text()
                return
            self.incoming.write(data)

    def _on_h2_event(self, event):
        if event[0] == "settings":
            for setting in event[1]:
                if setting["id"] == 4:
                    delta = setting["value"] - self.peer_initial_window
                    self.peer_initial_window = setting["value"]
                    for stream in self.windows:
                        self.windows[stream] += delta
                elif setting["id"] == 5:
                    self.peer_max_frame = max(MAX_FRAME_SIZE, setting["value"])
                elif setting["id"] == 1 and setting["value"] < 4096:
                    self.size_update_due = True     # our encoder never indexes; say so at the next block
            self._send_frame(SETTINGS, FLAG_ACK, 0, b"")
        elif event[0] == "ping":
            self._send_frame(PING, FLAG_ACK, 0, event[1])
        elif event[0] == "window_update":
            if event[1] == 0:
                self.connection_window += event[2]
            elif event[1] in self.windows:
                self.windows[event[1]] += event[2]
        elif event[0] == "rst_stream":
            self.pending.pop(event[1], None)
            self.windows.pop(event[1], None)
        elif event[0] == "stream_open":
            self.windows.setdefault(event[1], self.peer_initial_window)
        elif event[0] == "request":
            request = event[1]
            self.meta["request_times"][str(request["stream"])] = now_text()
            status, headers, body = build_response(request["kind"], self.number, self._kinds(), "h2")
            block = encode_header_block([(":status", str(status))] + headers, 0 if self.size_update_due else None)
            self.size_update_due = False
            self._send_frame(HEADERS, FLAG_END_HEADERS | (0 if body else FLAG_END_STREAM), request["stream"], block)
            if body:
                self.windows.setdefault(request["stream"], self.peer_initial_window)
                self.pending[request["stream"]] = bytearray(body)

    def _pump(self):
        for stream in list(self.pending):
            data = self.pending[stream]
            while data:
                size = min(len(data), self.connection_window, self.windows[stream], self.peer_max_frame)
                if size <= 0:
                    break
                chunk = bytes(data[:size])
                del data[:size]
                self.connection_window -= size
                self.windows[stream] -= size
                self._send_frame(DATA, 0 if data else FLAG_END_STREAM, stream, chunk)
            if not data:
                del self.pending[stream], self.windows[stream]

    def _goaway(self, code, text=""):
        if self.goaway_sent:
            return
        self.goaway_sent = True
        last = self.follower.highest_stream
        self.meta["goaway"] = {"last_stream": last, "error_code": code}
        self._send_frame(GOAWAY, 0, 0, struct.pack(">II", last, code) + text.encode("ascii", "replace")[:128])
        self._flush()

    # ---- HTTP/1.1 ----

    def _respond_h1(self, status, headers, body, closing):
        lines = ["HTTP/1.1 %d %s" % (status, REASONS.get(status, "Status"))]
        lines += ["%s: %s" % ("-".join(part.capitalize() for part in name.split("-")), value)
                  for name, value in headers]
        if closing:
            lines.append("Connection: close")
        self.tls.write(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + body)

    def _serve_h1(self):
        deadline = None
        while True:
            plaintext = self._decrypt()
            if plaintext:
                try:
                    requests = self.follower.feed(plaintext)
                except ProtocolError as error:
                    self._respond_h1(400, [("content-length", "0")], b"", True)
                    self.meta["ended_by"] = "an HTTP/1.1 error from the client: %s" % error.text
                    return self._flush()
                for request in requests:
                    closing = self.server.stop_requested
                    self.meta["request_times"][str(request["index"])] = now_text()
                    self._respond_h1(*build_response(request["kind"], self.number, self._kinds(), "http/1.1"),
                                     closing=closing)
                    if closing:
                        self.meta["ended_by"] = "the tool was stopped (Connection: close)"
                        return self._flush()
                if self.follower.partial():
                    self.partial_request.set()
                else:
                    self.partial_request.clear()
            self._flush()
            if self.client_gone is not None:
                self.meta["ended_by"] = self._gone_text()
                return
            if self.client_closed:
                self.meta["ended_by"] = "the client sent close_notify"
                return
            if self.server.stop_requested:          # on every pass, not only an idle one: a trickle cannot hold it
                if deadline is None:
                    deadline = time.monotonic() + self.server.linger_seconds
                if not self.follower.partial():
                    self.meta["ended_by"] = "the tool was stopped"
                    return
                self.finishing_request.set()        # it is answered, with Connection: close, when it is whole
                if time.monotonic() >= deadline:
                    self.meta["ended_by"] = "the tool was stopped with a request incomplete"
                    return
            data = self._read(POLL_SECONDS)
            if data is None:
                continue
            if not data:
                self.meta["ended_by"] = self._gone_text()
                return
            self.incoming.write(data)

    # ---- the end of a connection ----

    def _linger_close(self):
        """
        GOAWAY or Connection: close has gone out where there was a conversation. Then close_notify,
        shutdown( SHUT_WR ), and reads - recorded, never answered - until the client's end of stream
        or the bound, and only then close: the client's late segments meet an open socket, so they
        draw no reset (G4).
        """
        linger = {"bound_seconds": self.server.linger_seconds, "plain_offset": self.plain_length}
        decrypting = self.meta.get("handshake") == "complete"
        start, outcome, raw_bytes = time.monotonic(), "the bound was reached", 0
        if self.client_gone is None and self.tls is not None:
            try:
                self.tls.unwrap()
            except (ssl.SSLError, ValueError):
                pass                                # SSLWantReadError: our close_notify is written
            try:
                self._flush()
            except OSError as error:
                linger["send_error"] = str(error)
        if self.client_gone is None:
            try:
                self.sock.shutdown(socket.SHUT_WR)
            except OSError as error:
                linger["shutdown_error"] = str(error)
        # A client already gone has nothing more to send; there is no one to linger for
        while self.client_gone is None:
            remaining = self.server.linger_seconds - (time.monotonic() - start)
            if remaining <= 0:
                break
            try:
                data = self._read(min(remaining, POLL_SECONDS))
            except OSError as error:
                outcome = "error: %s" % error
                break
            if data is None:
                continue
            if not data:
                break
            raw_bytes += len(data)
            if decrypting:
                try:
                    self.incoming.write(data)
                    plaintext = self._decrypt()
                    if plaintext and self.follower is not None:
                        self.follower.feed(plaintext)
                except (ssl.SSLError, ProtocolError):
                    decrypting = False
        if self.client_gone is not None:
            # a reset after the client's own close_notify or end of stream is still the client closing
            closed = self.client_gone == "closed" or self.client_closed
            outcome = "the client closed" if closed else "the client reset the connection"
        linger.update(outcome=outcome, seconds=round(time.monotonic() - start, 3), raw_bytes=raw_bytes,
                      plain_bytes=self.plain_length - linger["plain_offset"])
        self.meta["linger"] = linger
        self.sock.close()

    def _finish(self):
        self.meta["closed_at"] = now_text()
        self.raw.close()
        self.plain.close()
        write_json(os.path.join(self.directory, META_FILE), self.meta)


class CaptureServer:
    """The listener, one thread per connection, and the session's records."""

    def __init__(self, cert_file, key_file, out_dir, browser, host=DEFAULT_HOST, listen="127.0.0.1", port=443,
                 http1=False, linger_seconds=DEFAULT_LINGER_SECONDS):
        self.host = validate_host(host)
        self.context = make_server_context(cert_file, key_file, http1)
        self.out_dir = os.path.abspath(out_dir)
        if os.path.isdir(self.out_dir) and os.listdir(self.out_dir):
            raise CaptureError("%s is not empty: use a new directory for each capture" % self.out_dir)
        os.makedirs(self.out_dir, exist_ok=True)
        self.listen, self.port, self.linger_seconds = listen, port, linger_seconds
        self.stop_event = threading.Event()
        self.connections, self.lock = [], threading.Lock()
        self.listener = self.acceptor = None
        self.session = {
            "tool_version": TOOL_VERSION, "browser": browser, "host": self.host, "listen": listen,
            "pass": "http/1.1" if http1 else "h2", "alpn_offered": ["http/1.1"] if http1 else ["h2", "http/1.1"],
            "server_settings": [{"id": key, "value": value} for key, value in SERVER_SETTINGS],
            "session_tickets": "off (OP_NO_TICKET, num_tickets = 0)", "linger_seconds": linger_seconds,
            "interpreter": interpreter_report(),
        }

    @property
    def stop_requested(self):
        return self.stop_event.is_set()

    @property
    def url(self):
        return "https://%s/" % self.host if self.port == 443 else "https://%s:%d/" % (self.host, self.port)

    def start(self):
        family = socket.AF_INET6 if ":" in self.listen else socket.AF_INET
        self.listener = socket.socket(family, socket.SOCK_STREAM)
        if os.name != "nt":
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.listener.bind((self.listen, self.port))
        except OSError as error:
            self.listener.close()
            raise CaptureError("cannot listen on %s port %d: %s" % (self.listen, self.port, error))
        self.listener.listen(64)
        self.listener.settimeout(POLL_SECONDS)
        self.port = self.listener.getsockname()[1]
        self.session.update(port=self.port, url=self.url, started_at=now_text())
        write_json(os.path.join(self.out_dir, SESSION_FILE), self.session)
        self.acceptor = threading.Thread(target=self._accept, name="capture-acceptor", daemon=True)
        self.acceptor.start()

    def _accept(self):
        number = 0
        try:
            while not self.stop_event.is_set():
                try:
                    sock, peer = self.listener.accept()
                except socket.timeout:
                    continue
                except OSError:
                    self.stop_event.wait(POLL_SECONDS)
                    continue
                number += 1
                sock.settimeout(SEND_TIMEOUT_SECONDS)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                connection = Connection(self, sock, peer, number)
                with self.lock:
                    self.connections.append(connection)
                connection.start()
        finally:
            self.listener.close()

    def request_stop(self):
        self.stop_event.set()

    def wait(self):
        """Once stopped: every connection closed gently, then the summary derived from the bytes."""
        self.acceptor.join()
        with self.lock:
            connections = list(self.connections)
        for connection in connections:
            connection.join(self.linger_seconds + SEND_TIMEOUT_SECONDS)
        self.session["stopped_at"] = now_text()
        write_json(os.path.join(self.out_dir, SESSION_FILE), self.session)
        return analyse_session(self.out_dir)

    def stop(self):
        self.request_stop()
        return self.wait()


# ==================================================================================================
# The analysis: everything in the summary is derived from the stored bytes
# ==================================================================================================

def analyse_connection(directory):
    meta = read_json(os.path.join(directory, META_FILE)) or {}
    records = RecordStream()
    records.feed(read_bytes(os.path.join(directory, RAW_FILE)))
    plain = read_bytes(os.path.join(directory, PLAIN_FILE))
    result = {"connection": meta.get("connection", os.path.basename(directory)), "meta": meta,
              "record_types": records.record_types}
    if records.hello is not None:
        try:
            result["hello"] = summarize_hello(records.hello)
        except ParseError as error:
            result["hello_error"] = "the ClientHello does not parse: %s" % error
        if meta.get("tls_version") == "TLSv1.3" and 22 in records.record_types[records.hello_records:]:
            result["hello_retry_request"] = True
    else:
        result["hello_error"] = records.error or "no complete ClientHello was received"
    late = (meta.get("linger") or {}).get("plain_offset")
    times = meta.get("request_times", {})
    if plain:
        is_h2 = meta.get("alpn") == "h2" or plain.startswith(H2_PREFACE)
        follower = H2Follower() if is_h2 else H1Follower()
        section = {"error": None}
        try:
            follower.feed(plain)
        except (ProtocolError, ValueError) as error:
            section["error"] = getattr(error, "text", str(error))
        for request in follower.requests:
            key = str(request["stream"] if is_h2 else request["index"])
            start = request["header_block"]["offset"] if is_h2 else request["offset"]
            request.update(received_at=times.get(key), after_close_began=late is not None and start >= late)
        section.update(requests=follower.requests, trailing_bytes=len(follower.buffer))
        if is_h2:
            for frame in follower.frames:
                frame["after_close_began"] = late is not None and frame["offset"] >= late
            opening = []
            for frame in follower.frames:
                opening.append(frame)
                if frame["type"] == "HEADERS":
                    break
            section.update(
                opening=[frame["type"] for frame in opening],
                settings=next((frame["settings"] for frame in follower.frames if "settings" in frame), None),
                connection_window_update=next((frame["increment"] for frame in opening
                                               if frame["type"] == "WINDOW_UPDATE" and frame["stream"] == 0), None),
                priority_frames=[frame for frame in opening if frame["type"] == "PRIORITY"],
                frames=follower.frames)
        result["http2" if is_h2 else "http1"] = section
    write_json(os.path.join(directory, CONNECTION_FILE), result)
    return result


def analyse_session(out_dir):
    session = read_json(os.path.join(out_dir, SESSION_FILE)) or {}
    directories = sorted(name for name in os.listdir(out_dir)
                         if name.startswith("conn-") and os.path.isdir(os.path.join(out_dir, name)))
    rows, requests, ja4_values = [], [], {}
    for directory in directories:
        analysed = analyse_connection(os.path.join(out_dir, directory))
        meta, hello = analysed["meta"], analysed.get("hello")
        exchanges = (analysed.get("http2") or analysed.get("http1") or {}).get("requests", [])
        if hello is not None:
            ja4_values.setdefault(hello["ja4"]["fingerprint"], []).append(analysed["connection"])
        rows.append({
            "connection": analysed["connection"], "accepted_at": meta.get("accepted_at"),
            "handshake": meta.get("handshake"), "tls_version": meta.get("tls_version"), "alpn": meta.get("alpn"),
            "sni": hello["sni"]["host_name"] if hello else None, "ja4": hello["ja4"]["fingerprint"] if hello else None,
            "pre_shared_key": hello["pre_shared_key"] if hello else None,
            "hello_retry_request": bool(analysed.get("hello_retry_request")),
            "kinds": [request["kind"] for request in exchanges if not request["after_close_began"]],
            "ended_by": meta.get("ended_by"), "lingering_close": (meta.get("linger") or {}).get("outcome"),
        })
        for request in exchanges:
            requests.append({"connection": analysed["connection"], "stream": request.get("stream"),
                             "kind": request["kind"], "method": request["method"],
                             "path": request.get("path", request.get("target")),
                             "received_at": request["received_at"], "after_close_began": request["after_close_began"]})
    requests.sort(key=lambda request: (request["received_at"] is None, request["received_at"] or "",
                                       str(request["connection"])))
    navigations = []
    for request in requests:
        if request["kind"] == "navigation" and not request["after_close_began"]:
            if request["connection"] not in navigations:
                navigations.append(request["connection"])
    by_number = {row["connection"]: row for row in rows}
    hellos = [row for row in rows if row["ja4"]]
    sources = [number for number in navigations if by_number[number]["ja4"] and not by_number[number]["pre_shared_key"]]
    checks = {
        "connections_recorded": len(rows),
        "hellos_recorded": len(hellos),
        "ja4_identical_across_hellos": len(ja4_values) == 1,
        "ja4_values": ja4_values,
        "hellos_refused_as_profile_source": [row["connection"] for row in rows if row["pre_shared_key"]],
        "navigation_connections": navigations,
        "profile_source_connection": sources[0] if sources else None,
        "kinds_missing_from_navigation_connection": {
            str(number): [kind for kind in REQUIRED_KINDS if kind not in by_number[number]["kinds"]]
            for number in navigations},
        "alpn_selected": {str(row["connection"]): row["alpn"] for row in rows},
        "sni_is_the_capture_host": bool(hellos) and all(row["sni"] == session.get("host") for row in hellos),
        "hello_retry_requests": [row["connection"] for row in rows if row["hello_retry_request"]],
        "handshakes_not_complete": [row["connection"] for row in rows if row["handshake"] != "complete"],
        "lingering_close_outcomes": {str(row["connection"]): row["lingering_close"] for row in rows},
    }
    summary = {"tool_version": TOOL_VERSION, "session": session, "checks": checks, "connections": rows,
               "requests": requests}
    write_json(os.path.join(out_dir, SUMMARY_FILE), summary)
    return summary


def format_report(summary):
    checks, session = summary["checks"], summary["session"]
    lines = ["%s: %d connection(s), %d ClientHello(s)" % (session.get("browser"), checks["connections_recorded"],
                                                         checks["hellos_recorded"])]
    ja4_values = checks["ja4_values"]
    if checks["ja4_identical_across_hellos"]:
        lines.append("JA4 identical across every hello: yes, %s" % next(iter(ja4_values)))
    else:
        lines.append("PROBLEM: JA4 differs across the hellos: %s" % json.dumps(ja4_values))
    if checks["hellos_refused_as_profile_source"]:
        lines.append("PROBLEM: hello(s) carrying pre_shared_key, refused as a profile source: connection(s) %s"
                     % checks["hellos_refused_as_profile_source"])
    if not checks["navigation_connections"]:
        lines.append("PROBLEM: no navigation was recorded")
    for number in checks["navigation_connections"]:
        missing = checks["kinds_missing_from_navigation_connection"][str(number)]
        lines.append("Navigation on connection %s, ALPN %s: %s" % (
            number, checks["alpn_selected"][str(number)],
            "the script, the image and the fetch on the same connection" if not missing
            else "not on this connection: " + ", ".join(missing)))
    if not checks["sni_is_the_capture_host"]:
        lines.append("PROBLEM: not every hello named %s in its SNI" % session.get("host"))
    for key, text in (("hello_retry_requests", "a HelloRetryRequest on connection(s)"),
                      ("handshakes_not_complete", "no complete handshake on connection(s)")):
        if checks[key]:
            lines.append("Note: %s %s" % (text, checks[key]))
    outcomes = sorted(set(str(value) for value in checks["lingering_close_outcomes"].values()))
    lines.append("Lingering closes: %s" % ", ".join(outcomes))
    return "\n".join(lines)


# ==================================================================================================
# Certificates, from the openssl command-line tool: the standard library cannot make one
# ==================================================================================================

_CA_CONFIG = """[req]
prompt = no
distinguished_name = dn
[dn]
CN = {name}
[v3_ca]
basicConstraints = critical, CA:TRUE, pathlen:0
keyUsage = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash
"""

_LEAF_CONFIG = """[req]
prompt = no
distinguished_name = dn
[dn]
CN = {host}
[v3_leaf]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = DNS:{host}
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
"""


def find_openssl(explicit=None):
    if explicit:
        return explicit
    found = shutil.which("openssl")
    if found:
        return found
    for variable in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"):
        base = os.environ.get(variable)
        for relative in (("Git", "usr", "bin", "openssl.exe"), ("Git", "mingw64", "bin", "openssl.exe")):
            if base and os.path.isfile(os.path.join(base, *relative)):
                return os.path.join(base, *relative)
    return None


def inside_git_work_tree(path):
    path = os.path.realpath(path)
    while True:
        if os.path.exists(os.path.join(path, ".git")):
            return True
        parent = os.path.dirname(path)
        if parent == path:
            return False
        path = parent


def make_certificates(host, directory, openssl=None, days=7):
    """
    A per-session CA and a leaf for the capture host. The CA's key is deleted as soon as the leaf is
    signed, so nothing else can ever be signed by the CA the operator trusts, and no key is written
    inside a git work tree, so none can be committed.
    """
    host = validate_host(host)
    if inside_git_work_tree(directory):
        raise CaptureError("%s is inside a git work tree: keep the session's keys outside any repository"
                           % os.path.realpath(directory))
    tool = find_openssl(openssl)
    if tool is None:
        raise CaptureError("no openssl command was found; on Windows it comes with Git for Windows: pass "
                           "--openssl \"C:\\Program Files\\Git\\usr\\bin\\openssl.exe\"")
    directory = os.path.realpath(directory)
    os.makedirs(directory, exist_ok=True)
    path = {name: os.path.join(directory, name)
            for name in ("ca.cnf", "leaf.cnf", "ca.key", "ca.pem", "leaf.key", "leaf.csr", "leaf.pem")}
    if any(os.path.exists(path[name]) for name in ("ca.pem", "leaf.key", "leaf.pem")):
        raise CaptureError("%s already holds certificates: use a new directory for each session" % directory)
    name = "swblocks capture CA %s (temporary)" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    with open(path["ca.cnf"], "w", encoding="ascii") as handle:
        handle.write(_CA_CONFIG.format(name=name))
    with open(path["leaf.cnf"], "w", encoding="ascii") as handle:
        handle.write(_LEAF_CONFIG.format(host=host))
    serial = str(int.from_bytes(os.urandom(8), "big") >> 1)
    steps = [
        ["genrsa", "-out", path["ca.key"], "2048"],
        ["req", "-new", "-x509", "-key", path["ca.key"], "-sha256", "-days", str(days), "-config", path["ca.cnf"],
         "-extensions", "v3_ca", "-out", path["ca.pem"]],
        ["genrsa", "-out", path["leaf.key"], "2048"],
        ["req", "-new", "-key", path["leaf.key"], "-config", path["leaf.cnf"], "-out", path["leaf.csr"]],
        ["x509", "-req", "-in", path["leaf.csr"], "-CA", path["ca.pem"], "-CAkey", path["ca.key"], "-set_serial",
         serial, "-days", str(days), "-sha256", "-extfile", path["leaf.cnf"], "-extensions", "v3_leaf",
         "-out", path["leaf.pem"]],
    ]
    try:
        for step in steps:
            done = subprocess.run([tool] + step, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if done.returncode != 0:
                raise CaptureError("openssl %s failed:\n%s" % (step[0], done.stdout.decode("latin-1")))
    finally:
        for temporary in ("ca.key", "leaf.csr", "ca.cnf", "leaf.cnf"):
            if os.path.exists(path[temporary]):
                os.remove(path[temporary])
    make_server_context(path["leaf.pem"], path["leaf.key"], False)         # the key matches the certificate
    with open(path["ca.pem"], encoding="ascii") as handle:
        der = ssl.PEM_cert_to_DER_cert(handle.read())
    info = {"name": name, "host": host, "days": days, "created_at": now_text(), "ca": path["ca.pem"],
            "leaf": path["leaf.pem"], "key": path["leaf.key"], "sha1": hashlib.sha1(der).hexdigest().upper(),
            "sha256": hashlib.sha256(der).hexdigest().upper()}
    write_json(os.path.join(directory, "ca-info.json"), info)
    return info


def trust_instructions(info):
    ca, sha1 = info["ca"], info["sha1"]
    if sys.platform == "win32":
        return ["Trust the CA for this session (Windows asks you to confirm; answer Yes):",
                "    certutil -user -addstore Root \"%s\"" % ca,
                "Firefox keeps its own store: Settings > Privacy & Security > Certificates > View Certificates >",
                "Authorities > Import..., choose ca.pem, tick \"Trust this CA to identify websites\".",
                "When the session is over, remove it:",
                "    certutil -user -delstore Root %s" % sha1]
    if sys.platform == "darwin":
        return ["Trust the CA for this session (macOS asks for your password):",
                "    security add-trusted-cert -r trustRoot -k ~/Library/Keychains/login.keychain-db \"%s\"" % ca,
                "When the session is over, remove it and its trust setting:",
                "    security delete-certificate -t -Z %s ~/Library/Keychains/login.keychain-db" % sha1]
    return ["Trust %s in the client for this session, and remove it afterwards." % ca]


# ==================================================================================================
# The command line
# ==================================================================================================

def _default_out(browser, http1):
    slug = "".join(character if character.isalnum() or character in ".-" else "-" for character in browser)
    return "capture-%s-%s-%s" % (slug.strip("-")[:40], "http1" if http1 else "h2",
                                 datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))


def _serve(args):
    out = args.out or _default_out(args.browser, args.http1)
    server = CaptureServer(os.path.join(args.cert_dir, "leaf.pem"), os.path.join(args.cert_dir, "leaf.key"), out,
                           args.browser, args.host, args.listen, args.port, args.http1, args.linger_seconds)
    server.start()
    print("Listening on %s port %d; ALPN offered: %s" % (args.listen, server.port,
                                                       ", ".join(server.session["alpn_offered"])))
    print("Recording to %s" % server.out_dir)
    print("Type this into the browser's address bar: %s" % server.url)
    print("Press Ctrl-C here when the browser steps are done.")
    sys.stdout.flush()
    for name in ("SIGTERM", "SIGBREAK"):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), lambda *_: server.request_stop())
    try:
        while not server.stop_event.wait(0.5):
            pass
    except KeyboardInterrupt:
        pass
    print("Stopping: closing every connection gently (up to %s s)..." % args.linger_seconds)
    sys.stdout.flush()
    summary = server.stop()
    print(format_report(summary))
    print("Summary: %s" % os.path.join(server.out_dir, SUMMARY_FILE))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="capture.py", description=__doc__.strip().splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="print the TLS library and whether this Python can capture")
    certs = commands.add_parser("certs", help="make a per-session CA and a leaf with the openssl command")
    certs.add_argument("--dir", required=True, help="a new directory for this session's certificates")
    certs.add_argument("--host", default=DEFAULT_HOST)
    certs.add_argument("--openssl", help="the openssl executable, if it is not on PATH")
    certs.add_argument("--days", type=int, default=7)
    serve = commands.add_parser("serve", help="serve the capture page and record every connection")
    serve.add_argument("--browser", required=True, help="the exact browser version, as its About page shows it")
    serve.add_argument("--cert-dir", required=True, help="the directory 'certs' wrote")
    serve.add_argument("--out", help="a new directory for the capture (default: capture-<browser>-<pass>-<time>)")
    serve.add_argument("--host", default=DEFAULT_HOST)
    serve.add_argument("--listen", default="127.0.0.1", help="the only address listened on")
    serve.add_argument("--port", type=int, default=443)
    serve.add_argument("--http1", action="store_true", help="offer only http/1.1: the second pass")
    serve.add_argument("--linger-seconds", type=float, default=DEFAULT_LINGER_SECONDS)
    analyse = commands.add_parser("analyse", help="derive the summary again from a capture's bytes")
    analyse.add_argument("directory")
    hello = commands.add_parser("hello", help="parse a ClientHello: a handshake message or TLS records")
    hello.add_argument("file")
    args = parser.parse_args(argv)
    try:
        if args.command in ("check", "certs", "serve"):
            report = interpreter_report()
            print("capture.py %s: Python %s (%s), %s (TLS 1.3: %s, ALPN: %s)" % (
                TOOL_VERSION, report["python"], sys.executable, report["openssl"], report["tls13"], report["alpn"]))
            problems = interpreter_problems()
            if problems:
                print("This Python cannot capture: " + "; ".join(problems) + ".")
                print("Install Python from python.org (or Homebrew on a Mac) and run this with it.")
                return 2
        if args.command == "check":
            print("This Python can capture.")
        elif args.command == "certs":
            info = make_certificates(args.host, args.dir, args.openssl, args.days)
            print("Wrote %s (the CA to trust), %s and %s (for the tool)." % (info["ca"], info["leaf"], info["key"]))
            print("The CA's private key is already deleted: nothing more can be signed with it.")
            print("CA: %s, SHA-1 %s" % (info["name"], info["sha1"]))
            print("\n".join(trust_instructions(info)))
        elif args.command == "serve":
            return _serve(args)
        elif args.command == "analyse":
            print(format_report(analyse_session(os.path.abspath(args.directory))))
        else:
            if not os.path.isfile(args.file):
                raise CaptureError("no such file: %s" % args.file)
            data = read_bytes(args.file)
            if data[:1] == b"\x16":
                records = RecordStream()
                records.feed(data)
                if records.hello is None:
                    raise CaptureError(records.error or "no complete ClientHello in %s" % args.file)
                data = records.hello
            print(json.dumps(summarize_hello(data), indent=2))
    except (CaptureError, ParseError, OSError) as error:
        print("Error: %s" % error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
