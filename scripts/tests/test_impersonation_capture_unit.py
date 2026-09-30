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
Unit tests for scripts/impersonation/capture.py, the capture endpoint of browser impersonation.

The expected values are never the tool's own output played back:

- HPACK is checked against RFC 7541 Appendix C, block by block with the dynamic table after each,
  and every block is asserted to appear byte for byte in utf_baselib_h2core/TestHpack.h, whose cases
  assert baselib's own decoder on those same bytes. The Huffman code and the static table are compared
  row by row with that file's transcriptions of Appendices B and A.
- The ClientHello parser, JA3 and JA4 are checked on the vectors of
  utf_baselib_h2profiles/TestTlsClientHello.h, built here field by field as they are built there, and
  every expected literal is asserted to still appear verbatim in that header.

Neither C++ file is ever written to; both are read as the record of what baselib asserts.
"""

import ast
import fractions
import json
import re
import ssl
import subprocess
import sys
from pathlib import Path

import pytest

REPOSITORY = Path(__file__).resolve().parents[2]
TOOL = REPOSITORY / "scripts" / "impersonation" / "capture.py"
BASELIB_TLS_CLIENT_HELLO_TESTS = REPOSITORY / "src" / "utests" / "utf_baselib_h2profiles" / "TestTlsClientHello.h"
BASELIB_HPACK_TESTS = REPOSITORY / "src" / "utests" / "utf_baselib_h2core" / "TestHpack.h"

sys.path.insert(0, str(TOOL.parent))

import capture  # noqa: E402


def octets(text):
    """The RFC's hex dumps, spacing and all."""
    return bytes.fromhex(re.sub(r"[^0-9a-fA-F]", "", text))


def baselib_hpack_blocks():
    """Every octets( "..." ) argument of TestHpack.h, adjacent string literals joined, as bytes."""
    source = BASELIB_HPACK_TESTS.read_text(encoding="utf-8")
    blocks = set()
    for match in re.finditer(r'octets\(\s*((?:"[^"]*"\s*)+)\)', source):
        blocks.add(octets("".join(re.findall(r'"([^"]*)"', match.group(1)))))
    return blocks


# ========== HPACK: integers, Huffman and the static table ==========

class TestHpackPrimitives:
    """RFC 7541 Appendix C.1, and Appendices A and B against baselib's transcriptions of them."""

    @pytest.mark.parametrize("value, prefix, encoded", [(10, 5, "0a"), (1337, 5, "1f 9a 0a"), (42, 8, "2a")])
    def test_integers_of_appendix_c1(self, value, prefix, encoded):
        """C.1.1 to C.1.3, both directions; the bits above the prefix are masked (0xea is 10 too)."""
        assert capture.encode_integer(value, prefix, 0x00) == octets(encoded)
        assert capture.decode_integer(octets(encoded), 0, prefix) == (value, len(octets(encoded)))
        assert capture.decode_integer(octets("ea"), 0, 5) == (10, 1)

    def test_integers_that_are_errors(self):
        """Truncated, and beyond 32 bits."""
        for block in ("1f", "7f ff ff ff ff 0f"):
            with pytest.raises(capture.HpackError):
                capture.decode_integer(octets(block), 0, 7 if block.startswith("7f") else 5)

    def test_the_huffman_code_is_complete(self):
        """257 symbols, a code whose Kraft sum is exactly one, EOS thirty ones, '/' as section 5.2 says."""
        codes = capture.HUFFMAN_CODES
        assert len(codes) == 257
        assert sum(fractions.Fraction(1, 1 << length) for _, length in codes) == 1
        assert codes[capture.HUFFMAN_EOS] == (0x3FFFFFFF, 30)
        assert codes[ord("/")] == (0x18, 6)

    def test_every_huffman_row_equals_baselibs_appendix_b(self):
        """The tool derives the code from the lengths alone; TestHpack.h transcribes all 257 rows."""
        source = BASELIB_HPACK_TESTS.read_text(encoding="utf-8")
        block = source[source.index("g_rows[ HpackHuffman::CODE_COUNT ]"):]
        block = block[:block.index("};")]
        rows = [(int(code, 16), int(length)) for code, length in re.findall(r"\{\s*0x([0-9a-fA-F]+)U,\s*(\d+)U\s*\}", block)]
        assert len(rows) == 257
        assert list(capture.HUFFMAN_CODES) == rows

    def test_huffman_strings_and_the_padding_rules(self):
        """C.4.1's authority, C.6.2's status, and the three rules of section 5.2."""
        assert capture.huffman_decode(octets("f1e3 c2e5 f23a 6ba0 ab90 f4ff")) == b"www.example.com"
        assert capture.huffman_decode(octets("640e ff")) == b"307"
        assert capture.huffman_decode(octets("07")) == b"0"
        assert capture.huffman_decode(b"") == b""
        for broken in ("00", "07 ff", "ff ff ff ff"):
            with pytest.raises(capture.HpackError):
                capture.huffman_decode(octets(broken))

    def test_the_static_table_equals_baselibs_appendix_a(self):
        source = BASELIB_HPACK_TESTS.read_text(encoding="utf-8")
        block = source[source.index("UTF_AUTO_TEST_CASE( Hpack_StaticTableTests )"):]
        block = block[:block.index("};")]
        rows = re.findall(r'\{\s*"([^"]*)",\s*"([^"]*)"\s*\}', block)
        assert len(rows) == 61
        assert list(capture.STATIC_TABLE) == rows


