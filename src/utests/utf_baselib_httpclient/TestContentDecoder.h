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

#include <baselib/httpclient/ContentDecoder.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/BaseIncludes.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <utests/baselib/Utf.h>

/*
 * Slice S2.9 - the content-decoder seam (design 5.6, decision D9)
 *
 * THE DECODERS BELOW ARE TEST-ONLY AND DELIBERATELY NOT DECOMPRESSORS. D9 ships the seam and no
 * decompressor, so what the cases prove is that the seam WORKS - registration, lookup, streaming
 * across chunk boundaries, the flush at the end, and both bomb caps - using transforms which are a
 * few lines each and have no dependency:
 *
 *   x-echo      copies its input through, which is what proves registration and streaming
 *   x-hold      emits nothing until finish( ), which is what proves the flush happens at all
 *   x-expand    emits N copies of its input, which is what makes both caps reachable
 *
 * The first case asserts that a default registry is EMPTY and that no real coding is registered,
 * so D9 is a passing assertion in this module rather than a claim in a document
 */

namespace utest
{
    namespace contentdecoder
    {
        inline bl::om::ObjPtr< bl::data::DataBlock > blockOf( const std::string& text )
        {
            auto block = bl::data::DataBlock::get(
                nullptr     /* dataBlocksPool */,
                text.size() + 1U
                );

            if( ! text.empty() )
            {
                std::memcpy( block -> begin(), text.c_str(), text.size() );
            }

            block -> setSize( text.size() );

            return block;
        }

        inline std::string textOf( const bl::om::ObjPtr< bl::data::DataBlock >& block )
        {
            return std::string(
                block -> begin() + block -> offset1(),
                block -> size() - block -> offset1()
                );
        }

        /**
         * @brief The identity transform - input straight through, one output block per input block
         */

        template
        <
            typename E = void
        >
        class EchoDecoderT : public bl::httpclient::ContentDecoder
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( EchoDecoderT, bl::httpclient::ContentDecoder )

        protected:

            std::string                                                         m_coding;

            EchoDecoderT()
                :
                m_coding( "x-echo" )
            {
            }

        public:

            virtual auto contentCoding() const NOEXCEPT -> const std::string& OVERRIDE
            {
                return m_coding;
            }

            virtual void write(
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    input,
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                ) OVERRIDE
            {
                output( input );
            }

            virtual void finish(
                SAA_in          const bl::httpclient::decoder_output_callback_t& /* output */
                ) OVERRIDE
            {
            }
        };

        typedef bl::om::ObjectImpl< EchoDecoderT<> > EchoDecoder;

        /**
         * @brief Emits nothing until finish( ), which is what makes the flush observable
         */

        template
        <
            typename E = void
        >
        class HoldingDecoderT : public bl::httpclient::ContentDecoder
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( HoldingDecoderT, bl::httpclient::ContentDecoder )

        protected:

            std::string                                                         m_coding;
            std::string                                                         m_held;

            HoldingDecoderT()
                :
                m_coding( "x-hold" )
            {
            }

        public:

            virtual auto contentCoding() const NOEXCEPT -> const std::string& OVERRIDE
            {
                return m_coding;
            }

            virtual void write(
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    input,
                SAA_in          const bl::httpclient::decoder_output_callback_t& /* output */
                ) OVERRIDE
            {
                m_held += textOf( input );
            }

