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

#ifndef __TEST_UTF_H_
#define __TEST_UTF_H_

#if defined(UTF_TEST_MODULE)
#define BOOST_TEST_MODULE                            UTF_TEST_MODULE
#endif // defined(UTF_TEST_MODULE)

#define BOOST_TEST_NO_MAIN

#include <baselib/core/detail/BoostIncludeGuardPush.h>
#if defined( UTF_TEST_MODULE )
/*
 * This will include the entire UTF library, so it doesn't need to be linked statically
 * against boost static library
 */
#include <boost/test/included/unit_test.hpp>
#else // defined( UTF_TEST_MODULE )
#include <boost/test/unit_test.hpp>
#endif // defined( UTF_TEST_MODULE )
#include <baselib/core/detail/BoostIncludeGuardPop.h>

#include <baselib/cmdline/EhUtils.h>

#include <baselib/core/TimeUtils.h>
#include <baselib/core/OS.h>
#include <baselib/core/Logging.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstddef>

/*
 * There were some internal macros changes made in Boost.Test framework in 1.59 which we
 * need to accommodate
 *
 * TODO: note that this file needs some major surgery and cleanup, but this will have to
 * wait for another day
 */

#if BOOST_VERSION < 105900
#define UTF_INTERNAL_CHECK_IMPL( P, assertion_descr, TL, CT ) \
    BOOST_CHECK_IMPL( P, assertion_descr, TL, CT )
#else
#define UTF_INTERNAL_CHECK_IMPL( P, assertion_descr, TL, CT ) \
    BOOST_TEST_TOOL_IMPL( 2 /* frwd_type */, P, assertion_descr, TL, CT, _ /* ARGS */ )
#endif


namespace test
{
    /**
     * @brief The number of outstanding baselib object references observed when the global
     * test fixture was torn down
     *
     * The leak check in ~DefaultUtfConfigT() cannot influence the outcome of the run by
     * itself - by the time it executes Boost.Test has already produced its report and
     * framework::test_in_progress() is false, so UTF_ERROR_MESSAGE would be discarded -
     * therefore the count is recorded here and main() turns a non-zero count into a
     * non-zero exit code after unit_test_main() has returned
     */

    inline std::atomic< long >& outstandingRefsAtExit() NOEXCEPT
    {
        static std::atomic< long > g_outstandingRefsAtExit( 0L );

        return g_outstandingRefsAtExit;
    }

    /**
     * @brief A snapshot of the cases which UTF_SKIP_UNLESS( ... ) has skipped
     *
     * Only declared here because main() below is defined ahead of test::UtfGlobalsT, which
     * owns the list; the definition follows that class
     */

    inline std::vector< std::string > skippedCasesSnapshot();

} // test

/*
 * We need to implement main, so we can force defaults for some command line parameters
 * like e.g. --catch_system_errors=no
 *
 * Apparently hacking it like this is the only possible way (concluded after extensive
 * research and debugging the code)
 */

#if defined( UTF_TEST_MODULE )

class PrintTestCaseVisitor : public boost::unit_test::test_tree_visitor
{
public:

    virtual void visit( boost::unit_test::test_case const& testCase ) OVERRIDE
    {
        std::cout << testCase.p_name << std::endl;
    }
};