# ========== HPACK: RFC 7541 Appendix C.2 to C.6 ==========

REQUEST_ONE = [(":method", "GET"), (":scheme", "http"), (":path", "/"), (":authority", "www.example.com")]
REQUEST_TWO = REQUEST_ONE + [("cache-control", "no-cache")]
REQUEST_THREE = [(":method", "GET"), (":scheme", "https"), (":path", "/index.html"),
                 (":authority", "www.example.com"), ("custom-key", "custom-value")]
REQUEST_TABLES = [
    [(":authority", "www.example.com", 57)],
    [("cache-control", "no-cache", 53), (":authority", "www.example.com", 57)],
    [("custom-key", "custom-value", 54), ("cache-control", "no-cache", 53), (":authority", "www.example.com", 57)],
]

DATE_1, DATE_2 = "Mon, 21 Oct 2013 20:13:21 GMT", "Mon, 21 Oct 2013 20:13:22 GMT"
COOKIE = "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"
RESPONSE_ONE = [(":status", "302"), ("cache-control", "private"), ("date", DATE_1),
                ("location", "https://www.example.com")]
RESPONSE_TWO = [(":status", "307")] + RESPONSE_ONE[1:]
RESPONSE_THREE = [(":status", "200"), ("cache-control", "private"), ("date", DATE_2),
                  ("location", "https://www.example.com"), ("content-encoding", "gzip"), ("set-cookie", COOKIE)]
RESPONSE_TABLES = [
    [("location", "https://www.example.com", 63), ("date", DATE_1, 65), ("cache-control", "private", 52),
     (":status", "302", 42)],
    [(":status", "307", 42), ("location", "https://www.example.com", 63), ("date", DATE_1, 65),
     ("cache-control", "private", 52)],
    [("set-cookie", COOKIE, 98), ("content-encoding", "gzip", 52), ("date", DATE_2, 65)],
]

CHAINS = {
    "C.3": (4096, [
        "8286 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d",
        "8286 84be 5808 6e6f 2d63 6163 6865",
        "8287 85bf 400a 6375 7374 6f6d 2d6b 6579 0c63 7573 746f 6d2d 7661 6c75 65",
    ], [REQUEST_ONE, REQUEST_TWO, REQUEST_THREE], REQUEST_TABLES),
    "C.4": (4096, [
        "8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4 ff",
        "8286 84be 5886 a8eb 1064 9cbf",
        "8287 85bf 4088 25a8 49e9 5ba9 7d7f 8925 a849 e95b b8e8 b4bf",
    ], [REQUEST_ONE, REQUEST_TWO, REQUEST_THREE], REQUEST_TABLES),
    "C.5": (256, [
        "4803 3330 3258 0770 7269 7661 7465 611d 4d6f 6e2c 2032 3120 4f63 7420 3230 3133 2032 303a 3133 3a32"
        "3120 474d 546e 1768 7474 7073 3a2f 2f77 7777 2e65 7861 6d70 6c65 2e63 6f6d",
        "4803 3330 37c1 c0bf",
        "88c1 611d 4d6f 6e2c 2032 3120 4f63 7420 3230 3133 2032 303a 3133 3a32 3220 474d 54c0 5a04 677a 6970"
        "7738 666f 6f3d 4153 444a 4b48 514b 425a 584f 5157 454f 5049 5541 5851 5745 4f49 553b 206d 6178 2d61"
        "6765 3d33 3630 303b 2076 6572 7369 6f6e 3d31",
    ], [RESPONSE_ONE, RESPONSE_TWO, RESPONSE_THREE], RESPONSE_TABLES),
    "C.6": (256, [
        "4882 6402 5885 aec3 771a 4b61 96d0 7abe 9410 54d4 44a8 2005 9504 0b81 66e0 82a6 2d1b ff6e 919d 29ad"
        "1718 63c7 8f0b 97c8 e9ae 82ae 43d3",
        "4883 640e ffc1 c0bf",
        "88c1 6196 d07a be94 1054 d444 a820 0595 040b 8166 e084 a62d 1bff c05a 839b d9ab 77ad 94e7 821d d7f2"
        "e6c7 b335 dfdf cd5b 3960 d5af 2708 7f36 72c1 ab27 0fb5 291f 9587 3160 65c0 03ed 4ee5 b106 3d50 07",
    ], [RESPONSE_ONE, RESPONSE_TWO, RESPONSE_THREE], RESPONSE_TABLES),
}