            virtual void finish(
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                ) OVERRIDE
            {
                output( blockOf( m_held ) );

                m_held.clear();
            }
        };

        typedef bl::om::ObjectImpl< HoldingDecoderT<> > HoldingDecoder;

        /**
         * @brief Emits its input 'factor' times, so one small input produces a large output - the
         * shape of a decompression bomb, with none of the machinery
         */

        template
        <
            typename E = void
        >
        class ExpandingDecoderT : public bl::httpclient::ContentDecoder
        {
            BL_DECLARE_OBJECT_IMPL_ONEIFACE( ExpandingDecoderT, bl::httpclient::ContentDecoder )

        protected:

            std::string                                                         m_coding;
            bl::cpp::ScalarTypeIniter< std::size_t >                            m_factor;

            ExpandingDecoderT( const std::size_t factor )
                :
                m_coding( "x-expand" )
            {
                m_factor = factor;
            }

        public:

            virtual auto contentCoding() const NOEXCEPT -> const std::string& OVERRIDE
            {
                return m_coding;
            }

            virtual void write(
                SAA_in          const bl::om::ObjPtr< bl::data::DataBlock >&    input,
                SAA_in          const bl::httpclient::decoder_output_callback_t& output
                ) OVERRIDE
            {
                for( std::size_t index = 0U; index < m_factor; ++index )
                {
                    output( input );
                }
            }

            virtual void finish(
                SAA_in          const bl::httpclient::decoder_output_callback_t& /* output */
                ) OVERRIDE
            {
            }
        };

        typedef bl::om::ObjectImpl< ExpandingDecoderT<> > ExpandingDecoder;
    }

} // utest

UTF_AUTO_TEST_CASE( ContentDecoder_NoDecompressorShipsTests )
{
    using namespace bl::httpclient;

    /*
     * DECISION D9, AS A PASSING ASSERTION RATHER THAN A CLAIM IN A DOCUMENT. The seam ships and no
     * decompressor does: this library has no compression dependency at all, and choosing how to
     * supply gzip, Brotli and Zstandard is a dependency decision recorded in
     * notes/plans/issues/http-content-decoders-deferral.md
     *
     * If a decoder is ever added, this case fails - which is the point. It has to be edited
     * deliberately, next to the deferral it settles
     */

    const ContentDecoderRegistry registry;

    UTF_CHECK( registry.empty() );
    UTF_CHECK_EQUAL( registry.size(), 0U );
    UTF_CHECK( registry.registeredCodings().empty() );

    const char* const realCodings[] = { "gzip", "deflate", "br", "zstd", "compress", "x-gzip" };

    for( std::size_t index = 0U; index < ( sizeof( realCodings ) / sizeof( realCodings[ 0 ] ) ); ++index )
    {
        const std::string context( realCodings[ index ] );

        UTF_CHECK_EQUAL(
            context + ( registry.hasDecoder( realCodings[ index ] ) ? " ships" : " does not ship" ),
            context + " does not ship"
            );
    }

    /*
     * ... and a body in a coding nothing is registered for is a REFUSAL, not a pass-through.
     * Handing a caller bytes still in a coding it never advertised, labelled as the body, is worse
     * than failing
     */

    UTF_CHECK_THROW(
        registry.createStream(
            "gzip",
            decoder_output_callback_t(
                []( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& ) -> void {}
                )
            ),
        bl::NotSupportedException
        );
}

