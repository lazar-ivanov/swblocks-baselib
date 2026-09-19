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

#ifndef __BL_HTTPCLIENT_CONTENTDECODER_H_
#define __BL_HTTPCLIENT_CONTENTDECODER_H_

#include <baselib/data/DataBlock.h>

#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

BL_IID_DECLARE( ContentDecoder, "32c3069b-c4aa-487f-a616-97a4cb92afc5" )

namespace bl
{
    namespace httpclient
    {
        /*
         * The content-decoding SEAM - notes/plans/http2-design.md 5.6 and 6.5, decision D9, and
         * notes/plans/issues/http-content-decoders-deferral.md
         *
         * THE SEAM SHIPS AND NO DECOMPRESSOR SHIPS. That is decision D9 and it is not an omission:
         * this library has no compression dependency at all - Boost is built without iostreams and
         * OpenSSL without a compressor - and choosing how to supply gzip, Brotli and Zstandard is a
         * dependency decision with four options, recorded in the deferral above. A default registry
         * is therefore EMPTY, and a test asserts it is.
         *
         * What that costs, and where it is visible: the accept-encoding a request sends is the
         * profile's list intersected with what is registered, so with nothing registered the header
         * is omitted and the fidelity report names the deviation (design 6.5). The intersection
         * itself belongs to the session (S6.1), which is why this file offers registeredCodings()
         * and not the intersection.
         *
         * THE CAPS ARE ON THE SEAM AND NOT IN THE DECODERS, which is the one structural decision
         * here worth arguing. A decoder is a decompression-bomb surface: a few hundred bytes of
         * input can produce gigabytes of output, and a client which streams that into memory is
         * denial-of-serviced by a response it asked for. Both caps are therefore enforced by
         * ContentDecoderStreamT, which sits between the decoder and its consumer and counts. A
         * decoder supplied later cannot forget them, cannot get them subtly wrong, and does not
         * have to be trusted with them - which matters most for a decoder the application supplies
         * rather than one this library wrote.
         */

        /**
         * @brief Where a decoder's output goes
         *
         * A callback rather than a return value because the transform is STREAMING: one input
         * block may produce none, one or many output blocks, and a body is never held twice
         */

        typedef cpp::function
        <
            void (
                SAA_in          const om::ObjPtr< data::DataBlock >&            output
                )
        >
        decoder_output_callback_t;

        /**
         * @brief A streaming transform for one content-coding
         *
         * An implementation decodes and nothing else: it does not count bytes, enforce a limit or
         * know what its output is for. See the file note on why the caps are not here
         *
         * The contract is that write( ... ) may call 'output' any number of times including none,
         * that finish( ... ) flushes whatever the decoder was holding, and that neither is called
         * again after finish( ... )
         */

        class ContentDecoder : public om::Object
        {
            BL_DECLARE_INTERFACE( ContentDecoder )

        public:

            /**
             * @brief The content-coding token this decoder implements, lower case
             */

            virtual const std::string& contentCoding() const NOEXCEPT = 0;

            /**
             * @brief Offers the bytes between input -> offset1() and input -> size()
             *
             * All of them are consumed; a decoder which cannot use them all yet buffers the
             * remainder itself. Backpressure on a response body is the stream window's job
             * (design 5.3) and does not belong in a transform
             */

            virtual void write(
                SAA_in          const om::ObjPtr< data::DataBlock >&            input,
                SAA_in          const decoder_output_callback_t&               output
                ) = 0;

            /**
             * @brief The input is complete; emit whatever remains
             *
             * @throw InvalidDataFormatException when the coded stream is truncated - a decoder
             * which silently accepts a truncated body hands its caller a truncated one
             */

            virtual void finish( SAA_in const decoder_output_callback_t& output ) = 0;
        };

        /**
         * @brief The two decompression-bomb caps
         */

        struct DecoderLimits
        {
            enum : std::uint64_t
            {
                /**
                 * 64 MB of decoded body. A response larger than this is not something this client
                 * decodes into memory by default
                 */

                DEFAULT_MAX_OUTPUT_BYTES            = 64ULL * 1024ULL * 1024ULL,