SINGLE_BLOCKS = {
    "C.2.1": ("400a 6375 7374 6f6d 2d6b 6579 0d63 7573 746f 6d2d 6865 6164 6572",
              ("custom-key", "custom-header"), "incremental", [("custom-key", "custom-header", 55)]),
    "C.2.2": ("040c 2f73 616d 706c 652f 7061 7468", (":path", "/sample/path"), "without_indexing", []),
    "C.2.3": ("1008 7061 7373 776f 7264 0673 6563 7265 74", ("password", "secret"), "never_indexed", []),
    "C.2.4": ("82", (":method", "GET"), "indexed", []),
}


def table_of(decoder):
    return [(name, value, len(name) + len(value) + 32) for name, value in decoder.entries]


class TestHpackAppendixC:
    """Every block of C.2 to C.6, the dynamic table checked after each, one decoder per chain."""

    @pytest.mark.parametrize("label", sorted(SINGLE_BLOCKS))
    def test_the_four_representations_of_c2(self, label):
        block, field, representation, table = SINGLE_BLOCKS[label]
        decoder = capture.HpackDecoder()
        fields, updates = decoder.decode(octets(block))
        assert [(f["name"], f["value"]) for f in fields] == [field]
        assert fields[0]["representation"] == representation
        assert updates == []
        assert table_of(decoder) == table

    @pytest.mark.parametrize("label", sorted(CHAINS))
    def test_each_chain_on_one_table(self, label):
        """The same table from block to block is what a connection's decoder is."""
        size, blocks, lists, tables = CHAINS[label]
        decoder = capture.HpackDecoder(size)
        for block, expected, table in zip(blocks, lists, tables):
            fields, _ = decoder.decode(octets(block))
            assert [(f["name"], f["value"]) for f in fields] == expected
            assert table_of(decoder) == table
            assert decoder.size == sum(entry[2] for entry in table)

    def test_c4_and_c6_record_the_huffman_flags(self):
        """The flag is what a profile's encoder choice is read from (F5(h))."""
        decoder = capture.HpackDecoder()
        fields, _ = decoder.decode(octets(CHAINS["C.4"][1][0]))
        assert fields[3] == {"name": ":authority", "value": "www.example.com", "representation": "incremental",
                             "name_index": 1, "name_table": "static", "name_huffman": None, "value_huffman": True}
        decoder.decode(octets(CHAINS["C.4"][1][1]))
        fields, _ = decoder.decode(octets(CHAINS["C.4"][1][2]))
        assert (fields[4]["name_huffman"], fields[4]["value_huffman"]) == (True, True)
        assert fields[3] == {"name": ":authority", "value": "www.example.com", "representation": "indexed",
                             "index": 63, "table": "dynamic"}

    def test_every_block_is_one_baselib_decodes_in_its_own_tests(self):
        """The cross-check with baselib's decoder: its TestHpack.h asserts the same outputs on the same bytes."""
        known = baselib_hpack_blocks()
        ours = [entry[0] for entry in SINGLE_BLOCKS.values()]
        ours += [block for _, blocks, _, _ in CHAINS.values() for block in blocks]
        assert len(ours) == 16
        assert [block for block in ours if octets(block) not in known] == []

    def test_a_table_size_update_is_recorded_and_applied(self):
        decoder = capture.HpackDecoder()
        decoder.decode(octets(CHAINS["C.3"][1][0]))
        fields, updates = decoder.decode(octets("20 82"))
        assert updates == [{"size": 0, "after_fields": 0}]
        assert decoder.entries == [] and decoder.max_size == 0
        assert [f["name"] for f in fields] == [":method"]

    @pytest.mark.parametrize("block", ["80", "be", "3fe21f", "400a6375", "41"])
    def test_decoding_errors(self, block):
        """Index 0; an index past the table; an update above SETTINGS_HEADER_TABLE_SIZE; truncated strings."""
        with pytest.raises(capture.HpackError):
            capture.HpackDecoder().decode(octets(block))

    def test_the_response_encoder_never_indexes_and_never_codes(self):
        block = capture.encode_header_block([(":status", "200"), ("content-type", "text/html")], size_update=0)
        decoder = capture.HpackDecoder()
        fields, updates = decoder.decode(block)
        assert updates == [{"size": 0, "after_fields": 0}]
        assert [(f["name"], f["value"], f["representation"], f["name_huffman"], f["value_huffman"]) for f in fields] \
            == [(":status", "200", "without_indexing", False, False),
                ("content-type", "text/html", "without_indexing", False, False)]
        assert decoder.entries == []


# ========== The ClientHello, JA3 and JA4 against crypto/TlsClientHello.h's vectors ==========

def u16(value):
    return value.to_bytes(2, "big")


def with_u8_length(body):
    return bytes([len(body)]) + body


def with_u16_length(body):
    return u16(len(body)) + body


def extension(kind, body):
    return u16(kind) + with_u16_length(body)