int BOOST_TEST_CALL_DECL main(
    SAA_in          int                 argc,
    SAA_in          char*               argv[]
    )
{
    extern boost::unit_test::test_suite* init_unit_test_suite( int argc, char* argv[] );

    const int MAX_ARGS = 256;

    char* newArgv[ MAX_ARGS ];
    int newArgc = 0;

    if( ( argc + 1 ) >= MAX_ARGS )
    {
        std::cerr << "ERROR: Too many command line parameters" << std::endl;
        return -1;
    }

    bool catchSystemErrorsAdded = false;

    for( int i = 0; i < argc; ++i  )
    {
        if( std::strstr( argv[ i ], "--catch_system_errors" ) )
        {
            /*
             * Skip all arguments which have --catch_system_errors, so the user
             * can't override it on command line and also avoid duplicate parameters
             */

            continue;
        }

        if( std::strstr( argv[ i ], "--help" ) )
        {
            std::cout << "Available test cases:" << std::endl;

            PrintTestCaseVisitor visitor;

            boost::unit_test::traverse_test_tree( boost::unit_test::framework::master_test_suite(), visitor );
        }

        /*
         * Check for the separator for the user arguments to insert the --catch_system_errors=no
         * argument before that
         */

        if( 0 == std::strcmp( argv[ i ], "--" ) )
        {
            newArgv[ newArgc ] = ( char* ) "--catch_system_errors=no";
            ++newArgc;

            catchSystemErrorsAdded = true;
        }

        newArgv[ newArgc ] = argv[ i ];
        ++newArgc;
    }

    BL_ASSERT( ( newArgc + 1 ) < MAX_ARGS );

    if( ! catchSystemErrorsAdded )
    {
        newArgv[ newArgc ] = ( char* ) "--catch_system_errors=no";
        ++newArgc;
    }

    /*
     * Set the global UT flag before we execute the UT main function
     */

    bl::global::isInUnitTest( true /* isInUnitTest */ );
    bl::BoostAsioErrorCallback::installCallback( &bl::cmdline::EhUtils::asioErrorCallback );

    const int result = boost::unit_test::unit_test_main( &init_unit_test_suite, newArgc, newArgv );

    /*
     * Report the cases which did not run, so a skipped case is distinguishable from a passing
     * one without reading the source, and growth of the skipped set shows up as a diff
     */

    {
        const auto skippedCases = test::skippedCasesSnapshot();

        if( ! skippedCases.empty() )
        {
            std::cout
                << std::endl
                << "SKIPPED CASES ("
                << skippedCases.size()
                << "):"
                << std::endl;

            for( const auto& skippedCase : skippedCases )
            {
                std::cout << "    " << skippedCase << std::endl;
            }
        }
    }

    /*
     * The global fixture has been torn down by now, so the object leak check it recorded can
     * finally be turned into a failure of the run - even when the tests themselves passed
     */

    const long outstandingRefs = test::outstandingRefsAtExit().load();

    if( 0L != outstandingRefs )
    {
        std::cerr
            << "FATAL: "
            << outstandingRefs
            << " baselib object references leaked at exit"
            << std::endl;

        return boost::exit_test_failure;
    }

    return result;
}

#endif

namespace test
{
    template
    <
        typename T,
        typename U,
        bool arePrintable
    >
    class UtfPrintableToolImpl;

    template
    <
        typename T,
        typename U
    >
    class UtfPrintableToolImpl< T, U, false >
    {
    public:

        static void print( T&& /* lhs */, U&& /* rhs */ )
        {
        }
    };

    template
    <
        typename T,
        typename U
    >
    class UtfPrintableToolImpl< T, U, true >
    {
    public:

        static void print( T&& lhs, U&& rhs )
        {
            BL_LOG_MULTILINE(
                bl::Logging::debug(),
                BL_MSG()
                    << "lhs value: "
                    << lhs
                    << "\nrhs value: "
                    << rhs
                );
        }
    };

    template
    <
    >
    class UtfPrintableToolImpl< std::string, std::string, true >
    {
    public:

        static void print( std::string&& lhs, std::string&& rhs )
        {
            BL_LOG_MULTILINE(
                bl::Logging::debug(),
                BL_MSG()
                    << "lhs value[size="
                    << lhs.size()
                    << "]: "
                    << lhs
                    << "\nrhs value[size="
                    << rhs.size()
                    << "]: "
                    << rhs
                );
        }
    };

    template
    <
        typename T,
        typename U,
        bool isStringCase
    >
    class UtfEqualsToolImpl;