UTF_AUTO_TEST_CASE( ContentDecoder_RegistrationAndLookupTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::contentdecoder;

    ContentDecoderRegistry registry;

    const auto echoFactory = ContentDecoderRegistry::factory_t(
        []() -> om::ObjPtr< ContentDecoder >
        {
            return om::qi< ContentDecoder >( EchoDecoder::createInstance() );
        }
        );

    registry.registerDecoder( "x-echo", ContentDecoderRegistry::factory_t( echoFactory ) );

    UTF_CHECK( ! registry.empty() );
    UTF_CHECK_EQUAL( registry.size(), 1U );
    UTF_CHECK( registry.hasDecoder( "x-echo" ) );

    /*
     * Content-coding tokens are case insensitive (RFC 9110 section 8.4.1), so a server which
     * spelled the header differently still finds the decoder
     */

    UTF_CHECK( registry.hasDecoder( "X-Echo" ) );
    UTF_CHECK( registry.hasDecoder( "X-ECHO" ) );
    UTF_CHECK_EQUAL( ContentDecoderRegistry::normalizeCoding( "GZip" ), std::string( "gzip" ) );

    UTF_CHECK( ! registry.hasDecoder( "x-echo2" ) );

    /*
     * Registration order is what registeredCodings() reports, because the session builds its
     * accept-encoding from it and that header must be stable rather than alphabetical by accident
     */

    registry.registerDecoder(
        "x-hold",
        ContentDecoderRegistry::factory_t(
            []() -> om::ObjPtr< ContentDecoder >
            {
                return om::qi< ContentDecoder >( HoldingDecoder::createInstance() );
            }
            )
        );

    UTF_CHECK_EQUAL( registry.registeredCodings().size(), 2U );
    UTF_CHECK_EQUAL( registry.registeredCodings()[ 0 ], std::string( "x-echo" ) );
    UTF_CHECK_EQUAL( registry.registeredCodings()[ 1 ], std::string( "x-hold" ) );

    /*
     * Re-registering replaces and does not duplicate the token in that order
     */

    registry.registerDecoder( "X-ECHO", ContentDecoderRegistry::factory_t( echoFactory ) );

    UTF_CHECK_EQUAL( registry.size(), 2U );
    UTF_CHECK_EQUAL( registry.registeredCodings().size(), 2U );
    UTF_CHECK_EQUAL( registry.registeredCodings()[ 0 ], std::string( "x-echo" ) );

    /*
     * 'identity' is the ABSENCE of a coding (RFC 9110 section 8.4.1), so a transform for it would
     * transform a body that was never coded
     */

    UTF_CHECK_THROW(
        registry.registerDecoder( "identity", ContentDecoderRegistry::factory_t( echoFactory ) ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        registry.registerDecoder( "IDENTITY", ContentDecoderRegistry::factory_t( echoFactory ) ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        registry.registerDecoder( "", ContentDecoderRegistry::factory_t( echoFactory ) ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        registry.registerDecoder( "x-nothing", ContentDecoderRegistry::factory_t() ),
        ArgumentException
        );

    UTF_CHECK_EQUAL( registry.size(), 2U );

    /*
     * A stream requires both a decoder and somewhere to put the output
     */

    UTF_CHECK_THROW(
        ContentDecoderStream(
            nullptr,
            DecoderLimits(),
            decoder_output_callback_t(
                []( SAA_in const om::ObjPtr< data::DataBlock >& ) -> void {}
                )
            ),
        ArgumentException
        );

    UTF_CHECK_THROW(
        ContentDecoderStream(
            om::qi< ContentDecoder >( EchoDecoder::createInstance() ),
            DecoderLimits(),
            decoder_output_callback_t()
            ),
        ArgumentException
        );
}

UTF_AUTO_TEST_CASE( ContentDecoder_StreamingThroughTheSeamTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::contentdecoder;

    ContentDecoderRegistry registry;

    registry.registerDecoder(
        "x-echo",
        ContentDecoderRegistry::factory_t(
            []() -> om::ObjPtr< ContentDecoder >
            {
                return om::qi< ContentDecoder >( EchoDecoder::createInstance() );
            }
            )
        );

    std::string decoded;

    auto stream = registry.createStream(
        "X-Echo",
        decoder_output_callback_t(
            [ &decoded ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
            {
                decoded += textOf( output );
            }
            )
        );

    UTF_CHECK_EQUAL( stream.contentCoding(), std::string( "x-echo" ) );
    UTF_CHECK_EQUAL( stream.inputBytes(), 0U );
    UTF_CHECK_EQUAL( stream.outputBytes(), 0U );
    UTF_CHECK( ! stream.isFinished() );
    UTF_CHECK( ! stream.isTripped() );

    /*
     * A body arrives in whatever chunks the connection read it in, so the transform is driven
     * incrementally and the result must not depend on where the boundaries fell
     */

    const char* const chunks[] = { "the ", "quick ", "brown ", "fox" };

    for( std::size_t index = 0U; index < ( sizeof( chunks ) / sizeof( chunks[ 0 ] ) ); ++index )
    {
        stream.write( blockOf( chunks[ index ] ) );
    }

    UTF_CHECK_EQUAL( decoded, std::string( "the quick brown fox" ) );
    UTF_CHECK_EQUAL( stream.inputBytes(), 19U );
    UTF_CHECK_EQUAL( stream.outputBytes(), 19U );

    stream.finish();

    UTF_CHECK( stream.isFinished() );
    UTF_CHECK( ! stream.isTripped() );

    /*
     * A finished stream is finished; feeding one again is a defect in the caller rather than
     * something to absorb quietly
     */

    UTF_CHECK_THROW( stream.write( blockOf( "more" ) ), ArgumentException );
    UTF_CHECK_THROW( stream.finish(), ArgumentException );

    /*
     * A block which is partly consumed already - offset1 past zero - contributes only what remains,
     * which is the shape every consumer of a data block in this library works in
     */

    std::string partial;

    auto offsetStream = registry.createStream(
        "x-echo",
        decoder_output_callback_t(
            [ &partial ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
            {
                partial += textOf( output );
            }
            )
        );

    const auto block = blockOf( "abcdef" );
    block -> setOffset1( 4U );

    offsetStream.write( block );

    UTF_CHECK_EQUAL( partial, std::string( "ef" ) );
    UTF_CHECK_EQUAL( offsetStream.inputBytes(), 2U );
    UTF_CHECK_EQUAL( offsetStream.outputBytes(), 2U );

    /*
     * The flush at the end of the body is what a real decompressor needs - it may be holding a
     * partial window when the last byte arrives. The holding decoder emits everything only there,
     * so a seam which never called finish( ) would lose the whole body
     */

    registry.registerDecoder(
        "x-hold",
        ContentDecoderRegistry::factory_t(
            []() -> om::ObjPtr< ContentDecoder >
            {
                return om::qi< ContentDecoder >( HoldingDecoder::createInstance() );
            }
            )
        );

    std::string held;

    auto holdStream = registry.createStream(
        "x-hold",
        decoder_output_callback_t(
            [ &held ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
            {
                held += textOf( output );
            }
            )
        );

    holdStream.write( blockOf( "held " ) );
    holdStream.write( blockOf( "back" ) );

    UTF_CHECK( held.empty() );
    UTF_CHECK_EQUAL( holdStream.inputBytes(), 9U );
    UTF_CHECK_EQUAL( holdStream.outputBytes(), 0U );

    holdStream.finish();

    UTF_CHECK_EQUAL( held, std::string( "held back" ) );
    UTF_CHECK_EQUAL( holdStream.outputBytes(), 9U );
}

UTF_AUTO_TEST_CASE( ContentDecoder_BombCapsTripTests )
{
    using namespace bl;
    using namespace bl::httpclient;
    using namespace utest::contentdecoder;

    /*
     * The defaults are documented rather than accidental
     */

    const DecoderLimits defaults;

    UTF_CHECK_EQUAL(
        defaults.maxOutputBytes.value(),
        static_cast< std::uint64_t >( DecoderLimits::DEFAULT_MAX_OUTPUT_BYTES )
        );

    UTF_CHECK_EQUAL(
        defaults.maxExpansionRatio.value(),
        static_cast< std::uint32_t >( DecoderLimits::DEFAULT_MAX_EXPANSION_RATIO )
        );

    ContentDecoderRegistry registry;

    registry.registerDecoder(
        "x-expand",
        ContentDecoderRegistry::factory_t(
            []() -> om::ObjPtr< ContentDecoder >
            {
                return om::qi< ContentDecoder >( ExpandingDecoder::createInstance( 8U /* factor */ ) );
            }
            )
        );

    /*
     * THE ABSOLUTE CAP. A decoder is a decompression-bomb surface and a client which streams a
     * bomb into memory is denial-of-serviced by a response it asked for. The cap is on the SEAM
     * rather than in the decoder, so a decoder supplied later cannot forget it
     */

    {
        DecoderLimits limits;

        limits.maxOutputBytes = 40U;
        limits.ratioGraceBytes = 1000000U;      /* out of the way, so only the absolute cap acts */

        registry.limits( limits );

        std::string decoded;

        auto stream = registry.createStream(
            "x-expand",
            decoder_output_callback_t(
                [ &decoded ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
                {
                    decoded += textOf( output );
                }
                )
            );

        UTF_CHECK_EQUAL( stream.limits().maxOutputBytes.value(), 40U );

        /*
         * Ten bytes in, eighty out at a factor of eight - so it trips partway
         */

        UTF_CHECK_THROW( stream.write( blockOf( "0123456789" ) ), SecurityException );

        UTF_CHECK( stream.isTripped() );

        /*
         * The check is BEFORE delivery, so nothing over the cap ever reaches the consumer
         */

        UTF_CHECK( decoded.size() <= 40U );

        /*
         * ... and the stream stays tripped; a caller cannot carry on past a refusal
         */

        UTF_CHECK_THROW( stream.write( blockOf( "more" ) ), SecurityException );
        UTF_CHECK_THROW( stream.finish(), SecurityException );
    }

    /*
     * THE EXPANSION RATIO, which catches the bomb the absolute cap does not: a body which stays
     * under the size limit but is still wildly out of proportion to what was received
     */

    {
        DecoderLimits limits;

        limits.maxOutputBytes = 1000000U;       /* out of the way, so only the ratio acts */
        limits.ratioGraceBytes = 16U;
        limits.maxExpansionRatio = 4U;

        registry.limits( limits );

        std::string decoded;

        auto stream = registry.createStream(
            "x-expand",
            decoder_output_callback_t(
                [ &decoded ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
                {
                    decoded += textOf( output );
                }
                )
            );

        UTF_CHECK_THROW( stream.write( blockOf( "0123456789" ) ), SecurityException );
        UTF_CHECK( stream.isTripped() );
    }

    /*
     * THE GRACE WINDOW. Without it a small body legitimately trips - ten bytes expanding to eighty
     * is a ratio of eight, and eighty bytes is not a bomb by any measure. With the grace above the
     * output, the same transform passes
     */

    {
        DecoderLimits limits;

        limits.maxOutputBytes = 1000000U;
        limits.ratioGraceBytes = 1024U;
        limits.maxExpansionRatio = 4U;

        registry.limits( limits );

        std::string decoded;

        auto stream = registry.createStream(
            "x-expand",
            decoder_output_callback_t(
                [ &decoded ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
                {
                    decoded += textOf( output );
                }
                )
            );

        stream.write( blockOf( "0123456789" ) );
        stream.finish();

        UTF_CHECK( ! stream.isTripped() );
        UTF_CHECK_EQUAL( decoded.size(), 80U );
        UTF_CHECK_EQUAL( stream.outputBytes(), 80U );
        UTF_CHECK_EQUAL( stream.inputBytes(), 10U );
    }

    /*
     * ... and a transform which stays within both caps is untouched by either, which is the half
     * that proves the caps are not simply always on
     */

    {
        DecoderLimits limits;

        limits.maxOutputBytes = 1024U;
        limits.ratioGraceBytes = 8U;
        limits.maxExpansionRatio = 16U;

        registry.limits( limits );

        std::string decoded;

        auto stream = registry.createStream(
            "x-expand",
            decoder_output_callback_t(
                [ &decoded ]( SAA_in const om::ObjPtr< data::DataBlock >& output ) -> void
                {
                    decoded += textOf( output );
                }
                )
            );

        stream.write( blockOf( "0123456789" ) );
        stream.finish();

        UTF_CHECK( ! stream.isTripped() );
        UTF_CHECK( stream.isFinished() );
        UTF_CHECK_EQUAL( decoded.size(), 80U );
    }
}