def client_hello(legacy_version, suites, extensions, include_block=True):
    """Built as TestTlsClientHello.h's makeClientHello builds it: random 0..31, no session id, null compression."""
    body = u16(legacy_version) + bytes(range(32)) + b"\x00" + with_u16_length(b"".join(u16(s) for s in suites))
    body += with_u8_length(b"\x00")
    if include_block:
        body += with_u16_length(b"".join(extensions))
    return b"\x01" + len(body).to_bytes(3, "big") + body


def server_name(host):
    return extension(0x0000, with_u16_length(b"\x00" + with_u16_length(host.encode("ascii"))))


def alpn(protocols):
    return extension(0x0010, with_u16_length(b"".join(with_u8_length(p) for p in protocols)))


def u16_list(kind, values):
    return extension(kind, with_u16_length(b"".join(u16(v) for v in values)))


def supported_versions(versions):
    return extension(0x002B, with_u8_length(b"".join(u16(v) for v in versions)))


def ec_point_formats(formats):
    return extension(0x000B, with_u8_length(bytes(formats)))


HAND_WORKED = client_hello(0x0303, [0x1301, 0x1302, 0xC02B, 0xC02F], [
    server_name("example.com"), ec_point_formats([0x00]), u16_list(0x000A, [0x001D, 0x0017]),
    u16_list(0x000D, [0x0403, 0x0804]), alpn([b"h2", b"http/1.1"]), supported_versions([0x0304, 0x0303])])
GREASE = client_hello(0x0303, [0x0A0A, 0x1301, 0xC02B], [
    extension(0x1A1A, b""), u16_list(0x000A, [0x2A2A, 0x001D]), ec_point_formats([0x00])])
BARE = client_hello(0x0301, [0x002F], [], include_block=False)

# (vector, JA3 string, JA3 hash, JA4 prefix, JA4 cipher input, JA4 extension input, JA4), as asserted in
# TestTlsClientHello.h; the bare hello's hash and inputs are not written there, so only its JA3 and JA4 are
BASELIB_VECTORS = {
    "hand worked": (HAND_WORKED, "771,4865-4866-49195-49199,0-11-10-13-16-43,29-23,0",
                    "2990dac6608f7ee3d85866e604ab2d2b", "t13d0406h2", "1301,1302,c02b,c02f",
                    "000a,000b,000d,002b_0403,0804", "t13d0406h2_e00fd9ffaebd_fb71836bce29"),
    "grease": (GREASE, "771,4865-49195,10-11,29,0", "d19247c417933029394ec08e89b4ac96", "t12i020200", "1301,c02b",
               "000a,000b", "t12i020200_777cda164f4b_33a13ba74d1c"),
    "bare": (BARE, "769,47,,,", None, None, None, None, "t10i010000_ba72b8082249_000000000000"),
}