    template
    <
        typename T,
        typename U
    >
    class UtfEqualsToolImpl< T, U, false >
    {
    public:

        static bool equals( T&& lhs, U&& rhs )
        {
            const auto areEqual = ( lhs == rhs );

            typedef UtfPrintableToolImpl<
                T,
                U,
                (
                    /*
                     * C++11 enums generate ambiguity errors when serializes in std::ostream and
                     * thus they have to be excluded from the printable arithmetic types
                     */

                    std::is_arithmetic< typename std::decay< decltype( lhs ) >::type >::value &&
                    ! std::is_enum< typename std::decay< decltype( lhs ) >::type >::value &&
                    ! std::is_enum< typename std::decay< decltype( rhs ) >::type >::value
                ) ||
                std::is_same< bl::time::ptime, typename std::decay< decltype( lhs ) >::type >::value ||
                std::is_same< bl::time::date, typename std::decay< decltype( lhs ) >::type >::value ||
                std::is_same< bl::time::time_duration, typename std::decay< decltype( lhs ) >::type >::value ||
                std::is_same< std::string, typename std::decay< decltype( lhs ) >::type >::value
                > print_t;

            if( ! areEqual )
            {
                print_t::print( std::forward< T >( lhs ), std::forward< U >( rhs ) );
            }

            return areEqual;
        }
    };

    template
    <
        typename T,
        typename U
    >
    class UtfEqualsToolImpl< T, U, true >
    {
    public:

        static bool equals( T&& lhs, U&& rhs )
        {
            return UtfEqualsToolImpl< std::string, std::string, false >::equals( std::string( lhs ), std::string( rhs ) );
        }
    };

    template
    <
        typename E = void
    >
    class UtfGlobalsT
    {
    public:

        static bl::os::mutex g_lock;

        /*
         * The names and reasons of the cases which UTF_SKIP_UNLESS( ... ) has skipped, so the
         * set of cases which did not run becomes a visible, diffable artefact of every run
         * instead of being indistinguishable from the cases which passed
         */

        static std::vector< std::string > g_skippedCases;

        static void recordSkippedCase( SAA_in const std::string& description )
        {
            BL_MUTEX_GUARD( g_lock );

            g_skippedCases.push_back( description );
        }

        static std::vector< std::string > skippedCases()
        {
            BL_MUTEX_GUARD( g_lock );

            return g_skippedCases;
        }

        template
        <
            typename T,
            typename U
        >
        static bool equals( T&& lhs, U&& rhs )
        {
            typedef UtfEqualsToolImpl<
                T,
                U,
                std::is_convertible< decltype( lhs ), const char * >::value ||
                std::is_convertible< decltype( lhs ), char * >::value ||
                std::is_convertible< decltype( rhs ), const char * >::value ||
                std::is_convertible< decltype( rhs ), char * >::value
                > impl_t;

            return impl_t::equals( std::forward< T >( lhs ), std::forward< U >( rhs ) );
        }
    };

    typedef UtfGlobalsT<> UtfGlobals;

    template
    <
        typename E
    >
    bl::os::mutex
    UtfGlobalsT< E >::g_lock;

    template
    <
        typename E
    >
    std::vector< std::string >
    UtfGlobalsT< E >::g_skippedCases;

    inline std::vector< std::string > skippedCasesSnapshot()
    {
        return UtfGlobals::skippedCases();
    }

    /**
     * @brief The predicates behind the UTF_*_THROW_ERROR_CODE*, and UTF_*_THROW_ERRNO macros
     *
     * These are shared by the CHECK and the REQUIRE flavour of each macro in the same way
     * UtfGlobalsT::equals() is shared by UTF_CHECK_EQUAL and UTF_REQUIRE_EQUAL
     */

    template
    <
        typename E = void
    >
    class UtfExceptionToolsT
    {
    public:

        template
        <
            typename EXCEPTION
        >
        static void logException( SAA_in const EXCEPTION& ex )
        {
            BL_LOG_MULTILINE(
                bl::Logging::debug(),
                BL_MSG()
                    << "Required exception what():\n"
                    << ex.what()
                    << "\n"
                    << bl::eh::diagnostic_information( ex )
                );
        }

        /**
         * @brief Note that 'no error code attached' is reported distinctly from 'a different
         * error code was attached' because the two have quite different causes
         */

        template
        <
            typename EXCEPTION
        >
        static bool matchErrorCode(
            SAA_in              const EXCEPTION&                                    ex,
            SAA_in              const bl::eh::error_code&                           expectedErrorCode
            )
        {
            logException( ex );

            const bl::eh::error_code* ec = ex.errorCode();

            if( ! ec )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::info(),
                    BL_MSG()
                        << "No error code is attached to the exception; the expected error code was:\n"
                        << expectedErrorCode
                    );

                return false;
            }

            if( *ec != expectedErrorCode )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::info(),
                    BL_MSG()
                        << "Different than expected error code:\nexpected: "
                        << expectedErrorCode
                        << "\nactual: "
                        << *ec
                    );

                return false;
            }

            return true;
        }

        template
        <
            typename EXCEPTION
        >
        static bool matchErrNo(
            SAA_in              const EXCEPTION&                                    ex,
            SAA_in              const int                                           expectedErrNo
            )
        {
            logException( ex );

            const int* errNo = ex.errNo();

            if( ! errNo )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::info(),
                    BL_MSG()
                        << "No errno is attached to the exception; the expected errno was:\n"
                        << expectedErrNo
                    );

                return false;
            }

            if( *errNo != expectedErrNo )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::info(),
                    BL_MSG()
                        << "Different than expected errno:\nexpected: "
                        << expectedErrNo
                        << "\nactual: "
                        << *errNo
                    );

                return false;
            }

            return true;
        }

        template
        <
            typename EXCEPTION
        >
        static bool matchMessage(
            SAA_in              const EXCEPTION&                                    ex,
            SAA_in              const std::string&                                  expectedMessage
            )
        {
            const auto found = bl::cpp::contains( std::string( ex.what() ), expectedMessage );

            if( ! found )
            {
                BL_LOG_MULTILINE(
                    bl::Logging::info(),
                    BL_MSG()
                        << "Different than expected message:\n"
                        << expectedMessage
                    );
            }

            return found;
        }
    };

    typedef UtfExceptionToolsT<> UtfExceptionTools;

} // test

#define UTF_IMPL_WRAPPER_BEGIN() \
    { \
        BL_MUTEX_GUARD( test::UtfGlobals::g_lock ); \

#define UTF_IMPL_WRAPPER_END() \
        ; \
    } \

#define UTF_IMPL_WRAPPER_EXPR_BEGIN( expr, evaluated ) \
    { \
        const bool evaluated = !!( expr ); \
        UTF_IMPL_WRAPPER_BEGIN() \

#define UTF_IMPL_WRAPPER_EXPR_END() \
        UTF_IMPL_WRAPPER_END() \
    } \

#define UTF_AUTO_TEST_CASE( testName )                          BOOST_AUTO_TEST_CASE( testName )
#define UTF_FIXTURE_TEST_CASE( testName, fixtureName )          BOOST_FIXTURE_TEST_CASE( testName, fixtureName )

#define UTF_CHECK( expr )                                       UTF_IMPL_WRAPPER_EXPR_BEGIN( expr, __evaluated ) UTF_INTERNAL_CHECK_IMPL( __evaluated, BOOST_TEST_STRINGIZE( ( expr ) ), CHECK, CHECK_PRED ) UTF_IMPL_WRAPPER_EXPR_END()
#define UTF_REQUIRE( expr )                                     UTF_IMPL_WRAPPER_EXPR_BEGIN( expr, __evaluated ) UTF_INTERNAL_CHECK_IMPL( __evaluated, BOOST_TEST_STRINGIZE( ( expr ) ), REQUIRE, CHECK_PRED ) UTF_IMPL_WRAPPER_EXPR_END()
#define UTF_FAIL( msg )                                         UTF_IMPL_WRAPPER_BEGIN() BOOST_FAIL( bl::resolveMessage( msg ) ) UTF_IMPL_WRAPPER_END()