                /**
                 * The output below which the ratio is not applied. Without a grace window a small
                 * body legitimately trips: 10 bytes of input expanding to 500 is a ratio of 50 and
                 * is harmless, while 500 bytes of output is not a bomb by any measure
                 */

                DEFAULT_RATIO_GRACE_BYTES           = 1024ULL * 1024ULL,
            };

            enum : std::uint32_t
            {
                /**
                 * Output over input. DEFLATE's theoretical maximum is about 1032:1 and Brotli's is
                 * far higher, so a limit of 100 refuses a bomb while passing ordinary text, which
                 * compresses at well under 10:1
                 */

                DEFAULT_MAX_EXPANSION_RATIO         = 100U,
            };

            cpp::ScalarTypeIniter< std::uint64_t >                              maxOutputBytes;
            cpp::ScalarTypeIniter< std::uint64_t >                              ratioGraceBytes;
            cpp::ScalarTypeIniter< std::uint32_t >                              maxExpansionRatio;

            DecoderLimits() NOEXCEPT
            {
                maxOutputBytes = DEFAULT_MAX_OUTPUT_BYTES;
                ratioGraceBytes = DEFAULT_RATIO_GRACE_BYTES;
                maxExpansionRatio = DEFAULT_MAX_EXPANSION_RATIO;
            }
        };

        /**
         * @brief class ContentDecoderStreamT - one decoder, one response, and the caps between
         * them
         *
         * It wraps the decoder's output callback with its own, so every byte the decoder produces
         * is counted before the consumer sees it. A cap which trips throws SecurityException and
         * the stream stays tripped - the partial output already delivered is the consumer's to
         * discard, because a caller which kept it would be acting on a body the client refused to
         * finish reading
         *
         * Single threaded by contract: one response body is decoded by one request task
         */

        template
        <
            typename E = void
        >
        class ContentDecoderStreamT FINAL
        {
        public:

            typedef ContentDecoderStreamT< E >                                  this_type;

        private:

            om::ObjPtr< ContentDecoder >                                        m_decoder;
            DecoderLimits                                                       m_limits;
            decoder_output_callback_t                                           m_output;

            cpp::ScalarTypeIniter< std::uint64_t >                              m_inputBytes;
            cpp::ScalarTypeIniter< std::uint64_t >                              m_outputBytes;
            cpp::ScalarTypeIniter< bool >                                       m_isFinished;
            cpp::ScalarTypeIniter< bool >                                       m_isTripped;

        public:

            ContentDecoderStreamT(
                SAA_in          om::ObjPtr< ContentDecoder >&&                  decoder,
                SAA_in          const DecoderLimits&                            limits,
                SAA_in          decoder_output_callback_t&&                     output
                )
                :
                m_decoder( BL_PARAM_FWD( decoder ) ),
                m_limits( limits ),
                m_output( BL_PARAM_FWD( output ) )
            {
                BL_CHK_T(
                    true,
                    nullptr == m_decoder,
                    ArgumentException(),
                    BL_MSG()
                        << "A content decoder stream requires a decoder"
                    );

                BL_CHK_T(
                    false,
                    !! m_output,
                    ArgumentException(),
                    BL_MSG()
                        << "A content decoder stream requires an output callback"
                    );
            }

            const std::string& contentCoding() const NOEXCEPT
            {
                return m_decoder -> contentCoding();
            }

            std::uint64_t inputBytes() const NOEXCEPT
            {
                return m_inputBytes;
            }

            std::uint64_t outputBytes() const NOEXCEPT
            {
                return m_outputBytes;
            }

            bool isFinished() const NOEXCEPT
            {
                return m_isFinished;
            }

            /**
             * @brief Whether a cap has tripped; the stream produces nothing further once it has
             */

            bool isTripped() const NOEXCEPT
            {
                return m_isTripped;
            }

            const DecoderLimits& limits() const NOEXCEPT
            {
                return m_limits;
            }

            /**
             * @brief Feeds one block of the coded body
             *
             * @throw SecurityException when either cap trips
             */