class TestClientHelloAgainstBaselib:
    """The tool's parser and fingerprints give crypto/TlsClientHello.h's answers on its own vectors."""

    @pytest.mark.parametrize("label", sorted(BASELIB_VECTORS))
    def test_ja3_and_ja4_equal_baselibs(self, label):
        message, ja3, ja3_hash, prefix, ciphers, extensions, ja4 = BASELIB_VECTORS[label]
        info = capture.parse_client_hello(message)
        assert capture.ja3_string(info) == ja3
        assert capture.ja4(info) == ja4
        if ja3_hash is not None:
            assert capture.ja3_hash(ja3) == ja3_hash
            assert capture.ja4_prefix(info) == prefix
            assert capture.ja4_cipher_input(info) == ciphers
            assert capture.ja4_extension_input(info) == extensions

    def test_every_expected_literal_is_still_baselibs(self):
        """A change to TestTlsClientHello.h's expected values reds this, so the two cannot drift apart."""
        source = BASELIB_TLS_CLIENT_HELLO_TESTS.read_text(encoding="utf-8")
        literals = {value for vector in BASELIB_VECTORS.values() for value in vector[1:] if value is not None}
        assert len(literals) == 14
        assert sorted(literal for literal in literals if '"%s"' % literal not in source) == []

    def test_the_parse_keeps_what_was_sent(self):
        info = capture.parse_client_hello(HAND_WORKED)
        assert (info["legacy_version"], info["highest_version"], info["has_server_name"]) == (0x0303, 0x0304, True)
        assert info["cipher_suites"] == [0x1301, 0x1302, 0xC02B, 0xC02F]
        assert info["extensions"] == [0x0000, 0x000B, 0x000A, 0x000D, 0x0010, 0x002B]
        assert (info["supported_groups"], info["ec_point_formats"]) == ([0x001D, 0x0017], [0x00])
        assert (info["signature_algorithms"], info["alpn"]) == ([0x0403, 0x0804], [b"h2", b"http/1.1"])
        assert info["server_name"] == "example.com"
        grease = capture.parse_client_hello(GREASE)
        assert grease["cipher_suites"] == [0x0A0A, 0x1301, 0xC02B]
        assert grease["extensions"] == [0x1A1A, 0x000A, 0x000B]
        assert grease["supported_groups"] == [0x2A2A, 0x001D]
        assert (grease["has_server_name"], grease["alpn"], grease["highest_version"]) == (False, [], 0x0303)
        assert capture.parse_client_hello(BARE)["extensions"] == []

    def test_grease_values(self):
        for value in range(0x0A0A, 0x10000, 0x1010):
            assert capture.is_grease(value)
        assert len([value for value in range(0x10000) if capture.is_grease(value)]) == 16
        for value in (0x0A0B, 0x1A2A, 0x1301, 0x0000):
            assert not capture.is_grease(value)

    def test_malformed_messages_are_refused(self):
        """As TlsClientHello_MalformedMessagesAreRefusedTests: empty, not a ClientHello, every proper prefix,
        and a body length one short."""
        message = client_hello(0x0303, [0x1301, 0xC02B], [server_name("example.com"), u16_list(0x000A, [0x001D]),
                                                          alpn([b"h2"]), supported_versions([0x0304])])
        capture.parse_client_hello(message)
        for broken in [b"", b"\x02" + message[1:]] + [message[:length] for length in range(len(message))]:
            with pytest.raises(capture.ParseError):
                capture.parse_client_hello(broken)
        shortened = bytearray(message)
        shortened[3] -= 1
        with pytest.raises(capture.ParseError):
            capture.parse_client_hello(bytes(shortened))

    def test_a_non_alphanumeric_alpn_is_reported_by_its_hex(self):
        """TlsClientHello.h's rule: the first hex digit of the first byte, the last of the last."""
        message = client_hello(0x0303, [0x1301], [alpn([b"\x01ab\xfe"])])
        assert capture.ja4_prefix(capture.parse_client_hello(message)) == "t12i01010e"

    def test_signature_algorithms_keep_the_order_they_were_sent_in(self):
        """
        Every vector above sends its suites and signature algorithms already sorted, so none of them can
        tell sorting from keeping the order. This one sends both out of order, worked by hand:

          prefix     t13i020300
          ciphers    1301,1302                         (sorted)
          extensions 000a,000d,002b_0804,0403,0401     (sorted; the algorithms as sent)

        and the digests, from outside Python (logs/l7/a/hand-worked-unsorted-sigalgs.txt):

          printf '%s' '1301,1302' | sha256sum
          printf '%s' '000a,000d,002b_0804,0403,0401' | sha256sum
          printf '%s' '771,4866-4865,13-10-43,29,' | md5sum
        """
        info = capture.parse_client_hello(client_hello(0x0303, [0x1302, 0x1301], [
            u16_list(0x000D, [0x0804, 0x0403, 0x0401]), u16_list(0x000A, [0x001D]), supported_versions([0x0304])]))
        assert capture.ja4_extension_input(info) == "000a,000d,002b_0804,0403,0401"
        assert capture.ja4(info) == "t13i020300_62ed6f6ca7ad_d538c6400f45"
        assert capture.ja3_string(info) == "771,4866-4865,13-10-43,29,"
        assert capture.ja3_hash(capture.ja3_string(info)) == "30e066712c3c8666db74dda8b1492c36"


# ========== The summary: GREASE stripped, the set, the profile-source rule ==========

class TestHelloSummary:

    def test_grease_is_stripped_and_counted(self):
        """F5(c): no GREASE value reaches the lists a profile is derived from."""
        summary = capture.summarize_hello(GREASE)
        assert summary["cipher_suites"] == ["1301", "c02b"]
        assert summary["extensions"] == ["000a", "000b"] and summary["extension_set"] == ["000a", "000b"]
        assert summary["supported_groups"] == ["001d"]
        assert summary["grease_stripped"] == {"cipher_suites": 1, "extensions": 1, "supported_groups": 1,
                                              "signature_algorithms": 0, "supported_versions": 0, "key_shares": 0}
        assert summary["ja4"]["fingerprint"] == "t12i020200_777cda164f4b_33a13ba74d1c"

    def test_the_extension_set_is_sorted_and_the_order_is_kept(self):
        """F5(d): a shuffled order changes the list and neither the set nor the JA4."""
        extensions = [server_name("capture.test"), u16_list(0x000A, [0x001D]), alpn([b"h2"]),
                      supported_versions([0x0304]), u16_list(0x000D, [0x0403])]
        one = capture.summarize_hello(client_hello(0x0303, [0x1301], extensions))
        other = capture.summarize_hello(client_hello(0x0303, [0x1301], extensions[::-1]))
        assert one["extensions"] != other["extensions"]
        assert one["extension_set"] == other["extension_set"] == ["0000", "000a", "000d", "0010", "002b"]
        assert one["ja4"]["fingerprint"] == other["ja4"]["fingerprint"]
        assert one["sni"] == {"present": True, "host_name": "capture.test"}

    def test_a_pre_shared_key_hello_is_refused_as_a_profile_source(self):
        """F5(a): a resumed connection's hello carries 41 and a different JA4."""
        psk = extension(41, with_u16_length(with_u16_length(b"ticket") + b"\x00\x00\x00\x01")
                        + with_u16_length(with_u8_length(b"\x00" * 32)))
        summary = capture.summarize_hello(client_hello(0x0303, [0x1301], [supported_versions([0x0304]), psk]))
        assert summary["pre_shared_key"] is True
        assert summary["profile_source"]["eligible"] is False
        assert summary["other_extensions"]["pre_shared_key_identities"] == 1
        assert capture.summarize_hello(HAND_WORKED)["profile_source"]["eligible"] is True

    def test_key_shares_and_the_fields_a_profile_needs(self):
        key_share = extension(51, with_u16_length(u16(0x3A3A) + with_u16_length(b"\x00")
                                                  + u16(0x11EC) + with_u16_length(b"\x11" * 1216)
                                                  + u16(0x001D) + with_u16_length(b"\x22" * 32)))
        alps = extension(17613, with_u16_length(with_u8_length(b"h2")))
        compress = extension(27, with_u8_length(u16(0x0002)))
        summary = capture.summarize_hello(client_hello(0x0303, [0x1301], [key_share, alps, compress,
                                                                          extension(0x5A5A, b"")]))
        assert summary["key_shares"] == [{"group": "11ec", "name": "X25519MLKEM768", "key_length": 1216},
                                         {"group": "001d", "name": "x25519", "key_length": 32}]
        assert summary["grease_stripped"]["key_shares"] == 1
        assert summary["other_extensions"] == {"application_settings": ["h2"], "compress_certificate": ["0002"]}
        assert summary["extension_names"] == ["key_share", "application_settings", "compress_certificate"]

    def test_a_malformed_extra_extension_is_recorded_not_fatal(self):
        summary = capture.summarize_hello(client_hello(0x0303, [0x1301], [extension(51, b"\x00\x09\x00")]))
        assert summary["other_extensions"] == {"malformed key_share": True}


