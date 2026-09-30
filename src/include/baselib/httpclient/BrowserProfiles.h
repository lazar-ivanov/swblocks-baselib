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

#ifndef __BL_HTTPCLIENT_BROWSERPROFILES_H_
#define __BL_HTTPCLIENT_BROWSERPROFILES_H_

#include <baselib/httpclient/HeaderProfile.h>

#include <baselib/http2/Http2Profile.h>
#include <baselib/http/HeaderList.h>

#include <baselib/crypto/TlsNameRules.h>
#include <baselib/crypto/TlsClientProfile.h>

/*
 * core/Utils.h before the data model: the property macros of data/DataModelObjectDefs.h use
 * bl::utils::lexical_cast without including the header which declares it, so a translation unit
 * which includes a data model first does not compile
 */

#include <baselib/core/Utils.h>

#include <baselib/data/models/HttpClientProfiles.h>
#include <baselib/data/DataModelObject.h>

#include <baselib/core/JsonUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bl
{
    namespace httpclient
    {
        /*
         * Browser profiles - notes/plans/http2-design.md 6.2, and 5.8 for the API
         *
         * A browser profile is a JSON document: its identity, its shape - a TLS, an HTTP/2 and a
         * header profile - and its version strings. load( ) turns one into a BrowserProfile,
         * through the data model of data/models/HttpClientProfiles.h, validating as it goes;
         * get( ) returns a built-in one by id. Built-in profiles are JSON literals which go through
         * load( ) exactly as a supplied one does (6.2), so there is one code path and one set of
         * rules
         *
         * A LOADED PROFILE IS UNTRUSTED INPUT (6.2), and a document which breaks any rule below is
         * refused whole, with an InvalidDataFormatException which names the offending property by
         * its path in the document - never by echoing its value, which may carry the very CR or LF
         * a rule exists to stop. Nothing half-loaded is ever returned
         *
         *   - the document is at most MAX_DOCUMENT_SIZE bytes, checked before it is parsed; it is
         *     well-formed JSON of exactly the data model's shape: every value of its type, every
         *     required property present, and NO PROPERTY THE MODEL DOES NOT KNOW - a misspelled
         *     key would otherwise be dropped silently and its list read as empty, which for a
         *     suite list means "keep the library default"
         *   - the identity: an id which is safe as a registry key and in a ConnectionKey (see
         *     isIdSafe( )), a family, one of the two grades, and deviations of printable ASCII
         *   - the TLS names by crypto::TlsNameRules - a plain name, and no anonymous or NULL suite
         *   - ALPN protocol ids of 1 to 255 bytes of visible ASCII (RFC 7301)
         *   - SETTINGS ids and values in range: an id is 16 bits and a value 32 (RFC 9113 6.5.1);
         *     the ids of 6.5.2 this library interprets are held to that section's ranges and
         *     SETTINGS_ENABLE_PUSH to zero, because this client never accepts a push (D11) and the
         *     session refuses a profile which says otherwise; every other id passes through
         *     unread, as http2::Http2Profile allows. A repeated id is legal - 6.5.3 gives it a
         *     meaning, the later value, and the session implements exactly that
         *   - the flow-control numbers the session casts or adds to a window, in range
         *   - priority fields in range: stream ids and dependencies of 31 bits, a weight of 8 -
         *     the octet on the wire, so the fingerprint's weight 256 is 255 here - no PRIORITY on
         *     stream 0 and no stream depending on itself
         *   - header names as lower-case tokens and values as field values, by http::HeaderList's
         *     own rules, which are the rules the session applies when it builds a request: no CR,
         *     LF, NUL or any other control character, no leading or trailing whitespace
         *   - no header the session or a driver owns: cookie and proxy-authorization, which the
         *     session merges or drops; authorization, a credential; content-length and
         *     transfer-encoding, the framing; keep-alive, proxy-connection and upgrade, which
         *     RFC 9113 8.2.2 forbids and no browser sends by default
         *   - every list bounded - the MAX_ constants below, each a generous multiple of what a
         *     browser sends, because the point is a finite ceiling rather than a tight fit; and
         *     no list repeats an element where a repeat has no meaning on the wire
         *
         * THE VERSION STRINGS (6.2). userAgent, secChUaBrands and platform change every few weeks
         * while the shape changes a few times a year, and a refresh must be an edit of those three
         * properties and nothing else. So a header list never carries their values: it carries
         * the header by NAME, WITH NO VALUE, at the position the browser sends it, and the loader
         * writes the composed value there -
         *
         *   - user-agent            <- userAgent, verbatim
         *   - sec-ch-ua             <- secChUaBrands, as a structured-field list (RFC 8941): each
         *                              brand a string with a 'v' parameter when it has a version,
         *                              in the order given, joined by ", ", and '"' and '\'
         *                              escaped - so a greased brand keeps its exact spelling
         *   - sec-ch-ua-platform    <- platform, as a structured-field string
         *
         * A list which carries one of these with a value of its own is refused, because that
         * value would survive a refresh. sec-ch-ua-mobile is not a version string - it is the
         * platform's and does not change on a refresh - so a profile which sends it carries it as
         * a literal, and the only literals it may carry are ?0 and ?1
         *
         * THE HTTP/1.1-ONLY HEADERS (the plan review's F6). There is one default list per request
         * kind, for both protocols. A browser's HTTP/1.1 request also carries Host, first, and
         * Connection: both sit in that one list at their HTTP/1.1 positions. 'host' is a marker:
         * isComputed, with no value, because its value is the authority of each request's URL -
         * and a captured Host naming the capture machine must never ship. 'connection' carries its
         * literal, keep-alive or close. HTTP/2 needs no mark of its own for either: the HTTP/2
         * driver removes Connection and every field it names (RFC 9113 8.2.2) and drops a Host
         * which agrees with :authority (8.3.1), without moving anything else. accept-encoding is
         * a marker too: its value is headers.acceptEncoding intersected with the registered
         * decoders, computed by the session per request (6.5)
         *
         * OpenSSL. This header names no OpenSSL header and makes no OpenSSL call; nothing in it
         * depends on the OpenSSL version. The data model's base header includes crypto/ for its
         * object hashes (data/DataModelObject.h), so a translation unit including this one
         * compiles OpenSSL's headers - as every data model user does - and this header therefore
         * stays out of every PreCompiled.h (design 9)
         */

        /**
         * @brief How closely a profile's TLS layer can match its browser (design 6.1)
         *
         * Approximate is first, so that a profile nobody filled claims the weaker grade
         */

        enum class BrowserProfileGrade : std::uint8_t
        {
            /**
             * The ClientHello cannot match the browser's JA4, for stated reasons
             */

            Approximate,

            /**
             * The ClientHello is expected to match the browser's JA4, subject to the spike
             */

            Ja4Candidate,
        };

        /**
         * @brief One browser profile, loaded and validated - see BrowserProfilesT::load( )
         *
         * The version strings are not fields: the loader has composed them into the header
         * lists, which is the only place they are sent from
         */

        struct BrowserProfile
        {
            std::string                                                         id;
            std::string                                                         family;
            cpp::ScalarTypeIniter< BrowserProfileGrade >                        grade;
            std::vector< std::string >                                          deviations;

            bl::crypto::TlsClientProfile                                        tls;
            bl::http2::Http2Profile                                             http2;
            bl::httpclient::HeaderProfile                                       headers;
        };

        /**
         * @brief Loads, validates and looks up browser profiles
         */

        template
        <
            typename E = void
        >
        class BrowserProfilesT FINAL
        {
            BL_DECLARE_STATIC( BrowserProfilesT )

        public:

            /*
             * The bounds. Each is a generous multiple of what a browser sends - Chrome offers about
             * fifteen suites, four groups, eight signature algorithms and four SETTINGS, and sends
             * about fifteen headers per request - so that a real profile never meets one, while a
             * hostile document meets a finite ceiling on every list it can grow
             */

            enum : std::size_t
            {
                MAX_DOCUMENT_SIZE                       = 256U * 1024U,
                MAX_ID_SIZE                             = 64U,
                MAX_DEVIATIONS                          = 64U,
                MAX_CIPHER_SUITES_TLS12                 = 64U,
                MAX_CIPHER_SUITES_TLS13                 = 16U,
                MAX_GROUPS                              = 32U,
                MAX_SIGNATURE_ALGORITHMS                = 32U,
                MAX_ALPN_PROTOCOLS                      = 8U,
                MAX_ALPN_PROTOCOL_SIZE                  = 255U,
                MAX_SETTINGS                            = 32U,
                MAX_IDLE_STREAM_PRIORITIES              = 16U,
                MAX_DEFAULT_HEADERS                     = 64U,
                MAX_CASE_MAP_ENTRIES                    = 64U,
                MAX_ACCEPT_ENCODINGS                    = 16U,
                MAX_ACCEPT_LANGUAGE_QVALUES             = 16U,
                MAX_SEC_CH_UA_BRANDS                    = 8U,
            };

        private:

            /*
             * The protocol numbers the rules are stated in - RFC 9113 4.1, 6.5.2, 6.9 and 6.9.1
             */

            enum : std::uint64_t
            {
                MAX_SETTING_ID                          = 0xFFFFU,
                MAX_SETTING_VALUE                       = 0xFFFFFFFFU,
                MAX_STREAM_ID                           = 0x7FFFFFFFU,
                MAX_WINDOW_SIZE                         = 0x7FFFFFFFU,
                INITIAL_WINDOW_SIZE                     = 65535U,
                MIN_MAX_FRAME_SIZE                      = 16384U,
                MAX_MAX_FRAME_SIZE                      = 16777215U,
                MAX_PRIORITY_WEIGHT                     = 255U,

                SETTINGS_ENABLE_PUSH                    = 0x2U,
                SETTINGS_INITIAL_WINDOW_SIZE            = 0x4U,
                SETTINGS_MAX_FRAME_SIZE                 = 0x5U,
            };

            typedef dm::httpclient::BrowserProfile                              model_t;

            /*************************************************************************************
             * Refusal - the one way out of a rule
             */

            SAA_noreturn
            static void refuse(
                SAA_in          const std::string&                              path,
                SAA_in          const std::string&                              reason
                )
            {
                BL_THROW(
                    InvalidDataFormatException()
                        << eh::errinfo_is_user_friendly( true ),
                    BL_MSG()
                        << "Invalid browser profile: "
                        << path
                        << " "
                        << reason
                    );
            }

            static std::string at(
                SAA_in          const std::string&                              path,
                SAA_in          const std::size_t                               index
                )
            {
                return path + "[" + std::to_string( index ) + "]";
            }

            static void chkBound(
                SAA_in          const std::string&                              path,
                SAA_in          const std::size_t                               size,
                SAA_in          const std::size_t                               bound
                )
            {
                if( size > bound )
                {
                    refuse( path, "has more than " + std::to_string( bound ) + " entries" );
                }
            }

            /*************************************************************************************
             * Character rules
             */

            static char toLowerAscii( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'A' && ch <= 'Z' ) ? static_cast< char >( ch - 'A' + 'a' ) : ch;
            }

            static bool equalsIgnoreCase(
                SAA_in          const std::string&                              lhs,
                SAA_in          const std::string&                              rhs
                ) NOEXCEPT
            {
                return http::HeaderList::equalsIgnoreCase( lhs, rhs );
            }

            static bool isPrintableAscii( SAA_in const char ch ) NOEXCEPT
            {
                return ch >= 0x20 && ch <= 0x7E;
            }

            static bool isPrintableAscii( SAA_in const std::string& text ) NOEXCEPT
            {
                for( std::size_t i = 0U; i < text.size(); ++i )
                {
                    if( ! isPrintableAscii( text[ i ] ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            static bool isLowerCaseToken( SAA_in const std::string& name ) NOEXCEPT
            {
                if( ! http::HeaderList::isValidHeaderName( name ) )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < name.size(); ++i )
                {
                    if( name[ i ] >= 'A' && name[ i ] <= 'Z' )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*
             * An identifier: 1 to MAX_ID_SIZE characters of letters, digits, '.', '_' and '-',
             * beginning with a letter or a digit; lower case only when isLowerCaseOnly
             */

            static bool isIdentifier(
                SAA_in          const std::string&                              text,
                SAA_in          const bool                                      isLowerCaseOnly
                ) NOEXCEPT
            {
                if( text.empty() || text.size() > MAX_ID_SIZE )
                {
                    return false;
                }

                for( std::size_t i = 0U; i < text.size(); ++i )
                {
                    const char ch = text[ i ];

                    const bool isAlphanumeric =
                        ( ch >= 'a' && ch <= 'z' ) ||
                        ( ch >= '0' && ch <= '9' ) ||
                        ( ! isLowerCaseOnly && ch >= 'A' && ch <= 'Z' );

                    if( isAlphanumeric )
                    {
                        continue;
                    }

                    if( 0U == i || ( '.' != ch && '_' != ch && '-' != ch ) )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*
             * qvalue = ( "0" [ "." 0*3DIGIT ] ) / ( "1" [ "." 0*3("0") ] ) - RFC 9110 12.4.2
             */

            static bool isQValue( SAA_in const std::string& text ) NOEXCEPT
            {
                if( text.empty() || text.size() > 5U || ( '0' != text[ 0 ] && '1' != text[ 0 ] ) )
                {
                    return false;
                }

                if( 1U == text.size() )
                {
                    return true;
                }

                if( '.' != text[ 1 ] )
                {
                    return false;
                }

                for( std::size_t i = 2U; i < text.size(); ++i )
                {
                    const bool isAllowed = '0' == text[ 0 ] ?
                        ( text[ i ] >= '0' && text[ i ] <= '9' ) :
                        '0' == text[ i ];

                    if( ! isAllowed )
                    {
                        return false;
                    }
                }

                return true;
            }

            /*
             * A field value as the session's own http::HeaderList will accept it - RFC 9110 5.5
             */

            static bool isFieldValue( SAA_in const std::string& value ) NOEXCEPT
            {
                return http::HeaderList::isValidHeaderValue( value );
            }

            /*************************************************************************************
             * The version strings - RFC 8941 serialization
             */

            /*
             * An sf-string (RFC 8941 4.1.6): the text between double quotes, with '"' and '\'
             * escaped by '\'. The caller has already refused anything but printable ASCII, which is
             * the one case in which that section fails serialization
             */

            static std::string structuredFieldString( SAA_in const std::string& text )
            {
                std::string result( 1U, '"' );

                for( std::size_t i = 0U; i < text.size(); ++i )
                {
                    if( '"' == text[ i ] || '\\' == text[ i ] )
                    {
                        result += '\\';
                    }

                    result += text[ i ];
                }

                result += '"';

                return result;
            }

            /*
             * The version strings, validated and composed once for every kind to use
             */

            struct VersionStrings
            {
                std::string                                                     userAgent;
                std::string                                                     secChUa;
                std::string                                                     secChUaPlatform;
            };

            static auto composeVersionStrings( SAA_in const model_t& model ) -> VersionStrings
            {
                VersionStrings result;

                if( ! model.userAgent().empty() && ! isFieldValue( model.userAgent() ) )
                {
                    refuse( "userAgent", "is not a valid field value" );
                }

                result.userAgent = model.userAgent();

                const auto& brands = model.secChUaBrands();

                chkBound( "secChUaBrands", brands.size(), MAX_SEC_CH_UA_BRANDS );

                for( std::size_t i = 0U; i < brands.size(); ++i )
                {
                    const auto& brand = *brands[ i ];

                    /*
                     * Not empty: brand is a required property of the data model, which refuses
                     * an empty string as not provided
                     */

                    if( ! isPrintableAscii( brand.brand() ) )
                    {
                        refuse(
                            at( "secChUaBrands", i ) + ".brand",
                            "carries a character outside printable ASCII"
                            );
                    }

                    if( ! isPrintableAscii( brand.version() ) )
                    {
                        refuse(
                            at( "secChUaBrands", i ) + ".version",
                            "carries a character outside printable ASCII"
                            );
                    }

                    if( ! result.secChUa.empty() )
                    {
                        result.secChUa += ", ";
                    }

                    result.secChUa += structuredFieldString( brand.brand() );

                    if( ! brand.version().empty() )
                    {
                        result.secChUa += ";v=";
                        result.secChUa += structuredFieldString( brand.version() );
                    }
                }

                if( ! isPrintableAscii( model.platform() ) )
                {
                    refuse( "platform", "carries a character outside printable ASCII" );
                }

                if( ! model.platform().empty() )
                {
                    result.secChUaPlatform = structuredFieldString( model.platform() );
                }

                return result;
            }

            /*************************************************************************************
             * The document
             */

            static std::string printableCopy( SAA_in const std::string& text )
            {
                const std::size_t maxSize = 512U;

                std::string result;

                for( std::size_t i = 0U; i < text.size() && i < maxSize; ++i )
                {
                    result += isPrintableAscii( text[ i ] ) ? text[ i ] : '?';
                }

                if( text.size() > maxSize )
                {
                    result += "...";
                }

                return result;
            }

            SAA_noreturn
            static void refuseDocument( SAA_in const std::exception& cause )
            {
                /*
                 * The data model's own message is kept because it names what failed - but made
                 * printable first, since one of its messages echoes an unrecognized property's
                 * name, which is the document's text and may carry CR or LF
                 */

                BL_THROW(
                    InvalidDataFormatException()
                        << eh::errinfo_is_user_friendly( true )
                        << eh::errinfo_nested_exception_ptr( std::current_exception() ),
                    BL_MSG()
                        << "Invalid browser profile: document is not a browser profile of the "
                        << "expected shape - "
                        << printableCopy( cause.what() )
                    );
            }

            /*
             * JSON to the data model, strictly: DataModelUtils::loadFromJsonText( ) keeps an
             * unrecognized property aside and carries on, which for untrusted input is the wrong
             * answer, so the context here is asked to refuse one - at every level, because each
             * nested object inherits the setting
             */

            static auto loadModel( SAA_in const std::string& jsonText ) -> om::ObjPtr< model_t >
            {
                if( jsonText.size() > MAX_DOCUMENT_SIZE )
                {
                    refuse(
                        "document",
                        "is larger than " +
                            std::to_string( static_cast< std::size_t >( MAX_DOCUMENT_SIZE ) ) +
                            " bytes"
                        );
                }

                try
                {
                    auto rootValue = json::readFromString( jsonText );

                    BL_CHK_T(
                        false,
                        rootValue.is_object(),
                        JsonException(),
                        "The JSON document must be an object at the top level"
                        );

                    dm::SerializationContextBase context( std::move( rootValue.as_object() ) );

                    context.detectUnknownProperties( true );

                    auto model = model_t::template createInstance<>();

                    model -> serializeProperties( context );

                    return model;
                }
                catch( JsonException& e )
                {
                    refuseDocument( e );
                }
                catch( UserMessageException& e )
                {
                    refuseDocument( e );
                }
            }

            /*************************************************************************************
             * The identity
             */

            /*
             * Lower-case letters, digits, '.', '_' and '-'. The id keys the registry and is carried
             * into every ConnectionKey as its TLS and HTTP/2 profile ids, where the session appends
             * "#h2-only" to mark a request which must not go over HTTP/1.1 and recognises the mark
             * by that suffix (ClientSession.h, isH2OnlyKey( )). An id with a '#' in it could
             * therefore pass for another profile's marked key; this rule has none
             */

            static bool isIdSafe( SAA_in const std::string& id ) NOEXCEPT
            {
                return isIdentifier( id, true /* isLowerCaseOnly */ );
            }

            static void convertIdentity(
                SAA_in          const model_t&                                  model,
                SAA_inout       BrowserProfile&                                 result
                )
            {
                if( ! isIdSafe( model.id() ) )
                {
                    refuse(
                        "id",
                        "is not 1 to 64 characters of a-z, 0-9, '.', '_' and '-', beginning with "
                        "a letter or a digit"
                        );
                }

                if( ! isIdentifier( model.family(), false /* isLowerCaseOnly */ ) )
                {
                    refuse(
                        "family",
                        "is not 1 to 64 characters of A-Z, a-z, 0-9, '.', '_' and '-', beginning "
                        "with a letter or a digit"
                        );
                }

                if( "Ja4Candidate" == model.grade() )
                {
                    result.grade = BrowserProfileGrade::Ja4Candidate;
                }
                else if( "Approximate" == model.grade() )
                {
                    result.grade = BrowserProfileGrade::Approximate;
                }
                else
                {
                    refuse( "grade", "is neither Ja4Candidate nor Approximate" );
                }

                chkBound( "deviations", model.deviations().size(), MAX_DEVIATIONS );

                for( std::size_t i = 0U; i < model.deviations().size(); ++i )
                {
                    const auto& deviation = model.deviations()[ i ];

                    if( deviation.empty() || ! isPrintableAscii( deviation ) )
                    {
                        refuse(
                            at( "deviations", i ),
                            "is empty or carries a character outside printable ASCII"
                            );
                    }
                }

                result.id = model.id();
                result.family = model.family();
                result.deviations = model.deviations();
            }

            /*************************************************************************************
             * The TLS shape
             */

            static void convertSuites(
                SAA_in          const std::string&                              path,
                SAA_in          const std::vector< std::string >&               names,
                SAA_in          const std::size_t                               bound,
                SAA_inout       std::vector< std::string >&                     result
                )
            {
                chkBound( path, names.size(), bound );

                for( std::size_t i = 0U; i < names.size(); ++i )
                {
                    const auto& name = names[ i ];

                    if( ! crypto::TlsNameRules::isNameSafe( name ) )
                    {
                        refuse( at( path, i ), "is not a plain suite name" );
                    }

                    if( crypto::TlsNameRules::isAnonymousCipherSuiteName( name ) )
                    {
                        refuse( at( path, i ), "names an anonymous cipher suite" );
                    }

                    if( crypto::TlsNameRules::isNullCipherSuiteName( name ) )
                    {
                        refuse( at( path, i ), "names a NULL cipher suite" );
                    }

                    for( std::size_t j = 0U; j < i; ++j )
                    {
                        if( equalsIgnoreCase( names[ j ], name ) )
                        {
                            refuse( at( path, i ), "repeats an earlier suite" );
                        }
                    }

                    BL_ASSERT( crypto::TlsNameRules::isCipherSuiteNameAllowed( name ) );
                }

                result = names;
            }

            static void convertTls(
                SAA_in          const dm::httpclient::TlsClientProfile&        model,
                SAA_inout       crypto::TlsClientProfile&                       result
                )
            {
                convertSuites(
                    "tls.cipherSuitesTls12",
                    model.cipherSuitesTls12(),
                    MAX_CIPHER_SUITES_TLS12,
                    result.cipherSuitesTls12
                    );

                convertSuites(
                    "tls.cipherSuitesTls13",
                    model.cipherSuitesTls13(),
                    MAX_CIPHER_SUITES_TLS13,
                    result.cipherSuitesTls13
                    );

                const auto& groups = model.groups();

                chkBound( "tls.groups", groups.size(), MAX_GROUPS );

                for( std::size_t i = 0U; i < groups.size(); ++i )
                {
                    const auto& name = groups[ i ] -> name();

                    if( ! crypto::TlsNameRules::isGroupNameAllowed( name ) )
                    {
                        refuse( at( "tls.groups", i ) + ".name", "is not a plain group name" );
                    }

                    for( std::size_t j = 0U; j < i; ++j )
                    {
                        if( equalsIgnoreCase( groups[ j ] -> name(), name ) )
                        {
                            refuse( at( "tls.groups", i ) + ".name", "repeats an earlier group" );
                        }
                    }

                    crypto::TlsGroup group;

                    group.name = name;
                    group.keyShare = groups[ i ] -> keyShare();

                    result.groups.push_back( std::move( group ) );
                }

                const auto& algorithms = model.signatureAlgorithms();

                chkBound( "tls.signatureAlgorithms", algorithms.size(), MAX_SIGNATURE_ALGORITHMS );

                for( std::size_t i = 0U; i < algorithms.size(); ++i )
                {
                    if( ! crypto::TlsNameRules::isSignatureAlgorithmNameAllowed( algorithms[ i ] ) )
                    {
                        refuse(
                            at( "tls.signatureAlgorithms", i ),
                            "is not a plain signature algorithm name"
                            );
                    }

                    for( std::size_t j = 0U; j < i; ++j )
                    {
                        if( equalsIgnoreCase( algorithms[ j ], algorithms[ i ] ) )
                        {
                            refuse(
                                at( "tls.signatureAlgorithms", i ),
                                "repeats an earlier signature algorithm"
                                );
                        }
                    }
                }

                result.signatureAlgorithms = algorithms;

                /*
                 * RFC 7301 lets an ALPN id be any 1 to 255 octets; every id IANA has registered is
                 * visible ASCII, and holding a profile to that keeps a control character out of the
                 * stream wrapper's own diagnostics, which quote the id
                 */

                const auto& protocols = model.alpnProtocols();

                chkBound( "tls.alpnProtocols", protocols.size(), MAX_ALPN_PROTOCOLS );

                for( std::size_t i = 0U; i < protocols.size(); ++i )
                {
                    const auto& protocol = protocols[ i ];

                    bool isVisibleAscii = ! protocol.empty() && protocol.size() <= MAX_ALPN_PROTOCOL_SIZE;

                    for( std::size_t k = 0U; isVisibleAscii && k < protocol.size(); ++k )
                    {
                        isVisibleAscii = protocol[ k ] > 0x20 && protocol[ k ] <= 0x7E;
                    }

                    if( ! isVisibleAscii )
                    {
                        refuse( at( "tls.alpnProtocols", i ), "is not 1 to 255 bytes of visible ASCII" );
                    }

                    for( std::size_t j = 0U; j < i; ++j )
                    {
                        if( protocols[ j ] == protocol )
                        {
                            refuse( at( "tls.alpnProtocols", i ), "repeats an earlier protocol" );
                        }
                    }
                }

                result.alpnProtocols = protocols;

                result.sessionTicket = model.sessionTicket();
                result.statusRequest = model.statusRequest();
                result.signedCertificateTimestamp = model.signedCertificateTimestamp();
                result.padding = model.padding();
            }

            /*************************************************************************************
             * The HTTP/2 shape
             */

            static void convertSettings(
                SAA_in          const dm::httpclient::Http2Profile&            model,
                SAA_inout       http2::Http2Profile&                            result
                )
            {
                const auto& settings = model.settings();

                chkBound( "http2.settings", settings.size(), MAX_SETTINGS );

                for( std::size_t i = 0U; i < settings.size(); ++i )
                {
                    const auto id = settings[ i ] -> id();
                    const auto value = settings[ i ] -> value();

                    if( id < 0 || static_cast< std::uint64_t >( id ) > MAX_SETTING_ID )
                    {
                        refuse( at( "http2.settings", i ) + ".id", "is outside 0 to 65535" );
                    }

                    if( value > MAX_SETTING_VALUE )
                    {
                        refuse( at( "http2.settings", i ) + ".value", "is outside 0 to 4294967295" );
                    }

                    const auto wireId = static_cast< std::uint64_t >( id );

                    if( SETTINGS_ENABLE_PUSH == wireId && 0U != value )
                    {
                        refuse(
                            at( "http2.settings", i ) + ".value",
                            "is not 0, and SETTINGS_ENABLE_PUSH must be: this client never "
                            "accepts a push"
                            );
                    }

                    if( SETTINGS_INITIAL_WINDOW_SIZE == wireId && value > MAX_WINDOW_SIZE )
                    {
                        refuse(
                            at( "http2.settings", i ) + ".value",
                            "is above 2147483647, the largest SETTINGS_INITIAL_WINDOW_SIZE "
                            "(RFC 9113 6.5.2)"
                            );
                    }

                    if(
                        SETTINGS_MAX_FRAME_SIZE == wireId &&
                        ( value < MIN_MAX_FRAME_SIZE || value > MAX_MAX_FRAME_SIZE )
                        )
                    {
                        refuse(
                            at( "http2.settings", i ) + ".value",
                            "is outside 16384 to 16777215, the range of SETTINGS_MAX_FRAME_SIZE "
                            "(RFC 9113 6.5.2)"
                            );
                    }

                    http2::Http2Setting setting;

                    setting.id = static_cast< std::uint16_t >( id );
                    setting.value = static_cast< std::uint32_t >( value );

                    result.settings.push_back( setting );
                }
            }

            static void convertPriorities(
                SAA_in          const dm::httpclient::Http2Profile&            model,
                SAA_inout       http2::Http2Profile&                            result
                )
            {
                const auto& frames = model.idleStreamPriorities();

                chkBound( "http2.idleStreamPriorities", frames.size(), MAX_IDLE_STREAM_PRIORITIES );

                for( std::size_t i = 0U; i < frames.size(); ++i )
                {
                    const auto& frame = *frames[ i ];
                    const auto path = at( "http2.idleStreamPriorities", i );

                    if( 0U == frame.streamId() || frame.streamId() > MAX_STREAM_ID )
                    {
                        refuse( path + ".streamId", "is outside 1 to 2147483647" );
                    }

                    if( frame.streamDependency() > MAX_STREAM_ID )
                    {
                        refuse( path + ".streamDependency", "is above 2147483647" );
                    }

                    if( frame.streamDependency() == frame.streamId() )
                    {
                        refuse( path + ".streamDependency", "is the stream itself" );
                    }

                    if( frame.weight() < 0 || static_cast< std::uint64_t >( frame.weight() ) > MAX_PRIORITY_WEIGHT )
                    {
                        refuse( path + ".weight", "is outside 0 to 255" );
                    }

                    for( std::size_t j = 0U; j < i; ++j )
                    {
                        if( frames[ j ] -> streamId() == frame.streamId() )
                        {
                            refuse( path + ".streamId", "repeats an earlier stream" );
                        }
                    }

                    http2::Http2PriorityFrame priority;

                    priority.streamId = static_cast< std::uint32_t >( frame.streamId() );
                    priority.streamDependency = static_cast< std::uint32_t >( frame.streamDependency() );
                    priority.weight = static_cast< std::uint8_t >( frame.weight() );
                    priority.exclusive = frame.exclusive();

                    result.idleStreamPriorities.push_back( priority );
                }

                /*
                 * The HEADERS priority is one fixed set of fields for every request, so the only
                 * dependency it can meaningfully name is none, stream 0, or one of the idle streams
                 * this profile sets up for exactly that purpose - a request's own stream id is not
                 * known when the profile is written, and a stream may not depend on itself
                 */

                const auto& headersPriority = model.headersPriority();

                if( ! headersPriority )
                {
                    return;
                }

                if( ! headersPriority -> isSet() )
                {
                    if(
                        0U != headersPriority -> streamDependency() ||
                        0 != headersPriority -> weight() ||
                        headersPriority -> exclusive()
                        )
                    {
                        refuse( "http2.headersPriority", "carries priority fields but isSet is false" );
                    }

                    return;
                }

                if(
                    headersPriority -> weight() < 0 ||
                    static_cast< std::uint64_t >( headersPriority -> weight() ) > MAX_PRIORITY_WEIGHT
                    )
                {
                    refuse( "http2.headersPriority.weight", "is outside 0 to 255" );
                }

                bool isDependencyKnown = 0U == headersPriority -> streamDependency();

                for( std::size_t i = 0U; ! isDependencyKnown && i < frames.size(); ++i )
                {
                    isDependencyKnown = frames[ i ] -> streamId() == headersPriority -> streamDependency();
                }

                if( ! isDependencyKnown )
                {
                    refuse(
                        "http2.headersPriority.streamDependency",
                        "is neither 0 nor the stream of one of idleStreamPriorities"
                        );
                }

                result.headersPriority.isSet = true;
                result.headersPriority.streamDependency =
                    static_cast< std::uint32_t >( headersPriority -> streamDependency() );
                result.headersPriority.weight = static_cast< std::uint8_t >( headersPriority -> weight() );
                result.headersPriority.exclusive = headersPriority -> exclusive();
            }

            static void convertPseudoHeaderOrder(
                SAA_in          const dm::httpclient::Http2Profile&            model,
                SAA_inout       http2::Http2Profile&                            result
                )
            {
                /*
                 * All four or none: none is the RFC 9113 8.3.1 order, and anything between would
                 * leave out a pseudo-header every request needs, or send one twice - a malformed
                 * request either way (8.3)
                 */

                const auto& order = model.pseudoHeaderOrder();

                for( std::size_t i = 0U; i < order.size(); ++i )
                {
                    auto pseudoHeader = http2::Http2PseudoHeader::Method;

                    if( "method" == order[ i ] )
                    {
                        pseudoHeader = http2::Http2PseudoHeader::Method;
                    }
                    else if( "authority" == order[ i ] )
                    {
                        pseudoHeader = http2::Http2PseudoHeader::Authority;
                    }
                    else if( "scheme" == order[ i ] )
                    {
                        pseudoHeader = http2::Http2PseudoHeader::Scheme;
                    }
                    else if( "path" == order[ i ] )
                    {
                        pseudoHeader = http2::Http2PseudoHeader::Path;
                    }
                    else
                    {
                        refuse(
                            at( "http2.pseudoHeaderOrder", i ),
                            "is none of method, authority, scheme and path"
                            );
                    }

                    for( std::size_t j = 0U; j < result.pseudoHeaderOrder.size(); ++j )
                    {
                        if( result.pseudoHeaderOrder[ j ] == pseudoHeader )
                        {
                            refuse( at( "http2.pseudoHeaderOrder", i ), "repeats an earlier pseudo-header" );
                        }
                    }

                    result.pseudoHeaderOrder.push_back( pseudoHeader );
                }

                if( ! result.pseudoHeaderOrder.empty() && 4U != result.pseudoHeaderOrder.size() )
                {
                    refuse(
                        "http2.pseudoHeaderOrder",
                        "names only some of the four pseudo-headers; it must name all of them "
                        "or none"
                        );
                }
            }

            static void convertHttp2(
                SAA_in          const dm::httpclient::Http2Profile&            model,
                SAA_inout       http2::Http2Profile&                            result
                )
            {
                convertSettings( model, result );

                /*
                 * The connection window starts at 65,535 and the increment is added to it at once,
                 * so the sum must stay within 2^31-1 (6.9.1); the session refuses more when it is
                 * constructed. The threshold is held by the receive windows as a signed 32-bit
                 * number, and a larger one reads as negative
                 */

                if( model.connectionWindowUpdateIncrement() > MAX_WINDOW_SIZE - INITIAL_WINDOW_SIZE )
                {
                    refuse(
                        "http2.connectionWindowUpdateIncrement",
                        "is above 2147418112, which would take the connection window past 2^31-1"
                        );
                }

                result.connectionWindowUpdateIncrement =
                    static_cast< std::uint32_t >( model.connectionWindowUpdateIncrement() );

                if( model.windowUpdateThreshold() > MAX_WINDOW_SIZE )
                {
                    refuse( "http2.windowUpdateThreshold", "is above 2147483647" );
                }

                result.windowUpdateThreshold = static_cast< std::uint32_t >( model.windowUpdateThreshold() );

                convertPriorities( model, result );

                convertPseudoHeaderOrder( model, result );

                if( model.hpackEncoderTableSize() > MAX_SETTING_VALUE )
                {
                    refuse( "http2.hpackEncoderTableSize", "is above 4294967295" );
                }

                result.hpackEncoderTableSize = static_cast< std::uint32_t >( model.hpackEncoderTableSize() );

                const auto& policy = model.hpackIndexingPolicy();

                if( policy.empty() || "Incremental" == policy )
                {
                    result.hpackIndexingPolicy = http2::HpackIndexingPolicy::Incremental;
                }
                else if( "WithoutIndexing" == policy )
                {
                    result.hpackIndexingPolicy = http2::HpackIndexingPolicy::WithoutIndexing;
                }
                else if( "NeverIndexed" == policy )
                {
                    result.hpackIndexingPolicy = http2::HpackIndexingPolicy::NeverIndexed;
                }
                else
                {
                    refuse(
                        "http2.hpackIndexingPolicy",
                        "is none of Incremental, WithoutIndexing and NeverIndexed"
                        );
                }

                result.cookieCrumbling = model.cookieCrumbling();
            }

            /*************************************************************************************
             * The header shape
             */

            /*
             * The headers a profile may not carry - see the class note
             */

            static bool isOwnedByTheSessionOrTransport( SAA_in const std::string& name ) NOEXCEPT
            {
                return
                    "cookie" == name ||
                    "authorization" == name ||
                    "proxy-authorization" == name ||
                    "content-length" == name ||
                    "transfer-encoding" == name ||
                    "keep-alive" == name ||
                    "proxy-connection" == name ||
                    "upgrade" == name;
            }

            /*
             * The value a version-string marker takes, or nullptr when the name is not one
             */

            static const std::string* versionStringFor(
                SAA_in          const std::string&                              name,
                SAA_in          const VersionStrings&                           versionStrings,
                SAA_out         const char**                                    source
                ) NOEXCEPT
            {
                if( "user-agent" == name )
                {
                    *source = "userAgent";
                    return &versionStrings.userAgent;
                }

                if( "sec-ch-ua" == name )
                {
                    *source = "secChUaBrands";
                    return &versionStrings.secChUa;
                }

                if( "sec-ch-ua-platform" == name )
                {
                    *source = "platform";
                    return &versionStrings.secChUaPlatform;
                }

                return nullptr;
            }

            static void convertDefaultHeader(
                SAA_in          const std::string&                              path,
                SAA_in          const dm::httpclient::ProfileHeader&           model,
                SAA_in          const VersionStrings&                           versionStrings,
                SAA_inout       ProfileHeader&                                  result
                )
            {
                const auto& name = model.name();

                if( ! isLowerCaseToken( name ) )
                {
                    refuse( path + ".name", "is not a lower-case token" );
                }

                /*
                 * The name is a token from here, so it is safe to name in a message
                 */

                if( ! isFieldValue( model.value() ) )
                {
                    refuse(
                        path + ".value",
                        "is not a valid field value: it carries a control character, or begins "
                        "or ends with whitespace"
                        );
                }

                if( isOwnedByTheSessionOrTransport( name ) )
                {
                    refuse( path, "is " + name + ", which the session or the transport owns" );
                }

                result.name = name;
                result.value = model.value();
                result.isComputed = model.isComputed();

                const char* source = nullptr;

                if( const auto* const composed = versionStringFor( name, versionStrings, &source ) )
                {
                    if( ! model.value().empty() || model.isComputed() )
                    {
                        refuse(
                            path,
                            "is " + name + ", whose value is composed from " + source +
                            ": it must carry no value and not be isComputed"
                            );
                    }

                    if( composed -> empty() )
                    {
                        refuse( path, "is " + name + " but " + source + " is empty" );
                    }

                    result.value = *composed;
                }
                else if( "host" == name )
                {
                    if( ! model.value().empty() || ! model.isComputed() )
                    {
                        refuse(
                            path,
                            "is host, which must be isComputed and carry no value: it is the "
                            "authority of each request's URL"
                            );
                    }
                }
                else if( "accept-encoding" == name )
                {
                    if( ! model.value().empty() )
                    {
                        refuse(
                            path,
                            "is accept-encoding, whose value the session computes from "
                            "headers.acceptEncoding: it must carry no value"
                            );
                    }
                }
                else if( "connection" == name )
                {
                    if(
                        ! equalsIgnoreCase( model.value(), "keep-alive" ) &&
                        ! equalsIgnoreCase( model.value(), "close" )
                        )
                    {
                        refuse( path + ".value", "is neither keep-alive nor close" );
                    }
                }
                else if( "te" == name )
                {
                    if( ! equalsIgnoreCase( model.value(), "trailers" ) )
                    {
                        refuse(
                            path + ".value",
                            "is not trailers, the only value te may carry (RFC 9113 8.2.2)"
                            );
                    }
                }
                else if( "sec-ch-ua-mobile" == name )
                {
                    if( "?0" != model.value() && "?1" != model.value() )
                    {
                        refuse( path + ".value", "is neither ?0 nor ?1" );
                    }
                }
            }

            static void convertKind(
                SAA_in          const std::string&                              path,
                SAA_in          const om::ObjPtr< dm::httpclient::HeaderProfileForKind >& model,
                SAA_in          const VersionStrings&                           versionStrings,
                SAA_inout       HeaderProfileForKind&                           result
                )
            {
                if( ! model )
                {
                    refuse( path, "is missing" );
                }

                const auto& headers = model -> defaultHeaders();

                chkBound( path + ".defaultHeaders", headers.size(), MAX_DEFAULT_HEADERS );

                for( std::size_t i = 0U; i < headers.size(); ++i )
                {
                    ProfileHeader header;

                    convertDefaultHeader(
                        at( path + ".defaultHeaders", i ),
                        *headers[ i ],
                        versionStrings,
                        header
                        );

                    for( std::size_t j = 0U; j < result.defaultHeaders.size(); ++j )
                    {
                        if( result.defaultHeaders[ j ].name == header.name )
                        {
                            refuse( at( path + ".defaultHeaders", i ) + ".name", "repeats an earlier header" );
                        }
                    }

                    result.defaultHeaders.push_back( std::move( header ) );
                }

                const auto& placement = model -> callerHeaderPlacement();

                if( placement.empty() || "Appended" == placement )
                {
                    result.callerHeaderPlacement = CallerHeaderPlacement::Appended;
                }
                else if( "Prepended" == placement )
                {
                    result.callerHeaderPlacement = CallerHeaderPlacement::Prepended;
                }
                else if( "BeforeAnchor" == placement )
                {
                    result.callerHeaderPlacement = CallerHeaderPlacement::BeforeAnchor;
                }
                else
                {
                    refuse(
                        path + ".callerHeaderPlacement",
                        "is none of Appended, Prepended and BeforeAnchor"
                        );
                }

                const auto& anchor = model -> callerHeaderAnchor();

                if( CallerHeaderPlacement::BeforeAnchor == result.callerHeaderPlacement.value() )
                {
                    bool isAnchorPresent = false;

                    for( std::size_t i = 0U; ! isAnchorPresent && i < result.defaultHeaders.size(); ++i )
                    {
                        isAnchorPresent = result.defaultHeaders[ i ].name == anchor;
                    }

                    if( ! isAnchorPresent )
                    {
                        refuse( path + ".callerHeaderAnchor", "names no header of defaultHeaders" );
                    }
                }
                else if( ! anchor.empty() )
                {
                    refuse( path + ".callerHeaderAnchor", "is set but callerHeaderPlacement is not BeforeAnchor" );
                }

                result.callerHeaderAnchor = anchor;

                /*
                 * The case map renders a name's CASE over HTTP/1.1 and nothing else: a value which
                 * spelled another name would rename a header on the wire, so each value must be its
                 * own key in another case
                 */

                const auto& caseMap = model -> http1CaseMap();

                chkBound( path + ".http1CaseMap", caseMap.size(), MAX_CASE_MAP_ENTRIES );

                for( auto it = caseMap.begin(); it != caseMap.end(); ++it )
                {
                    if( ! isLowerCaseToken( it -> first ) )
                    {
                        refuse( path + ".http1CaseMap", "has a key which is not a lower-case token" );
                    }

                    if(
                        ! http::HeaderList::isValidHeaderName( it -> second ) ||
                        ! equalsIgnoreCase( it -> first, it -> second )
                        )
                    {
                        refuse(
                            path + ".http1CaseMap." + it -> first,
                            "is not a token spelling the same name as its key"
                            );
                    }
                }

                result.http1CaseMap = caseMap;

                if( ! isFieldValue( model -> priorityHeaderValue() ) )
                {
                    refuse( path + ".priorityHeaderValue", "is not a valid field value" );
                }

                result.priorityHeaderValue = model -> priorityHeaderValue();
            }

            static void convertHeaders(
                SAA_in          const dm::httpclient::HeaderProfile&           model,
                SAA_in          const VersionStrings&                           versionStrings,
                SAA_inout       HeaderProfile&                                  result
                )
            {
                convertKind(
                    "headers.navigation",
                    model.navigation(),
                    versionStrings,
                    result.byRequestKind[ HttpRequestKind::Navigation ]
                    );

                convertKind(
                    "headers.fetch",
                    model.fetch(),
                    versionStrings,
                    result.byRequestKind[ HttpRequestKind::Fetch ]
                    );

                convertKind(
                    "headers.subresource",
                    model.subresource(),
                    versionStrings,
                    result.byRequestKind[ HttpRequestKind::Subresource ]
                    );

                const auto& codings = model.acceptEncoding();

                chkBound( "headers.acceptEncoding", codings.size(), MAX_ACCEPT_ENCODINGS );

                for( std::size_t i = 0U; i < codings.size(); ++i )
                {
                    if( ! http::HeaderList::isValidHeaderName( codings[ i ] ) )
                    {
                        refuse( at( "headers.acceptEncoding", i ), "is not a token" );
                    }

                    for( std::size_t j = 0U; j < i; ++j )
                    {
                        if( equalsIgnoreCase( codings[ j ], codings[ i ] ) )
                        {
                            refuse( at( "headers.acceptEncoding", i ), "repeats an earlier coding" );
                        }
                    }
                }

                result.acceptEncoding = codings;

                const auto& qValues = model.acceptLanguageQValues();

                chkBound( "headers.acceptLanguageQValues", qValues.size(), MAX_ACCEPT_LANGUAGE_QVALUES );

                for( std::size_t i = 0U; i < qValues.size(); ++i )
                {
                    if( ! isQValue( qValues[ i ] ) )
                    {
                        refuse( at( "headers.acceptLanguageQValues", i ), "is not a qvalue (RFC 9110 12.4.2)" );
                    }
                }

                result.acceptLanguageQValues = qValues;
            }

            /*************************************************************************************
             * The built-in profiles
             */

            /*
             * The built-in profiles' JSON documents, in no particular order. Their content is
             * derived from captures of the real browsers (design 6.7), which is L7-E's work; until
             * it lands there are none, and get( ) of every id throws
             */

            static auto builtInDocuments() -> std::vector< const char* >
            {
                return std::vector< const char* >();
            }

            static auto loadBuiltInProfiles() -> std::map< std::string, BrowserProfile >
            {
                std::map< std::string, BrowserProfile > result;

                const auto documents = builtInDocuments();

                for( std::size_t i = 0U; i < documents.size(); ++i )
                {
                    auto profile = load( documents[ i ] );

                    BL_CHK(
                        false,
                        result.find( profile.id ) == result.end(),
                        BL_MSG()
                            << "Two built-in browser profiles share an id"
                        );

                    auto id = profile.id;

                    result.emplace( std::move( id ), std::move( profile ) );
                }

                return result;
            }

            /*
             * Parsed at first use (6.2) and once: a function-local static is initialized exactly
             * once even when first used from several threads at the same time, and is never
             * written after
             */

            static auto builtInProfiles() -> const std::map< std::string, BrowserProfile >&
            {
                static const std::map< std::string, BrowserProfile > g_profiles = loadBuiltInProfiles();

                return g_profiles;
            }

        public:

            /**
             * @brief Loads and validates one browser profile from its JSON document
             *
             * @throw InvalidDataFormatException when the document breaks any rule of the class
             * note, naming the property by its path; the data model's own failure, when it is the
             * document's shape which is wrong, is nested in it
             */

            static auto load( SAA_in const std::string& jsonText ) -> BrowserProfile
            {
                const auto model = loadModel( jsonText );

                BrowserProfile result;

                convertIdentity( *model, result );

                if( ! model -> tls() )
                {
                    refuse( "tls", "is missing" );
                }

                convertTls( *model -> tls(), result.tls );

                if( ! model -> http2() )
                {
                    refuse( "http2", "is missing" );
                }

                convertHttp2( *model -> http2(), result.http2 );

                const auto versionStrings = composeVersionStrings( *model );

                if( ! model -> headers() )
                {
                    refuse( "headers", "is missing" );
                }

                convertHeaders( *model -> headers(), versionStrings, result.headers );

                return result;
            }

            /**
             * @brief A built-in browser profile, by id - design 5.8
             *
             * @throw NotFoundException when there is no built-in profile with that id
             */

            static auto get( SAA_in const std::string& id ) -> BrowserProfile
            {
                const auto& profiles = builtInProfiles();

                const auto pos = profiles.find( id );

                /*
                 * The id is the caller's and is not echoed, for the reason the class note gives
                 */

                BL_CHK_T(
                    true,
                    pos == profiles.end(),
                    NotFoundException()
                        << eh::errinfo_is_user_friendly( true ),
                    BL_MSG()
                        << "There is no built-in browser profile with the id given"
                    );

                return pos -> second;
            }
        };

        typedef BrowserProfilesT<> BrowserProfiles;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_BROWSERPROFILES_H_ */
