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

#ifndef __BL_CRYPTO_TLSCLIENTHELLO_H_
#define __BL_CRYPTO_TLSCLIENTHELLO_H_

#include <baselib/crypto/ErrorHandling.h>

#include <baselib/core/BaseIncludes.h>

#include <openssl/md5.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace bl
{
    namespace crypto
    {
        /**
         * @brief What a ClientHello carried, as it was sent
         *
         * This is the input to the fingerprints below and it is deliberately faithful: nothing is
         * filtered out here, GREASE values included, because what a fidelity report (design 6.6)
         * has to state is what went on the wire. JA3 and JA4 each drop GREASE where their own
         * definitions say to, and that is done when they are computed
         */

        struct TlsClientHelloInfo
        {
            /*
             * The legacy_version field of the ClientHello itself. From TLS 1.3 onwards this is
             * pinned at 0x0303 whatever is really being offered, which is why JA4 prefers the
             * supported_versions extension and JA3, which predates it, does not
             */

            cpp::ScalarTypeIniter< std::uint16_t >                              legacyVersion;

            /*
             * The highest non-GREASE value of the supported_versions extension, or legacyVersion
             * when the extension is absent
             */

            cpp::ScalarTypeIniter< std::uint16_t >                              highestSupportedVersion;

            cpp::ScalarTypeIniter< bool >                                       hasServerName;

            std::vector< std::uint16_t >                                        cipherSuites;
            std::vector< std::uint16_t >                                        extensions;
            std::vector< std::uint16_t >                                        supportedGroups;
            std::vector< std::uint8_t >                                         ecPointFormats;
            std::vector< std::uint16_t >                                        signatureAlgorithms;
            std::vector< std::string >                                          alpnProtocols;
        };

        /**
         * @brief The fingerprints of one ClientHello
         */

        struct TlsClientHelloFingerprint
        {
            std::string                                                         ja3;
            std::string                                                         ja3Hash;
            std::string                                                         ja4;
        };

        namespace detail
        {
            /**
             * @brief A bounds checked forward cursor over the captured bytes
             *
             * Every read goes through it, so no length field in the message can make the parser
             * step outside the buffer it was given
             */

            class TlsByteCursor
            {
            private:

                const unsigned char*                                            m_data;
                std::size_t                                                     m_size;
                std::size_t                                                     m_pos;

            public:

                TlsByteCursor(
                    SAA_in                  const unsigned char*                data,
                    SAA_in                  const std::size_t                   size
                    ) NOEXCEPT
                    :
                    m_data( data ),
                    m_size( size ),
                    m_pos( 0U )
                {
                }

                std::size_t remaining() const NOEXCEPT
                {
                    return m_size - m_pos;
                }

                bool atEnd() const NOEXCEPT
                {
                    return m_pos >= m_size;
                }

                void chkAvailable( SAA_in const std::size_t count ) const
                {
                    if( remaining() < count )
                    {
                        BL_THROW(
                            InvalidDataFormatException(),
                            BL_MSG()
                                << "A TLS ClientHello is truncated"
                            );
                    }
                }

                std::uint8_t readUint8()
                {
                    chkAvailable( 1U );

                    return m_data[ m_pos++ ];
                }

                std::uint16_t readUint16()
                {
                    chkAvailable( 2U );

                    const std::uint16_t value = static_cast< std::uint16_t >(
                        ( static_cast< std::uint16_t >( m_data[ m_pos ] ) << 8 ) |
                        static_cast< std::uint16_t >( m_data[ m_pos + 1U ] )
                        );

                    m_pos += 2U;

                    return value;
                }

                std::uint32_t readUint24()
                {
                    chkAvailable( 3U );

                    const std::uint32_t value =
                        ( static_cast< std::uint32_t >( m_data[ m_pos ] ) << 16 ) |
                        ( static_cast< std::uint32_t >( m_data[ m_pos + 1U ] ) << 8 ) |
                        static_cast< std::uint32_t >( m_data[ m_pos + 2U ] );

                    m_pos += 3U;

                    return value;
                }

                auto take( SAA_in const std::size_t count ) -> const unsigned char*
                {
                    chkAvailable( count );

                    const unsigned char* const result = m_data + m_pos;

                    m_pos += count;

                    return result;
                }

                void skip( SAA_in const std::size_t count )
                {
                    ( void ) take( count );
                }

                /**
                 * @brief A cursor over the next 'count' bytes, which are consumed from this one
                 *
                 * This is how a length-prefixed block is parsed without the block being able to
                 * read past its own end
                 */

                auto subCursor( SAA_in const std::size_t count ) -> TlsByteCursor
                {
                    return TlsByteCursor( take( count ), count );
                }
            };

        } // detail

        /**
         * @brief Parses a captured ClientHello and computes its JA3 and JA4 fingerprints
         *
         * The bytes come from AsioSslStreamWrapperT::getCapturedClientHello(), i.e. from the
         * OpenSSL message callback, which hands over the whole handshake message - the one byte
         * type, the three byte length, then the body
         *
         * References: JA3 is https://github.com/salesforce/ja3 and JA4 is the TLS member of the
         * JA4+ suite, https://github.com/FoxIO-LLC/ja4. Both are summarized inline where they are
         * implemented, because each has details which are not obvious from its name
         */

        template
        <
            typename E = void
        >
        class TlsClientHelloT
        {
            BL_DECLARE_STATIC( TlsClientHelloT )

        private:

            enum : std::uint16_t
            {
                EXTENSION_SERVER_NAME                   = 0x0000,
                EXTENSION_SUPPORTED_GROUPS              = 0x000A,
                EXTENSION_EC_POINT_FORMATS              = 0x000B,
                EXTENSION_SIGNATURE_ALGORITHMS          = 0x000D,
                EXTENSION_ALPN                          = 0x0010,
                EXTENSION_SUPPORTED_VERSIONS            = 0x002B,
            };

            enum : std::uint8_t
            {
                HANDSHAKE_TYPE_CLIENT_HELLO             = 0x01,
            };

            /**
             * @brief Hexadecimal, lower case, without touching a stream
             *
             * A stream would bring the global locale into a value which has to be byte for byte
             * reproducible across hosts; the digits are written out instead
             */

            static void appendHex(
                SAA_inout           std::string&                        text,
                SAA_in              const std::uint8_t                  value
                )
            {
                static const char digits[] = "0123456789abcdef";

                text += digits[ ( value >> 4 ) & 0x0F ];
                text += digits[ value & 0x0F ];
            }

            static auto toHex4( SAA_in const std::uint16_t value ) -> std::string
            {
                std::string text;

                appendHex( text, static_cast< std::uint8_t >( value >> 8 ) );
                appendHex( text, static_cast< std::uint8_t >( value & 0xFF ) );

                return text;
            }

            static auto toHex( SAA_in const std::uint8_t* digest, SAA_in const std::size_t size )
                -> std::string
            {
                std::string text;

                for( std::size_t i = 0U; i < size; ++i )
                {
                    appendHex( text, digest[ i ] );
                }

                return text;
            }

            static auto joinDecimal( SAA_in const std::vector< std::uint16_t >& values )
                -> std::string
            {
                std::string text;

                for( const auto value : values )
                {
                    if( ! text.empty() )
                    {
                        text += '-';
                    }

                    text += std::to_string( value );
                }

                return text;
            }

            static auto joinHex( SAA_in const std::vector< std::uint16_t >& values )
                -> std::string
            {
                std::string text;

                for( const auto value : values )
                {
                    if( ! text.empty() )
                    {
                        text += ',';
                    }

                    text += toHex4( value );
                }

                return text;
            }

            static auto withoutGrease( SAA_in const std::vector< std::uint16_t >& values )
                -> std::vector< std::uint16_t >
            {
                std::vector< std::uint16_t > result;

                for( const auto value : values )
                {
                    if( ! isGreaseValue( value ) )
                    {
                        result.push_back( value );
                    }
                }

                return result;
            }

            /**
             * @brief Two decimal digits, saturating, which is what both counted fields of JA4 use
             */

            static auto toTwoDigits( SAA_in const std::size_t count ) -> std::string
            {
                const std::size_t capped = count > 99U ? 99U : count;

                std::string text;

                text += static_cast< char >( '0' + ( capped / 10U ) );
                text += static_cast< char >( '0' + ( capped % 10U ) );

                return text;
            }

            static bool isAsciiAlphaNumeric( SAA_in const char ch ) NOEXCEPT
            {
                return
                    ( ch >= 'A' && ch <= 'Z' ) ||
                    ( ch >= 'a' && ch <= 'z' ) ||
                    ( ch >= '0' && ch <= '9' );
            }

            static void parseServerNameExtension(
                SAA_inout           TlsClientHelloInfo&                 info,
                SAA_inout           detail::TlsByteCursor&              cursor
                )
            {
                /*
                 * The presence of the extension is what JA4 reports, not the name in it, so the
                 * body is not decoded; an empty body still counts as present, which is what an
                 * encrypted or padded hello would carry
                 */

                BL_UNUSED( cursor );

                info.hasServerName = true;
            }

            static void parseSupportedGroupsExtension(
                SAA_inout           TlsClientHelloInfo&                 info,
                SAA_inout           detail::TlsByteCursor&              cursor
                )
            {
                auto list = cursor.subCursor( cursor.readUint16() );

                while( ! list.atEnd() )
                {
                    info.supportedGroups.push_back( list.readUint16() );
                }
            }

            static void parseEcPointFormatsExtension(
                SAA_inout           TlsClientHelloInfo&                 info,
                SAA_inout           detail::TlsByteCursor&              cursor
                )
            {
                auto list = cursor.subCursor( cursor.readUint8() );

                while( ! list.atEnd() )
                {
                    info.ecPointFormats.push_back( list.readUint8() );
                }
            }

            static void parseSignatureAlgorithmsExtension(
                SAA_inout           TlsClientHelloInfo&                 info,
                SAA_inout           detail::TlsByteCursor&              cursor
                )
            {
                auto list = cursor.subCursor( cursor.readUint16() );

                while( ! list.atEnd() )
                {
                    info.signatureAlgorithms.push_back( list.readUint16() );
                }
            }

            static void parseAlpnExtension(
                SAA_inout           TlsClientHelloInfo&                 info,
                SAA_inout           detail::TlsByteCursor&              cursor
                )
            {
                auto list = cursor.subCursor( cursor.readUint16() );

                while( ! list.atEnd() )
                {
                    const std::size_t length = list.readUint8();

                    const auto* const bytes = list.take( length );

                    info.alpnProtocols.push_back(
                        std::string( reinterpret_cast< const char* >( bytes ), length )
                        );
                }
            }

            static void parseSupportedVersionsExtension(
                SAA_inout           TlsClientHelloInfo&                 info,
                SAA_inout           detail::TlsByteCursor&              cursor
                )
            {
                auto list = cursor.subCursor( cursor.readUint8() );

                std::uint16_t highest = 0U;

                while( ! list.atEnd() )
                {
                    const auto version = list.readUint16();

                    if( ! isGreaseValue( version ) && version > highest )
                    {
                        highest = version;
                    }
                }

                if( 0U != highest )
                {
                    info.highestSupportedVersion = highest;
                }
            }

        public:

            /**
             * @brief Whether a two byte value is one of the sixteen GREASE code points
             *
             * RFC 8701 reserves 0x0A0A, 0x1A1A ... 0xFAFA - both bytes equal, and the low nibble
             * of each is 0x0A - in the cipher suite, extension, named group, signature algorithm
             * and version registries. A client sends them so that a peer which cannot ignore an
             * unknown value is found out early. Neither JA3 nor JA4 includes them, because they
             * are chosen at random per connection and would make every hello a different one
             */

            static bool isGreaseValue( SAA_in const std::uint16_t value ) NOEXCEPT
            {
                return
                    ( value >> 8 ) == ( value & 0x00FF ) &&
                    0x0A == ( value & 0x000F );
            }

            /**
             * @brief Parses the handshake message the capture hook recorded
             *
             * Throws InvalidDataFormatException when the bytes are not a well formed ClientHello
             */

            static auto parse( SAA_in const std::vector< unsigned char >& handshakeMessage )
                -> TlsClientHelloInfo
            {
                TlsClientHelloInfo info;

                detail::TlsByteCursor message( handshakeMessage.data(), handshakeMessage.size() );

                if( HANDSHAKE_TYPE_CLIENT_HELLO != message.readUint8() )
                {
                    BL_THROW(
                        InvalidDataFormatException(),
                        BL_MSG()
                            << "The captured handshake message is not a ClientHello"
                        );
                }

                /*
                 * The three byte length is honored rather than trusted to equal what is left: the
                 * callback hands over one message, but a body shorter than the buffer would
                 * otherwise let trailing bytes be parsed as extensions
                 */

                auto body = message.subCursor( message.readUint24() );

                info.legacyVersion = body.readUint16();
                info.highestSupportedVersion = info.legacyVersion;

                body.skip( 32U );                                   /* random */

                body.skip( body.readUint8() );                      /* legacy_session_id */

                {
                    auto suites = body.subCursor( body.readUint16() );

                    while( ! suites.atEnd() )
                    {
                        info.cipherSuites.push_back( suites.readUint16() );
                    }
                }

                body.skip( body.readUint8() );                      /* legacy_compression_methods */

                if( body.atEnd() )
                {
                    /*
                     * A hello predating TLS 1.2 may carry no extension block at all, and every
                     * extension derived field of the fingerprints is then empty
                     */

                    return info;
                }

                auto extensions = body.subCursor( body.readUint16() );

                while( ! extensions.atEnd() )
                {
                    const auto extensionType = extensions.readUint16();

                    auto extension = extensions.subCursor( extensions.readUint16() );

                    info.extensions.push_back( extensionType );

                    switch( extensionType )
                    {
                        case EXTENSION_SERVER_NAME:
                            parseServerNameExtension( info, extension );
                            break;

                        case EXTENSION_SUPPORTED_GROUPS:
                            parseSupportedGroupsExtension( info, extension );
                            break;

                        case EXTENSION_EC_POINT_FORMATS:
                            parseEcPointFormatsExtension( info, extension );
                            break;

                        case EXTENSION_SIGNATURE_ALGORITHMS:
                            parseSignatureAlgorithmsExtension( info, extension );
                            break;

                        case EXTENSION_ALPN:
                            parseAlpnExtension( info, extension );
                            break;

                        case EXTENSION_SUPPORTED_VERSIONS:
                            parseSupportedVersionsExtension( info, extension );
                            break;

                        default:
                            break;
                    }
                }

                return info;
            }

            /**
             * @brief The JA3 string
             *
             * Five comma separated fields - the ClientHello version, the cipher suites, the
             * extension types, the named groups and the EC point formats - each a list of decimal
             * values joined with '-', with the GREASE values removed. A field whose extension was
             * absent is empty and its commas remain, which is why an absent extension and an empty
             * one are the same thing to JA3
             *
             * The version is the ClientHello's own legacy_version and not the supported_versions
             * extension, because JA3 predates TLS 1.3: every TLS 1.3 client therefore reports 771
             * here. That is a property of JA3 rather than an oversight of this code
             */

            static auto computeJa3String( SAA_in const TlsClientHelloInfo& info ) -> std::string
            {
                std::string text = std::to_string( info.legacyVersion.value() );

                text += ',';
                text += joinDecimal( withoutGrease( info.cipherSuites ) );

                text += ',';
                text += joinDecimal( withoutGrease( info.extensions ) );

                text += ',';
                text += joinDecimal( withoutGrease( info.supportedGroups ) );

                text += ',';

                {
                    std::string formats;

                    for( const auto value : info.ecPointFormats )
                    {
                        if( ! formats.empty() )
                        {
                            formats += '-';
                        }

                        formats += std::to_string( static_cast< unsigned >( value ) );
                    }

                    text += formats;
                }

                return text;
            }

            /**
             * @brief The MD5 of the JA3 string, which is what "the JA3 hash" means
             *
             * MD5 is what the definition specifies and this is a fingerprint rather than a
             * security primitive, so its weakness is not relevant here - it is never used to
             * decide anything, only to name a hello
             */

            static auto computeJa3Hash( SAA_in const std::string& ja3String ) -> std::string
            {
                std::uint8_t digest[ MD5_DIGEST_LENGTH ];

                ::MD5_CTX context;

                BL_CHK_CRYPTO_API_NM( ::MD5_Init( &context ) );

                BL_CHK_CRYPTO_API_NM( ::MD5_Update( &context, ja3String.c_str(), ja3String.size() ) );

                BL_CHK_CRYPTO_API_NM( ::MD5_Final( digest, &context ) );

                return toHex( digest, sizeof( digest ) );
            }

            /**
             * @brief The first twelve hexadecimal characters of the SHA-256, which is the
             * truncation both halves of a JA4 use
             */

            static auto computeTruncatedSha256( SAA_in const std::string& text ) -> std::string
            {
                std::uint8_t digest[ SHA256_DIGEST_LENGTH ];

                ::SHA256_CTX context;

                BL_CHK_CRYPTO_API_NM( ::SHA256_Init( &context ) );

                BL_CHK_CRYPTO_API_NM( ::SHA256_Update( &context, text.c_str(), text.size() ) );

                BL_CHK_CRYPTO_API_NM( ::SHA256_Final( digest, &context ) );

                return toHex( digest, sizeof( digest ) ).substr( 0U, 12U );
            }

            /**
             * @brief The first section of a JA4, which is readable rather than hashed
             *
             * Ten characters: the transport, the TLS version, whether a server name was sent, the
             * number of cipher suites, the number of extensions, and two characters of the first
             * ALPN protocol
             *
             * The two counts exclude GREASE and saturate at 99. The extension count includes the
             * server name and ALPN extensions even though the hashed section below excludes them -
             * that asymmetry is in the definition, so that the count still reflects the whole
             * hello while the hash stays the same across the destinations of one client
             */

            static auto computeJa4Prefix( SAA_in const TlsClientHelloInfo& info ) -> std::string
            {
                std::string text = "t";

                switch( info.highestSupportedVersion.value() )
                {
                    case 0x0304:    text += "13"; break;
                    case 0x0303:    text += "12"; break;
                    case 0x0302:    text += "11"; break;
                    case 0x0301:    text += "10"; break;
                    case 0x0300:    text += "s3"; break;
                    case 0x0200:    text += "s2"; break;
                    case 0x0100:    text += "s1"; break;
                    default:        text += "00"; break;
                }

                text += info.hasServerName ? 'd' : 'i';

                text += toTwoDigits( withoutGrease( info.cipherSuites ).size() );
                text += toTwoDigits( withoutGrease( info.extensions ).size() );

                if( info.alpnProtocols.empty() || info.alpnProtocols.front().empty() )
                {
                    text += "00";
                }
                else
                {
                    const auto& protocol = info.alpnProtocols.front();

                    const char first = protocol.front();
                    const char last = protocol.back();

                    if( isAsciiAlphaNumeric( first ) && isAsciiAlphaNumeric( last ) )
                    {
                        text += first;
                        text += last;
                    }
                    else
                    {
                        /*
                         * A protocol id which is not printable is reported as the first character
                         * of the first byte's hexadecimal and the last character of the last
                         * byte's, so that the field stays two characters wide
                         */

                        std::string firstHex;
                        std::string lastHex;

                        appendHex( firstHex, static_cast< std::uint8_t >( first ) );
                        appendHex( lastHex, static_cast< std::uint8_t >( last ) );

                        text += firstHex.front();
                        text += lastHex.back();
                    }
                }

                return text;
            }

            /**
             * @brief The string the middle section of a JA4 is the hash of
             *
             * The cipher suites as four digit hexadecimal, GREASE removed, sorted and joined with
             * commas. Sorting is what makes a JA4 survive a client which shuffles its suite order
             * per connection, which is the whole reason the format exists
             */

            static auto computeJa4CipherInput( SAA_in const TlsClientHelloInfo& info )
                -> std::string
            {
                auto values = withoutGrease( info.cipherSuites );

                std::sort( values.begin(), values.end() );

                return joinHex( values );
            }

            /**
             * @brief The string the last section of a JA4 is the hash of
             *
             * The extension types, GREASE removed and the server name and ALPN extensions removed
             * as well, sorted and joined with commas; then an underscore and the signature
             * algorithms in the order they were sent, which are deliberately not sorted
             *
             * The server name and ALPN are left out so that one client contacting two different
             * hosts, or negotiating two different protocols, still fingerprints the same. When
             * there is no signature algorithm extension the underscore is not written either
             */

            static auto computeJa4ExtensionInput( SAA_in const TlsClientHelloInfo& info )
                -> std::string
            {
                std::vector< std::uint16_t > values;

                for( const auto value : info.extensions )
                {
                    if(
                        isGreaseValue( value ) ||
                        EXTENSION_SERVER_NAME == value ||
                        EXTENSION_ALPN == value
                        )
                    {
                        continue;
                    }

                    values.push_back( value );
                }

                std::sort( values.begin(), values.end() );

                std::string text = joinHex( values );

                const auto signatureAlgorithms = withoutGrease( info.signatureAlgorithms );

                if( ! signatureAlgorithms.empty() )
                {
                    text += '_';
                    text += joinHex( signatureAlgorithms );
                }

                return text;
            }

            /**
             * @brief The JA4 of a parsed ClientHello
             *
             * Its three sections are joined with underscores; an empty cipher or extension list
             * hashes to twelve zeros rather than to the hash of the empty string, which is what
             * the definition asks for so that "nothing there" is visibly nothing
             */

            static auto computeJa4( SAA_in const TlsClientHelloInfo& info ) -> std::string
            {
                const auto cipherInput = computeJa4CipherInput( info );
                const auto extensionInput = computeJa4ExtensionInput( info );

                std::string text = computeJa4Prefix( info );

                text += '_';
                text += cipherInput.empty() ? std::string( 12U, '0' ) : computeTruncatedSha256( cipherInput );

                text += '_';
                text += extensionInput.empty() ? std::string( 12U, '0' ) : computeTruncatedSha256( extensionInput );

                return text;
            }

            /**
             * @brief Both fingerprints of a captured ClientHello, in one step
             */

            static auto computeFingerprint( SAA_in const std::vector< unsigned char >& handshakeMessage )
                -> TlsClientHelloFingerprint
            {
                const auto info = parse( handshakeMessage );

                TlsClientHelloFingerprint fingerprint;

                fingerprint.ja3 = computeJa3String( info );
                fingerprint.ja3Hash = computeJa3Hash( fingerprint.ja3 );
                fingerprint.ja4 = computeJa4( info );

                return fingerprint;
            }
        };

        typedef TlsClientHelloT<> TlsClientHello;

    } // crypto

} // bl

#endif /* __BL_CRYPTO_TLSCLIENTHELLO_H_ */