# ========== The hello at the record layer, and a real OpenSSL hello ==========

def records(message, sizes):
    """The handshake message cut into TLS records of these fragment sizes."""
    out, start = b"", 0
    for size in sizes + [len(message)]:
        fragment = message[start:start + size]
        if fragment:
            out += b"\x16\x03\x01" + with_u16_length(fragment)
        start += size
    return out


CHANGE_CIPHER_SPEC = b"\x14\x03\x03\x00\x01\x01"
APPLICATION_DATA = b"\x17\x03\x03\x00\x02zz"


def openssl_client_hello():
    """What Python's own OpenSSL client sends first, taken from its memory BIO: no socket at all."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.set_alpn_protocols(["h2", "http/1.1"])
    incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
    tls = context.wrap_bio(incoming, outgoing, server_hostname="example.com")
    with pytest.raises(ssl.SSLWantReadError):
        tls.do_handshake()
    return outgoing.read()


class TestRecordLayer:

    def test_the_hello_is_reassembled_across_reads_and_records(self):
        """Three records, fed one byte at a time: the handshake header itself is split across records."""
        stream = records(HAND_WORKED, [2, 40])
        follower = capture.RecordStream()
        for index in range(len(stream)):
            assert follower.hello is None
            follower.feed(stream[index:index + 1])
        assert follower.hello == HAND_WORKED
        assert follower.record_types == [22, 22, 22] and follower.error is None

    def test_it_stops_at_application_data(self):
        follower = capture.RecordStream()
        follower.feed(records(HAND_WORKED, []) + b"\x14\x03\x03\x00\x01\x01" + b"\x17\x03\x03\x00\x02zz"
                      + b"\x16\x03\x03\x00\x01\x00")
        assert follower.record_types == [22, 20, 23] and follower.done and follower.error is None

    @pytest.mark.parametrize("stream, text", [
        (b"GET / HTTP/1.1\r\n\r\n", "not a TLS handshake record"),
        (b"\x16\x03\x01\x00\x02\x01\x00" + b"\x17\x03\x03\x00\x01z", "inside the ClientHello"),
        (b"\x16\x03\x01\x00\x04\x02\x00\x00\x00", "not a ClientHello"),
    ])
    def test_what_is_not_a_hello_is_refused(self, stream, text):
        follower = capture.RecordStream()
        follower.feed(stream)
        assert follower.hello is None and text in follower.error

    @staticmethod
    def analysed(tmp_path, stream):
        (tmp_path / capture.RAW_FILE).write_bytes(stream)
        capture.write_json(str(tmp_path / capture.META_FILE),
                           {"connection": 1, "tls_version": "TLSv1.3", "handshake": "complete"})
        return capture.analyse_connection(str(tmp_path))

    def test_a_hello_in_two_records_is_one_hello_and_no_retry(self, tmp_path):
        """A1-2: the records the hello itself arrived in are not a second hello."""
        result = self.analysed(tmp_path, records(HAND_WORKED, [40]) + CHANGE_CIPHER_SPEC + APPLICATION_DATA)
        assert result["record_types"] == [22, 22, 20, 23]
        assert result["hello"]["ja4"]["fingerprint"] == "t13d0406h2_e00fd9ffaebd_fb71836bce29"
        assert "hello_retry_request" not in result

    def test_a_second_hello_after_the_change_cipher_spec_is_a_retry(self, tmp_path):
        """The positive case beside it, so the fix cannot become "never report a HelloRetryRequest"."""
        stream = records(HAND_WORKED, [40]) + CHANGE_CIPHER_SPEC + records(HAND_WORKED, []) + APPLICATION_DATA
        result = self.analysed(tmp_path, stream)
        assert result["record_types"] == [22, 22, 20, 22, 23]
        assert result["hello_retry_request"] is True

    def test_a_real_openssl_hello(self):
        """As TlsClientHello_CapturedFromRealHandshakeTests asserts it, on the OpenSSL this Python links."""
        follower = capture.RecordStream()
        follower.feed(openssl_client_hello())
        info = capture.parse_client_hello(follower.hello)
        assert (info["legacy_version"], info["highest_version"], info["has_server_name"]) == (0x0303, 0x0304, True)
        assert info["alpn"] == [b"h2", b"http/1.1"] and info["server_name"] == "example.com"
        assert info["supported_groups"] and info["signature_algorithms"] and info["key_shares"]
        fingerprint = capture.ja4(info)
        assert capture.ja3_string(info).startswith("771,") and len(capture.ja3_hash(capture.ja3_string(info))) == 32
        assert len(fingerprint) == 36 and fingerprint[:4] == "t13d" and fingerprint[8:10] == "h2"
        assert fingerprint[10] == fingerprint[23] == "_"

    def test_the_hello_command_reads_the_capture_hooks_format_and_records(self, tmp_path, capsys):
        """enableClientHelloCapture( )'s form (type, length, body) and a record stream give one answer."""
        follower = capture.RecordStream()
        follower.feed(openssl_client_hello())
        (tmp_path / "hook.bin").write_bytes(follower.hello)
        (tmp_path / "records.bin").write_bytes(records(follower.hello, [100]))
        answers = []
        for name in ("hook.bin", "records.bin"):
            assert capture.main(["hello", str(tmp_path / name)]) == 0
            answers.append(json.loads(capsys.readouterr().out))
        assert answers[0] == answers[1] == capture.summarize_hello(follower.hello)
        assert capture.main(["hello", str(tmp_path / "missing.bin")]) == 1
        assert "no such file" in capsys.readouterr().out