            void write( SAA_in const om::ObjPtr< data::DataBlock >& input )
            {
                chkUsable();

                m_inputBytes = m_inputBytes + static_cast< std::uint64_t >(
                    input -> size() - input -> offset1()
                    );

                m_decoder -> write( input, wrappedOutput() );
            }

            /**
             * @brief The coded body is complete
             *
             * @throw SecurityException when either cap trips on the flush
             */

            void finish()
            {
                chkUsable();

                m_decoder -> finish( wrappedOutput() );

                m_isFinished = true;
            }

        private:

            void chkUsable() const
            {
                BL_CHK_T(
                    true,
                    m_isTripped.value(),
                    SecurityException(),
                    BL_MSG()
                        << "This content decoder stream has exceeded a decoding limit and cannot "
                        << "be used further"
                    );

                BL_CHK_T(
                    true,
                    m_isFinished.value(),
                    ArgumentException(),
                    BL_MSG()
                        << "This content decoder stream is already finished"
                    );
            }

            decoder_output_callback_t wrappedOutput()
            {
                return decoder_output_callback_t(
                    [ this ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
                    {
                        this -> onDecoded( output );
                    }
                    );
            }

            /**
             * @brief Counts and checks before the consumer sees the block
             *
             * The check is BEFORE delivery on purpose. Checking afterwards would hand the consumer
             * the block which broke the cap, which for the absolute cap is the one block that
             * matters
             */

            void onDecoded( SAA_in const om::ObjPtr< data::DataBlock >& output )
            {
                const auto produced = static_cast< std::uint64_t >(
                    output -> size() - output -> offset1()
                    );

                const auto total = m_outputBytes + produced;

                if( total > m_limits.maxOutputBytes )
                {
                    m_isTripped = true;

                    BL_THROW(
                        SecurityException(),
                        BL_MSG()
                            << "A decoded response body of "
                            << total
                            << " bytes exceeds the decoding output limit of "
                            << m_limits.maxOutputBytes.value()
                            << " bytes"
                        );
                }

                /*
                 * The ratio is only meaningful once there is enough output for it to mean
                 * something - see DEFAULT_RATIO_GRACE_BYTES
                 */

                if( total > m_limits.ratioGraceBytes && 0U != m_inputBytes )
                {
                    const auto ratio = total / m_inputBytes;

                    if( ratio > m_limits.maxExpansionRatio )
                    {
                        m_isTripped = true;

                        BL_THROW(
                            SecurityException(),
                            BL_MSG()
                                << "A decoded response body expanded "
                                << ratio
                                << " times, which exceeds the decoding expansion limit of "
                                << m_limits.maxExpansionRatio.value()
                            );
                    }
                }

                m_outputBytes = total;

                m_output( output );
            }
        };

        typedef ContentDecoderStreamT<> ContentDecoderStream;

        /**
         * @brief class ContentDecoderRegistryT - the decoders one session has, and their limits
         *
         * EMPTY BY DEFAULT (D9) - see the file note. A session which registers nothing sends no
         * accept-encoding and receives identity bodies, which is exactly what the existing client
         * does today.
         *
         * Content-coding tokens are case insensitive (RFC 9110 section 8.4.1), so keys are folded
         * to lower case - with an ASCII-only fold, for the reason the header of http::HeaderList
         * gives: str::to_lower_copy takes std::locale() and a lookup which decides whether a body
         * is decoded must not depend on a global a caller can change.
         *
         * CONCURRENCY. Populated when a session is constructed and read afterwards, so concurrent
         * createStream( ... ) calls from many request tasks are safe and registerDecoder( ... ) is
         * not to be called while any of them runs. There is deliberately no lock: it would be a
         * lock on the path of every response body for a race that does not exist in the one way
         * this object is used.
         */

        template
        <
            typename E = void
        >
        class ContentDecoderRegistryT FINAL
        {
        public:

            typedef ContentDecoderRegistryT< E >                                this_type;
            typedef ContentDecoderStreamT< E >                                  stream_type;

            /**
             * @brief Makes one decoder instance; a decoder is stateful, so a response gets its own
             */