/*
 * Skips the remainder of the current test case when a precondition it needs is not
 * available - typically a command line argument which only a manual run provides
 *
 * The early-return form is the only one compatible with the existing lifecycle: Boost.Test's
 * 'precondition' decorator needs its condition at test registration time, whereas these
 * conditions depend on UtfArgsParser state which the global fixture populates afterwards.
 *
 * Note that this must be used in place of a bare 'return', which reports the case as passed
 * and leaves no record at all that it did not run
 */

#define UTF_SKIP_UNLESS( cond, reason ) \
    { \
        if( ! ( cond ) ) \
        { \
            const std::string __skippedCase42 = \
                bl::resolveMessage( \
                    BL_MSG() \
                        << boost::unit_test::framework::current_test_case().p_name.get() \
                        << ": " \
                        << reason \
                    ); \
            \
            test::UtfGlobals::recordSkippedCase( __skippedCase42 ); \
            \
            UTF_MESSAGE( "SKIPPED: " + __skippedCase42 ); \
            \
            return; \
        } \
    } \

/*
 * There were some API changes made in Boost.Test framework in 1.59 which we need
 * to accommodate - e.g. BOOST_MESSAGE -> BOOST_TEST_MESSAGE
 */

#if BOOST_VERSION < 105900
#define UTF_MESSAGE( msg )                                      UTF_IMPL_WRAPPER_BEGIN() BOOST_MESSAGE( bl::resolveMessage( msg ) ) UTF_IMPL_WRAPPER_END()
#else
#define UTF_MESSAGE( msg )                                      UTF_IMPL_WRAPPER_BEGIN() BOOST_TEST_MESSAGE( bl::resolveMessage( msg ) ) UTF_IMPL_WRAPPER_END()
#endif
#define UTF_WARNING_MESSAGE( msg )                              UTF_IMPL_WRAPPER_BEGIN() BOOST_WARN_MESSAGE( false, bl::resolveMessage( msg ) ) UTF_IMPL_WRAPPER_END()
#define UTF_ERROR_MESSAGE( msg )                                UTF_IMPL_WRAPPER_BEGIN() BOOST_ERROR( bl::resolveMessage( msg ) ) UTF_IMPL_WRAPPER_END()

/*
 * Older versions of boost have a semicolon already and if we put another one
 * GCC complains because of the pedantic option
 */

#if BOOST_VERSION < 105900
#define UTF_GLOBAL_FIXTURE( fixtureName )                       BOOST_GLOBAL_FIXTURE( fixtureName )
#else
#define UTF_GLOBAL_FIXTURE( fixtureName )                       BOOST_GLOBAL_FIXTURE( fixtureName );
#endif

#define UTF_FIXTURE_TEST_SUITE( suiteName, fixtureName )        BOOST_FIXTURE_TEST_SUITE( suiteName, fixtureName )
#define UTF_AUTO_TEST_SUITE_END()                               BOOST_AUTO_TEST_SUITE_END()

#define UTF_CHECK_EQUAL( lhs, rhs )                             UTF_IMPL_WRAPPER_EXPR_BEGIN( test::UtfGlobals::equals( lhs, rhs ), __evaluated ) UTF_INTERNAL_CHECK_IMPL( __evaluated, BOOST_TEST_STRINGIZE( EQUAL( lhs, rhs ) ), CHECK, CHECK_PRED ) UTF_IMPL_WRAPPER_EXPR_END()
#define UTF_REQUIRE_EQUAL( lhs, rhs )                           UTF_IMPL_WRAPPER_EXPR_BEGIN( test::UtfGlobals::equals( lhs, rhs ), __evaluated ) UTF_INTERNAL_CHECK_IMPL( __evaluated, BOOST_TEST_STRINGIZE( EQUAL( lhs, rhs ) ), REQUIRE, CHECK_PRED ) UTF_IMPL_WRAPPER_EXPR_END()