# ========== HTTP/2 and HTTP/1.1 followers on hand-built bytes ==========

def frame(frame_type, flags, stream, payload):
    return len(payload).to_bytes(3, "big") + bytes([frame_type, flags]) + stream.to_bytes(4, "big") + payload


class TestFollowers:

    def test_an_h2_opening_and_a_request(self):
        block = octets("8287 8441 0f77 7777 2e65 7861 6d70 6c65 2e63 6f6d")    # :method :scheme :path :authority
        stream = capture.H2_PREFACE + frame(4, 0, 0, bytes.fromhex("000100010000" "000400600000"))
        stream += frame(8, 0, 0, (15663105).to_bytes(4, "big")) + frame(2, 0, 3, bytes.fromhex("80000000c8"))
        stream += frame(1, 0x21, 1, bytes.fromhex("80000000ff") + block[:5]) + frame(9, 0x04, 1, block[5:])
        follower = capture.H2Follower()
        events = follower.feed(stream)
        assert [event[0] for event in events] == ["settings", "window_update", "request"]
        assert [frame["type"] for frame in follower.frames] == ["SETTINGS", "WINDOW_UPDATE", "PRIORITY", "HEADERS",
                                                                "CONTINUATION"]
        request = follower.requests[0]
        assert request["pseudo_header_short"] == "m,s,p,a" and request["kind"] == "navigation"
        assert request["header_block"]["priority"] == {"exclusive": True, "depends_on": 0, "weight": 256,
                                                       "weight_on_wire": 255}
        assert follower.frames[2]["priority"] == {"exclusive": True, "depends_on": 0, "weight": 201,
                                                  "weight_on_wire": 200}
        assert follower.frames[3]["flags"] == ["END_STREAM", "PRIORITY"]

    @pytest.mark.parametrize("stream, code", [
        (b"GET / HTTP/1.1\r\n\r\n" + b"x" * 8, capture.PROTOCOL_ERROR),
        (capture.H2_PREFACE + frame(1, 0x00, 1, b"\x82") + frame(8, 0, 0, b"\x00\x00\x00\x01"), capture.PROTOCOL_ERROR),
        (capture.H2_PREFACE + frame(8, 0, 0, b"\x00"), capture.FRAME_SIZE_ERROR),
        (capture.H2_PREFACE + frame(1, 0x05, 1, b"\x80"), capture.COMPRESSION_ERROR),
    ])
    def test_h2_violations(self, stream, code):
        with pytest.raises(capture.ProtocolError) as raised:
            capture.H2Follower().feed(stream)
        assert raised.value.code == code

    def test_http1_heads_are_kept_as_sent(self):
        follower = capture.H1Follower()
        first = b"GET /capture.js HTTP/1.1\r\nHost: capture.test\r\nX-Mixed-CASE: a\r\n\r\n"
        second = b"POST /fetch HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc"
        assert follower.feed(first + second[:20]) == follower.requests[:1]
        assert follower.partial()
        follower.feed(second[20:])
        assert not follower.partial()
        assert [(r["kind"], r["offset"], r["body_length"]) for r in follower.requests] == [
            ("subresource-script", 0, 0), ("fetch", len(first), 3)]
        assert follower.requests[0]["headers"] == [["Host", "capture.test"], ["X-Mixed-CASE", "a"]]
        assert follower.requests[0]["raw_head"] == first.decode("latin-1")

    @pytest.mark.parametrize("value", ["²", "1" * 5000, "-1", "3a", ""],
                             ids=["superscript-two", "5000-digits", "negative", "letter", "empty"])
    def test_a_malformed_content_length_is_a_malformed_request(self, value):
        """A1-5: a superscript digit passes isdigit() and a 5,000-digit value passes it too, and neither
        converts; each is a malformed request, never a ValueError that would stop the analysis."""
        head = ("POST /fetch HTTP/1.1\r\nContent-Length: %s\r\n\r\n" % value).encode("latin-1")
        with pytest.raises(capture.ProtocolError, match="Content-Length"):
            capture.H1Follower().feed(head)


