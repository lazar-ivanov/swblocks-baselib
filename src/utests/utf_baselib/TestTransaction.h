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
#include <utests/baselib/LoggerUtils.h>

#include <baselib/core/Transaction.h>
#include <baselib/core/BaseIncludes.h>

UTF_AUTO_TEST_CASE( TestTransaction1 )
{
    int storage = 0;

    const auto doit1 = [ & ]()
    {
        storage += 2;
    };

    const auto rollback1 = [ & ]()
    {
        storage -= 2;
    };

    const auto doit2 = [ & ]()
    {
        storage *= 2;
    };

    const auto rollback2 = [ & ]()
    {
        storage /= 2;
    };

    const auto doit3 = [ & ]()
    {
        storage += 2;
    };

    bl::Transaction t;
    t.add( doit1, rollback1 );
    t.add( doit2, rollback2 );
    t.add( doit3 );
    t.run();

    UTF_CHECK_EQUAL( storage, 6 );
}

UTF_AUTO_TEST_CASE( TestTransaction2 )
{
    int storage = 0;

    const auto doit1 = [ & ]()
    {
        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "Exception"
            );
    };

    const auto rollback1 = [ & ]()
    {
        storage += 2;
    };

    const auto doit2 = [ & ]()
    {
        storage *= 2;
    };

    const auto rollback2 = [ & ]()
    {
        storage /= 2;
    };

    const auto doit3 = [ & ]()
    {
        storage += 2;
    };

    bl::Transaction t;
    t.add( doit1, rollback1 );
    t.add( doit2, rollback2 );
    t.add( doit3 );

    UTF_REQUIRE_THROW( t.run(), bl::UnexpectedException );
    UTF_CHECK_EQUAL( storage, 0 );
}

UTF_AUTO_TEST_CASE( TestTransaction3 )
{
    int storage = 0;

    const auto doit1 = [ & ]()
    {
        storage += 2;
    };

    const auto rollback1 = [ & ]()
    {
        storage -= 2;
    };

    const auto doit2 = [ & ]()
    {
        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "Exception"
            );
    };

    const auto rollback2 = [ & ]()
    {
        storage /= 2;
    };

    const auto doit3 = [ & ]()
    {
        storage += 2;
    };

    bl::Transaction t;
    t.add( doit1, rollback1 );
    t.add( doit2, rollback2 );
    t.add( doit3 );

    UTF_REQUIRE_THROW( t.run(), bl::UnexpectedException );
    UTF_CHECK_EQUAL( storage, 0 );
}

UTF_AUTO_TEST_CASE( TestTransaction4 )
{
    int storage = 0;

    const auto doit1 = [ & ]()
    {
        storage += 2;
    };

    const auto rollback1 = [ & ]()
    {
        storage -= 2;
    };

    const auto doit2 = [ & ]()
    {
        storage *= 2;
    };

    const auto rollback2 = [ & ]()
    {
        storage /= 2;
    };

    const auto doit3 = [ & ]()
    {
        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "Exception"
            );
    };

    bl::Transaction t;
    t.add( doit1, rollback1 );
    t.add( doit2, rollback2 );
    t.add( doit3 );

    UTF_REQUIRE_THROW( t.run(), bl::UnexpectedException )
    UTF_CHECK_EQUAL( storage, 0 );
}

UTF_AUTO_TEST_CASE( TestTransaction5 )
{
    int storage = 2;

    const auto doit1 = [ & ]()
    {
    };

    const auto rollback1 = [ & ]()
    {
        storage -= 2;
    };

    const auto doit2 = [ & ]()
    {
        storage *= 2;
    };

    const auto rollback2 = [ & ]()
    {
        storage /= 2;
    };

    const auto doit3 = [ & ]()
    {
        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "Exception"
            );
    };

    bl::Transaction t;
    t.add( doit1, rollback1 );
    t.add( doit2, rollback2 );
    t.add( doit3 );

    UTF_REQUIRE_THROW( t.run(), bl::UnexpectedException )
    UTF_CHECK_EQUAL( storage, 0 );
}

UTF_AUTO_TEST_CASE( TestTransactionRollbackFailure )
{
    /*
     * doRollbacks() wraps every rollback in utils::tryCatchLog( ... ), which reports the
     * swallowed failure at warning level - and UtfMain.h maps LL_WARNING onto BOOST_ERROR,
     * so without this the case would fail on its own expected diagnostics
     */

    bl::Logging::LineLoggerPusher pushLineLogger( &utest::warningToDebugLineLogger );

    std::vector< int > order;

    const auto doIt1 = [ &order ]()
    {
        order.push_back( 1 );
    };

    const auto rollbackIt1 = [ &order ]()
    {
        order.push_back( -1 );
    };

    const auto doIt2 = [ &order ]()
    {
        order.push_back( 2 );
    };

    const auto rollbackIt2 = [ &order ]()
    {
        order.push_back( -2 );

        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "rollback failed"
            );
    };

    const auto doIt3 = [ &order ]()
    {
        order.push_back( 3 );
    };

    const auto rollbackIt3 = [ &order ]()
    {
        order.push_back( -3 );
    };

    const auto doIt4 = []()
    {
        BL_THROW(
            bl::ArgumentException(),
            BL_MSG()
                << "boom"
            );
    };

    const auto rollbackIt4 = [ &order ]()
    {
        order.push_back( -4 );
    };

    bl::Transaction t;
    t.add( doIt1, rollbackIt1 );
    t.add( doIt2, rollbackIt2 );
    t.add( doIt3, rollbackIt3 );
    t.add( doIt4, rollbackIt4 );

    /*
     * The exception which escapes run() must be the original one raised by the failing
     * command and not the UnexpectedException raised by the failing rollback
     */

    UTF_REQUIRE_THROW_MESSAGE( t.run(), bl::ArgumentException, "boom" );

    /*
     * This single sequence proves all three guarantees at once - the throwing rollback did
     * not stop the chain ( -1 ran after -2 threw ), the rollbacks run in reverse order, and
     * the failing command's own rollback ( -4 ) was never invoked
     */

    const int expected[] = { 1, 2, 3, -3, -2, -1 };

    UTF_REQUIRE_EQUAL( BL_ARRAY_SIZE( expected ), order.size() );

    for( std::size_t i = 0U; i < BL_ARRAY_SIZE( expected ); ++i )
    {
        UTF_CHECK_EQUAL( expected[ i ], order[ i ] );
    }
}