#define UTF_CHECK_THROW( expr, exception ) \
    BOOST_CHECK_EXCEPTION( \
        expr, \
        exception,  \
        []( const exception& ex ) \
        { \
            BL_LOG_MULTILINE( \
                bl::Logging::debug(), \
                BL_MSG() \
                    << "Checked " #exception " what():\n" \
                    << ex.what() \
                ); \
            return true; \
        } \
        ); \

#define UTF_REQUIRE_THROW( expr, exception ) \
    BOOST_REQUIRE_EXCEPTION( \
        expr, \
        exception,  \
        []( const exception& ex ) \
        { \
            BL_LOG_MULTILINE( \
                bl::Logging::debug(), \
                BL_MSG() \
                    << "Required " #exception " what():\n" \
                    << ex.what() \
                ); \
            return true; \
        } \
        ); \

#define UTF_CHECK_THROW_MESSAGE( expr, exception, msg ) \
    { \
        std::string expectedMessageString( msg ); \
        \
        BOOST_CHECK_EXCEPTION( \
            expr, \
            exception,  \
            [ &expectedMessageString ]( const exception& ex ) \
            { \
                BL_LOG_MULTILINE( \
                    bl::Logging::debug(), \
                    BL_MSG() \
                        << "Required " #exception " what():\n" \
                        << ex.what() \
                    ); \
                \
                const auto found = bl::cpp::contains( std::string( ex.what() ), expectedMessageString ); \
                \
                if( ! found ) \
                { \
                    BL_LOG_MULTILINE( \
                        bl::Logging::info(), \
                        BL_MSG() \
                            << "Different than expected message:\n" \
                            << expectedMessageString \
                        ); \
                } \
                \
                return found; \
            } \
            ); \
    } \

#define UTF_REQUIRE_THROW_MESSAGE( expr, exception, msg ) \
    { \
        std::string expectedMessageString( msg ); \
        \
        BOOST_REQUIRE_EXCEPTION( \
            expr, \
            exception,  \
            [ &expectedMessageString ]( const exception& ex ) \
            { \
                BL_LOG_MULTILINE( \
                    bl::Logging::debug(), \
                    BL_MSG() \
                        << "Required " #exception " what():\n" \
                        << ex.what() \
                    ); \
                \
                const auto found = bl::cpp::contains( std::string( ex.what() ), expectedMessageString ); \
                \
                if( ! found ) \
                { \
                    BL_LOG_MULTILINE( \
                        bl::Logging::info(), \
                        BL_MSG() \
                            << "Different than expected message:\n" \
                            << expectedMessageString \
                        ); \
                } \
                \
                return found; \
            } \
            ); \
    } \

/*
 * Note that the exception macros below are deliberately *not* wrapped in
 * UTF_IMPL_WRAPPER_BEGIN() / UTF_IMPL_WRAPPER_END(): they hold their lock across the
 * evaluation of an arbitrary expression, and the predicates below log unconditionally
 * through the UTF line logger, which takes UtfGlobals::g_lock itself - and bl::os::mutex
 * is not recursive, so wrapping them self-deadlocks the test binary
 */

#define UTF_CHECK_THROW_ERROR_CODE( expr, exception, expectedErrorCode ) \
    { \
        const bl::eh::error_code expectedErrorCodeValue( expectedErrorCode ); \
        \
        BOOST_CHECK_EXCEPTION( \
            expr, \
            exception, \
            [ &expectedErrorCodeValue ]( const exception& ex ) \
            { \
                return test::UtfExceptionTools::matchErrorCode( ex, expectedErrorCodeValue ); \
            } \
            ); \
    } \