# ========== The controls that need no socket ==========

class TestControls:

    @pytest.mark.parametrize("host", ["127.0.0.1", "::1", "[::1]", "localhost", "a.localhost", "capture"])
    def test_the_host_is_never_an_ip_literal_nor_localhost(self, host):
        """F5(e): an IP literal sends no SNI, and browsers special-case localhost."""
        with pytest.raises(capture.CaptureError):
            capture.validate_host(host)

    def test_a_hosts_file_name_is_accepted(self):
        assert capture.validate_host("Capture.Test.") == "capture.test"

    def test_no_key_is_ever_written_inside_a_git_work_tree(self, tmp_path):
        """No key is ever committed: certs refuses a directory any repository contains, before writing."""
        (tmp_path / "repository" / ".git").mkdir(parents=True)
        target = tmp_path / "repository" / "scripts" / "certs"
        with pytest.raises(capture.CaptureError, match="inside a git work tree"):
            capture.make_certificates(capture.DEFAULT_HOST, str(target))
        assert not target.exists()
        assert capture.inside_git_work_tree(str(REPOSITORY / "scripts"))
        assert not capture.inside_git_work_tree(str(tmp_path))

    def test_the_interpreter_check_prints_the_tls_library(self, capsys):
        """F5(k): the check prints ssl.OPENSSL_VERSION, and this host's Python passes it."""
        assert capture.interpreter_problems() == []
        assert capture.main(["check"]) == 0
        assert ssl.OPENSSL_VERSION in capsys.readouterr().out

    @pytest.mark.parametrize("attribute, text", [("HAS_TLSv1_3", "TLS 1.3"), ("HAS_ALPN", "ALPN")])
    def test_the_interpreter_check_refuses_without_tls13_or_alpn(self, monkeypatch, capsys, attribute, text):
        monkeypatch.setattr(capture.ssl, attribute, False)
        assert any(text in problem for problem in capture.interpreter_problems())
        assert capture.main(["check"]) == 2
        assert "cannot capture" in capsys.readouterr().out


# ========== Standard library only ==========

def third_party_imports(source):
    """Every imported top-level module outside the standard library, and any dynamic import."""
    found = []
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Import):
            found += [alias.name.split(".")[0] for alias in node.names]
        elif isinstance(node, ast.ImportFrom):
            found.append("." if node.level else node.module.split(".")[0])
        elif isinstance(node, ast.Call):
            called = getattr(node.func, "id", getattr(node.func, "attr", None))
            if called in ("__import__", "import_module"):
                found.append("dynamic import")
    return sorted(name for name in found if name not in sys.stdlib_module_names)


@pytest.mark.skipif(sys.version_info < (3, 10), reason="sys.stdlib_module_names is Python 3.10's")
class TestStandardLibraryOnly:

    def test_the_tool_imports_only_the_standard_library(self):
        assert third_party_imports(TOOL.read_text(encoding="utf-8")) == []

    def test_the_check_finds_what_it_is_for(self):
        """The negative control: a package import, a relative one and a dynamic one are each found."""
        assert third_party_imports("import hpack\nfrom h2 import connection\nimport ssl\n") == ["h2", "hpack"]
        assert third_party_imports("from . import x\n__import__('y')\n") == [".", "dynamic import"]

    def test_the_tool_runs_without_site_packages(self):
        """-I -S: no user site, no PYTHONPATH, no site-packages; only the standard library can be imported."""
        done = subprocess.run([sys.executable, "-I", "-S", str(TOOL), "check"], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=60)
        assert done.returncode == 0, done.stdout
        assert b"This Python can capture." in done.stdout
