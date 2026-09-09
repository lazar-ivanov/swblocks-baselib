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

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfBaseLibCommon.h>

#include <baselib/data/DataBlock.h>

#include <baselib/core/BaseIncludes.h>

#include <cstdint>
#include <string>

/************************************************************************
 * data::DataBlock read / write codec tests
 */

UTF_AUTO_TEST_CASE( BaseLib_DataBlockReadWriteCodecTests )
{
    using namespace bl;
    using namespace bl::data;

    const auto block = DataBlock::createInstance( 4096U );

    block -> reset();

    /*
     * A round trip through the self-describing wire format
     *
     * The strings are written with an std::int32_t length prefix and no terminating
     * NUL, so the exact framing below is part of the contract
     */

    block -> write( std::int32_t( -5 ) );
    block -> write( std::uint64_t( 1ULL << 40 ) );
    block -> write( std::string( "hello" ) );
    block -> write( "world" );
    block -> write( std::string() );

    UTF_REQUIRE_EQUAL( 4U + 8U + ( 4U + 5U ) + ( 4U + 5U ) + 4U, block -> size() );

    std::int32_t i32 = 0;
    block -> read( &i32 );
    UTF_REQUIRE_EQUAL( std::int32_t( -5 ), i32 );

    std::uint64_t u64 = 0U;
    block -> read( &u64 );
    UTF_REQUIRE_EQUAL( std::uint64_t( 1ULL << 40 ), u64 );

    std::string text;

    block -> read( &text );
    UTF_REQUIRE_EQUAL( std::string( "hello" ), text );

    block -> read( &text );
    UTF_REQUIRE_EQUAL( std::string( "world" ), text );

    block -> read( &text );
    UTF_REQUIRE_EQUAL( std::string(), text );

    /*
     * Every byte which was written has now been consumed and reading past the end
     * is an error rather than a silent zero
     */

    UTF_REQUIRE_EQUAL( block -> offset1(), block -> size() );

    UTF_REQUIRE_THROW( block -> read( &i32 ), BufferTooSmallException );

    /*
     * A negative length prefix must be rejected before it is widened into an
     * std::size_t - the guard which makes the availability check below it safe
     */

    block -> reset();

    block -> write( std::int32_t( -1 ) );
    block -> write( "AAAA", 4U );

    std::string untouched( "untouched" );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> read( &untouched ),
        BufferTooSmallException,
        "Invalid negative string length -1 in a data block"
        );

    /*
     * The count was consumed, so the block is left mid-stream, and the output
     * string was not modified
     */

    UTF_REQUIRE_EQUAL( 4U, block -> offset1() );
    UTF_REQUIRE_EQUAL( std::string( "untouched" ), untouched );

    /*
     * A well-formed but truncated payload is reported by the read availability check
     */

    block -> reset();

    block -> write( std::int32_t( 16 ) );
    block -> write( "AB", 2U );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> read( &text ),
        BufferTooSmallException,
        resolveMessage(
            BL_MSG()
                << "Attempt to read "
                << 16U
                << " bytes with read position "
                << 4U
                << " and write position "
                << 6U
            )
        );

    /*
     * write( ... ) does not grow the block - growing it is writeEnsureAvailable's job,
     * which BaseLib_DataEnsureAvailableTests covers - so an overflowing write fails and
     * leaves both the size and the capacity untouched
     */

    const auto small = DataBlock::createInstance( 8U );

    small -> reset();

    small -> write( "12345678", 8U );

    UTF_REQUIRE_THROW_MESSAGE(
        small -> write( "9", 1U ),
        BufferTooSmallException,
        resolveMessage(
            BL_MSG()
                << "Attempt to write "
                << 1U
                << " bytes with write position "
                << 8U
                << " and capacity "
                << 8U
            )
        );

    UTF_REQUIRE_EQUAL( 8U, small -> size() );
    UTF_REQUIRE_EQUAL( 8U, small -> capacity() );

    /*
     * The length prefix and the payload are two separate capacity checked writes, so the
     * whole 4 + size is checked once before either of them - a failed
     * write( const std::string& ) leaves the block exactly as it was rather than holding
     * a dangling length prefix of a text which was never written
     */

    small -> reset();

    UTF_REQUIRE_THROW( small -> write( std::string( 6U, 'x' ) ), BufferTooSmallException );

    UTF_REQUIRE_EQUAL( 0U, small -> size() );

    /*
     * The same for the const char* overload, and the largest text which still fits is
     * the positive control that the check is not off by the size of the prefix
     */

    UTF_REQUIRE_THROW( small -> write( "xxxxxx" ), BufferTooSmallException );

    UTF_REQUIRE_EQUAL( 0U, small -> size() );

    UTF_REQUIRE_NO_THROW( small -> write( std::string( 4U, 'x' ) ) );

    UTF_REQUIRE_EQUAL( 8U, small -> size() );
}
