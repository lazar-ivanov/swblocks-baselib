/*
 * This file is part of the swblocks-baselib library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __BL_HTTP2_FINGERPRINT_H_
#define __BL_HTTP2_FINGERPRINT_H_

#include <baselib/http2/FrameCodec.h>
#include <baselib/http2/Globals.h>
#include <baselib/http2/HpackDecoder.h>
#include <baselib/http2/HpackDynamicTable.h>
#include <baselib/http2/Http2Profile.h>

#include <baselib/core/BaseIncludes.h>
#include <baselib/core/ErrorHandling.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace bl
{
    namespace http2
    {
        /*
         * THE HTTP/2 FINGERPRINT OF A CLIENT'S OPENING (notes/plans/http2-design.md 6.4 and 6.6)
         *
         * What an HTTP/2 client sends before and with its first request - its SETTINGS, its
         * connection WINDOW_UPDATE, its PRIORITY frames and the order of its pseudo-headers -
         * differs from one implementation to the next, and a detector reads it to tell which
         * client it is talking to. This renders the frames a session ACTUALLY PRODUCED in the
         * conventional form, for the fidelity report (design 6.6) and the test vectors (6.7). It
         * reads the bytes, never the profile they were meant to follow, so that a difference
         * between the two is visible rather than assumed away
         *
         * Sans-I/O and OpenSSL-free, like the rest of the protocol core (design 2.1): it reads
         * frames with FrameReader and FrameCodec and decodes the first header block with
         * HpackDecoder, so it can disagree with the engine about the wire format only where it
         * says so below
         *
         * ------------------------------------------------------------------------------------
         * THE FORMAT, FROM ITS SOURCE
         * ------------------------------------------------------------------------------------
         *
         * Segal, Fridman and Shuster, "Passive Fingerprinting of HTTP/2 Clients", Akamai white
         * paper, Black Hat Europe 2017 (published 06/17), section 4.0, "Passive HTTP/2
         * Fingerprint - Suggested Format":
         *
         *   https://www.blackhat.com/docs/eu-17/materials/
         *       eu-17-Shuster-Passive-Fingerprinting-Of-HTTP2-Clients-wp.pdf
         *
         * It proposes S[;]|WU|P[,]#|PS[,] - four parts joined by '|'. The '#' of that line
         * appears in none of the paper's own rendered examples, nor in any implementation below,
         * and it is not written here. The parts, in the paper's words where it has them:
         *
         *   S   each SETTINGS parameter "in the form of Key:Value", concatenated "using a
         *       semicolon (;) according to the order of their appearance". Both in decimal
         *   WU  "the WINDOW_UPDATE increment size - '00' if the frame is not present"
         *   P   "StreamID:Exclusivity_Bit:Dependant_StreamID:Weight" for each PRIORITY frame,
         *       "concatenated by a comma (,). If this feature does not exist, the value should
         *       be '0'"
         *   PS  the pseudo-header order, one letter each, comma separated: "m (:method),
         *       p (:path), a (:authority), s (:scheme)"
         *
         * Its worked example, which a test here reproduces from frames built by hand:
         *
         *   1:65536;4:131072;5:16384|12517377|3:0:0:201,5:0:0:101,7:0:0:1,9:0:7:1,11:0:3:1|m,p,a,s
         *
         * EACH CHOICE THE PAPER LEAVES TO ITS READER, AND WHAT DECIDED IT:
         *
         *  - THE WEIGHT IS THE WIRE OCTET PLUS ONE - the 1 to 256 weight of RFC 7540 6.3, "Add
         *    one to the value to obtain a weight between 1 and 256". RFC 9113 deprecates that
         *    scheme and its 6.3 keeps only "An unsigned 8-bit integer", so it is RFC 7540 that
         *    defines what the paper counts. The paper does not say so in words, but its example
         *    does: it renders Firefox 53's priority tree as 201, 101, 1, 1 and 1, and Firefox of
         *    that date writes the octets 200, 100, 0, 0 and 0 (gecko-dev
         *    a06d325b04282b2bace31f8045561c22e5b85875, netwerk/protocol/http/Http2Session.cpp,
         *    CreatePriorityNode). All three implementations below add the one. The parts keep
         *    the octet, as Http2Profile does, and only render( ) adds it
         *  - the exclusivity bit is 1 or 0, as in the paper's tuples and in all three
         *  - a SETTINGS frame with no entries leaves S empty, which all three do as well
         *  - an increment is written in plain decimal, whatever its size
         *
         * ------------------------------------------------------------------------------------
         * WHAT DETECTORS WRITE, WHICH IS NOT ALWAYS THIS
         * ------------------------------------------------------------------------------------
         *
         * Three open implementations were read at the source. The first two follow the paper;
         * none of them agrees with the others on everything, so a string from one of them is
         * compared with a string from here knowing where they part:
         *
         *  - TrackMe, the server behind tls.peet.ws - github.com/pagpeter/TrackMe at
         *    9d2e865ad663ae114e28c33ddc08a4797b4ecaa8, pkg/http/fingerprint_h2.go and
         *    pkg/server/connection_handler.go. The paper's format, '00' and '0' included. WU is
         *    the first WINDOW_UPDATE on ANY stream, and P every PRIORITY frame up to the first
         *    frame carrying END_STREAM
         *  - fingerproxy - github.com/wi1dcard/fingerproxy at
         *    bcda1920cb3665dedcafebef8eaf008e74393fa9, pkg/metadata/http2.go and
         *    pkg/http2/server.go. The paper's format, but the increment is written "%02d", so a
         *    present increment below ten gains a leading zero; S is the LAST SETTINGS frame; and
         *    each HEADERS frame's own priority fields are added to P
         *  - nginx-ssl-fingerprint - github.com/phuslu/nginx-ssl-fingerprint at
         *    e72f932b630717939b508b58e5b4a48ec014adf3, src/nginx_ssl_fingerprint.c and
         *    patches/release-1.30.0.patch. An absent WINDOW_UPDATE is written '0' and an absent
         *    P part empty; P is built from the HEADERS frames' priority fields and never from
         *    PRIORITY frames; and a setting identifier and a stream identifier are kept in one
         *    octet
         *
         * So a client which puts priority fields on HEADERS - which Http2Profile can, and which
         * the design's Chrome shape does - gets a different P part from fingerproxy and from
         * nginx-ssl-fingerprint than from the paper, TrackMe and this
         *
         * ------------------------------------------------------------------------------------
         * WHICH FRAMES ARE READ
         * ------------------------------------------------------------------------------------
         *
         *  - the bytes must begin with the client connection preface of RFC 9113 3.4, which
         *    "MUST be followed by a SETTINGS frame" - and not by an acknowledgement, on the
         *    reading that 3.4's SETTINGS is the one the peer must itself acknowledge. Its
         *    entries are S, in order, duplicates and identifiers this library does not
         *    interpret included. A later SETTINGS frame, an acknowledgement among them, is not
         *    the fingerprint's
         *  - WU is the first WINDOW_UPDATE on stream 0 ahead of the first HEADERS frame. One on
         *    a stream is not the connection's and is not read. An increment of zero on stream 0
         *    is refused, as RFC 9113 6.9 makes it a connection error - which is also what lets
         *    zero mean "none was sent" in the parts, exactly as it does in Http2Profile
         *  - P is every PRIORITY frame ahead of the first HEADERS frame, in order - the frames
         *    the paper describes, sent "right after the connection phase ... for streams that
         *    have not been opened yet"
         *  - PS is read from the first header block - the first HEADERS frame and the
         *    CONTINUATION frames which complete it - once HPACK has decoded all of it. Only
         *    the four request pseudo-headers of RFC 9113 8.3.1 have a letter, so a block
         *    carrying another, carrying one twice, or carrying one after a regular field is
         *    refused: those are exactly the malformations the session refuses in a request it
         *    reads (8.3 and 8.3.1), and with any of them "the order" stops being one. Whether
         *    the required ones are present is not judged, because the order is well defined
         *    without them: a request with no :authority renders m,s,p
         *  - the first HEADERS frame's own priority fields are kept in the parts and NOT
         *    rendered. Design 6.1 lists them among what a detector observes; the paper's four
         *    parts leave them out, and adding them would be a fifth part no detector writes
         *  - every other frame ahead of the first HEADERS frame is skipped once the codec has
         *    judged its structure - an unknown type included, as RFC 9113 4.1 requires - and
         *    nothing after the first header block is examined at all
         *
         * ------------------------------------------------------------------------------------
         * WHAT IS REFUSED, AND HOW
         * ------------------------------------------------------------------------------------
         *
         * ONE DEFINED EXCEPTION, InvalidDataFormatException - the one crypto/TlsClientHello.h
         * throws for a ClientHello it cannot parse, which is this header's counterpart in the
         * same report. Its message names the rule, and is a string this library wrote: no byte
         * of the input is ever echoed, because a message reaches logs. A refusal the frame codec
         * or HPACK raised carries the RFC 9113 section 7 code as eh::errinfo_http2_error_code,
         * and the codec's own exception as eh::errinfo_nested_exception_ptr
         *
         *  - malformed input: whatever FrameReader, FrameCodec and HpackDecoder refuse, plus the
         *    rules of the section above
         *  - truncated input: parse( ) refuses bytes which end before the first header block is
         *    complete. The incremental reader instead reports it with isComplete( ), since more
         *    bytes may still come
         *  - hostile input: nothing grows without a bound. One frame is buffered at a time, of at
         *    most the 2^24-1 octets the Length field can say, and the reader is set to accept
         *    all of them so that no frame is refused for a size only the server's SETTINGS could
         *    have allowed. What is accumulated is bounded by the limits below, and each breach
         *    is a refusal naming the bound
         */

        /**
         * @brief The parts of the HTTP/2 fingerprint of a client's opening, as they were sent
         *
         * Each part is held in the profile's own type and under the profile's own name, so what
         * a profile said to send and what the fingerprint read back are compared field for field
         * - the same choice FrameCodec made for HeadersPayload::priority. The weights are the
         * octets on the wire, as the profile holds them; render( ) adds the one
         */

        struct Http2FingerprintParts
        {
            /*
             * S - the entries of the first SETTINGS frame, in the order sent
             */

            std::vector< Http2Setting >                                         settings;

            /*
             * WU - the increment of the first WINDOW_UPDATE on stream 0, or zero when none was
             * sent ahead of the first HEADERS frame
             */

            cpp::ScalarTypeIniter< std::uint32_t >                              connectionWindowUpdateIncrement;

            /*
             * P - the PRIORITY frames sent ahead of the first HEADERS frame, in order
             */

            std::vector< Http2PriorityFrame >                                   idleStreamPriorities;

            /*
             * PS - the pseudo-header fields of the first header block, in order
             */

            std::vector< Http2PseudoHeader >                                    pseudoHeaderOrder;

            /*
             * Not rendered - the first HEADERS frame's own priority fields. 'isSet' is false when
             * the frame carried no PRIORITY flag
             */

            Http2HeadersPriority                                                headersPriority;
        };

        /**
         * @brief class Http2FingerprintT - reads a client's opening and renders its fingerprint
         *
         * An incremental reader and two static functions. The opening a session produces reaches
         * the wire over several produce( ) calls - the preface, SETTINGS, WINDOW_UPDATE and
         * PRIORITY frames queued at construction, the first HEADERS once a request is submitted
         * - and a frame can be split anywhere between two reads, so feed( ) takes the bytes as
         * they come and isComplete( ) says when the first header block has ended. parse( ) is the
         * one-shot form for bytes already gathered, and render( ) writes the string
         *
         * A reader which has refused its input is spent: feeding it again is a programming error,
         * as it is for FrameReader, because nothing it could still read would mean anything
         */

        template
        <
            typename E = void
        >
        class Http2FingerprintT FINAL
        {
            BL_NO_COPY_OR_MOVE( Http2FingerprintT )

        public:

            enum : std::size_t
            {
                /*
                 * The first header block, compressed, and the CONTINUATION frames it may span -
                 * the design's 4.6 rows, as the session applies them to every block it reads
                 */

                MAX_FIRST_HEADER_BLOCK_SIZE             =
                    Globals::MAX_COMPRESSED_HEADER_BLOCK_SIZE_DEFAULT,

                MAX_FIRST_HEADER_BLOCK_CONTINUATIONS    =
                    Globals::MAX_CONTINUATION_FRAMES_PER_BLOCK_DEFAULT,

                /*
                 * The first header block, decoded - the 4.6 row again, and the bound on what an
                 * HPACK bomb can make of a block within the size above
                 */

                MAX_DECODED_FIRST_HEADER_BLOCK_SIZE     =
                    Globals::MAX_DECODED_HEADER_LIST_SIZE_DEFAULT,

                /*
                 * The largest dynamic table size update the first block may carry. The client's
                 * encoder starts at the protocol's 4096 and may open its first block with an
                 * update up to what the SERVER advertised, which is not in these bytes - so the
                 * decoder cannot be held to 4096 without refusing a well formed opening. It
                 * cannot be unbounded either: the table holds what the block inserts, and a block
                 * whose literals copy a long name out of the table can insert on the order of the
                 * square of its own size
                 *
                 * So it is the decoded bound above. Up to that bound a larger table would decode
                 * nothing differently: a block inserts no more than it emits, a field indexed
                 * once being emitted once, so a block within the bound never fills a table of
                 * that size. What this costs is the update itself - a first block which asks for
                 * more than 64 KB is refused, however little it then inserts
                 */

                HPACK_TABLE_SIZE_CEILING                = 64U * 1024U,

                /*
                 * A PRIORITY frame is nine octets of header and five of payload, and the session
                 * refuses an opening whose control frames exceed its queued control-frame bound
                 * (design 4.6; SessionT::checkControlQueueBound). That bound therefore admits at
                 * most this many PRIORITY frames in a session's opening, and it is the bound here
                 */

                PRIORITY_FRAME_SIZE                     = 14U,

                MAX_IDLE_STREAM_PRIORITIES              =
                    static_cast< std::size_t >( Globals::MAX_QUEUED_CONTROL_FRAME_BYTES_DEFAULT ) /
                        PRIORITY_FRAME_SIZE,
            };

        private:

            FrameReader                                                         m_reader;
            Http2FingerprintParts                                               m_parts;

            /*
             * The first header block as its fragments arrive, released once it is decoded
             */

            std::string                                                         m_block;

            std::size_t                                                         m_prefaceOctets;
            std::size_t                                                         m_continuations;

            cpp::ScalarTypeIniter< bool >                                       m_sawSettings;
            cpp::ScalarTypeIniter< bool >                                       m_inFirstBlock;
            cpp::ScalarTypeIniter< bool >                                       m_isComplete;
            cpp::ScalarTypeIniter< bool >                                       m_failed;

        public:

            Http2FingerprintT()
                :
                m_prefaceOctets( 0U ),
                m_continuations( 0U )
            {
                /*
                 * Every frame size the Length field can express, for the reason the class note
                 * gives: a client may send frames up to what the server advertised, and the
                 * server's SETTINGS are not in these bytes
                 */

                m_reader.setMaxFrameSize( Globals::MAX_FRAME_SIZE_UPPER_BOUND );
            }

            /**
             * @brief Reads more of the opening; throws InvalidDataFormatException on a refusal
             *
             * Bytes after the end of the first header block are not examined, so a caller may
             * hand over everything a connection wrote without trimming it
             */

            void feed(
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size
                )
            {
                BL_CHK(
                    false,
                    ! m_failed,
                    BL_MSG()
                        << "An HTTP/2 fingerprint was fed again after it had refused its input"
                    );

                BL_CHK(
                    false,
                    size == 0U || data != nullptr,
                    BL_MSG()
                        << "An HTTP/2 fingerprint was fed a non-empty block of no bytes"
                    );

                if( m_isComplete )
                {
                    return;
                }

                /*
                 * Anything which throws below leaves the reader spent, exactly as a connection
                 * error leaves FrameReader
                 */

                m_failed = true;

                try
                {
                    feedImpl( data, size );
                }
                catch( Http2ProtocolException& e )
                {
                    refuseFromCodec( e, std::current_exception() );
                }
                catch( Http2StreamException& e )
                {
                    refuseFromCodec( e, std::current_exception() );
                }

                m_failed = false;
            }

            /**
             * @brief Whether the first header block has been read, and the parts are therefore
             * all there
             */

            bool isComplete() const NOEXCEPT
            {
                return m_isComplete;
            }

            const Http2FingerprintParts& parts() const
            {
                BL_CHK(
                    false,
                    m_isComplete,
                    BL_MSG()
                        << "An HTTP/2 fingerprint was asked for before the first header block "
                        << "of the opening had been read"
                    );

                return m_parts;
            }

            /**
             * @brief The parts of a whole opening; throws InvalidDataFormatException when the
             * bytes are malformed or end before the first header block is complete
             */

            static Http2FingerprintParts parse(
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size
                )
            {
                Http2FingerprintT fingerprint;

                fingerprint.feed( data, size );

                if( ! fingerprint.isComplete() )
                {
                    refuse(
                        "the opening is truncated: it ends before its first header block is "
                        "complete"
                        );
                }

                return fingerprint.parts();
            }

            /**
             * @brief The same, over what a session's produce( ) calls appended
             */

            static Http2FingerprintParts parse( SAA_in const std::vector< std::uint8_t >& opening )
            {
                return parse( opening.empty() ? nullptr : opening.data(), opening.size() );
            }

            /**
             * @brief The fingerprint string - S|WU|P|PS, as the class note sets out
             */

            static std::string render( SAA_in const Http2FingerprintParts& parts )
            {
                std::string text;

                for( std::size_t i = 0U; i < parts.settings.size(); ++i )
                {
                    if( i != 0U )
                    {
                        text += ';';
                    }

                    text += std::to_string( parts.settings[ i ].id.value() );
                    text += ':';
                    text += std::to_string( parts.settings[ i ].value.value() );
                }

                text += '|';

                if( parts.connectionWindowUpdateIncrement == 0U )
                {
                    text += "00";
                }
                else
                {
                    text += std::to_string( parts.connectionWindowUpdateIncrement.value() );
                }

                text += '|';

                if( parts.idleStreamPriorities.empty() )
                {
                    text += '0';
                }

                for( std::size_t i = 0U; i < parts.idleStreamPriorities.size(); ++i )
                {
                    const auto& priority = parts.idleStreamPriorities[ i ];

                    if( i != 0U )
                    {
                        text += ',';
                    }

                    text += std::to_string( priority.streamId.value() );
                    text += ':';
                    text += priority.exclusive ? '1' : '0';
                    text += ':';
                    text += std::to_string( priority.streamDependency.value() );
                    text += ':';

                    /*
                     * The octet plus one - RFC 7540 6.3's weight, as the class note sets out
                     */

                    const unsigned weight = static_cast< unsigned >( priority.weight.value() ) + 1U;

                    text += std::to_string( weight );
                }

                text += '|';

                for( std::size_t i = 0U; i < parts.pseudoHeaderOrder.size(); ++i )
                {
                    if( i != 0U )
                    {
                        text += ',';
                    }

                    text += letterOf( parts.pseudoHeaderOrder[ i ] );
                }

                return text;
            }

        private:

            /*************************************************************************************
             * Refusals - see the class note. Every reason is a fixed string this library wrote
             */

            SAA_noreturn
            static void refuse( SAA_in const std::string& reason )
            {
                BL_THROW(
                    InvalidDataFormatException(),
                    BL_MSG()
                        << "The HTTP/2 fingerprint cannot be taken - "
                        << reason
                    );
            }

            SAA_noreturn
            static void refuseWithCode(
                SAA_in          const std::uint32_t                  errorCode,
                SAA_in          const std::string&                   reason
                )
            {
                BL_THROW(
                    InvalidDataFormatException()
                        << eh::errinfo_http2_error_code( errorCode ),
                    BL_MSG()
                        << "The HTTP/2 fingerprint cannot be taken - "
                        << reason
                    );
            }

            /**
             * @brief A refusal the frame codec or HPACK raised, as the one exception this class
             * throws
             *
             * The codec's message is kept, since it names the rule and is itself a fixed string
             * (FrameCodecT::throwConnectionError, HpackErrorT::throwCompressionError). One of its
             * phrasings reads oddly here - HPACK's "more than was advertised as
             * SETTINGS_HEADER_TABLE_SIZE" means more than HPACK_TABLE_SIZE_CEILING, which plays
             * that part for a reader who never saw the server's SETTINGS
             */

            SAA_noreturn
            static void refuseFromCodec(
                SAA_in          const BaseExceptionDefault&          codecException,
                SAA_in          const std::exception_ptr&            cause
                )
            {
                const auto* const code =
                    eh::get_error_info< eh::errinfo_http2_error_code >( codecException );

                BL_THROW(
                    InvalidDataFormatException()
                        << eh::errinfo_http2_error_code(
                            code != nullptr ?
                                *code :
                                static_cast< std::uint32_t >( Globals::ERROR_CODE_PROTOCOL_ERROR )
                            )
                        << eh::errinfo_nested_exception_ptr( cause ),
                    BL_MSG()
                        << "The HTTP/2 fingerprint cannot be taken - the opening is not well "
                        << "formed: "
                        << codecException.what()
                    );
            }

            /*************************************************************************************
             * Reading
             */

            void feedImpl(
                SAA_in_opt      const std::uint8_t*                  data,
                SAA_in          const std::size_t                    size
                )
            {
                const auto& preface = Globals::g_connectionPreface;

                std::size_t offset = 0U;

                while( m_prefaceOctets < preface.size() && offset < size )
                {
                    const auto expected = static_cast< std::uint8_t >( preface[ m_prefaceOctets ] );

                    if( data[ offset ] != expected )
                    {
                        refuse(
                            "the opening does not begin with the client connection preface "
                            "(RFC 9113 3.4)"
                            );
                    }

                    ++offset;
                    ++m_prefaceOctets;
                }

                while( offset < size && ! m_isComplete )
                {
                    const auto consumed = m_reader.feed( data + offset, size - offset );

                    offset += consumed;

                    if( ! m_reader.hasFrame() )
                    {
                        if( consumed == 0U )
                        {
                            break;
                        }

                        continue;
                    }

                    handleFrame( m_reader.frame() );

                    m_reader.consumeFrame();
                }
            }

            void handleFrame( SAA_in const FrameView& frame )
            {
                /*
                 * A frame which is the wrong size for its type is a STREAM error to the codec -
                 * it is consumed and flagged rather than thrown (FrameView::streamErrorCode) - but
                 * to a fingerprint it is a malformed opening like any other
                 */

                if( frame.streamErrorCode != Globals::ERROR_CODE_NO_ERROR )
                {
                    refuseWithCode(
                        frame.streamErrorCode,
                        "the opening carries a frame which is not of the size its type requires"
                        );
                }

                const auto type = frame.header.type.value();

                if( ! m_sawSettings )
                {
                    if(
                        type != Globals::FRAME_TYPE_SETTINGS ||
                        0U != ( frame.header.flags & Globals::FRAME_FLAG_ACK )
                        )
                    {
                        refuse(
                            "the opening does not follow its preface with a SETTINGS frame "
                            "(RFC 9113 3.4)"
                            );
                    }

                    m_parts.settings = FrameCodec::parseSettings( frame );

                    m_sawSettings = true;

                    return;
                }

                if( m_inFirstBlock )
                {
                    /*
                     * While the block is open the reader lets nothing through but a CONTINUATION
                     * on the same stream (RFC 9113 6.10), so this is one
                     */

                    const auto payload = FrameCodec::parseContinuation( frame );

                    ++m_continuations;

                    if( m_continuations > MAX_FIRST_HEADER_BLOCK_CONTINUATIONS )
                    {
                        refuse(
                            "the opening's first header block is spread over more than " +
                            std::to_string(
                                static_cast< std::size_t >( MAX_FIRST_HEADER_BLOCK_CONTINUATIONS )
                                ) +
                            " CONTINUATION frames"
                            );
                    }

                    appendToFirstBlock( payload.fieldBlock, payload.fieldBlockSize );

                    if( payload.endHeaders )
                    {
                        completeFirstBlock();
                    }

                    return;
                }

                switch( type )
                {
                    case Globals::FRAME_TYPE_HEADERS:
                        {
                            const auto payload = FrameCodec::parseHeaders( frame );

                            m_parts.headersPriority = payload.priority;

                            m_inFirstBlock = true;

                            appendToFirstBlock( payload.fieldBlock, payload.fieldBlockSize );

                            if( payload.endHeaders )
                            {
                                completeFirstBlock();
                            }
                        }
                        break;

                    case Globals::FRAME_TYPE_PRIORITY:

                        if( m_parts.idleStreamPriorities.size() >= MAX_IDLE_STREAM_PRIORITIES )
                        {
                            refuse(
                                "the opening sends more than " +
                                std::to_string(
                                    static_cast< std::size_t >( MAX_IDLE_STREAM_PRIORITIES )
                                    ) +
                                " PRIORITY frames ahead of its first HEADERS frame"
                                );
                        }

                        m_parts.idleStreamPriorities.push_back(
                            FrameCodec::parsePriority( frame )
                            );
                        break;

                    case Globals::FRAME_TYPE_WINDOW_UPDATE:

                        if( frame.header.streamId == Globals::STREAM_ID_CONNECTION )
                        {
                            const auto increment = FrameCodec::parseWindowUpdate( frame );

                            if( increment == 0U )
                            {
                                refuse(
                                    "the opening sends a connection WINDOW_UPDATE with an "
                                    "increment of zero (RFC 9113 6.9)"
                                    );
                            }

                            if( m_parts.connectionWindowUpdateIncrement == 0U )
                            {
                                m_parts.connectionWindowUpdateIncrement = increment;
                            }
                        }
                        break;

                    default:

                        /*
                         * A later SETTINGS frame, an acknowledgement, PING, GOAWAY, RST_STREAM,
                         * DATA, a PUSH_PROMISE and its CONTINUATION frames, an unknown type -
                         * none is a part of the fingerprint, and the reader has already judged
                         * its structure
                         */

                        break;
                }
            }

            void appendToFirstBlock(
                SAA_in_opt      const std::uint8_t*                  fragment,
                SAA_in          const std::size_t                    size
                )
            {
                if( size > MAX_FIRST_HEADER_BLOCK_SIZE - m_block.size() )
                {
                    refuse(
                        "the opening's first header block is larger than " +
                        std::to_string( static_cast< std::size_t >( MAX_FIRST_HEADER_BLOCK_SIZE ) ) +
                        " octets"
                        );
                }

                if( size != 0U )
                {
                    m_block.append( reinterpret_cast< const char* >( fragment ), size );
                }
            }

            /**
             * @brief Decodes the first block and reads the pseudo-header order out of it
             *
             * A fresh decoder is the right one: the first block is the first thing the client's
             * encoder ever wrote on the connection, so the dynamic table it was written against
             * was empty
             */

            void completeFirstBlock()
            {
                HpackDecoder decoder( HPACK_TABLE_SIZE_CEILING );
                HpackFieldList fields;

                const auto outcome = decoder.decode(
                    m_block.data(),
                    m_block.size(),
                    MAX_DECODED_FIRST_HEADER_BLOCK_SIZE,
                    fields
                    );

                if( outcome != HpackDecoder::Outcome::Complete )
                {
                    refuse(
                        "the opening's first header block decodes to more than " +
                        std::to_string(
                            static_cast< std::size_t >( MAX_DECODED_FIRST_HEADER_BLOCK_SIZE )
                            ) +
                        " octets"
                        );
                }

                std::vector< Http2PseudoHeader > order;

                bool sawRegularField = false;

                for( std::size_t i = 0U; i < fields.size(); ++i )
                {
                    const auto& name = fields[ i ].name();

                    if( name.empty() || name[ 0 ] != ':' )
                    {
                        sawRegularField = true;

                        continue;
                    }

                    if( sawRegularField )
                    {
                        refuse(
                            "the opening's first header block carries a pseudo-header after a "
                            "regular field (RFC 9113 8.3)"
                            );
                    }

                    Http2PseudoHeader pseudoHeader = Http2PseudoHeader::Method;

                    if( ! toPseudoHeader( name, pseudoHeader ) )
                    {
                        refuse(
                            "the opening's first header block carries a pseudo-header which is "
                            "not defined for a request (RFC 9113 8.3.1)"
                            );
                    }

                    if( std::find( order.begin(), order.end(), pseudoHeader ) != order.end() )
                    {
                        refuse(
                            "the opening's first header block carries a pseudo-header more than "
                            "once (RFC 9113 8.3)"
                            );
                    }

                    order.push_back( pseudoHeader );
                }

                m_parts.pseudoHeaderOrder.swap( order );

                std::string().swap( m_block );

                m_inFirstBlock = false;
                m_isComplete = true;
            }

            /*
             * The four of RFC 9113 8.3.1, spelled exactly - a field name is lowercase on the wire
             * (8.2.1), so ":Method" is not one of them - and not ":protocol", which the session
             * does not define either (design 4.7)
             */

            static bool toPseudoHeader(
                SAA_in          const std::string&                   name,
                SAA_out         Http2PseudoHeader&                   pseudoHeader
                )
            {
                if( name == ":method" )
                {
                    pseudoHeader = Http2PseudoHeader::Method;
                }
                else if( name == ":authority" )
                {
                    pseudoHeader = Http2PseudoHeader::Authority;
                }
                else if( name == ":scheme" )
                {
                    pseudoHeader = Http2PseudoHeader::Scheme;
                }
                else if( name == ":path" )
                {
                    pseudoHeader = Http2PseudoHeader::Path;
                }
                else
                {
                    return false;
                }

                return true;
            }

            static char letterOf( SAA_in const Http2PseudoHeader pseudoHeader )
            {
                char letter = '\0';

                switch( pseudoHeader )
                {
                    case Http2PseudoHeader::Method:
                        letter = 'm';
                        break;

                    case Http2PseudoHeader::Authority:
                        letter = 'a';
                        break;

                    case Http2PseudoHeader::Scheme:
                        letter = 's';
                        break;

                    case Http2PseudoHeader::Path:
                        letter = 'p';
                        break;
                }

                BL_CHK(
                    false,
                    letter != '\0',
                    BL_MSG()
                        << "An HTTP/2 fingerprint was asked to render a pseudo-header it has no "
                        << "letter for"
                    );

                return letter;
            }
        };

        typedef Http2FingerprintT<> Http2Fingerprint;

    } // http2

} // bl

#endif /* __BL_HTTP2_FINGERPRINT_H_ */