#define UTF_REQUIRE_THROW_ERROR_CODE( expr, exception, expectedErrorCode ) \
    { \
        const bl::eh::error_code expectedErrorCodeValue( expectedErrorCode ); \
        \
        BOOST_REQUIRE_EXCEPTION( \
            expr, \
            exception, \
            [ &expectedErrorCodeValue ]( const exception& ex ) \
            { \
                return test::UtfExceptionTools::matchErrorCode( ex, expectedErrorCodeValue ); \
            } \
            ); \
    } \

/*
 * Note that the two predicates below capture with [ & ] rather than naming both locals: a
 * lambda capture list is bracketed, not parenthesized, so a comma inside it would be seen
 * by the preprocessor as an extra macro argument
 */

#define UTF_CHECK_THROW_ERROR_CODE_AND_MESSAGE( expr, exception, expectedErrorCode, msg ) \
    { \
        const bl::eh::error_code expectedErrorCodeValue( expectedErrorCode ); \
        const std::string expectedMessageString( msg ); \
        \
        BOOST_CHECK_EXCEPTION( \
            expr, \
            exception, \
            [ & ]( const exception& ex ) \
            { \
                return test::UtfExceptionTools::matchErrorCode( ex, expectedErrorCodeValue ) && \
                    test::UtfExceptionTools::matchMessage( ex, expectedMessageString ); \
            } \
            ); \
    } \

#define UTF_REQUIRE_THROW_ERROR_CODE_AND_MESSAGE( expr, exception, expectedErrorCode, msg ) \
    { \
        const bl::eh::error_code expectedErrorCodeValue( expectedErrorCode ); \
        const std::string expectedMessageString( msg ); \
        \
        BOOST_REQUIRE_EXCEPTION( \
            expr, \
            exception, \
            [ & ]( const exception& ex ) \
            { \
                return test::UtfExceptionTools::matchErrorCode( ex, expectedErrorCodeValue ) && \
                    test::UtfExceptionTools::matchMessage( ex, expectedMessageString ); \
            } \
            ); \
    } \

#define UTF_CHECK_THROW_ERRNO( expr, exception, expectedErrNo ) \
    { \
        const int expectedErrNoValue( expectedErrNo ); \
        \
        BOOST_CHECK_EXCEPTION( \
            expr, \
            exception, \
            [ &expectedErrNoValue ]( const exception& ex ) \
            { \
                return test::UtfExceptionTools::matchErrNo( ex, expectedErrNoValue ); \
            } \
            ); \
    } \

#define UTF_REQUIRE_THROW_ERRNO( expr, exception, expectedErrNo ) \
    { \
        const int expectedErrNoValue( expectedErrNo ); \
        \
        BOOST_REQUIRE_EXCEPTION( \
            expr, \
            exception, \
            [ &expectedErrNoValue ]( const exception& ex ) \
            { \
                return test::UtfExceptionTools::matchErrNo( ex, expectedErrNoValue ); \
            } \
            ); \
    } \

#define UTF_CHECK_EXCEPTION( expr, exception, predicate )       BOOST_CHECK_EXCEPTION( expr, exception, predicate )
#define UTF_REQUIRE_EXCEPTION( expr, exception, predicate )     BOOST_REQUIRE_EXCEPTION( expr, exception, predicate )
#define UTF_CHECK_NO_THROW( expr )                              BOOST_CHECK_NO_THROW( expr )
#define UTF_REQUIRE_NO_THROW( expr )                            BOOST_REQUIRE_NO_THROW( expr )

#define UTF_CHECK_EQUAL_COLLECTIONS( lhs_begin, lhs_end, rhs_begin, rhs_end ) \
    BOOST_CHECK_EQUAL_COLLECTIONS( lhs_begin, lhs_end, rhs_begin, rhs_end )

#endif /* __TEST_UTF_H_ */