            typedef cpp::function< om::ObjPtr< ContentDecoder > () >            factory_t;

        private:

            std::map< std::string, factory_t >                                  m_factories;

            /*
             * Registration order, which is the order registeredCodings() reports - so that the
             * accept-encoding the session builds from it is stable rather than alphabetical by
             * accident
             */

            std::vector< std::string >                                          m_order;

            DecoderLimits                                                       m_limits;

            static char toLowerAscii( SAA_in const char ch ) NOEXCEPT
            {
                return ( ch >= 'A' && ch <= 'Z' ) ? static_cast< char >( ch - 'A' + 'a' ) : ch;
            }

        public:

            /**
             * @brief The content-coding token, folded the way the registry keys it
             */

            static std::string normalizeCoding( SAA_in const std::string& coding )
            {
                std::string result( coding );

                for( std::size_t pos = 0U; pos < result.size(); ++pos )
                {
                    result[ pos ] = toLowerAscii( result[ pos ] );
                }

                return result;
            }

            const DecoderLimits& limits() const NOEXCEPT
            {
                return m_limits;
            }

            void limits( SAA_in const DecoderLimits& limits ) NOEXCEPT
            {
                m_limits = limits;
            }

            /**
             * @brief Registers the factory for one content-coding, replacing any previous one
             *
             * @throw ArgumentException for an empty token, an empty factory, or "identity" - which
             * RFC 9110 section 8.4.1 defines as the absence of a coding, so a transform for it
             * would be a transform of a body that was never coded
             */

            void registerDecoder(
                SAA_in          const std::string&                              coding,
                SAA_in          factory_t&&                                     factory
                )
            {
                const auto key = normalizeCoding( coding );

                BL_CHK_T(
                    true,
                    key.empty(),
                    ArgumentException(),
                    BL_MSG()
                        << "A content coding token must not be empty"
                    );

                BL_CHK_T(
                    true,
                    key == "identity",
                    ArgumentException(),
                    BL_MSG()
                        << "'identity' is the absence of a content coding and cannot have a decoder"
                    );

                BL_CHK_T(
                    false,
                    !! factory,
                    ArgumentException(),
                    BL_MSG()
                        << "A content decoder factory must not be empty"
                    );

                if( m_factories.find( key ) == m_factories.end() )
                {
                    m_order.push_back( key );
                }

                m_factories[ key ] = BL_PARAM_FWD( factory );
            }

            bool hasDecoder( SAA_in const std::string& coding ) const
            {
                return m_factories.find( normalizeCoding( coding ) ) != m_factories.end();
            }

            std::size_t size() const NOEXCEPT
            {
                return m_factories.size();
            }

            bool empty() const NOEXCEPT
            {
                return m_factories.empty();
            }

            /**
             * @brief The registered tokens, in registration order
             *
             * This is what the session intersects the profile's accept-encoding list with
             * (design 6.5). The intersection is the session's and not this object's, because what
             * a profile claims is profile data and this registry knows nothing about profiles
             */

            const std::vector< std::string >& registeredCodings() const NOEXCEPT
            {
                return m_order;
            }

            /**
             * @brief A decoding stream for one response body
             *
             * @throw NotSupportedException when nothing is registered for the coding - which is
             * what a server sending a content-encoding the client never advertised looks like, and
             * is a refusal rather than a pass-through: handing a caller bytes still in a coding it
             * did not ask for, labelled as the body, is worse than failing
             */

            stream_type createStream(
                SAA_in          const std::string&                              coding,
                SAA_in          decoder_output_callback_t&&                     output
                ) const
            {
                const auto pos = m_factories.find( normalizeCoding( coding ) );

                BL_CHK_T(
                    true,
                    pos == m_factories.end(),
                    NotSupportedException(),
                    BL_MSG()
                        << "No content decoder is registered for the response's content coding"
                    );

                return stream_type( pos -> second(), m_limits, BL_PARAM_FWD( output ) );
            }
        };

        typedef ContentDecoderRegistryT<> ContentDecoderRegistry;

    } // httpclient

} // bl

#endif /* __BL_HTTPCLIENT_CONTENTDECODER_H_ */
