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

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/TestUtils.h>
#include "examples/objmodel/MyInterfaces.h"
#include "examples/objmodel/MyObjectImpl.h"

#include <baselib/core/GroupBy.h>
#include <baselib/core/SecureStringWrapper.h>
#include <baselib/core/Table.h>
#include <baselib/core/Tree.h>

#include <utests/baselib/TestFsUtils.h>

#include <thread>

UTF_AUTO_TEST_CASE( BaseLib_Default )
{
    const int result = 2 + 2;
    UTF_CHECK( 4 == result );
}

/************************************************************************
 * BaseDefs.h tests
 */

namespace
{
    enum class ParamForwardingReferenceType
    {
        Rvalue,
        Lvalue,
        ConstLvalue,
    };

    inline void paramForwardingConstIntTargetCall(
        SAA_in      const int&                                  x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "const int&"
            );

        UTF_REQUIRE_EQUAL( expectedReferenceType, ParamForwardingReferenceType::ConstLvalue );
        UTF_REQUIRE_EQUAL( x, 10 );
    }

    inline void paramForwardingConstIntTargetCall(
        SAA_inout   int&                                        x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "int&"
            );

        UTF_REQUIRE_EQUAL( expectedReferenceType, ParamForwardingReferenceType::Lvalue );
        UTF_REQUIRE_EQUAL( x, 10 );
    }

    inline void paramForwardingConstIntTargetCall(
        SAA_in      int&&                                       x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "int&&"
            );

        UTF_REQUIRE_EQUAL( expectedReferenceType, ParamForwardingReferenceType::Rvalue );
        UTF_REQUIRE_EQUAL( x, 10 );
    }

    template
    <
        typename T
    >
    inline void paramForwardingIntUniversalRefCall(
        SAA_in      T&&                                         x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        paramForwardingConstIntTargetCall( BL_PARAM_FWD( x ), expectedReferenceType );
    }

    inline void paramForwardingIntRvalueRefCall(
        SAA_in      int&&                                       x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        paramForwardingConstIntTargetCall( BL_PARAM_FWD( x ), expectedReferenceType );
    }

    inline void paramForwardingIntTests()
    {
        int                 i1 = 10;
        int&                i2 = i1;
        const int&          i3 = i1;
        int                 i4 = 10;

        paramForwardingIntUniversalRefCall( 10, ParamForwardingReferenceType::Rvalue );
        paramForwardingIntUniversalRefCall( i1, ParamForwardingReferenceType::Lvalue );
        paramForwardingIntUniversalRefCall( i2, ParamForwardingReferenceType::Lvalue );
        paramForwardingIntUniversalRefCall( i3, ParamForwardingReferenceType::ConstLvalue );
        paramForwardingIntUniversalRefCall( std::move( i4 ), ParamForwardingReferenceType::Rvalue );

        paramForwardingIntRvalueRefCall( 10, ParamForwardingReferenceType::Rvalue );
        paramForwardingIntRvalueRefCall( std::move( i1 ), ParamForwardingReferenceType::Rvalue );
    }

    class ParamForwardingTestClass
    {
        BL_NO_COPY( ParamForwardingTestClass )

    private:

        int m_value;

    public:

        ParamForwardingTestClass( SAA_in const int value )
            :
            m_value( value )
        {
        }

        ParamForwardingTestClass( SAA_in ParamForwardingTestClass&& other )
            :
            m_value( other.m_value )
        {
        }

        ParamForwardingTestClass& operator = ( SAA_in ParamForwardingTestClass&& other )
        {
            m_value = other.m_value;
            return *this;
        }

        int value() const NOEXCEPT
        {
            return m_value;
        }
    };

    inline void paramForwardingConstClassTargetCall(
        SAA_in      const ParamForwardingTestClass&             x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "const ParamForwardingTestClass&"
            );

        UTF_REQUIRE_EQUAL( expectedReferenceType, ParamForwardingReferenceType::ConstLvalue );
        UTF_REQUIRE_EQUAL( x.value(), 10 );
    }

    inline void paramForwardingConstClassTargetCall(
        SAA_inout   ParamForwardingTestClass&                   x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "ParamForwardingTestClass&"
            );

        UTF_REQUIRE_EQUAL( expectedReferenceType, ParamForwardingReferenceType::Lvalue );
        UTF_REQUIRE_EQUAL( x.value(), 10 );
    }

    inline void paramForwardingConstClassTargetCall(
        SAA_in      ParamForwardingTestClass&&                  x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "ParamForwardingTestClass&&"
            );

        UTF_REQUIRE_EQUAL( expectedReferenceType, ParamForwardingReferenceType::Rvalue );
        UTF_REQUIRE_EQUAL( x.value(), 10 );
    }

    template
    <
        typename T
    >
    inline void paramForwardingClassUniversalRefCall(
        SAA_in      T&&                                         x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        paramForwardingConstClassTargetCall( BL_PARAM_FWD( x ), expectedReferenceType );
    }

    inline void paramForwardingIntRvalueRefCall(
        SAA_in      ParamForwardingTestClass&&                  x,
        SAA_in      const ParamForwardingReferenceType          expectedReferenceType
        )
    {
        paramForwardingConstClassTargetCall( BL_PARAM_FWD( x ), expectedReferenceType );
    }

    inline ParamForwardingTestClass paramForwardingGetClass()
    {
        return ParamForwardingTestClass( 10 );
    }

    inline void paramForwardingClassTests()
    {
        typedef ParamForwardingTestClass class_t;

        class_t             c1( 10 );
        class_t&            c2 = c1;
        const class_t&      c3 = c1;
        class_t             c4( 10 );

        paramForwardingClassUniversalRefCall( paramForwardingGetClass(), ParamForwardingReferenceType::Rvalue );
        paramForwardingClassUniversalRefCall( c1, ParamForwardingReferenceType::Lvalue );
        paramForwardingClassUniversalRefCall( c2, ParamForwardingReferenceType::Lvalue );
        paramForwardingClassUniversalRefCall( c3, ParamForwardingReferenceType::ConstLvalue );
        paramForwardingClassUniversalRefCall( std::move( c4 ), ParamForwardingReferenceType::Rvalue );

        paramForwardingIntRvalueRefCall( std::move( c1 ), ParamForwardingReferenceType::Rvalue );
        paramForwardingIntRvalueRefCall( class_t( 10 ), ParamForwardingReferenceType::Rvalue );
        paramForwardingIntRvalueRefCall( paramForwardingGetClass(), ParamForwardingReferenceType::Rvalue );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_ParamFwding )
{
    paramForwardingIntTests();

    paramForwardingClassTests();
}

UTF_AUTO_TEST_CASE( BaseLib_BaseDefsTests )
{
    using bl::isPowerOfTwo;
    using bl::alignedOf;

    UTF_REQUIRE( ! isPowerOfTwo( 0 ) );
    UTF_REQUIRE( isPowerOfTwo( 1 ) );
    UTF_REQUIRE( isPowerOfTwo( 2 ) );
    UTF_REQUIRE( ! isPowerOfTwo( 3 ) );
    UTF_REQUIRE( isPowerOfTwo( 4 ) );
    UTF_REQUIRE( ! isPowerOfTwo( 5 ) );
    UTF_REQUIRE( ! isPowerOfTwo( 7 ) );
    UTF_REQUIRE( isPowerOfTwo( 8 ) );
    UTF_REQUIRE( ! isPowerOfTwo( 9 ) );
    UTF_REQUIRE( isPowerOfTwo( 16 ) );
    UTF_REQUIRE( isPowerOfTwo( 32 ) );
    UTF_REQUIRE( isPowerOfTwo( 256 ) );
    UTF_REQUIRE( isPowerOfTwo( 0x8000000000000000ULL ) );
    UTF_REQUIRE( ! isPowerOfTwo( 0x8000000000000001ULL ) );

    UTF_REQUIRE( ! isPowerOfTwo( -1 ) );
    UTF_REQUIRE( ! isPowerOfTwo( -2 ) );
    UTF_REQUIRE( ! isPowerOfTwo( -3 ) );
    UTF_REQUIRE( ! isPowerOfTwo( -4 ) );

    UTF_REQUIRE_EQUAL( 0, alignedOf( 0, 1 ) );
    UTF_REQUIRE_EQUAL( 0, alignedOf( 0, 2 ) );
    UTF_REQUIRE_EQUAL( 0, alignedOf( 0, 4 ) );
    UTF_REQUIRE_EQUAL( 0, alignedOf( 0, 8 ) );

    UTF_REQUIRE_EQUAL( 1, alignedOf( 1, 1 ) );
    UTF_REQUIRE_EQUAL( 2, alignedOf( 1, 2 ) );
    UTF_REQUIRE_EQUAL( 4, alignedOf( 1, 4 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 1, 8 ) );

    UTF_REQUIRE_EQUAL( 2, alignedOf( 2, 1 ) );
    UTF_REQUIRE_EQUAL( 2, alignedOf( 2, 2 ) );
    UTF_REQUIRE_EQUAL( 4, alignedOf( 2, 4 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 2, 8 ) );

    UTF_REQUIRE_EQUAL( 3, alignedOf( 3, 1 ) );
    UTF_REQUIRE_EQUAL( 4, alignedOf( 3, 2 ) );
    UTF_REQUIRE_EQUAL( 4, alignedOf( 3, 4 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 3, 8 ) );

    UTF_REQUIRE_EQUAL( 4, alignedOf( 4, 1 ) );
    UTF_REQUIRE_EQUAL( 4, alignedOf( 4, 2 ) );
    UTF_REQUIRE_EQUAL( 4, alignedOf( 4, 4 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 4, 8 ) );

    UTF_REQUIRE_EQUAL( 7, alignedOf( 7, 1 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 7, 2 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 7, 4 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 7, 8 ) );

    UTF_REQUIRE_EQUAL( 8, alignedOf( 8, 1 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 8, 2 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 8, 4 ) );
    UTF_REQUIRE_EQUAL( 8, alignedOf( 8, 8 ) );

    UTF_REQUIRE_EQUAL( 9, alignedOf( 9, 1 ) );
    UTF_REQUIRE_EQUAL( 10, alignedOf( 9, 2 ) );
    UTF_REQUIRE_EQUAL( 12, alignedOf( 9, 4 ) );
    UTF_REQUIRE_EQUAL( 16, alignedOf( 9, 8 ) );

    int* p0 = ( int* )0;
    int* p1 = ( int* )1;
    int* p2 = ( int* )2;
    int* p3 = ( int* )3;
    int* p4 = ( int* )4;
    int* p8 = ( int* )8;

    UTF_REQUIRE_EQUAL( p0, alignedOf( p0, 1 ) );
    UTF_REQUIRE_EQUAL( p0, alignedOf( p0, 2 ) );
    UTF_REQUIRE_EQUAL( p0, alignedOf( p0, 4 ) );
    UTF_REQUIRE_EQUAL( p0, alignedOf( p0, 8 ) );

    UTF_REQUIRE_EQUAL( p1, alignedOf( p1, 1 ) );
    UTF_REQUIRE_EQUAL( p2, alignedOf( p1, 2 ) );
    UTF_REQUIRE_EQUAL( p4, alignedOf( p1, 4 ) );
    UTF_REQUIRE_EQUAL( p8, alignedOf( p1, 8 ) );

    UTF_REQUIRE_EQUAL( p3, alignedOf( p3, 1 ) );
    UTF_REQUIRE_EQUAL( p4, alignedOf( p3, 2 ) );
    UTF_REQUIRE_EQUAL( p4, alignedOf( p3, 4 ) );
    UTF_REQUIRE_EQUAL( p8, alignedOf( p3, 8 ) );

    UTF_REQUIRE_EQUAL( p4, alignedOf( p4, 1 ) );
    UTF_REQUIRE_EQUAL( p4, alignedOf( p4, 2 ) );
    UTF_REQUIRE_EQUAL( p4, alignedOf( p4, 4 ) );
    UTF_REQUIRE_EQUAL( p8, alignedOf( p4, 8 ) );
}

/************************************************************************
 * ErrorHandling tests
 */

namespace
{
    inline bool getTrue()  { return true; }
    inline bool getFalse() { return false; }

    class DestructorTest
    {
        bl::cpp::ScalarTypeIniter< bool >           m_indestructible;

    public:

        ~DestructorTest()
        {
            if( m_indestructible )
            {
                std::puts( "Attempting to destroy an indestructible class" );
                std::terminate();
            }
        }

        /**
         * Report if static destructors are invoked during emergency shutdown
         */

        void makeIndestructible() NOEXCEPT
        {
            m_indestructible = true;
        }
    };

    static DestructorTest   g_destructorTest;
}

UTF_AUTO_TEST_CASE( BaseLib_EhGenerateAbort )
{
    if( test::UtfArgsParser::isClient() )
    {
        bl::os::GlobalProcessInit initer;

        g_destructorTest.makeIndestructible();

        BL_RIP_MSG( "Abort requested" );

        UTF_FAIL( "BL_RIP_MSG must terminate the process" );
    }

    UTF_CHECK( true );
}

UTF_AUTO_TEST_CASE( BaseLib_EhGenerateAssertionFailure )
{
    if( test::UtfArgsParser::isClient() )
    {
        bl::os::GlobalProcessInit initer;

        g_destructorTest.makeIndestructible();

        BL_ASSERT( true == false );

        UTF_FAIL( "BL_ASSERT must terminate the process" );
    }

    UTF_CHECK( true );
}

UTF_AUTO_TEST_CASE( BaseLib_EhGenerateCrash )
{
    if( test::UtfArgsParser::isClient() )
    {
        bl::os::GlobalProcessInit initer;

        g_destructorTest.makeIndestructible();

        UTF_MESSAGE( "Generating an Access violation" );

        int* i = nullptr;
        *i = 42;

        UTF_FAIL( "Access violation must terminate the process" );
    }

    UTF_CHECK( true );
}

UTF_AUTO_TEST_CASE( BaseLib_EhGenerateInvalidParameter )
{
    #if defined( _WIN32 )
    if( test::UtfArgsParser::isClient() )
    {
        /*
         * This test is Windows-specific and it produces meaningful output only
         * when linked against the debug version of the C run-time library
         */

        bl::os::GlobalProcessInit initer;

        g_destructorTest.makeIndestructible();

        UTF_MESSAGE( "Generating an Invalid Parameter error" );

        FILE* file = nullptr;
        ::fputs( "foo", file );

        UTF_FAIL( "Invalid Parameter error must terminate the process" );
    }
    #endif

    UTF_CHECK( true );
}

UTF_AUTO_TEST_CASE( BaseLib_EhGenerateTermination )
{
    if( test::UtfArgsParser::isClient() )
    {
        bl::os::GlobalProcessInit initer;

        std::terminate();

        UTF_FAIL( "terminate() must terminate the process" );
    }

    UTF_CHECK( true );
}

UTF_AUTO_TEST_CASE( BaseLib_TestErrorHandling )
{
    const auto cbTest = [] ( SAA_in const bool useMessageBuffer, SAA_in const int errNo )
    {
        UTF_MESSAGE( "***************** Exception dump *****************\n" );

        try
        {
            if( useMessageBuffer )
            {
                if( -1 == errNo )
                    BL_THROW( bl::UnexpectedException(), BL_MSG() << "This is test exception " << 42 << " ; " << 1.2 );
                else
                    BL_CHK_EC( bl::eh::error_code( errNo, bl::eh::generic_category() ), BL_MSG() << "This is test exception " << 42 << " ; " << 1.2 );
            }
            else
            {
                if( -1 == errNo )
                    BL_THROW( bl::UnexpectedException(), "This is test exception 42 ; 1.2" );
                else
                    BL_CHK_EC( bl::eh::error_code( errNo, bl::eh::generic_category() ), "This is test exception 42 ; 1.2" );
            }

            UTF_CHECK( false );
        }
        catch( bl::UnexpectedException& e )
        {
            UTF_CHECK_EQUAL( "bl::UnexpectedException", e.fullTypeName() );

            const auto msg = e.what();
            UTF_MESSAGE( msg );
            UTF_CHECK_EQUAL( "This is test exception 42 ; 1.2", msg );

            const auto details = e.details();
            UTF_MESSAGE( details );

            UTF_CHECK( nullptr == e.errNo() );

            const auto* timeThrown = e.timeThrown();
            UTF_CHECK( nullptr != timeThrown && ! timeThrown -> empty() );

            bl::str::regex regex( bl::time::regexLocalTimeISO() );
            bl::str::smatch results;
            UTF_CHECK( bl::str::regex_match( *timeThrown, results, regex ) );
        }
        catch( bl::SystemException& e )
        {
            UTF_CHECK_EQUAL( "bl::SystemException", e.fullTypeName() );

            const auto msg = e.what();
            UTF_MESSAGE( msg );
            UTF_CHECK(
                std::string( msg ) == "This is test exception 42 ; 1.2: Permission denied" ||
                std::string( msg ) == "This is test exception 42 ; 1.2: Permission denied [generic:13]"
                );

            const auto details = e.details();
            UTF_MESSAGE( details );

            UTF_REQUIRE( nullptr != e.errNo() );
            UTF_CHECK( errNo == *e.errNo() );

            UTF_REQUIRE_EQUAL( EACCES, errNo );
            const auto* str = e.errorCodeMessage();
            UTF_REQUIRE( nullptr != str );
            UTF_REQUIRE_EQUAL( "Permission denied", *str );
        }

        UTF_MESSAGE( "***************** End exception dump *****************\n" );
    };

    cbTest( false /* useMessageBuffer */, -1 /* errNo */ );
    cbTest( false /* useMessageBuffer */, EACCES /* errNo */ );

    cbTest( true /* useMessageBuffer */, -1 /* errNo */ );
    cbTest( true /* useMessageBuffer */, EACCES /* errNo */ );

    BL_CHK( false, getTrue(), BL_MSG() << "This one should not throw ... " );
    BL_CHK( false, getTrue(), BL_MSG() << "This one should not throw: " << 42 );
    BL_CHK_T( false, getTrue(), bl::ArgumentNullException(), BL_MSG() << "This one should not throw: " << 42 );

    const auto cbTestChk = [] ( SAA_in const int errNo )
    {
        UTF_MESSAGE( "***************** Exception dump *****************\n" );

        try
        {
            if( -1 == errNo )
                BL_CHK( false, getFalse(), BL_MSG() << "This one should throw: " << 42 );
            else
                BL_CHK_EC(
                    bl::eh::error_code( errNo, bl::eh::generic_category() ),
                    BL_MSG()
                        << "This one should throw: "
                        << 42
                    );

            UTF_CHECK( false );
        }
        catch( bl::UnexpectedException& e )
        {
            const auto msg = e.what();
            UTF_MESSAGE( msg );
            UTF_CHECK_EQUAL( "This one should throw: 42", msg );

            const auto details = e.details();
            UTF_MESSAGE( details );

            UTF_CHECK( nullptr == e.errNo() );
        }
        catch( bl::SystemException& e )
        {
            const auto msg = e.what();
            UTF_MESSAGE( msg );
            UTF_CHECK(
                std::string( msg ) == "This one should throw: 42: Permission denied" ||
                std::string( msg ) == "This one should throw: 42: Permission denied [generic:13]"
                );

            const auto details = e.details();
            UTF_MESSAGE( details );

            UTF_REQUIRE( nullptr != e.errNo() );
            UTF_CHECK( errNo == *e.errNo() );

            UTF_REQUIRE_EQUAL( EACCES, errNo );
            const auto* str = bl::eh::get_error_info< bl::eh::errinfo_error_code_message >( e );
            UTF_REQUIRE( nullptr != str );
            UTF_REQUIRE_EQUAL( "Permission denied", *str );
        }

        UTF_MESSAGE( "***************** End exception dump *****************\n" );
    };

    cbTestChk( -1 /* errNo */ );
    cbTestChk( EACCES /* errNo */ );
}

UTF_AUTO_TEST_CASE( BaseLib_TestUserMessageException )
{
    const std::string message = "This is message for the user!";

    try
    {
        BL_THROW_USER( message );
        UTF_FAIL( BL_MSG() << "API must throw" );
    }
    catch( bl::UserMessageException& e )
    {
        BL_LOG_MULTILINE(
            bl::Logging::debug(),
            BL_MSG()
                << "User message exception dump:\n"
                << bl::eh::diagnostic_information( e )
            );

        UTF_CHECK_EQUAL( message, std::string( e.what() ) );
        UTF_REQUIRE( bl::eh::isUserFriendly( e ) );

        const bool* isUserFriendly =
            bl::eh::get_error_info< bl::eh::errinfo_is_user_friendly >( e );

        UTF_REQUIRE( isUserFriendly && *isUserFriendly );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_TestCheckArgumentMacro )
{
    const auto arg1 = 1U;

    class Foo
    {
    public:
    };

    Foo f;

    BL_CHK_ARG( true, arg1 );
    BL_CHK_ARG( true, f );

    try
    {
        BL_CHK_ARG( false, arg1 );
        UTF_FAIL( BL_MSG() << "API must throw" );
    }
    catch( bl::ArgumentException& e )
    {
        const auto msg = e.what();
        UTF_MESSAGE( BL_MSG() << "Expected message: " << msg );
        UTF_CHECK_EQUAL( "Invalid argument value '1' provided for argument 'arg1'", msg );
    }

    try
    {
        BL_CHK_ARG( false, f );
        UTF_FAIL( BL_MSG() << "API must throw" );
    }
    catch( bl::ArgumentException& e )
    {
        const auto msg = e.what();
        UTF_MESSAGE( BL_MSG() << "Expected message: " << msg );
        UTF_CHECK_EQUAL( "Invalid argument value provided for argument 'f'", msg );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_TestErrorCodeToStringAndHasher )
{
    using namespace bl;

    const auto ec1 = eh::errc::make_error_code( eh::errc::no_such_file_or_directory );
    const auto ec2 = eh::errc::make_error_code( eh::errc::file_exists );

    UTF_REQUIRE( ec1 != ec2 );

    const auto ec1AsString = eh::errorCodeToString( ec1 );
    const auto ec2AsString = eh::errorCodeToString( ec2 );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Error code 1 as string: "
            << ec1AsString
            << "; ("
            << ec1
            << ")"
        );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Error code 2 as string: "
            << ec2AsString
            << "; ("
            << ec2
            << ")"
        );

    std::hash< eh::error_code > hasher;
    std::hash< std::string > stringHasher;

    UTF_REQUIRE( ec1AsString != ec2AsString );

    UTF_REQUIRE( hasher( ec1 ) != hasher( ec2 ) );
    UTF_REQUIRE_EQUAL( hasher( ec1 ), stringHasher( ec1AsString ) );
    UTF_REQUIRE_EQUAL( hasher( ec2 ), stringHasher( ec2AsString ) );

    /*
     * Two codes which share a numeric value but differ in category is the whole
     * justification for hashing the string form rather than ec.value() - without the
     * category in the string they would collide as unordered_map keys
     */

    const auto ecGeneric = eh::error_code( 13, eh::generic_category() );
    const auto ecSystem = eh::error_code( 13, eh::system_category() );
    const auto ecGenericCopy = eh::error_code( 13, eh::generic_category() );

    UTF_REQUIRE( eh::errorCodeToString( ecGeneric ) != eh::errorCodeToString( ecSystem ) );

    UTF_REQUIRE( hasher( ecGeneric ) != hasher( ecSystem ) );

    /*
     * Equal codes must hash equally - the container invariant
     */

    UTF_REQUIRE_EQUAL( hasher( ecGeneric ), hasher( ecGenericCopy ) );

    /*
     * These are deliberately 'contains' and not equality checks - the exact stream format
     * of an error_code varies across the Boost versions this library supports
     */

    UTF_REQUIRE( cpp::contains( eh::errorCodeToString( ecGeneric ), std::string( "generic" ) ) );
    UTF_REQUIRE( cpp::contains( eh::errorCodeToString( ecSystem ), std::string( "system" ) ) );
}

UTF_AUTO_TEST_CASE( BaseLib_TestErrorCodeFromExceptionPtr )
{
    using namespace bl;

    const auto ec = eh::errc::make_error_code( eh::errc::errc_t::file_exists );

    try
    {
        BL_THROW_EC( ec, "Test exception" );
    }
    catch( std::exception& )
    {
        UTF_REQUIRE( ec == eh::errorCodeFromExceptionPtr( std::current_exception() ) );
    }

    try
    {
        BL_THROW(
            UnexpectedException()
                << eh::errinfo_error_code( ec ),
            "Test exception"
            );
    }
    catch( std::exception& )
    {
        UTF_REQUIRE( ec == eh::errorCodeFromExceptionPtr( std::current_exception() ) );
    }

    try
    {
        BL_THROW(
            UnexpectedException(),
            "Test exception"
            );
    }
    catch( std::exception& )
    {
        UTF_REQUIRE( eh::error_code() == eh::errorCodeFromExceptionPtr( std::current_exception() ) );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_EhExceptionHooksTests )
{
    using namespace bl;

    const auto errorCodeExpected = eh::errc::make_error_code( eh::errc::argument_out_of_domain );

    const auto throwAndCatchException = [ & ]() -> void
    {
        try
        {
            BL_THROW(
                ArgumentException()
                    << eh::errinfo_error_code( errorCodeExpected ),
                BL_MSG()
                    << "This is test exception"
                );
        }
        catch( ArgumentException& )
        {
        }
    };

    const auto throwHook = [ & ](
        SAA_in                  const BaseException&            exception,
        SAA_inout               bool&                           invokedFlag
        ) NOEXCEPT -> void
    {
        if( std::string( "bl::ArgumentException" ) == exception.fullTypeName() )
        {
            const auto* errorCode = exception.errorCode();

            if( errorCode && errorCodeExpected == *errorCode )
            {
                invokedFlag = true;
            }
        }
    };

    {
        bool throwHookInvoked1 = false;
        bool throwHookInvoked2 = false;

        {
            BL_EXCEPTION_HOOKS_THROW_GUARD( cpp::bind< void >( throwHook, _1, cpp::ref( throwHookInvoked1 ) ) );
        }

        UTF_REQUIRE_EQUAL( throwHookInvoked1, false );

        {
            BL_EXCEPTION_HOOKS_THROW_GUARD( cpp::bind< void >( throwHook, _1, cpp::ref( throwHookInvoked1 ) ) );

            {
                BL_EXCEPTION_HOOKS_THROW_GUARD( cpp::bind< void >( throwHook, _1, cpp::ref( throwHookInvoked2 ) ) );

                throwAndCatchException();
            }

            UTF_REQUIRE_EQUAL( throwHookInvoked1, false );

            #ifdef BL_ENABLE_EXCEPTION_HOOKS
            UTF_REQUIRE_EQUAL( throwHookInvoked2, true );
            #else // BL_ENABLE_EXCEPTION_HOOKS
            UTF_REQUIRE_EQUAL( throwHookInvoked2, false );
            #endif // BL_ENABLE_EXCEPTION_HOOKS

            throwAndCatchException();
        }

        #ifdef BL_ENABLE_EXCEPTION_HOOKS
        UTF_REQUIRE_EQUAL( throwHookInvoked1, true );
        UTF_REQUIRE_EQUAL( throwHookInvoked2, true );
        #else // BL_ENABLE_EXCEPTION_HOOKS
        UTF_REQUIRE_EQUAL( throwHookInvoked1, false );
        UTF_REQUIRE_EQUAL( throwHookInvoked2, false );
        #endif // BL_ENABLE_EXCEPTION_HOOKS
    }

    /*
     * A hook which itself constructs an exception must not deadlock - every BL_EXCEPTION
     * goes through enableThrowHook, so a hook body which throws re-enters it on the same
     * thread; the hook is copied out under g_lock and invoked outside of it precisely so
     * that this can work
     *
     * Note the failure mode of a regression here is a HANG and not a failed assertion:
     * os::mutex is a non-recursive std::mutex, so re-entering it on the same thread
     * self-deadlocks rather than reporting an error - a CI timeout, not flakiness
     *
     * The hook only records what it sees - it is NOEXCEPT, so it must not assert
     */

    {
        bool inHook = false;

        std::atomic< int > outerInvocations( 0 );
        std::atomic< int > nestedInvocations( 0 );

        std::atomic< bool > sawMessage( false );
        std::atomic< bool > sawTimeThrown( false );
        std::atomic< bool > sawThrowFile( false );

        const auto reentrantHook = [ & ](
            SAA_in                  const BaseException&            exception
            ) NOEXCEPT -> void
        {
            if( inHook )
            {
                ++nestedInvocations;

                return;
            }

            inHook = true;

            BL_SCOPE_EXIT( { inHook = false; } );

            try
            {
                BL_THROW( UnexpectedException(), "hook-internal" );
            }
            catch( UnexpectedException& )
            {
            }

            /*
             * The hook must see the exception already decorated by BL_EXCEPTION_IMPL
             */

            sawMessage = ( nullptr != exception.message() );
            sawTimeThrown = ( nullptr != exception.timeThrown() );
            sawThrowFile = ( nullptr != eh::get_error_info< eh::throw_file >( exception ) );

            ++outerInvocations;
        };

        {
            BL_EXCEPTION_HOOKS_THROW_GUARD( cpp::bind< void >( reentrantHook, _1 ) );

            throwAndCatchException();
        }

        #ifdef BL_ENABLE_EXCEPTION_HOOKS
        UTF_REQUIRE_EQUAL( 1, outerInvocations.load() );
        UTF_REQUIRE_EQUAL( 1, nestedInvocations.load() );

        UTF_REQUIRE( sawMessage.load() );
        UTF_REQUIRE( sawTimeThrown.load() );
        UTF_REQUIRE( sawThrowFile.load() );
        #else // BL_ENABLE_EXCEPTION_HOOKS
        UTF_REQUIRE_EQUAL( 0, outerInvocations.load() );
        UTF_REQUIRE_EQUAL( 0, nestedInvocations.load() );

        UTF_REQUIRE( ! sawMessage.load() );
        UTF_REQUIRE( ! sawTimeThrown.load() );
        UTF_REQUIRE( ! sawThrowFile.load() );
        #endif // BL_ENABLE_EXCEPTION_HOOKS

        /*
         * The guard restored the previous (empty) hook, so this must not invoke it again
         */

        throwAndCatchException();

        #ifdef BL_ENABLE_EXCEPTION_HOOKS
        UTF_REQUIRE_EQUAL( 1, outerInvocations.load() );
        #else // BL_ENABLE_EXCEPTION_HOOKS
        UTF_REQUIRE_EQUAL( 0, outerInvocations.load() );
        #endif // BL_ENABLE_EXCEPTION_HOOKS
    }
}

/************************************************************************
 * MessageBuffer tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TestMessageBuffer )
{
    UTF_CHECK_EQUAL( ( BL_MSG() << "This is test: " << 42 << ", " << 1.3 ).text(), "This is test: 42, 1.3" );

    UTF_CHECK_EQUAL(
        ( BL_MSG()
            << "This is test: "
            << ( BL_MSG()
                    << "(Nested buffer: "
                    << ( BL_MSG() << 42 )
                    << ")"
               )
            << ", "
            << 1.3
        ).text(),
        "This is test: (Nested buffer: 42), 1.3"
        );
}


/************************************************************************
 * Uuid tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TestUuid )
{
    UTF_MESSAGE( "***************** uuid tests *****************\n" );

    const auto u1 = bl::uuids::create();
    const auto u2 = bl::uuids::create();
    UTF_CHECK( u1 != u2 );

    const auto s1 = bl::uuids::uuid2string( u1 );
    const auto s2 = bl::uuids::uuid2string( u2 );
    UTF_CHECK( s1 != s2 );

    UTF_MESSAGE( s1 );
    UTF_MESSAGE( s2 );

    const auto nu1 = bl::uuids::string2uuid( s1 );
    const auto nu2 = bl::uuids::string2uuid( s2 );

    UTF_CHECK_EQUAL( u1, nu1 );
    UTF_CHECK_EQUAL( u2, nu2 );

    const auto& nil = bl::uuids::nil();

    UTF_MESSAGE( BL_MSG() << nil );

    {
        UTF_REQUIRE( bl::uuids::isUuid( "4f082035-e301-4cce-94f0-68f1c99f9223" ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( "4f082035-e301-4cce-68f1c99f9223" ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( "" ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( "abcd0123" ) )

        const auto u = bl::uuids::create();
        std::string s = bl::uuids::uuid2string( u );

        UTF_REQUIRE( bl::uuids::isUuid( s ) )
        UTF_REQUIRE( bl::uuids::isUuid( bl::str::to_lower_copy(s) ) )
        UTF_REQUIRE( bl::uuids::isUuid( bl::str::to_upper_copy(s) ) )

        UTF_REQUIRE( bl::uuids::containsUuid( s ) )
        UTF_REQUIRE( bl::uuids::containsUuid( bl::str::to_lower_copy(s) ) )
        UTF_REQUIRE( bl::uuids::containsUuid( bl::str::to_upper_copy(s) ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( "prefix_" + s ) )
        UTF_REQUIRE( bl::uuids::containsUuid( "prefix_" + s ) )

        UTF_REQUIRE( ! bl::uuids::isUuid( s.substr( 0, s.size() - 1 ) ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( s + " " ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( s + "-" ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( s + "0" ) )
        UTF_REQUIRE( ! bl::uuids::isUuid( s + "-0" ) )

        /*
         * string2uuid is a different code path than the isUuid regex above - it parses via
         * bl::cpp::SafeInputStringStream and must report all rejections as bl::ArgumentException
         * rather than as std::ios_base::failure, which none of its callers would catch
         *
         * The trailing garbage forms must be rejected as well - the extractor consumes exactly
         * the first 36 characters, so without the end of stream check they would silently parse
         * as the leading uuid
         */

        UTF_REQUIRE_THROW( bl::uuids::string2uuid( "" ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( "abcd0123" ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( "4f082035-e301-4cce-68f1c99f9223" ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( "zzzzzzzz-e301-4cce-94f0-68f1c99f9223" ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( s + "0" ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( s + "-0" ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( s + " " ), bl::ArgumentException );
        UTF_REQUIRE_THROW( bl::uuids::string2uuid( "{" + s + "}" ), bl::ArgumentException );

        /*
         * The uppercase form is accepted, but the canonical output is always lowercase
         */

        UTF_REQUIRE_EQUAL( bl::uuids::string2uuid( bl::str::to_upper_copy( s ) ), bl::uuids::string2uuid( s ) );
        UTF_REQUIRE_EQUAL( bl::uuids::uuid2string( bl::uuids::string2uuid( bl::str::to_upper_copy( s ) ) ), s );

        UTF_REQUIRE_EQUAL( bl::uuids::string2uuid( "00000000-0000-0000-0000-000000000000" ), bl::uuids::nil() );
    }

    UTF_MESSAGE( "*************** end uuid tests ***************\n" );
}

UTF_AUTO_TEST_CASE( BaseLib_TestUuidPerformance )
{
    UTF_MESSAGE( "***************** uuid perf tests *****************\n" );

    const auto t1 = bl::time::microsec_clock::universal_time();

    /*
     * Just create a million uuids to ensure there is no perf issue.
     * It should be fast.
     */

    const std::size_t count = 1000000;

    for( std::size_t i = 0; i < count; ++i )
    {
        bl::uuids::create();
    }

    const auto duration = bl::time::microsec_clock::universal_time() - t1;

    UTF_CHECK( duration < bl::time::seconds( 10 ) );

    UTF_MESSAGE( BL_MSG() << "Creating " << count << " uuids took " << duration );

    UTF_MESSAGE( "*************** end uuid perf tests ***************\n" );
}

UTF_AUTO_TEST_CASE( BaseLib_TestUuidUniqueness )
{
    std::set< std::string > ids;

    const std::size_t count = 100000U;

    for( std::size_t i = 0; i < count; ++i )
    {
        const auto uuid = bl::str::to_lower_copy( bl::uuids::uuid2string( bl::uuids::create() ) );

        if( ! ids.insert( uuid ).second /* isInserted */ )
        {
            UTF_FAIL( "Non unique uuid was generated" );
        }
    }

    UTF_MESSAGE( BL_MSG() << "Created " << count << " unique uuids" );
}

UTF_AUTO_TEST_CASE( BaseLib_TestUuidUniquenessMultiThreaded )
{
    std::map< std::string, std::set< std::string > > ids;
    std::map< std::string, std::size_t > threadIds;

    bl::tasks::scheduleAndExecuteInParallel(
        [ &ids, &threadIds ]( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
        {
            const auto callback = [](
                SAA_inout       std::size_t&                threadId,
                SAA_inout       std::set< std::string >&    idsLocal
                )
                -> void
            {
                std::hash< std::thread::id > hasher;
                threadId = hasher( std::this_thread::get_id() );

                for( std::size_t i = 0; i < 20000U; ++i )
                {
                    const auto uuid = bl::str::to_lower_copy( bl::uuids::uuid2string( bl::uuids::create() ) );

                    if( ! idsLocal.insert( uuid ).second /* isInserted */ )
                    {
                        UTF_FAIL( "Non unique uuid was generated" );
                    }
                }
            };

            for( std::size_t i = 0; i < 10U; ++i )
            {
                const auto uuid = bl::str::to_lower_copy( bl::uuids::uuid2string( bl::uuids::create() ) );

                auto pair = ids.emplace( uuid, std::set< std::string >() );
                UTF_REQUIRE( pair.second );

                auto pair2 = threadIds.emplace( uuid, 0U );
                UTF_REQUIRE( pair2.second );

                eq -> push_back(
                    bl::cpp::bind< void >(
                        callback,
                        bl::cpp::ref( pair2.first /* iterator */ -> second ),
                        bl::cpp::ref( pair.first /* iterator */ -> second )
                        )
                    );
            }
        }
        );

    std::set< std::string > idsMerged;
    std::set< std::size_t > threadHashes;

    for( const auto& pair : ids )
    {
        const auto pos = threadIds.find( pair.first );
        UTF_REQUIRE( pos != std::end( threadIds ) );

        threadHashes.insert( pos -> second );

        UTF_MESSAGE( BL_MSG() << "UUIDs batch '" << pair.first << "' created in thread " << pos -> second );

        for( const auto& uuid : pair.second /* idsLocal */ )
        {
            if( ! idsMerged.insert( uuid ).second /* isInserted */ )
            {
                UTF_FAIL( "Non unique uuid was generated" );
            }
        }
    }

    UTF_MESSAGE( BL_MSG() << "Created " << idsMerged.size() << " unique uuids" );

    /*
     * The ten batches run on the shared thread pool, so on a machine with a single
     * hardware thread they can all legitimately execute on one thread - assert the
     * diversity only where the machine can actually deliver it
     *
     * Without this the case would still pass if scheduleAndExecuteInParallel( ... ) ever
     * degenerated to serial execution, covering nothing about per thread seeding
     */

    if( std::thread::hardware_concurrency() > 1 )
    {
        UTF_REQUIRE( threadHashes.size() >= 2U );
    }
    else
    {
        UTF_MESSAGE( BL_MSG() << "Single hardware thread machine - thread diversity not asserted" );
    }
}

BL_IID_DECLARE( iid_test12345, "2a5b48f8-fc88-40f6-b69a-af53e5932603" )
BL_IID_DECLARE( iid_test12346, "1b9fb58d-1ff6-4c84-8d6e-c64ee42670e2" )

namespace
{
    const bl::uuid_t& UuidReturnAsConstRef1() NOEXCEPT
    {
        return iids::iid_test12345();
    }

    const bl::uuid_t& UuidReturnAsConstRef2() NOEXCEPT
    {
        return iids::iid_test12346();
    }
}

UTF_AUTO_TEST_CASE( BaseLib_UuidTestDeclareMacro )
{
    const auto u1 = bl::uuids::string2uuid( "2a5b48f8-fc88-40f6-b69a-af53e5932603" );
    const auto u2 = bl::uuids::string2uuid( "1b9fb58d-1ff6-4c84-8d6e-c64ee42670e2" );

    const auto& cu1 = UuidReturnAsConstRef1();
    const auto& cu2 = UuidReturnAsConstRef2();

    UTF_MESSAGE( BL_MSG() << cu1 );
    UTF_MESSAGE( BL_MSG() << cu2 );

    UTF_CHECK_EQUAL( u1, cu1 );
    UTF_CHECK_EQUAL( u2, cu2 );
}

/************************************************************************
 * Logging tests
 */

namespace
{
    void loggingExpectLine(
        SAA_in    const std::string&         line,
        SAA_in    const bool                 hasTimestamp,
        SAA_in    const std::string&         prefix,
        SAA_in    const std::string&         text
        )
    {
        if( hasTimestamp )
        {
            UTF_CHECK( 0 == line.find( prefix + "[" ) );
            UTF_CHECK( std::string::npos != line.find( "] " + text ) );
        }
        else
        {
            UTF_CHECK_EQUAL( line, prefix + text );
        }
    }

    void loggingValidateOutput(
        SAA_in    const std::string&         text,
        SAA_in    const bool                 expectDebug = false,
        SAA_in    const bool                 hasTimestamp = false
        )
    {
        std::string line;
        bl::cpp::SafeInputStringStream is( text );

        std::getline( is, line );
        loggingExpectLine( line, hasTimestamp, "", "Logging level 0" );
        std::getline( is, line );
        loggingExpectLine( line, hasTimestamp, "ERROR: ", "Logging level 1" );
        std::getline( is, line );
        loggingExpectLine( line, hasTimestamp, "WARNING: ", "Logging level 2" );
        std::getline( is, line );
        loggingExpectLine( line, hasTimestamp, "INFO: ", "Logging level 3" );

        if( expectDebug )
        {
            std::getline( is, line );
            UTF_CHECK( 0 == line.find( "DEBUG: [" ) );
            UTF_CHECK( std::string::npos != line.find( "] Logging level 4" ) );
        }

        std::getline( is, line );
        UTF_CHECK_EQUAL( line, "" );

        UTF_CHECK( is.eof() );
    }

    void loggingLogAllLevels( SAA_in const int level = bl::Logging::LL_DEFAULT )
    {
        bl::cpp::SafeUniquePtr< bl::Logging::LevelPusher > pushLevel;

        if( bl::Logging::LL_DEFAULT != level )
        {
            pushLevel.reset( new bl::Logging::LevelPusher( level, false /* global */ ) );
        }

        BL_LOG( bl::Logging::notify(), BL_MSG() << "Logging level " << 0 );
        BL_LOG( bl::Logging::error(), BL_MSG() << "Logging level " << 1 );
        BL_LOG( bl::Logging::warning(), BL_MSG() << "Logging level " << 2 );
        BL_LOG( bl::Logging::info(), BL_MSG() << "Logging level " << 3 );
        BL_LOG( bl::Logging::debug(), BL_MSG() << "Logging level " << 4 );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_LoggingBasicTests )
{
    const auto cbTest = [] ( SAA_in const bool tryDebug ) -> void
    {
        using bl::Logging;

        bl::cpp::SafeOutputStringStream os;
        const Logging::line_logger_t ll(
            bl::cpp::bind( &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( os ) )
            );
        Logging::LineLoggerPusher pushLogger( ll );

        loggingLogAllLevels( Logging::LL_INFO );
        loggingValidateOutput( os.str(), false /* expectDebug */ );

        if( tryDebug )
        {
            Logging::LevelPusher pushLevel( Logging::LL_DEBUG );
            BL_LOG( Logging::debug(), BL_MSG() << "Logging level " << 4 );
            loggingValidateOutput( os.str(), true /* expectDebug */ );
        }
    };

    cbTest( true  /* tryDebug */ );
    cbTest( false /* tryDebug */ );

    {
        using bl::Logging;

        Logging::Level level;

        UTF_CHECK( Logging::tryStringToLogLevel( "NOTIFY", level ) );

        UTF_CHECK_EQUAL( level, Logging::Level::LL_NOTIFY );

        UTF_CHECK( ! Logging::tryStringToLogLevel( "ERROR2", level ) );

        UTF_CHECK_EQUAL(
            Logging::Level::LL_ERROR,
            Logging::stringToLogLevel( "ERROR" )
            );

        UTF_CHECK_EQUAL(
            Logging::Level::LL_DEBUG,
            Logging::stringToLogLevel( "debug" )
            );

        UTF_CHECK_THROW(
            Logging::stringToLogLevel( "TRACE2" ),
            bl::UnexpectedException
            );

        UTF_CHECK_EQUAL(
            Logging::debug().prefix(),
            Logging::levelToChannel( Logging::stringToLogLevel( "debug" ) ).prefix()
            );

        UTF_CHECK_THROW(
            Logging::levelToChannel( Logging::stringToLogLevel( "warning2" ) ),
            bl::UnexpectedException
            );

        UTF_CHECK_THROW(
            Logging::levelToChannel( Logging::stringToLogLevel( "none" ) ),
            bl::UnexpectedException
            );

        /*
         * logLevelToString and tryStringToLogLevel are two independent tables over the
         * same seven labels - a label added to or renamed in one of them and not the
         * other would break level parsing for any consumer which formats a level and
         * reads it back
         *
         * The checks are deliberately non-fatal so a single drifted label reports all
         * seven rather than aborting the case on the first one
         */

        const Logging::Level allLevels[] =
        {
            Logging::LL_NONE,
            Logging::LL_NOTIFY,
            Logging::LL_ERROR,
            Logging::LL_WARNING,
            Logging::LL_INFO,
            Logging::LL_DEBUG,
            Logging::LL_TRACE,
        };

        for( const auto levelToFormat : allLevels )
        {
            const auto levelAsString = Logging::logLevelToString( levelToFormat );

            UTF_CHECK( ! levelAsString.empty() );

            Logging::Level parsed = Logging::LL_LAST;

            UTF_CHECK( Logging::tryStringToLogLevel( levelAsString, parsed ) );
            UTF_CHECK_EQUAL( levelToFormat, parsed );
        }

        /*
         * The parsing side is case insensitive in the formatting direction too
         *
         * Note that the labels logLevelToString produces are already lower case, so the
         * to_lower_copy round trip only restates the loop above; the to_upper_copy one is
         * what actually exercises the str::iequals comparisons
         */

        {
            Logging::Level parsed = Logging::LL_LAST;

            UTF_CHECK(
                Logging::tryStringToLogLevel(
                    bl::str::to_lower_copy( Logging::logLevelToString( Logging::LL_WARNING ) ),
                    parsed
                    )
                );

            UTF_CHECK_EQUAL( Logging::Level::LL_WARNING, parsed );

            parsed = Logging::LL_LAST;

            UTF_CHECK(
                Logging::tryStringToLogLevel(
                    bl::str::to_upper_copy( Logging::logLevelToString( Logging::LL_WARNING ) ),
                    parsed
                    )
                );

            UTF_CHECK_EQUAL( Logging::Level::LL_WARNING, parsed );
        }
    }
}

UTF_AUTO_TEST_CASE( BaseLib_LoggingThreadLocalTest )
{
    using bl::Logging;

    bl::cpp::SafeOutputStringStream os;

    const Logging::line_logger_t ll(
        bl::cpp::bind( &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( os ) )
        );
    Logging::LineLoggerPusher pushLogger( ll );

    Logging::LevelPusher pushLevel( Logging::LL_DEBUG );

    bl::os::thread t( bl::cpp::bind( &loggingLogAllLevels, Logging::LL_INFO )  );
    t.join();

    /*
     * Timestamps are always forced by the UTF logger
     */

    loggingValidateOutput( os.str(), false /* expectDebug */, false /* hasTimestamp */ );
}

UTF_AUTO_TEST_CASE( BaseLib_LoggingVerboseModeTests )
{
    using bl::Logging;

    bl::cpp::SafeOutputStringStream os;

    const Logging::line_logger_t ll(
        bl::cpp::bind( &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( os ) )
        );
    Logging::LineLoggerPusher pushLogger( ll );

    loggingLogAllLevels( Logging::LL_DEBUG );

    std::string line;
    bl::cpp::SafeInputStringStream is( os.str() );

    for( std::size_t i = 0; i < 5; ++i )
    {
        std::getline( is, line );
        UTF_MESSAGE( line );

        if( 0 == i )
        {
            UTF_CHECK( 0 == line.find( "[" ) );
        }
        else
        {
            UTF_CHECK( std::string::npos != line.find( ": [" ) );
        }

        UTF_CHECK( std::string::npos != line.find( "] Logging level " ) );
    }

    UTF_CHECK( ! is.eof() );
    std::getline( is, line );
    UTF_CHECK_EQUAL( line, "" );

    UTF_CHECK( is.eof() );
}

UTF_AUTO_TEST_CASE( BaseLib_LoggingMultiLineTests )
{
    using bl::Logging;

    bl::cpp::SafeOutputStringStream os;

    const Logging::line_logger_t ll(
            bl::cpp::bind( &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( os ) )
            );

    Logging::LineLoggerPusher pushLogger( ll );

    Logging::LevelPusher pushLevel( Logging::LL_INFO );

    BL_LOG_MULTILINE( Logging::info(), BL_MSG() << "Line1 test\nLine2\nLine3" );

    std::string line;
    bl::cpp::SafeInputStringStream is( os.str() );

    std::getline( is, line );
    UTF_MESSAGE( line );
    UTF_CHECK_EQUAL( line, "INFO: Line1 test" );

    std::getline( is, line );
    UTF_MESSAGE( line );
    UTF_CHECK_EQUAL( line, "INFO: Line2" );

    std::getline( is, line );
    UTF_MESSAGE( line );
    UTF_CHECK_EQUAL( line, "INFO: Line3" );

    UTF_CHECK( ! is.eof() );
    std::getline( is, line );
    UTF_CHECK_EQUAL( line, "" );

    UTF_CHECK( is.eof() );

    /*
     * The loop in Channel::outMultiLine is 'while( ! is.eof() ) { std::getline( ... ); }'
     * and std::getline consumes the delimiter without setting eofbit, so a message which
     * ENDS with a newline costs one extra iteration which logs an empty line, and an
     * empty message logs exactly one empty line
     *
     * The message above has no trailing newline and therefore never reaches that
     * boundary, yet production logs messages with leading and trailing newlines routinely
     * (BL_LOG_MULTILINE( ..., "\n**** Starting test ... ****\n" ))
     *
     * THIS PINS CURRENT BEHAVIOUR, including the trailing blank line, which may or may
     * not be considered a defect. If the team decides the blank line is wrong, this is
     * the single place the expectation flips
     */

    const auto countLoggedLines = []( SAA_in const std::string& text ) -> std::vector< std::string >
    {
        std::vector< std::string > result;

        bl::cpp::SafeInputStringStream input( text );

        std::string current;

        while( std::getline( input, current ) )
        {
            result.push_back( current );
        }

        return result;
    };

    const std::string prefix = Logging::info().prefix();

    {
        bl::cpp::SafeOutputStringStream osTrailing;

        const Logging::line_logger_t llTrailing(
                bl::cpp::bind(
                    &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( osTrailing )
                    )
                );

        Logging::LineLoggerPusher pushLoggerTrailing( llTrailing );

        BL_LOG_MULTILINE( Logging::info(), BL_MSG() << "Line1\nLine2\n" );

        const auto lines = countLoggedLines( osTrailing.str() );

        UTF_REQUIRE_EQUAL( 3U, lines.size() );

        UTF_CHECK_EQUAL( lines[ 0 ], prefix + "Line1" );
        UTF_CHECK_EQUAL( lines[ 1 ], prefix + "Line2" );
        UTF_CHECK_EQUAL( lines[ 2 ], prefix );
    }

    {
        bl::cpp::SafeOutputStringStream osEmpty;

        const Logging::line_logger_t llEmpty(
                bl::cpp::bind(
                    &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( osEmpty )
                    )
                );

        Logging::LineLoggerPusher pushLoggerEmpty( llEmpty );

        BL_LOG_MULTILINE( Logging::info(), BL_MSG() << "" );

        const auto lines = countLoggedLines( osEmpty.str() );

        UTF_REQUIRE_EQUAL( 1U, lines.size() );

        UTF_CHECK_EQUAL( lines[ 0 ], prefix );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_LoggingConcurrencyTests )
{
    UTF_MESSAGE( BL_MSG() << "********************* Logging concurrency tests begin *********************" );

    using bl::Logging;

    bl::cpp::SafeOutputStringStream os;

    const Logging::line_logger_t ll(
            bl::cpp::bind( &Logging::defaultLineLoggerWithLock, _1, _2, _3, _4, true /*addNewLine */, bl::cpp::ref( os ) )
            );

    Logging::LineLoggerPusher pushLogger( ll );

    Logging::LevelPusher pushLevel( Logging::LL_INFO, true /* global */ );

    bl::tasks::scheduleAndExecuteInParallel(
        []( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
        {
            const auto cb = []( SAA_in const std::size_t id ) -> void
            {
                if( 0 == ( id % 2 ) )
                {
                    BL_LOG(
                        Logging::info(),
                        BL_MSG()
                            << "Task executed: "
                            << id
                        );
                }
                else
                {
                    BL_LOG_MULTILINE(
                        Logging::info(),
                        BL_MSG()
                            << "Task executed successfully\n"
                            << "The task id was: "
                            << id
                        );
                }
            };

            for( std::size_t i = 0; i < 100; ++i )
            {
                if( 0 == ( i % 2 ) )
                {
                    eq -> push_front( bl::cpp::bind< void >( cb, i ) );
                }
                else
                {
                    eq -> push_back( bl::cpp::bind< void >( cb, i ) );
                }
            }
        }
        );

    bl::cpp::SafeInputStringStream is( os.str() );

    std::string line;
    while( ! is.eof() )
    {
        std::getline( is, line );

        if( line.empty() )
        {
            /*
             * Only the last line is allowed to be empty
             */

            UTF_REQUIRE( is.eof() );
            break;
        }

        UTF_MESSAGE( BL_MSG() << "Log line is: " << line );
        UTF_REQUIRE( 0 == line.find( "INFO: " ) );
    }

    UTF_MESSAGE( BL_MSG() << "********************* Logging concurrency tests end *********************" );
}

/************************************************************************
 * ThreadPool tests
 */

UTF_AUTO_TEST_CASE( BaseLib_ThreadPoolTests )
{
    const auto tp = bl::om::lockDisposable(
        bl::ThreadPoolImpl::createInstance( bl::os::getAbstractPriorityDefault(), 8 )
        );

    UTF_CHECK_EQUAL( tp -> size(), 8U );

    /*
     * Grow the thread pool from 8 to 20 threads (must be idempotent)
     */

    UTF_CHECK_EQUAL( tp -> resize( 20 ), 20U );
    UTF_CHECK_EQUAL( tp -> size(), 20U );

    UTF_CHECK_EQUAL( tp -> resize( 20 ), 20U );
    UTF_CHECK_EQUAL( tp -> size(), 20U );

    /*
     * Try to shrink the thread pool but it must not change
     */

    UTF_CHECK_EQUAL( tp -> resize( 10 ), 20U );
    UTF_CHECK_EQUAL( tp -> size(), 20U );

    if( test::UtfArgsParser::isClient() )
    {
        /*
         * Schedule some task in the default thread pool which hangs and then try
         * to shut it down to test that it will abort after some timeout
         */

        bl::tasks::scheduleAndExecuteInParallel(
            []( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
            {
                eq -> push_back(
                    [ & ]() -> void
                    {
                        bl::os::sleep( bl::time::hours( 2 ) );
                    }
                    );

                bl::os::sleep( bl::time::seconds( 1 ) );

                const auto tpDefault = bl::ThreadPoolDefault::getDefault();
                tpDefault -> dispose();
            }
            );
    }

    {
        /*
         * The observable state of a disposed pool
         *
         * Note that the pool below is deliberately not wrapped in om::lockDisposable( ... )
         * since this block disposes of it explicitly
         */

        const auto tp2 = bl::ThreadPoolImpl::createInstance< bl::ThreadPool >(
            bl::os::AbstractPriority::Normal,
            2U
            );

        UTF_REQUIRE_EQUAL( 2U, tp2 -> size() );
        UTF_REQUIRE_NO_THROW( tp2 -> aioService() );

        tp2 -> dispose();

        /*
         * disposeInternal( ... ) swaps the threads vector out, so size() must not lie
         */

        UTF_REQUIRE_EQUAL( 0U, tp2 -> size() );

        /*
         * The two guards below are the only thing which prevents aioService() from
         * handing out a destroyed I/O service object
         */

        UTF_REQUIRE_THROW_MESSAGE(
            tp2 -> resize( 4U ),
            bl::UnexpectedException,
            "Thread pool object has been disposed"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            tp2 -> aioService(),
            bl::UnexpectedException,
            "Thread pool object has been disposed"
            );

        /*
         * dispose() must be idempotent and a clean shutdown must not latch an exception
         */

        UTF_REQUIRE_NO_THROW( tp2 -> dispose() );

        UTF_REQUIRE( ! tp2 -> lastException() );
    }
}

/************************************************************************
 * os::< shared library support > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSSharedLibTests )
{
    if( bl::os::onWindows() )
    {
        const auto lib = bl::os::loadLibrary( "kernel32.dll" );
        UTF_CHECK( lib );

        const auto pfn = bl::os::getProcAddress( lib, "CloseHandle" );
        UTF_CHECK( pfn );

        try
        {
            bl::os::loadLibrary( "kernel32-unknown.dll" );
            UTF_CHECK( false );
        }
        catch( bl::SystemException& e )
        {
            const std::string details = e.details();
            UTF_MESSAGE( details );

            const auto ec = bl::eh::get_error_info< bl::eh::errinfo_system_code >( e );
            UTF_REQUIRE( nullptr != ec );
            UTF_REQUIRE_EQUAL( 126, *ec );

            const auto* str = e.errorCodeMessage();
            UTF_REQUIRE( nullptr != str );
            UTF_REQUIRE_EQUAL( "The specified module could not be found", *str );
        }
    }

    if( bl::os::onUNIX() )
    {
        const auto lib = bl::os::loadLibrary( bl::os::onDarwin() ? "libc.dylib" : "libc.so.6" );
        UTF_CHECK( lib );

        const auto pfn = bl::os::getProcAddress( lib, "strlen" );
        UTF_CHECK( pfn );

        try
        {
            bl::os::loadLibrary( "libunknown.so" );
            UTF_CHECK( false );
        }
        catch( bl::SystemException& e )
        {
            const std::string details = e.details();
            UTF_MESSAGE( details );

            const auto ec = bl::eh::get_error_info< bl::eh::errinfo_errno >( e );
            UTF_REQUIRE( nullptr != ec );

            /*
             * Note: on Unix, dlopen does not necessarily set errno. In
             * OSImplUNIX.h, when createException is invoked with 0 for errno,
             * ENOTSUP is used instead. Yet the original error is the one
             * reported by dlerror and encoded in the exception message.
             */

            if( bl::os::onDarwin() )
            {
                UTF_REQUIRE( *ec == ENOENT );
            }
            else
            {
                UTF_REQUIRE( *ec == EACCES || *ec == ENOTSUP || *ec == ENOENT );
            }

            const auto str = e.message();

            UTF_REQUIRE( nullptr != str );

            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << "message: "
                    << *str
                );

            if( bl::os::onDarwin() )
            {
                UTF_REQUIRE(
                    str -> find( "image not found" ) != std::string::npos ||
                    str -> find( "Shared library 'libunknown.so' cannot be loaded" ) != std::string::npos
                    );
            }
            else
            {
                UTF_REQUIRE( str -> find( "No such file or directory" ) != std::string::npos );
            }
        }
    }
}

/************************************************************************
 * os::< create process support > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessTests )
{
    if( bl::os::onWindows() )
    {
        if( test::UtfArgsParser::isAnalysisEnabled() )
        {
            /*
             * TODO: CreateProcess is triggering some weird application verifier
             * breaks in apphelp.dll which do not appear to be issue with the code
             *
             * The breaks also happen only if we turn on the unaligned flag on
             * page heap
             */

            return;
        }

        {
            const auto proc = bl::os::createProcess( "cmd.exe /c exit 3" );
            UTF_CHECK( proc );

            UTF_REQUIRE( bl::os::getPid() );
            UTF_REQUIRE( bl::os::getPid( proc ) );
            UTF_REQUIRE( bl::os::getPid() != bl::os::getPid( proc ) );

            const auto ec = bl::os::tryAwaitTermination( proc );
            UTF_CHECK_EQUAL( ec, 3 );

            std::vector< std::string > vec;
            vec.push_back( "cmd.exe" );
            vec.push_back( "/c" );
            vec.push_back( "exit 3" );

            const auto proc2 = bl::os::createProcess( vec );
            UTF_CHECK( proc2 );
            UTF_REQUIRE( bl::os::getPid( proc2 ) != bl::os::getPid( proc ) );

            const auto ec2 = bl::os::tryAwaitTermination( proc2 );
            UTF_CHECK_EQUAL( ec2, 3 );

        }

        {
            const auto proc = bl::os::createProcess( "cmd.exe /c exit 0", bl::os::ProcessCreateFlags::DetachProcess );
            UTF_CHECK( proc );

            const auto ec = bl::os::tryAwaitTermination( proc );
            UTF_CHECK_EQUAL( ec, 0 );
        }

        UTF_CHECK_THROW( bl::os::createProcess( "doesnotexistproc" ), bl::SystemException );
    }

    if( bl::os::onUNIX() )
    {
        {
            const auto proc = bl::os::createProcess( "bash -c \"exit 3\"" );
            UTF_CHECK( proc );

            const auto ec = bl::os::tryAwaitTermination( proc );
            UTF_CHECK_EQUAL( ec, 3 );
        }

        {
            const auto proc = bl::os::createProcess( "bash -c \"exit 0\"", bl::os::ProcessCreateFlags::DetachProcess );
            UTF_CHECK( proc );

            const auto ec = bl::os::tryAwaitTermination( proc );
            UTF_CHECK_EQUAL( ec, 0 );
        }

        {
            std::vector< std::string > vec;
            vec.push_back( "bash" );
            vec.push_back( "-c" );
            vec.push_back( "exit 3" );

            const auto proc = bl::os::createProcess( vec );
            UTF_CHECK( proc );

            const auto ec = bl::os::tryAwaitTermination( proc );
            UTF_CHECK_EQUAL( ec, 3 );
        }

        UTF_CHECK_THROW( bl::os::createProcess( "doesnotexistproc" ), bl::SystemException );

        UTF_CHECK_THROW( bl::os::createProcess( "A:\\Hello.exe world" ), bl::ArgumentException );
        UTF_CHECK_THROW( bl::os::createProcess( "foo bar\\" ), bl::ArgumentException );

        /*
         * These tests are intended to run when the process is running as root.
         */

        if( test::UtfArgsParser::isClient() )
        {
            if( test::UtfArgsParser::userId().empty() )
            {
                UTF_FAIL( "Test BaseLib_OSCreateProcessTests with the is-client argument must also specify userid." );
            }

            /*
             * Remove the temporary files so that we don't get a false positive.  Because process should be running as root,
             * it should be able to do this, no matter who created the file.
             */

            const auto cleanup = bl::os::createProcess( "rm -f /tmp/out /tmp/out2" );
            UTF_CHECK( cleanup );

            UTF_CHECK_EQUAL( bl::os::tryAwaitTermination( cleanup ) , 0 );

            const auto filename = "/tmp/out";
            const std::string command = std::string( "touch " ) + filename;

            const auto proc3 = bl::os::createProcess( test::UtfArgsParser::userId(), command );
            UTF_CHECK( proc3 );

            const auto ec3 = bl::os::tryAwaitTermination( proc3 );
            UTF_CHECK_EQUAL( ec3, 0 );

            const auto fileOwner = bl::os::getFileOwner( filename );
            UTF_REQUIRE_EQUAL( test::UtfArgsParser::userId(), fileOwner );

            const auto filename2 = "/tmp/out2";
            std::vector< std::string > vec2;
            vec2.push_back( "touch" );
            vec2.push_back( filename2 );

            const auto proc4 = bl::os::createProcess( test::UtfArgsParser::userId(), vec2 );
            UTF_CHECK( proc4 );

            const auto ec4 = bl::os::tryAwaitTermination( proc4 );
            UTF_CHECK_EQUAL( ec4, 0 );

            const auto fileOwner2 = bl::os::getFileOwner( filename2 );
            UTF_REQUIRE_EQUAL( test::UtfArgsParser::userId(), fileOwner2 );
        }
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessTests2 )
{
    if( bl::os::onUNIX() && test::UtfArgsParser::isClient() )
    {
        const auto proc = bl::os::createProcess( test::UtfArgsParser::userId(), "bash -c 'env|sort; ulimit -a'" );
        UTF_CHECK( proc );

        const auto ec = bl::os::tryAwaitTermination( proc );
        UTF_CHECK_EQUAL( ec, 0 );
    }
}

#if ! defined( _WIN32 )

namespace
{
    std::string readWholeFile( SAA_in const bl::fs::path& path )
    {
        std::ifstream is( path.string() );

        std::string text;
        std::string line;

        while( std::getline( is, line ) )
        {
            text += line;
            text += '\n';
        }

        return text;
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessDetachedStdioTests )
{
    /*
     * A detached child must start with the standard descriptors attached to /dev/null
     * (not closed) and a detached child with redirection must deliver its output to
     * the callback
     */

    bl::fs::TmpDir tmpDir;

    const auto outputFile = tmpDir.path() / "detached_stdio.txt";

    {
        std::vector< std::string > args;
        args.push_back( "bash" );
        args.push_back( "-c" );
        /*
         * Note: fd 1 is duplicated into fd 3 before it gets redirected to the output file
         *
         * The descriptors are compared against /dev/null with test -ef (same device and inode)
         * rather than resolved with readlink, because only Linux exposes them as symbolic links:
         * /dev/fd is procfs there, but on macOS it is the fdesc filesystem, whose entries are the
         * opened objects themselves (sockets, pipes, devices), so readlink fails on all of them.
         * /dev/fd is the portable spelling - on Linux it is a symbolic link to /proc/self/fd
         */

        args.push_back(
            "exec 3>&1; for fd in 0 3 2; do"
            " if [ /dev/fd/$fd -ef /dev/null ]; then echo /dev/null; else echo other; fi;"
            " done > " + outputFile.string()
            );

        const auto proc = bl::os::createProcess( args, bl::os::ProcessCreateFlags::DetachProcess );
        UTF_REQUIRE( proc );

        UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

        UTF_CHECK_EQUAL( readWholeFile( outputFile ), std::string( "/dev/null\n/dev/null\n/dev/null\n" ) );
    }

    {
        std::string line;

        const auto callbackIos = [ & ](
            SAA_in              const bl::os::process_handle_t  process,
            SAA_in_opt          std::istream*                   out,
            SAA_in_opt          std::istream*                   err,
            SAA_in_opt          std::ostream*                   in
            ) -> void
        {
            UTF_REQUIRE( process );
            UTF_REQUIRE( out );
            UTF_REQUIRE( ! err );
            UTF_REQUIRE( ! in );

            std::getline( *out, line );
        };

        const auto proc = bl::os::createProcess(
            "bash -c \"echo hello\"",
            bl::os::ProcessCreateFlags::DetachProcess | bl::os::ProcessCreateFlags::RedirectStdout,
            callbackIos
            );

        UTF_REQUIRE( proc );

        UTF_CHECK_EQUAL( line, std::string( "hello" ) );
        UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSSendSignalAfterTerminationTests )
{
    /*
     * Sending a signal through a handle whose process has already been waited on must be a
     * no-op - the pid of such a handle is zero and ::kill( 0, ... ) / ::kill( -0, ... ) would
     * signal the process group of the caller, i.e. the test process itself
     *
     * If this regresses the test process is terminated by the SIGTERM below
     */

    const auto proc = bl::os::createProcess( "true" );

    UTF_REQUIRE( proc );
    UTF_REQUIRE_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

    bl::os::sendSignal( proc.get(), SIGTERM, false /* includeSubprocesses */ );
    bl::os::sendSignal( proc.get(), SIGTERM, true /* includeSubprocesses */ );

    UTF_REQUIRE( true );
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessDetachedReleaseTests )
{
    /*
     * Releasing the handle of a running detached child must not stall and the
     * abandoned child must be reaped by a subsequent process creation once it
     * exits (i.e. no zombies accumulate in a long running parent)
     */

    std::uint64_t pid = 0U;

    const auto start = bl::time::microsec_clock::universal_time();

    {
        const auto proc = bl::os::createProcess( "sleep 30", bl::os::ProcessCreateFlags::DetachProcess );
        UTF_REQUIRE( proc );

        pid = bl::os::getPid( proc );
        UTF_REQUIRE( pid );
    }

    const auto elapsed = bl::time::microsec_clock::universal_time() - start;

    UTF_CHECK( elapsed < bl::time::seconds( 1 ) );

    UTF_REQUIRE_EQUAL( 0, ::kill( static_cast< ::pid_t >( pid ), SIGKILL ) );

    const auto cbIsStillOurChild = [ & ]() -> bool
    {
        const auto statusFile = bl::fs::path( "/proc" ) / std::to_string( pid ) / "status";

        if( ! bl::fs::exists( statusFile ) )
        {
            return false;
        }

        const std::string ppidLine = "PPid:\t" + std::to_string( bl::os::getPid() ) + "\n";

        return std::string::npos != readWholeFile( statusFile ).find( ppidLine );
    };

    bool reaped = false;

    for( std::size_t i = 0U; i < 50U && ! reaped; ++i )
    {
        bl::os::sleep( bl::time::milliseconds( 100 ) );

        const auto proc = bl::os::createProcess( "true" );
        UTF_REQUIRE_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

        reaped = ! cbIsStillOurChild();
    }

    UTF_CHECK( reaped );
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessExecFailureWhileLoggingTests )
{
    /*
     * An exec failure must be reported to the parent with the original errno and the
     * child must exit even when other threads hold the logging lock at the time of the
     * fork (the child inherits the locked state and must never touch the lock)
     */

    const auto cbCheckExecFailure = [](
        SAA_in          const std::string&                      command,
        SAA_in          const bl::os::ProcessCreateFlags        flags,
        SAA_in          const int                               errnoExpected
        ) -> void
    {
        try
        {
            bl::os::createProcess( command, flags );
            UTF_FAIL( "os::createProcess must throw" );
        }
        catch( bl::SystemException& e )
        {
            const auto* ec = bl::eh::get_error_info< bl::eh::errinfo_errno >( e );
            UTF_REQUIRE( nullptr != ec );
            UTF_CHECK_EQUAL( errnoExpected, *ec );
        }
    };

    bl::fs::TmpDir tmpDir;

    cbCheckExecFailure( "doesnotexistproc", bl::os::ProcessCreateFlags::NoRedirect, ENOENT );
    cbCheckExecFailure( tmpDir.path().string(), bl::os::ProcessCreateFlags::NoRedirect, EACCES );

    const auto cbNoopLineLogger = [](
        SAA_in      const std::string&                          prefix,
        SAA_in      const std::string&                          text,
        SAA_in      const bool                                  enableTimestamp,
        SAA_in      const bl::Logging::Level                    level
        ) -> void
    {
        BL_UNUSED( prefix );
        BL_UNUSED( text );
        BL_UNUSED( enableTimestamp );
        BL_UNUSED( level );
    };

    std::atomic< bool > stopLogging( false );
    std::vector< bl::os::thread > loggers;

    const auto cbStopLogging = [ & ]() -> void
    {
        stopLogging = true;

        for( auto& logger : loggers )
        {
            if( logger.joinable() )
            {
                logger.join();
            }
        }
    };

    BL_SCOPE_EXIT(
        {
            cbStopLogging();
        }
        );

    {
        bl::Logging::LineLoggerPusher pusher( cbNoopLineLogger );

        for( std::size_t i = 0U; i < 4U; ++i )
        {
            loggers.emplace_back(
                [ &stopLogging ]() -> void
                {
                    while( ! stopLogging )
                    {
                        BL_LOG( bl::Logging::debug(), "spawn while logging stress" );
                    }
                }
                );
        }

        for( std::size_t i = 0U; i < 20U; ++i )
        {
            const auto start = bl::time::microsec_clock::universal_time();

            cbCheckExecFailure( "doesnotexistproc", bl::os::ProcessCreateFlags::NoRedirect, ENOENT );
            cbCheckExecFailure( "doesnotexistproc", bl::os::ProcessCreateFlags::DetachProcess, ENOENT );

            const auto elapsed = bl::time::microsec_clock::universal_time() - start;

            UTF_REQUIRE( elapsed < bl::time::seconds( 1 ) );
        }

        cbStopLogging();
    }

    /*
     * No child process may be left behind (a child stuck after a failed exec would
     * still be a child of this process); the last child may still be exiting, so
     * allow a short grace period
     */

    const auto until = bl::time::microsec_clock::universal_time() + bl::time::seconds( 5 );

    for( ;; )
    {
        int status = 0;

        const auto rc = ::waitpid( -1, &status, WNOHANG );

        if( rc > 0 )
        {
            continue;
        }

        if( 0 == rc && bl::time::microsec_clock::universal_time() < until )
        {
            bl::os::sleep( bl::time::milliseconds( 10 ) );

            continue;
        }

        UTF_CHECK_EQUAL( -1, rc );
        UTF_CHECK_EQUAL( ECHILD, errno );

        break;
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessDescriptorLimitTests )
{
    /*
     * The close-on-exec sweep in the child must not cost time proportional to the soft
     * descriptor limit; raise the soft limit to the hard limit for the duration of the
     * test and require the spawns to stay fast
     */

    struct ::rlimit limits = {};

    UTF_REQUIRE_EQUAL( 0, ::getrlimit( RLIMIT_NOFILE, &limits ) );

    const ::rlim_t targetLimit = std::min< ::rlim_t >( limits.rlim_max, 1048576U );

    if( targetLimit < 65536U )
    {
        UTF_MESSAGE( "The hard descriptor limit is too low for this test to be meaningful; skipping" );

        return;
    }

    struct ::rlimit raisedLimits = limits;
    raisedLimits.rlim_cur = targetLimit;

    UTF_REQUIRE_EQUAL( 0, ::setrlimit( RLIMIT_NOFILE, &raisedLimits ) );

    BL_SCOPE_EXIT(
        {
            ::setrlimit( RLIMIT_NOFILE, &limits );
        }
        );

    UTF_REQUIRE( static_cast< ::rlim_t >( ::getdtablesize() ) == targetLimit );

    const std::size_t spawnCount = 5U;

    const auto start = bl::time::microsec_clock::universal_time();

    for( std::size_t i = 0U; i < spawnCount; ++i )
    {
        const auto proc = bl::os::createProcess( "true", bl::os::ProcessCreateFlags::WaitToFinish );
        UTF_REQUIRE( proc );
    }

    const auto elapsed = bl::time::microsec_clock::universal_time() - start;

    UTF_MESSAGE(
        BL_MSG()
            << "Average spawn time with soft descriptor limit "
            << targetLimit
            << " is "
            << ( elapsed.total_milliseconds() / spawnCount )
            << " ms"
        );

    UTF_CHECK( elapsed < bl::time::milliseconds( 20 * spawnCount ) );
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessDescriptorHygieneTests )
{
    /*
     * No descriptor other than the standard ones may leak into the child, including
     * descriptors which are not close-on-exec and one being opened by another thread
     * at the time of the spawn
     */

    std::vector< int > fds;

    BL_SCOPE_EXIT(
        {
            for( const int fd : fds )
            {
                ::close( fd );
            }
        }
        );

    for( std::size_t i = 0U; i < 10U; ++i )
    {
        const int fd = ::open( "/dev/null", O_RDWR );
        UTF_REQUIRE( -1 != fd );

        fds.push_back( fd );
    }

    std::atomic< bool > stopOpening( false );

    bl::os::thread opener(
        [ &stopOpening ]() -> void
        {
            while( ! stopOpening )
            {
                const int fd = ::open( "/dev/null", O_RDWR );

                if( -1 != fd )
                {
                    ::close( fd );
                }
            }
        }
        );

    BL_SCOPE_EXIT(
        {
            stopOpening = true;
            opener.join();
        }
        );

    std::string line;

    const auto callbackIos = [ & ](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in_opt          std::istream*                   out,
        SAA_in_opt          std::istream*                   err,
        SAA_in_opt          std::ostream*                   in
        ) -> void
    {
        UTF_REQUIRE( process );
        UTF_REQUIRE( out );
        UTF_REQUIRE( ! err );
        UTF_REQUIRE( ! in );

        std::getline( *out, line );
    };

    /*
     * The count is taken from the kernel's view of the child's own descriptors, which is
     * spelled differently per platform: procfs on Linux and the fdesc filesystem on macOS,
     * which reports one entry more than Linux does for the same set of open descriptors
     *
     * 'wc -l' is padded with leading blanks by the BSD implementation but not the GNU one,
     * so the blanks are stripped to keep the expected value identical in both cases
     */

#ifdef __linux__

    /*
     * 0, 1, 2 and the directory descriptor 'ls' itself opens
     */

    const std::string fdDirectory = "/proc/self/fd";
    const std::string expectedCount = "4";

#else

    /*
     * 0, 1, 2, the directory descriptor 'ls' itself opens and the one the fdesc filesystem
     * exposes while that directory is being read
     */

    const std::string fdDirectory = "/dev/fd";
    const std::string expectedCount = "5";

#endif

    /*
     * The script is passed as an argument vector rather than as a single command line because
     * the command line form is split by the library and does not preserve the single quotes
     * which 'tr' needs around its operand
     */

    std::vector< std::string > args;
    args.push_back( "bash" );
    args.push_back( "-c" );
    args.push_back( "ls " + fdDirectory + " | wc -l | tr -d ' '" );

    for( std::size_t i = 0U; i < 5U; ++i )
    {
        line.clear();

        const auto proc = bl::os::createProcess(
            args,
            bl::os::ProcessCreateFlags::RedirectStdout | bl::os::ProcessCreateFlags::WaitToFinish,
            callbackIos
            );

        UTF_REQUIRE( proc );

        UTF_CHECK_EQUAL( line, expectedCount );
    }
}

#endif // ! defined( _WIN32 )

#if defined( _WIN32 )

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessDetachedWindowsTests )
{
    if( test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * See the note in BaseLib_OSCreateProcessTests about CreateProcess and the
         * application verifier
         */

        return;
    }

    /*
     * The Windows counterpart of the detached process tests above: a detached child
     * which is not redirected must start with usable standard handles (the ones of
     * its own hidden console), a detached child with redirection must deliver its
     * output to the callback and releasing the handle of a running detached child
     * must return promptly without terminating it (there are no zombies to reap on
     * Windows; the process object goes away with its last handle)
     */

    bl::fs::TmpDir tmpDir;

    const auto outputFile = tmpDir.path() / "detached_stdio.txt";

    {
        /*
         * Each of the handle duplications below fails (and breaks the && chain) if the
         * respective standard handle of the child is not valid, so the file is written
         * only if all three standard handles are usable
         */

        const auto proc = bl::os::createProcess(
            "cmd.exe /c \"ver 3<&0 4>&1 5>&2 >nul && echo hello>\"" + outputFile.string() + "\"\"",
            bl::os::ProcessCreateFlags::DetachProcess
            );

        UTF_REQUIRE( proc );

        UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );

        UTF_REQUIRE( bl::fs::exists( outputFile ) );
        UTF_CHECK_EQUAL( bl::str::trim_copy( bl::encoding::readTextFile( outputFile ) ), std::string( "hello" ) );
    }

    {
        const auto proc = bl::os::createProcess( "cmd.exe /c \"echo hello\"", bl::os::ProcessCreateFlags::DetachProcess );
        UTF_REQUIRE( proc );

        UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
    }

    {
        std::string line;

        const auto callbackIos = [ & ](
            SAA_in              const bl::os::process_handle_t  process,
            SAA_in_opt          std::istream*                   out,
            SAA_in_opt          std::istream*                   err,
            SAA_in_opt          std::ostream*                   in
            ) -> void
        {
            UTF_REQUIRE( process );
            UTF_REQUIRE( out );
            UTF_REQUIRE( ! err );
            UTF_REQUIRE( ! in );

            std::getline( *out, line );
        };

        const auto proc = bl::os::createProcess(
            "cmd.exe /c \"echo hello\"",
            bl::os::ProcessCreateFlags::DetachProcess | bl::os::ProcessCreateFlags::RedirectStdout,
            callbackIos
            );

        UTF_REQUIRE( proc );

        UTF_CHECK_EQUAL( bl::str::trim_copy( line ), std::string( "hello" ) );
        UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( proc ) );
    }

    std::uint64_t pid = 0U;

    const auto start = bl::time::microsec_clock::universal_time();

    {
        const auto proc = bl::os::createProcess( "ping -n 31 127.0.0.1", bl::os::ProcessCreateFlags::DetachProcess );
        UTF_REQUIRE( proc );

        pid = bl::os::getPid( proc );
        UTF_REQUIRE( pid );
    }

    const auto elapsed = bl::time::microsec_clock::universal_time() - start;

    UTF_CHECK( elapsed < bl::time::seconds( 1 ) );

    {
        const auto handle = ::OpenProcess(
            PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE /* bInheritHandle */,
            static_cast< DWORD >( pid )
            );

        UTF_REQUIRE( NULL != handle );

        BL_SCOPE_EXIT(
            {
                ::CloseHandle( handle );
            }
            );

        /*
         * The detached child must have survived the release of its handle
         */

        UTF_CHECK( WAIT_TIMEOUT == ::WaitForSingleObject( handle, 0 /* dwMilliseconds */ ) );

        UTF_REQUIRE( ::TerminateProcess( handle, 1 /* uExitCode */ ) );
        UTF_REQUIRE( WAIT_OBJECT_0 == ::WaitForSingleObject( handle, 10000 /* dwMilliseconds */ ) );
    }
}

#endif // defined( _WIN32 )

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessRedirectedTests )
{
    if( bl::os::onWindows() && test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * TODO: CreateProcess is triggering some weird application verifier
         * breaks in apphelp.dll which do not appear to be issue with the code
         *
         * The breaks also happen only if we turn on the unaligned flag on
         * page heap
         */

        return;
    }

    bl::tasks::scheduleAndExecuteInParallel(
        []( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
        {
            BL_UNUSED( eq );

            std::vector< std::string > linesOut;
            std::vector< std::string > linesErr;

            const auto cbRedirectedIos = [ & ](
                SAA_in              const bl::os::process_handle_t  process,
                SAA_in_opt          std::istream*                   out,
                SAA_in_opt          std::istream*                   err,
                SAA_in_opt          std::ostream*                   in
                ) -> void
            {
                UTF_REQUIRE( process );
                UTF_REQUIRE( out );
                UTF_REQUIRE( err );
                UTF_REQUIRE( in );

                linesOut.clear();
                linesErr.clear();

                /*
                 * Read all pipes in separate tasks
                 */

                eq -> push_back(
                    [ & ]() -> void
                    {
                        std::string line;

                        while( ! out -> eof() )
                        {
                            line.clear();
                            std::getline( *out, line );

                            const auto i = linesOut.size();

                            linesOut.push_back( std::move( line ) );

                            BL_LOG(
                                bl::Logging::debug(),
                                BL_MSG()
                                    << "STDOUT[ "
                                    << std::setw( 3 )
                                    << i
                                    << " ] is '"
                                    << linesOut[ i ]
                                    << "'"
                                );
                        }
                    }
                );

                eq -> push_back(
                    [ & ]() -> void
                    {
                        std::string line;

                        while( ! err -> eof() )
                        {
                            line.clear();
                            std::getline( *err, line );

                            if( line.empty() && err -> fail() )
                            {
                                /*
                                 * Only add empty lines if they're legitimate
                                 * -- i.e. not the last line after the stream
                                 * was closed, but there was not real new line
                                 * at the end
                                 */

                                continue;
                            }

                            const auto i = linesErr.size();

                            linesErr.push_back( std::move( line ) );

                            BL_LOG(
                                bl::Logging::debug(),
                                BL_MSG()
                                    << "STDERR[ "
                                    << std::setw( 3 )
                                    << i
                                    << " ] is '"
                                    << linesErr[ i ]
                                    << "'"
                                );
                        }
                    }
                );

                bl::os::sleep( bl::time::seconds( 5 ) );

                /*
                 * Answer the 'Do you wish to continue?' question
                 * and verify the answer was echoed
                 */

                ( *in ) << "test1234" << std::endl;

                bl::os::sleep( bl::time::seconds( 5 ) );

                /*
                 * Answer the 'Press any key to continue . . .' question
                 * and verify the text
                 */

                ( *in ) << std::endl;

                /*
                 * Flush will throw if some of the tasks have failed
                 */

                eq -> flush();
            };

            const auto cbRedirectedFile = [ & ](
                SAA_in              const bl::os::process_handle_t  process,
                SAA_in_opt          std::FILE*                      outFile,
                SAA_in_opt          std::FILE*                      errFile,
                SAA_in_opt          std::FILE*                      inFile
                ) -> void
            {
                UTF_REQUIRE( process );
                UTF_REQUIRE( outFile );
                UTF_REQUIRE( errFile );
                UTF_REQUIRE( inFile );

                const auto out = bl::os::fileptr2istream( outFile );
                const auto err = bl::os::fileptr2istream( errFile );
                const auto in = bl::os::fileptr2ostream( inFile );

                cbRedirectedIos( process, out.get(), err.get(), in.get() );
            };

            const auto cbTestDefault = [ & ](
                SAA_in_opt      const bl::os::process_redirect_callback_ios_t&      callbackIos,
                SAA_in_opt      const bl::os::process_redirect_callback_file_t&     callbackFile
                ) -> void
            {
                bl::fs::TmpDir tmpDir;

                const auto filePath =
                    tmpDir.path() / ( bl::os::onWindows() ? "batch_file.cmd" :"batch_file.sh" );

                auto filePathStr = filePath.string();

                {
                    bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );
                    auto& os = outputFile.stream();

                    if( bl::os::onWindows() )
                    {
                        os << "@echo off" << std::endl;
                        os << "setlocal" << std::endl;
                        os << "set /p answer=Do you wish to continue?" << std::endl;
                        os << "echo %answer%" << std::endl;
                        os << "pause" << std::endl;
                        os << "dir c:\\" << std::endl;
                        os << "dir c:\\ 1>&2" << std::endl;
                        os << "set" << std::endl;
                        os << "endlocal" << std::endl;
                    }
                    else if( bl::os::onUNIX() )
                    {
                        os << "read -p \"Do you wish to continue?\" yn" << std::endl;
                        os << "read -s anykey" << std::endl;
                        os << "ls -la /" << std::endl;
                        os << "ls -la / 1>&2" << std::endl;
                        os << "env|sort" << std::endl;
                        os << "exit 0" << std::endl;
                    }
                    else
                    {
                        UTF_FAIL( "Unknown platform" );
                    }
                }

                if( bl::os::onWindows() )
                {
                    const std::string lfnPrefix( "\\\\?\\" );

                    if( 0 == filePathStr.find( lfnPrefix ) )
                    {
                        filePathStr.erase( 0, lfnPrefix.size() );
                    }
                }

                /*
                 * This environment variable must be inherited by the child process
                 */

                const auto envVarName = "TEST_" + bl::uuids::uuid2string( bl::uuids::create() );

                bl::os::setEnvironmentVariable( envVarName, "1234" );

                BL_SCOPE_EXIT(
                    {
                        bl::os::unsetEnvironmentVariable( envVarName );
                    }
                    );

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "****************************"
                    );

                const auto processRef = bl::os::createProcess(
                    ( bl::os::onWindows() ? "cmd.exe /c " : "bash " ) + filePathStr,
                    bl::os::ProcessCreateFlags::RedirectAll | bl::os::ProcessCreateFlags::WaitToFinish,
                    callbackIos,
                    callbackFile
                    );

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "****************************"
                    );

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "STDOUT count is "
                        << linesOut.size()
                    );

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "STDERR count is "
                        << linesErr.size()
                    );

                const int exitCode = bl::os::tryAwaitTermination( processRef );

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "exit code is "
                        << exitCode
                    );

                UTF_REQUIRE( 0 == exitCode );

                UTF_REQUIRE( 2U <= linesOut.size() );
                UTF_REQUIRE( 2U <= linesErr.size() );

                if( bl::os::onWindows() )
                {
                    UTF_REQUIRE_EQUAL( linesOut[ 0 ], "Do you wish to continue?test1234" );
                    UTF_REQUIRE_EQUAL( linesOut[ 1 ], "Press any key to continue . . . " );
                }

                UTF_REQUIRE( bl::cpp::contains( linesOut, envVarName + "=1234" ) );
            };

            cbTestDefault( cbRedirectedIos, bl::os::process_redirect_callback_file_t() );

            cbTestDefault( bl::os::process_redirect_callback_ios_t(), cbRedirectedFile );
        }
        );
}

UTF_AUTO_TEST_CASE( BaseLib_OSCreateProcessRedirectedMergedTests )
{
    if( bl::os::onWindows() && test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * TODO: CreateProcess is triggering some weird application verifier
         * breaks in apphelp.dll which do not appear to be issue with the code
         *
         * The breaks also happen only if we turn on the unaligned flag on
         * page heap
         */

        return;
    }

    std::vector< std::string > lines;

    const auto cbRedirectedIos = [ & ](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in              std::istream&                   out
        ) -> void
    {
        BL_UNUSED( process );
        BL_ASSERT( process );

        std::string line;

        while( ! out.eof() )
        {
            line.clear();
            std::getline( out, line );

            const auto i = lines.size();

            lines.push_back( std::move( line ) );

            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << "STDOUT[ "
                    << std::setw( 3 )
                    << i
                    << " ] is '"
                    << lines[ i ]
                    << "'"
                );
        }
    };

    bl::fs::TmpDir tmpDir;

    const auto filePath =
        tmpDir.path() / ( bl::os::onWindows() ? "batch_file.cmd" :"batch_file.sh" );

    auto filePathStr = filePath.string();

    {
        bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );
        auto& os = outputFile.stream();

        if( bl::os::onWindows() )
        {
            os << "@echo off" << std::endl;
            os << "setlocal" << std::endl;
            os << "dir c:\\" << std::endl;
            os << "dir c:\\ 1>&2" << std::endl;
            os << "endlocal" << std::endl;
        }
        else if( bl::os::onUNIX() )
        {
            os << "ls -la /" << std::endl;
            os << "ls -la / 1>&2" << std::endl;
            os << "exit 0" << std::endl;
        }
        else
        {
            UTF_FAIL( "Unknown platform" );
        }
    }

    if( bl::os::onWindows() )
    {
        const std::string lfnPrefix( "\\\\?\\" );

        if( 0 == filePathStr.find( lfnPrefix ) )
        {
            filePathStr.erase( 0, lfnPrefix.size() );
        }
    }

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "****************************"
        );

    const auto processRef = bl::os::createRedirectedProcessMergeOutputAndWait(
        ( bl::os::onWindows() ? "cmd.exe /c " : "bash " ) + filePathStr,
        cbRedirectedIos
        );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "****************************"
        );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "STDOUT count is "
            << lines.size()
        );

    const int exitCode = bl::os::tryAwaitTermination( processRef );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "exit code is "
            << exitCode
        );

    UTF_REQUIRE( 0 == exitCode );

    UTF_REQUIRE( 2U <= lines.size() );

    if( bl::os::onUNIX() )
    {
        /*
         * ProcessCreateFlags::CloseStdin - the script above never reads its standard
         * input, so the EOF property of that flag is not exercised by it
         *
         * With RedirectStdin | CloseStdin the parent creates the pipe and immediately
         * resets the write end, so no writable end survives anywhere and the child's very
         * first read returns EOF. Dropping that reset, or reordering the post-fork
         * inPipe.first.reset() so that a writable end survives in the parent, turns every
         * createRedirectedProcess*AndWait( ... ) call over a stdin reading command into an
         * unrecoverable deadlock: the child blocks on read while the parent blocks in
         * WaitToFinish
         *
         * NOTE THAT A REGRESSION HERE MANIFESTS AS A HANG, not as a failed assertion
         *
         * The block is UNIX only because it runs bash; it also inherits the Windows
         * analysis skip at the top of this case
         */

        lines.clear();

        const auto stdinProcessRef = bl::os::createRedirectedProcessMergeOutputAndWait(
            std::vector< std::string >
            {
                "bash",
                "-c",
                "if read line; then echo GOT; else echo EOF; fi"
            },
            cbRedirectedIos
            );

        UTF_REQUIRE( ! lines.empty() );
        UTF_CHECK_EQUAL( std::string( "EOF" ), lines.front() );

        UTF_CHECK_EQUAL( 0, bl::os::tryAwaitTermination( stdinProcessRef ) );
    }
}

/************************************************************************
 * os::< await process termination > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSTryAwaitTerminationTests )
{
    const auto cbTestWait = []( SAA_in const bl::os::process_handle_t proc )
    {
        try
        {
            bl::os::tryAwaitTermination( proc, 1000 /* in milliseconds */ );
            UTF_FAIL( "os::tryAwaitTermination must throw" );
        }
        catch( bl::eh::system_error& e )
        {
            const auto* message = bl::eh::get_error_info< bl::eh::errinfo_message >( e );
            UTF_REQUIRE( message );
            UTF_REQUIRE( 0U == message -> find( "Process did not terminate within specified timeout interval" ) );
        }

        UTF_REQUIRE( false == bl::os::tryTimedAwaitTermination( proc, nullptr, 1000 /* in milliseconds */ ) );

        bl::os::terminateProcess( proc );

        int exitCode = 0;

        UTF_REQUIRE( true == bl::os::tryTimedAwaitTermination( proc, &exitCode ) );
        UTF_REQUIRE( 0 != exitCode );

        exitCode = bl::os::tryAwaitTermination( proc );
        UTF_REQUIRE( 0 != exitCode );
    };

    const auto cbRedirectedIos = [ & ](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in_opt          std::istream*                   out,
        SAA_in_opt          std::istream*                   err,
        SAA_in_opt          std::ostream*                   in
        ) -> void
    {
        UTF_REQUIRE( process );
        UTF_REQUIRE( out );
        UTF_REQUIRE( err );
        UTF_REQUIRE( in );

        cbTestWait( process );
    };

    if( bl::os::onWindows() )
    {
        if( test::UtfArgsParser::isAnalysisEnabled() )
        {
            /*
             * TODO: CreateProcess is triggering some weird application verifier
             * breaks in apphelp.dll which do not appear to be issue with the code
             *
             * The breaks also happen only if we turn on the unaligned flag on
             * page heap
             */

            return;
        }

        const auto proc = bl::os::createProcess(
            "cmd.exe",
            bl::os::ProcessCreateFlags::RedirectAll | bl::os::ProcessCreateFlags::WaitToFinish,
            cbRedirectedIos
            );

        UTF_CHECK( proc );
    }

    if( bl::os::onUNIX() )
    {
        const auto proc = bl::os::createProcess(
            "bash",
            bl::os::ProcessCreateFlags::RedirectAll | bl::os::ProcessCreateFlags::WaitToFinish,
            cbRedirectedIos
            );

        UTF_CHECK( proc );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSTerminateProcessTree )
{
    const auto cbTestWait = []( SAA_in const bl::os::process_handle_t proc )
    {
        try
        {
            bl::os::tryAwaitTermination( proc, 1000 /* in milliseconds */ );
            UTF_FAIL( "os::tryAwaitTermination must throw" );
        }
        catch( bl::eh::system_error& e )
        {
            const auto* message = bl::eh::get_error_info< bl::eh::errinfo_message >( e );
            UTF_REQUIRE( message );
            UTF_REQUIRE( 0U == message -> find( "Process did not terminate within specified timeout interval" ) );
        }

        UTF_REQUIRE( false == bl::os::tryTimedAwaitTermination( proc, nullptr, 1000 /* in milliseconds */ ) );

        bl::os::terminateProcess( proc, true /* force */, true /* includeSubprocesses */ );

        int exitCode = 0;

        UTF_REQUIRE( true == bl::os::tryTimedAwaitTermination( proc, &exitCode ) );
        UTF_REQUIRE( 0 != exitCode );

        exitCode = bl::os::tryAwaitTermination( proc );
        UTF_REQUIRE( 0 != exitCode );
    };

    const auto cbRedirectedIos = [ & ](
        SAA_in              const bl::os::process_handle_t  process,
        SAA_in_opt          std::istream*                  out,
        SAA_in_opt          std::istream*                 err,
        SAA_in_opt          std::ostream*                   in
        ) -> void
    {
        UTF_REQUIRE( process );
        UTF_REQUIRE( out );
        UTF_REQUIRE( err );
        UTF_REQUIRE( in );

        cbTestWait( process );
    };

    if( bl::os::onWindows() )
    {
        if( test::UtfArgsParser::isAnalysisEnabled() )
        {
            /*
             * TODO: CreateProcess is triggering some weird application verifier
             * breaks in apphelp.dll which do not appear to be issue with the code
             *
             * The breaks also happen only if we turn on the unaligned flag on
             * page heap
             */

            return;
        }

        const auto proc = bl::os::createProcess(
            "cmd.exe /c cmd.exe /c cmd.exe",
            bl::os::ProcessCreateFlags::RedirectAll | bl::os::ProcessCreateFlags::WaitToFinish,
            cbRedirectedIos
            );

        UTF_CHECK( proc );
    }

    if( bl::os::onUNIX() )
    {
        const auto proc = bl::os::createProcess(
            "bash -c \"cat\"",
            bl::os::ProcessCreateFlags::RedirectAll | bl::os::ProcessCreateFlags::WaitToFinish,
            cbRedirectedIos
            );

        UTF_CHECK( proc );
    }
}

/************************************************************************
 * os::< get current executable path > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetCurrentExecutablePathTests )
{
    const auto exe = bl::os::getCurrentExecutablePath();

    /*
     * Verify the executable name is correct
     */

    const auto filename = bl::fs::path( exe ).filename().string();

    if( bl::os::onWindows() )
    {
        UTF_CHECK( "utf-baselib.exe" == filename || "utf-baselib-shared.exe" == filename );
    }

    if( bl::os::onUNIX() )
    {
        UTF_CHECK_EQUAL( filename, "utf-baselib" );
    }
}

/************************************************************************
 * os::< get/set/unset env var > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetSetEnvVarTests )
{
    UTF_CHECK( bl::os::tryGetEnvironmentVariable( "PATH" ) );
    UTF_CHECK( ! bl::os::tryGetEnvironmentVariable( "THISENVVARDOESNOTEXIST__" ) );
    UTF_CHECK( ! bl::os::tryGetEnvironmentVariable( "" ) );

    UTF_CHECK_NO_THROW( bl::os::getEnvironmentVariable( "PATH" ) );
    UTF_CHECK_THROW( bl::os::getEnvironmentVariable( "THISENVVARDOESNOTEXIST__" ), bl::UnexpectedException );
    UTF_CHECK_THROW( bl::os::getEnvironmentVariable( "" ), bl::UnexpectedException );

    const auto varName = "TEST_" + bl::uuids::uuid2string( bl::uuids::create() );

    UTF_REQUIRE( ! bl::os::tryGetEnvironmentVariable( varName ) );

    bl::os::setEnvironmentVariable( varName, "1234" );
    UTF_CHECK_EQUAL( bl::os::getEnvironmentVariable( varName ), "1234" );

    bl::os::setEnvironmentVariable( varName, "56789" );
    UTF_CHECK_EQUAL( bl::os::getEnvironmentVariable( varName ), "56789" );

    bl::os::unsetEnvironmentVariable( varName );
    UTF_CHECK( ! bl::os::tryGetEnvironmentVariable( varName ) );

    bl::os::unsetEnvironmentVariable( varName );
    UTF_CHECK( ! bl::os::tryGetEnvironmentVariable( varName ) );

    bl::os::unsetEnvironmentVariable( "THISENVVARDOESNOTEXIST__" );
    UTF_CHECK( ! bl::os::tryGetEnvironmentVariable( "THISENVVARDOESNOTEXIST__" ) );

    UTF_CHECK_THROW( bl::os::setEnvironmentVariable( "", "" ), bl::SystemException );
    UTF_CHECK_THROW( bl::os::setEnvironmentVariable( "", "foo" ), bl::SystemException );
    UTF_CHECK_THROW( bl::os::setEnvironmentVariable( "FOO", "" ), bl::SystemException );
    UTF_CHECK_THROW( bl::os::setEnvironmentVariable( "FOO=BAR", "baz" ), bl::SystemException );

    UTF_CHECK_THROW( bl::os::unsetEnvironmentVariable( "" ), bl::SystemException );
    UTF_CHECK_THROW( bl::os::unsetEnvironmentVariable( "FOO=BAR" ), bl::SystemException );
}

/************************************************************************
 * os::< get local application data directory > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetLocalAppDataDirTests )
{
    /*
     * Simple test to ensure no exception is raised
     */

    UTF_CHECK_NO_THROW( bl::os::getLocalAppDataDir() );
}

/************************************************************************
 * os::getPid() tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetPidTests )
{
    /*
     * Simple test to ensure no exception is raised
     */

    UTF_REQUIRE( bl::os::getPid() );
}

/************************************************************************
 * os::< long path names support on Windows > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSLongFileNamesWindowsTests )
{
    if( ! bl::os::onWindows() )
    {
        /*
         * These tests apply to Windows only
         */

        return;
    }

    UTF_MESSAGE( BL_MSG() << "\n****************************** Paths testing ****************************\n\n" );

    bl::fs::TmpDir tmpDir;
    const auto& tmpPath = tmpDir.path();

    #if defined( BL_DEVENV_VERSION ) && BL_DEVENV_VERSION > 5
    /*
     * The function path::is_complete was depreciated in boost 1.84 when c++17 is enabled
     * When c++17 is enabled path::is_absolute() should be used instead
     */
    #if defined( __GNUC__ ) || defined( __clang__ )
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #endif
    #endif

    {
        bl::fs::path path( "c:\\" );

        UTF_MESSAGE(
            BL_MSG()
                << path
                << "; "
                << "is_absolute: "
                << path.is_absolute()
                << "; "
                << "is_complete: "
                << path.is_complete()
                << "; "
                << "has_root_path: "
                << path.has_root_path()
                << "; "
                << "has_root_name: "
                << path.has_root_name()
                << "; "
                << "has_root_directory: "
                << path.has_root_directory()
                << "; "
                << "root_path: "
                << path.root_path()
                << "; "
                << "root_name: "
                << path.root_name()
                << "; "
                << "root_directory: "
                << path.root_directory()
            );
    }

    {
        bl::fs::path path;

        UTF_MESSAGE(
            BL_MSG()
                << path
                << "; "
                << "is_absolute: "
                << path.is_absolute()
                << "; "
                << "is_complete: "
                << path.is_complete()
                << "; "
                << "has_root_path: "
                << path.has_root_path()
                << "; "
                << "has_root_name: "
                << path.has_root_name()
                << "; "
                << "has_root_directory: "
                << path.has_root_directory()
                << "; "
                << "root_path: "
                << path.root_path()
                << "; "
                << "root_name: "
                << path.root_name()
                << "; "
                << "root_directory: "
                << path.root_directory()
            );
    }

    {
        bl::fs::path path( tmpPath );

        UTF_MESSAGE(
            BL_MSG()
                << path
                << "; "
                << "is_absolute: "
                << path.is_absolute()
                << "; "
                << "is_complete: "
                << path.is_complete()
                << "; "
                << "has_root_path: "
                << path.has_root_path()
                << "; "
                << "has_root_name: "
                << path.has_root_name()
                << "; "
                << "has_root_directory: "
                << path.has_root_directory()
                << "; "
                << "root_path: "
                << path.root_path()
                << "; "
                << "root_name: "
                << path.root_name()
                << "; "
                << "root_directory: "
                << path.root_directory()
            );
    }
    #if defined( BL_DEVENV_VERSION ) && BL_DEVENV_VERSION > 5
    #if defined( __GNUC__ ) || defined( __clang__ )
    #pragma GCC diagnostic pop
    #endif
    #endif

    {
        bl::fs::path path( tmpPath );

        const auto dirName = bl::uuids::uuid2string( bl::uuids::create() );

        /*
         * Create a long path to be deleted at the end
         */

        for( std::size_t i = 0; i < 100; ++i )
        {
            path /= dirName;
            bl::fs::safeCreateDirectory( path );
        }
    }

    UTF_MESSAGE( BL_MSG() << "\n\n**************************** Paths testing end ****************************\n" );
}

UTF_AUTO_TEST_CASE( BaseLib_OSLongFileNamesAppVerifCrashTests )
{
    if( ! bl::os::onWindows() )
    {
        /*
         * These tests apply to Windows only
         */

        return;
    }

    UTF_MESSAGE( BL_MSG() << "\n****************************** AppVerif Crash test ****************************\n\n" );

    const auto* lfnTest1 =
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName/LongN"
    "ame/LongName/LongName/LongName/L"
    "ongName/LongName/LongName/LongNa"
    "me/LongName/LongName/LongName/Lo"
    "ngName/LongName/LongName/LongNam"
    "e/LongName/LongName/LongName/Lon"
    "gName/LongName/LongName/LongName"
    "/LongName/LongName/LongName/Long"
    "Name/LongName/LongName/LongName/"
    "LongName/LongName/LongName";

    const std::string lfnTest2( lfnTest1 );

    const bl::fs::path path1( lfnTest1 );
    UTF_REQUIRE( ! path1.empty() );

    const bl::fs::path path2( lfnTest2 );
    UTF_REQUIRE_EQUAL( path1, path2 );

    bl::fs::path path3;
    path3 = lfnTest1;
    UTF_REQUIRE_EQUAL( path1, path3 );

    bl::fs::path path4;
    path4 = lfnTest2;
    UTF_REQUIRE_EQUAL( path1, path4 );
}

UTF_AUTO_TEST_CASE( BaseLib_OSDeletePathTests )
{
    const auto& path = test::UtfArgsParser::path();

    if( path.empty() )
    {
        /*
         * Path parameter is required
         */

        return;
    }

    UTF_MESSAGE(
        BL_MSG()
            << "Deleting path "
            << path
            );

    bl::fs::safeRemoveAll( path );
}

/************************************************************************
 * os::< stdio std::FILE* operations and large file support > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSLargeFileSupportTests )
{
    bl::fs::TmpDir tmpDir;

    const auto& tmpPath = tmpDir.path();

    const auto largeFilePath = tmpPath / "large_file.bin";

    const unsigned char pattern[] = { 1, 2, 3, 4, 5 };
    unsigned char buffer[ BL_ARRAY_SIZE( pattern ) ];

    std::memset( buffer, 0, BL_ARRAY_SIZE( buffer ) );

    std::uint64_t pos = 0;

    /*
     * Seek at 5 GB and write something then read it later
     */

    pos += 1024U * 1024U;
    pos *= 1024U;
    pos *= 5U;

    {
        const auto fileptr = bl::os::fopen( largeFilePath, "wb" );
        bl::os::fseek( fileptr, pos, SEEK_SET );
        bl::os::fwrite( fileptr, pattern, BL_ARRAY_SIZE( pattern ) );

        const auto newPos = bl::os::ftell( fileptr );
        UTF_REQUIRE_EQUAL( ( pos + BL_ARRAY_SIZE( pattern ) ), newPos );
    }

    {
        const auto fileptr = bl::os::fopen( largeFilePath, "rb" );
        bl::os::fseek( fileptr, pos, SEEK_SET );
        bl::os::fread( fileptr, buffer, BL_ARRAY_SIZE( buffer ) );

        static_assert(
            BL_ARRAY_SIZE( buffer ) == BL_ARRAY_SIZE( pattern ),
            "Buffer and pattern must have the same size"
            );
        UTF_REQUIRE( 0 == ::memcmp( buffer, pattern, BL_ARRAY_SIZE( buffer ) ) );

        const auto newPos = bl::os::ftell( fileptr );
        UTF_REQUIRE( ( pos + BL_ARRAY_SIZE( buffer ) ) == newPos );
    }

    /*
     * Everything above goes through os::fseek / os::ftell, which are the OSImplWindows
     * wrappers - they bypass the Boost device entirely. The same 5 GiB offset is walked
     * again below through fs::SafeInputFileStreamWrapper, i.e. through
     * stdio_file_device_base::trySeekFile / ::tellFile / ::seek, which are what backs
     * every SafeInputFileStreamWrapper in the library
     *
     * On Windows std::fseek / std::ftell take and return a 32 bit long, which is exactly
     * why the device calls _fseeki64 / _ftelli64; replacing them would silently wrap here
     *
     * The file is sparse, so a filesystem without sparse file support would make this
     * expensive rather than wrong - the guard degrades to a skip in that case
     */

    if( bl::fs::file_size( largeFilePath ) != ( pos + BL_ARRAY_SIZE( pattern ) ) )
    {
        UTF_MESSAGE(
            "Skipping the 64 bit device seek assertions - the sparse file was not created at its full size"
            );
    }
    else
    {
        bl::fs::SafeInputFileStreamWrapper large( largeFilePath );

        auto& is = large.stream();

        is.seekg( 0, std::ios::end );

        UTF_REQUIRE_EQUAL(
            static_cast< std::int64_t >( pos ) + static_cast< std::int64_t >( BL_ARRAY_SIZE( pattern ) ),
            static_cast< std::int64_t >( is.tellg() )
            );

        std::memset( buffer, 0, BL_ARRAY_SIZE( buffer ) );

        is.seekg( static_cast< std::streamoff >( pos ), std::ios::beg );
        is.read( reinterpret_cast< char* >( buffer ), BL_ARRAY_SIZE( buffer ) );

        UTF_REQUIRE( ! is.fail() );
        UTF_REQUIRE( 0 == ::memcmp( buffer, pattern, BL_ARRAY_SIZE( buffer ) ) );

        std::memset( buffer, 0, BL_ARRAY_SIZE( buffer ) );

        is.seekg( -static_cast< std::streamoff >( BL_ARRAY_SIZE( pattern ) ), std::ios::end );
        is.read( reinterpret_cast< char* >( buffer ), BL_ARRAY_SIZE( buffer ) );

        UTF_REQUIRE( ! is.fail() );
        UTF_REQUIRE( 0 == ::memcmp( buffer, pattern, BL_ARRAY_SIZE( buffer ) ) );
    }
}

/************************************************************************
 * os::< getFileCreateTime() / setFileCreateTime() > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetSetFileCreateTimeTests )
{
    bl::fs::path tmpPath;

    const auto now = std::time( nullptr );
    BL_CHK_ERRNO_NM( ( std::time_t )( -1 ), now );

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        UTF_REQUIRE( bl::fs::exists( tmpPath ) );

        if( bl::os::onUNIX() )
        {
            /*
             * Create time is not supported on UNIX
             */

            UTF_REQUIRE_THROW( bl::fs::safeGetFileCreateTime( tmpPath ), bl::NotSupportedException );
            UTF_REQUIRE_THROW( bl::fs::safeSetFileCreateTime( tmpPath, now ), bl::NotSupportedException );
        }
        else
        {
            const auto time = bl::fs::safeGetFileCreateTime( tmpPath );
            UTF_REQUIRE( time );
            UTF_REQUIRE_EQUAL( time, bl::fs::safeGetFileCreateTime( tmpPath ) );

            const auto newTime = time + 2000;
            bl::fs::safeSetFileCreateTime( tmpPath, newTime );
            UTF_REQUIRE_EQUAL( newTime, bl::fs::safeGetFileCreateTime( tmpPath ) );
        }
    }

    UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );
}

/************************************************************************
 * os::getUserName() tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetUserNameTests )
{
    /*
     * Simple test to ensure no exception is raised
     */

    const auto userName = bl::os::getUserName();

    UTF_REQUIRE( ! userName.empty() );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "User name = "
            << userName
        );
}

UTF_AUTO_TEST_CASE( BaseLib_OSLoggedInUserNamesTests )
{
    if( bl::os::onWindows() )
    {
        if( ! bl::os::isUserInteractive() )
        {
            return;
        }

        const auto names = bl::os::getLoggedInUserNames();

        UTF_REQUIRE( ! names.empty() );

        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "Logged-in user names: "
                << bl::str::joinQuoteFormatted( names )
            );

        const auto currentUser = bl::os::getUserName();

        const auto pos = std::find_if(
            names.begin(),
            names.end(),
            [ &currentUser ]( SAA_in const std::string& value ) -> bool
            {
                return bl::str::iequals( currentUser, value );
            }
            );

        UTF_REQUIRE( pos != names.end() );

        /*
         * Ensure Window Manager account isn't returned
         */

        for( const auto& name : names )
        {
            UTF_REQUIRE( ! bl::str::istarts_with( name, "DWM-" ) );
        }
    }
    else
    {
        UTF_REQUIRE_THROW( bl::os::getLoggedInUserNames(), NotSupportedException );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSClientCheckTests )
{
    if( bl::os::onWindows() )
    {
        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "isClientOS: "
                << bl::os::isClientOS()
            );
    }
    else
    {
        UTF_REQUIRE( ! bl::os::isClientOS() );
    }
}

/************************************************************************
 * os::getConsoleSize() tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSGetConsoleSizeTests )
{
    /*
     * Simple test to ensure no exception is raised
     */

    int columns, rows;

    const auto success = bl::os::getConsoleSize( columns, rows );

    if( success )
    {
        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "Console size = "
                << columns
                << " x "
                << rows
        );

        UTF_REQUIRE( columns > 0 );
        UTF_REQUIRE( rows > 0 );
    }
    else
    {
        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "Unable to determine the console size"
        );
    }
}

/************************************************************************
 * os::readFromInput() tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSReadFromInputTests )
{
    if( test::UtfArgsParser::isClient() )
    {
        UTF_MESSAGE( "***************** readFromInput interactive tests *****************\n" );

        std::cout << "Enter name (must be visible): ";
        const auto name = bl::os::readFromInput();

        UTF_MESSAGE( BL_MSG() << "Entered name '" << name << "'" );
        UTF_CHECK( ! name.empty() );

        std::cout << "Enter password (must be hidden): ";
        const auto pass = bl::os::readFromInputHidden();
        std::cout << std::endl;

        UTF_MESSAGE( BL_MSG() << "Entered password '" << pass.getAsNonSecureString() << "'" );
        UTF_CHECK( ! pass.empty() );

        UTF_MESSAGE( "\n***************** end readFromInput interactive tests *****************\n" );
    }
}

/************************************************************************
 * os::< junctions support > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSJunctionsTests )
{
    if( ! bl::os::onWindows() )
    {
        /*
         * Junctions are only supported on Windows
         *
         * On Linux we use symlinks
         */

        return;
    }

    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        const auto subDir = tmpPath / "sub-dir";
        const auto junctionDir = tmpPath / "junction-dir";
        const auto filePath = subDir / "file.txt";

        bl::fs::safeMkdirs( subDir );
        bl::fs::safeMkdirs( junctionDir );

        bl::fs::ensureDirectoryAndNotJunction( subDir );
        bl::fs::ensureDirectoryAndNotJunction( junctionDir );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::ensureDirectoryJunction( junctionDir ),
            bl::UnexpectedException,
            " must be a valid directory junction"
            );

        UTF_REQUIRE( ! bl::os::isJunction( subDir ) );
        UTF_REQUIRE( ! bl::os::isJunction( junctionDir ) );

        bl::os::createJunction( subDir, junctionDir );

        UTF_REQUIRE( bl::os::isJunction( junctionDir ) );
        UTF_REQUIRE_EQUAL( subDir, bl::os::getJunctionTarget( junctionDir ) );

        bl::fs::ensureDirectoryJunction( junctionDir );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::ensureDirectoryAndNotJunction( junctionDir ),
            bl::UnexpectedException,
            " must be a valid directory that is not a junction point"
            );

        {
            const auto file = bl::os::fopen( filePath, "wb" );
            UTF_REQUIRE( bl::fs::exists( filePath ) );
        }

        const auto filePathViaJunction = junctionDir / "file.txt";
        UTF_REQUIRE( bl::fs::exists( filePathViaJunction ) );

        bl::os::deleteJunction( junctionDir );
        UTF_REQUIRE( ! bl::os::isJunction( junctionDir ) );

        UTF_REQUIRE( ! bl::fs::exists( filePathViaJunction ) );
        UTF_REQUIRE( bl::fs::exists( filePath ) );

        UTF_REQUIRE( bl::fs::exists( junctionDir ) );

        /*
         * REPARSE_BUFFER_SIZE_DEFAULT is sizeof( REPARSE_DATA_BUFFER ) + 4 * ( MAX_PATH + 2 ),
         * i.e. it holds about 260 wide characters of target path, so a longer target takes
         * the dynamicBuffer branch of createJunction and makes DeviceIoControl in
         * getAndProcessReparseBuffer return ERROR_INSUFFICIENT_BUFFER / ERROR_MORE_DATA,
         * which is what drives the doubling loop there
         *
         * Neither branch has ever executed in this suite - every other junction case uses
         * a short TmpDir path
         */

        {
            auto deepTarget = tmpPath / "deep-target";

            for( std::size_t i = 0U; i < 12U; ++i )
            {
                deepTarget /= std::string( 30U, 'a' );
            }

            bl::fs::safeMkdirs( deepTarget );

            /*
             * Guards this case against a future TmpDir change which shortens the path
             */

            UTF_REQUIRE( deepTarget.wstring().size() > 260U );

            const auto deepJunction = tmpPath / "deep-junction";

            bl::fs::createDirectoryJunction( deepTarget, deepJunction );

            UTF_REQUIRE( bl::fs::isDirectoryJunction( deepJunction ) );

            /*
             * Byte exact, which is what proves both the dynamic write buffer and the
             * growing read loop are correct
             */

            UTF_REQUIRE_EQUAL( deepTarget, bl::fs::getDirectoryJunctionTarget( deepJunction ) );

            bl::fs::deleteDirectoryJunction( deepJunction );

            UTF_REQUIRE( bl::fs::is_directory( deepTarget ) );
            UTF_REQUIRE( ! bl::fs::path_exists( deepJunction ) );
        }
    }

    UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );
}

/************************************************************************
 * os::< hardlink support > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_OSHardlinkTests )
{
    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        /*
         * On Windows, directories always have just 1 link
         * On *nix file systems, each directory has at least 2 links (itself and its parent), plus one
         * for each sub-directory
         */

        UTF_REQUIRE_EQUAL( bl::os::onWindows() ? 1U : 2U, bl::os::getHardlinkCount( tmpPath ) );

        const auto filePath = tmpPath / "file.txt";
        const auto hardlinkPath = tmpPath / "hardlink.txt";

        {
            const auto file = bl::os::fopen( filePath, "wb" );
            bl::os::fwrite( file, "text", 4 );
            UTF_REQUIRE( bl::fs::exists( filePath ) );
            UTF_REQUIRE_EQUAL( 1U, bl::os::getHardlinkCount( filePath ) );
        }

        UTF_REQUIRE( ! bl::fs::exists( hardlinkPath ) );

        bl::os::createHardlink( filePath, hardlinkPath );

        UTF_REQUIRE( bl::fs::exists( filePath ) );
        UTF_REQUIRE( bl::fs::exists( hardlinkPath ) );
        UTF_REQUIRE_EQUAL( 2U, bl::os::getHardlinkCount( filePath ) );
        UTF_REQUIRE_EQUAL( 2U, bl::os::getHardlinkCount( hardlinkPath ) );

        /*
         * Cannot hard-link a file to itself
         */

        UTF_REQUIRE_THROW( bl::os::createHardlink( filePath, filePath ), bl::SystemException );

        bl::fs::safeRemove( filePath );

        UTF_REQUIRE( ! bl::fs::exists( filePath ) );
        UTF_REQUIRE( bl::fs::exists( hardlinkPath ) );
        UTF_REQUIRE_THROW( bl::os::getHardlinkCount( filePath ), bl::SystemException );
        UTF_REQUIRE_EQUAL( 1U, bl::os::getHardlinkCount( hardlinkPath ) );

        /*
         * Cannot hard-link a non-existing file
         */

        UTF_REQUIRE_THROW( bl::os::createHardlink( filePath, hardlinkPath ), bl::SystemException );
    }

    UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );
}

/************************************************************************
 * os::< ipc > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_NamedMutexTests )
{
    const auto lockName = "BL-Test-Mutex-b04c79af-463f-418d-bac7-536a4056e8cf";

    bl::os::RobustNamedMutex lock( lockName );

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "Trying to acquire a named lock '"
            << lockName
            << "'"
        );

    {
        bl::os::ipc::scoped_lock< decltype( lock ) > guard( lock );

        const long timeoutInSeconds = test::UtfArgsParser::isClient() ? 30 : 1;

        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "A named lock '"
                << lockName
                << "' has been acquired; waiting for "
                << timeoutInSeconds
                << " seconds ..."
            );

        bl::os::sleep( bl::time::seconds( timeoutInSeconds ) );
    }

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "A named lock '"
            << lockName
            << "' has been released"
        );
}

/************************************************************************
 * DataBlock interface tests
 */

UTF_AUTO_TEST_CASE( BaseLib_DataBlockTests )
{
    {
        const auto block = bl::data::DataBlock::createInstance();
        UTF_REQUIRE( block );

        const std::size_t expectedDefaultSize = bl::data::DataBlock::defaultCapacity();

        UTF_CHECK( block -> size() );
        UTF_CHECK( expectedDefaultSize == block -> size() );
        UTF_CHECK( expectedDefaultSize == block -> size64() );
        UTF_CHECK( expectedDefaultSize == block -> capacity() );
        UTF_CHECK( expectedDefaultSize == block -> capacity64() );

        {
            auto& b = *block;

            UTF_CHECK( b.begin() );
            UTF_CHECK( b.end() );

            UTF_CHECK( b.begin() < b.end() );
            UTF_CHECK( expectedDefaultSize == ( std::size_t )( b.end() - b.begin() ) );
        }

        {
            const auto& b = *block;

            UTF_CHECK( b.begin() );
            UTF_CHECK( b.end() );

            UTF_CHECK( b.begin() < b.end() );
            UTF_CHECK( expectedDefaultSize == ( std::size_t )( b.end() - b.begin() ) );
        }

        block -> setSize( 1234 );
        UTF_CHECK( 1234U == block -> size() );
        UTF_CHECK( 1234U == block -> size64() );
        UTF_CHECK( expectedDefaultSize == block -> capacity() );
        UTF_CHECK( expectedDefaultSize == block -> capacity64() );

        {
            const auto& b = *block;

            UTF_CHECK( b.begin() );
            UTF_CHECK( b.end() );

            UTF_CHECK( b.begin() < b.end() );
            UTF_CHECK( 1234 == ( b.end() - b.begin() ) );
        }
    }

    {
        const auto block = bl::data::DataBlock::createInstance( 1234 );
        UTF_REQUIRE( block );

        UTF_CHECK( 1234U == block -> size() );
        UTF_CHECK( 1234U == block -> size64() );
        UTF_CHECK( 1234U == block -> capacity() );
        UTF_CHECK( 1234U == block -> capacity64() );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_DataEnsureAvailableTests )
{
    using namespace bl;
    using namespace bl::data;

    const auto block = DataBlock::createInstance();

    std::size_t capacity = DataBlock::defaultCapacity();

    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );
    UTF_REQUIRE_EQUAL( block -> size(), capacity );
    UTF_REQUIRE_EQUAL( block -> size64(), capacity );

    block -> readEnsureAvailable( capacity );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> readEnsureAvailable( capacity + 1U ),
        BufferTooSmallException,
        resolveMessage(
            BL_MSG()
                << "Attempt to read "
                << ( capacity + 1U )
                << " bytes with read position 0 and write position "
                << capacity
            )
            );

    block -> writeEnsureAvailable( 0U );
    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );
    block -> writeEnsureAvailable( 0U, DataBlock::DATA_BLOCK_ALIGNMENT_DEFAULT );
    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );

    const std::size_t bigAlignment = 4U * DataBlock::DATA_BLOCK_ALIGNMENT_DEFAULT;

    block -> writeEnsureAvailable( 0Ul, bigAlignment );
    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );

    block -> writeEnsureAvailable( 1U );
    capacity = DataBlock::defaultCapacity() + DataBlock::DATA_BLOCK_ALIGNMENT_DEFAULT;
    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );
    block -> writeEnsureAvailable( DataBlock::DATA_BLOCK_ALIGNMENT_DEFAULT );
    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );

    block -> writeEnsureAvailable( DataBlock::DATA_BLOCK_ALIGNMENT_DEFAULT + 1U, bigAlignment );
    capacity = DataBlock::defaultCapacity() + bigAlignment;
    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );

    block -> reset();

    UTF_REQUIRE_EQUAL( block -> capacity(), capacity );
    UTF_REQUIRE_EQUAL( block -> size(), 0U );
    UTF_REQUIRE_EQUAL( block -> size64(), 0UL );

    block -> readEnsureAvailable( 0U );

    UTF_REQUIRE_THROW_MESSAGE(
        block -> readEnsureAvailable( 1U ),
        BufferTooSmallException,
        resolveMessage(
            BL_MSG()
                << "Attempt to read "
                << 1U
                << " bytes with read position 0 and write position "
                << 0
            )
            );
}

UTF_AUTO_TEST_CASE( BaseLib_NetworkByteOrderFunctionsTests )
{
    /*
     * Tests network byte order functions
     */

    {
        const decltype( bl::os::host2NetworkLong( 0 ) ) value = 12345678;

        const auto h2n = bl::os::host2NetworkLong( value );
        UTF_CHECK( h2n );

        const auto n2h = bl::os::network2HostLong( h2n );
        UTF_CHECK( n2h );
        UTF_CHECK_EQUAL( n2h, value );
    }

    {
        const decltype( bl::os::host2NetworkShort( 0 ) ) value = 12345;

        const auto h2n = bl::os::host2NetworkShort( value );
        UTF_CHECK( h2n );

        const auto n2h = bl::os::network2HostShort( h2n );
        UTF_CHECK( n2h );
        UTF_CHECK_EQUAL( n2h, value );
    }

    {
        /*
         * Initialize input value that can't fit in 32bit integer
         * so that lo and hi parts are actually used.
         */
        std::uint64_t big = ( static_cast< std::uint64_t >( 123 ) << 32 ) + 456;

        std::uint32_t lo;
        std::uint32_t hi;

        bl::os::host2NetworkLongLong( big, lo, hi );

        std::uint64_t big2 = bl::os::network2HostLongLong( lo, hi );

        UTF_CHECK_EQUAL( big, big2 );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_NetworkHelperFunctionsTests )
{
    if( bl::os::onWindows() && test::UtfArgsParser::isAnalysisEnabled() )
    {
        /*
         * TODO: gethostname() for some reason is causing app verifier
         * leak on WSACleanup which isn't an issue with the application
         * and also non-critical
         *
         * We will disable for now and investigate later on
         */

        return;
    }

    const auto hostName = bl::net::getShortHostName();

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "Host name: "
            << hostName
        );

    UTF_CHECK( ! bl::cpp::contains( hostName, '.' ) );

    UTF_CHECK_EQUAL( bl::net::getShortHostName( "host1.com" ), "host1" );
    UTF_CHECK_EQUAL( bl::net::getShortHostName( "host2" ), "host2" );

    UTF_CHECK_THROW( bl::net::getShortHostName( "" ), bl::ArgumentException );

    /*
     * Fully-qualified domain name (FQDN)
     *
     * Hosts without a domain (e.g. virtual machines) return just their host name
     */

    const auto fullHostName = bl::net::getCanonicalHostName();

    BL_LOG(
        bl::Logging::debug(),
        BL_MSG()
            << "Fully qualified host name: "
            << fullHostName
        );

    if( bl::os::onDarwin() )
    {
        /*
         * On Darwin the host name can be different capitalization than what is in the
         * fullHostName, so we need to normalize it
         */

        UTF_CHECK(
            bl::str::starts_with(
                bl::str::to_lower_copy( fullHostName ),
                bl::str::to_lower_copy( hostName )
                )
            );
    }
    else
    {
        UTF_CHECK( bl::str::starts_with( fullHostName, hostName ) );
    }

    /*
     * Localhost alias and IP addresses return different results depending on the OS
     */

    UTF_CHECK( ! bl::net::getCanonicalHostName( "localhost" ).empty() );
    UTF_CHECK( ! bl::net::getCanonicalHostName( "127.0.0.1" ).empty() );

    UTF_CHECK_THROW( bl::net::getCanonicalHostName( "" ), bl::ArgumentException );
    UTF_CHECK_THROW( bl::net::getCanonicalHostName( "host.domain.invalid" ), bl::SystemException );

    /*
     * Depending on the OS and the network config the call below might throw or return
     * a non-empty name (e.g. invalidhostname42.localdomain in rhel6)
     */

    try
    {
        UTF_CHECK( ! bl::net::getCanonicalHostName( "invalidhostname42" ).empty() );
    }
    catch( bl::SystemException& )
    {
    }

    /*
     * Measure the performance of getCanonicalHostName()
     *
     * The current host name should be cached by the OS so the API call should be quite cheap
     * (under 1 ms per call)
     */

    if( test::UtfArgsParser::isClient() )
    {
        utest::TestUtils::measureRuntime(
            "Querying CanonicalHostName 10000 times",
            [ & ]() -> void
            {
                for( auto i = 0; i < 10000; ++i )
                {
                    ( void ) bl::net::getCanonicalHostName();
                }
            }
            );
    }
}

/************************************************************************
 * SafeUniquePtr< T > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_SafeUniquePtrTests )
{
    static long g_refs = 0L;

    class Foo :
        public bl::cpp::noncopyable,
        public bl::om::Object
    {
        BL_QITBL_DECLARE( bl::om::Object )

    protected:

        Foo()
        {
            ++g_refs;
        }

        ~Foo() NOEXCEPT
        {
            --g_refs;
        }
    };

    typedef bl::om::ObjectImpl< Foo > FooImpl;
    typedef bl::cpp::SafeUniquePtr< FooImpl, bl::om::detail::Deleter > FooImplPtr;

    UTF_REQUIRE_EQUAL( 0L, g_refs );

    {
        auto p1 = FooImplPtr::attach( FooImpl::createInstance().release() );
        auto p2 = FooImplPtr::attach( FooImpl::createInstance().release() );

        UTF_REQUIRE( nullptr != p1 );
        UTF_REQUIRE( nullptr != p2 );

        p1 = std::move( p2 );
        UTF_REQUIRE( nullptr != p1 );
        UTF_REQUIRE( nullptr == p2 );
    }

    /*
     * Ensure no memory leaks
     */

    UTF_REQUIRE_EQUAL( 0L, g_refs );
}

UTF_AUTO_TEST_CASE( BaseLib_SafeUniquePtrAndContainersTests )
{
    using namespace utest;

    typedef bl::cpp::SafeUniquePtr< MyObjectImpl, bl::om::detail::Deleter > MyObjectImplPtr;

    std::vector< MyObjectImplPtr > v1;
    std::vector< bl::om::ObjPtr< MyObjectImpl > > v2;

    v1.push_back( MyObjectImplPtr::attach( MyObjectImpl::createInstance().release() ) );
    v2.push_back( MyObjectImpl::createInstance() );

    /*
     * Add a fake UTF check, so the UTF library doesn't complain there are no checks
     */

    UTF_REQUIRE( v1.size() && v2.size() );
}

/************************************************************************
 * ScalarTypeIniter< T > tests
 */

namespace
{
    class ScalarTypeIniterTester
    {
    public:

        enum TestEnum
        {
            NonZero = 1,
            Zero = 0,
        };

        template
        <
            typename T
        >
        static void runTests( SAA_in const T realValue, SAA_in_opt const T defaultValue = T() )
        {
            T localValue;

            bl::cpp::ScalarTypeIniter< T > i1;
            bl::cpp::ScalarTypeIniter< T > i2( realValue );
            bl::cpp::ScalarTypeIniter< T > i3 = realValue;

            UTF_REQUIRE_EQUAL( i1, defaultValue );
            UTF_REQUIRE_EQUAL( i2, realValue );
            UTF_REQUIRE_EQUAL( i3, realValue );

            i1 = realValue;
            UTF_REQUIRE_EQUAL( i1, realValue );

            localValue = i1;
            UTF_REQUIRE_EQUAL( localValue, realValue );

            i1 = defaultValue;
            UTF_REQUIRE_EQUAL( i1, defaultValue );

            UTF_REQUIRE_EQUAL( i2, i3 );
        }
    };
}

UTF_AUTO_TEST_CASE( BaseLib_ScalarTypeIniterTests )
{
    int dummy = 0;

    ScalarTypeIniterTester::runTests< int >( 42, 0 );
    ScalarTypeIniterTester::runTests< int* >( &dummy, nullptr );
    ScalarTypeIniterTester::runTests< bool >( true, false );

    ScalarTypeIniterTester::runTests< ScalarTypeIniterTester::TestEnum >(
        ScalarTypeIniterTester::NonZero,
        ScalarTypeIniterTester::Zero
        );
}

/************************************************************************
 * PathUtils tests
 */

namespace
{
    bl::fs::path pathNormalizeForOS( SAA_in std::string&& path )
    {
        if( bl::os::onWindows() )
        {
            /*
             * The passed in paths are Windows, so in this case we
             * just return it as is
             */

            return std::move( path );
        }

        /*
         * Just delete the drive letter prefix and then convert the slashes
         */

        if( path.length() >= 2 && path[ 1 ] == ':' )
        {
            path.erase( 0, 2 );
        }

        std::replace( path.begin(), path.end(), '\\', '/' );

        return std::move( path );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_PathUtilsTests )
{
    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo" ), pathNormalizeForOS( "c:\\" ), relPath ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "foo" );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo" ), pathNormalizeForOS( "c:\\foo" ), relPath ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "" );
    }

    {
        bl::fs::path relPath( "baz" );

        UTF_REQUIRE( ! bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo" ), pathNormalizeForOS( "c:\\bar" ), relPath ) );

        /*
         * Verify that relPath should be left unmodified if fs::getRelativePath
         * returns false
         */

        UTF_REQUIRE_EQUAL( relPath.string(), "baz" );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo\\bar\\baz" ), pathNormalizeForOS( "c:\\foo\\bar" ), relPath ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "baz" );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo\\bar\\baz" ), pathNormalizeForOS( "c:\\foo\\bar\\" ), relPath ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "baz" );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo\\bar\\baz" ), pathNormalizeForOS( "c:\\foo\\bar\\\\" ), relPath ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "baz" );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo\\bar\\baz" ), pathNormalizeForOS( "c:/foo/bar/" ), relPath ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "baz" );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( ! bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo" ), pathNormalizeForOS( "c:\\bar" ), relPath, true /* allowNonStrictRoot */ ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), pathNormalizeForOS( "..\\foo" ) );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( ! bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo\\version2.0" ), pathNormalizeForOS( "c:\\bar" ), relPath, true /* allowNonStrictRoot */ ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), pathNormalizeForOS( "..\\foo\\version2.0" ) );
    }

    {
        bl::fs::path relPath;

        UTF_REQUIRE( bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo\\bar\\baz" ), pathNormalizeForOS( "c:/foo/bar/" ), relPath, true /* allowNonStrictRoot */ ) );
        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), "baz" );
    }

    {
        /*
         * 'path' is a strict PREFIX of 'root', i.e. the path iterator runs out first
         *
         * The walk condition is "ip != path.end() && ir != root.end() && *ip == *ir" - the
         * end checks deliberately precede the dereference, and this is the shape which
         * makes the difference: reordering them back to dereference-before-end-check is
         * undefined behaviour for exactly this input
         */

        bl::fs::path relPath;

        UTF_REQUIRE(
            ! bl::fs::getRelativePath(
                pathNormalizeForOS( "c:\\foo" ),
                pathNormalizeForOS( "c:\\foo\\bar\\baz" ),
                relPath,
                true /* allowNonStrictRoot */
                )
            );

        UTF_REQUIRE( relPath.is_relative() );
        UTF_REQUIRE_EQUAL( relPath.string(), pathNormalizeForOS( "..\\.." ) );
    }

    {
        bl::fs::path relPath( "keepme" );

        UTF_REQUIRE(
            ! bl::fs::getRelativePath(
                pathNormalizeForOS( "c:\\foo" ),
                pathNormalizeForOS( "c:\\foo\\bar\\baz" ),
                relPath
                )
            );

        UTF_REQUIRE_EQUAL( relPath.string(), "keepme" );
    }

    {
        /*
         * The argument preconditions - getRelativePath walks two iterators and is only
         * meaningful for absolute paths, so a relative or empty argument must be rejected
         * before the walk rather than producing a nonsense relative path
         *
         * An empty root reaches ensureAbsolute( ... ) first, so its exception carries the
         * "must be absolute" message rather than "cannot be empty" - which is why only the
         * type is asserted for the getRelativePath calls and the messages are pinned on
         * the two ensure* helpers directly ( nothing else in the repository calls them )
         */

        bl::fs::path relPath;

        UTF_REQUIRE_THROW(
            bl::fs::getRelativePath( "relative/path", pathNormalizeForOS( "c:\\foo" ), relPath ),
            bl::ArgumentException
            );

        UTF_REQUIRE_THROW(
            bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo" ), "relative/root", relPath ),
            bl::ArgumentException
            );

        UTF_REQUIRE_THROW(
            bl::fs::getRelativePath( pathNormalizeForOS( "c:\\foo" ), bl::fs::path(), relPath ),
            bl::ArgumentException
            );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::ensureAbsolute( "relative/path" ),
            bl::ArgumentException,
            "must be absolute"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::ensureNotEmpty( bl::fs::path() ),
            bl::ArgumentException,
            "cannot be empty"
            );
    }

    {
        /*
         * The trailing separator dot filter is applied to 'root' ONLY, so a trailing
         * separator on 'path' survives into the result while one on 'root' does not -
         * contrast the "c:\foo\bar\baz" over "c:\foo\bar\" block above, which yields
         * "baz" and not ".\baz"
         *
         * If this is judged a defect rather than a contract, the fix is a production
         * change ( filter the dots on 'ip' too ) and this assertion becomes its regression
         * test
         */

        bl::fs::path relPath;

        const auto isPrefix =
            bl::fs::getRelativePath(
                pathNormalizeForOS( "c:\\foo\\bar\\" ),
                pathNormalizeForOS( "c:\\foo\\bar" ),
                relPath
                );

        UTF_REQUIRE( isPrefix );
        UTF_CHECK_EQUAL( relPath.string(), "." );
    }

    {
        /*
         * The mirrored case - with a trailing separator on both sides the two dot elements
         * cancel out in the walk itself and the result is empty
         */

        bl::fs::path relPath;

        const auto isPrefix =
            bl::fs::getRelativePath(
                pathNormalizeForOS( "c:\\foo\\bar\\" ),
                pathNormalizeForOS( "c:\\foo\\bar\\" ),
                relPath
                );

        UTF_REQUIRE( isPrefix );
        UTF_CHECK_EQUAL( relPath.string(), "" );
    }

    {
        /*
         * All this is needed because std::make_tuple() supports only 5 arguments in MSVC and we need 7 values...
         */

        typedef enum : std::uint8_t
        {
            NONE            = 0,
            UNIX            = 1,
            WINDOWS         = 2,
            ALL             = UNIX | WINDOWS,
        } systems;

        const auto system = bl::os::onUNIX() ? UNIX : WINDOWS;

        static const std::tuple
        <
            systems /* valid on which OSes */,
            bool /* contains ./.. */,
            const char* /* path */
        > testDirs[] =
        {
            std::make_tuple(    NONE, false, "" ),
            std::make_tuple(     ALL,  true, "." ),
            std::make_tuple(     ALL,  true, ".." ),
            std::make_tuple(     ALL, false, ".dir" ),
            std::make_tuple(     ALL, false, "..dir" ),
            std::make_tuple(     ALL, false, "dir." ),
            std::make_tuple(     ALL, false, "dir.." ),
            std::make_tuple(     ALL, false, "my.dir" ),
            std::make_tuple(     ALL, false, "my..dir" ),
            std::make_tuple(     ALL, false, "my_dir" ),
            std::make_tuple(     ALL, false, "my-dir" ),
            std::make_tuple(     ALL, false, "my+dir" ),
            std::make_tuple(     ALL, false, "my dir" ),
            std::make_tuple(     ALL, false, "UTF8 characters δυο, три, čtyři, पाँच.dat" ),
            std::make_tuple(    UNIX, false, "my>dir" ),
            std::make_tuple(    UNIX, false, "my<dir" ),
            std::make_tuple(    UNIX, false, "my|dir" ),
            std::make_tuple(    UNIX, false, "my:dir" ),
            std::make_tuple(    UNIX, false, "my\"dir" ),
            std::make_tuple(    UNIX, false, "my?dir" ),
            std::make_tuple(    UNIX, false, "my*dir" ),
            std::make_tuple(    UNIX, false, "my\\dir" ),
            std::make_tuple(    NONE, false, "my/dir" ),
            std::make_tuple(    UNIX, false, "my" "\x01" "dir" ),
            std::make_tuple(    UNIX, false, "my" "\x1F" "dir" ),
            std::make_tuple(    UNIX, false, "my\r\ndir" ),
            std::make_tuple(    NONE, false, "/" ),
            std::make_tuple(    UNIX, false, "\\" ),
            std::make_tuple(    UNIX, false, "*.*" ),
            std::make_tuple(    UNIX, false, "?" ),
        };

        for( const auto& test : testDirs )
        {
            const bool isPortable = std::get< 0 >( test ) == ALL;
            const bool isValid = ( std::get< 0 >( test ) & system ) == system;
            const bool isDotDir = std::get< 1 >( test );
            const auto dir = std::get< 2 >( test );

            UTF_CHECK_EQUAL( isPortable && ! isDotDir, bl::fs::isPortableDirectoryName( dir, false /* don't allow current/parent dir */ ) );
            UTF_CHECK_EQUAL( isValid && ! isDotDir, bl::fs::isValidDirectoryName( dir, false /* don't allow current/parent dir */ ) );

            UTF_CHECK_EQUAL( isPortable, bl::fs::isPortableDirectoryName( dir, true /* allow current/parent dir */ ) );
            UTF_CHECK_EQUAL( isValid, bl::fs::isValidDirectoryName( dir, true /* allow current/parent dir */ ) );
        }

        static const std::tuple
        <
            bool /* portable */,
            systems /* valid on which OSes */,
            systems /* is absolute on which OSes */,
            bool /* contains ./.. */,
            const char* /* path */
        > testPaths[] =
        {
            std::make_tuple( false,    NONE,    NONE, false, "" ),
            std::make_tuple( false,     ALL,     ALL, false, "/" ),
            std::make_tuple( false,     ALL, WINDOWS, false, "\\" ),
            std::make_tuple( false,     ALL, WINDOWS, false, "C:" ),
            std::make_tuple( false,     ALL, WINDOWS, false, "C:\\" ),
            std::make_tuple(  true,     ALL,    NONE, false, "file name.txt" ),
            std::make_tuple(  true,     ALL,    NONE, false, "dir/file_name.txt" ),
            std::make_tuple( false,     ALL,     ALL, false, "/dir/file_name.txt" ),
            std::make_tuple(  true,     ALL,    NONE,  true, "./dir/file_name.txt" ),
            std::make_tuple(  true,     ALL,    NONE,  true, "../dir/file_name.txt" ),
            std::make_tuple(  true,     ALL,    NONE,  true, "dir/./file_name.txt" ),
            std::make_tuple(  true,     ALL,    NONE,  true, "dir/../file_name.txt" ),
            std::make_tuple(  true,     ALL,    NONE, false, "UTF8 characters/1/δυο/три/čtyři/पाँच.dat" ),
            std::make_tuple( false,     ALL,    NONE, false, "UTF8 characters\\1\\δυο\\три\\čtyři\\पाँच.dat" ),
            std::make_tuple( false,    UNIX,    NONE, false, "file/with:some::colons" ),
            std::make_tuple(  true,     ALL,    NONE, false, "dir name/ with / spaces.TXT " ),
            std::make_tuple( false,     ALL, WINDOWS, false, "C:\\Program Files\\myapp\\bin\\myapp-binary" ),
            std::make_tuple( false,     ALL,    NONE, false, "Program Files\\myapp\\bin\\myapp-binary" ),
            std::make_tuple( false,     ALL, WINDOWS, false, "\\\\HOST-123\\share$\\some file.dat" ),
            std::make_tuple( false,     ALL,     ALL, false, "//HOST-123/share$/another file.dat" ),
        };

        for( const auto& test : testPaths )
        {
            const bool isPortable = std::get< 0 >( test );
            const bool isValid = ( std::get< 1 >( test ) & system ) == system;
            const bool isAbsolute = ( std::get< 2 >( test ) & system ) == system;
            const bool isDotDir = std::get< 3 >( test );
            const std::string path = std::get< 4 >( test );

            UTF_MESSAGE( path );

            UTF_CHECK_EQUAL(
                isPortable && ! isDotDir && ! isAbsolute,
                bl::fs::isPortablePath( path, false /* don't allow current/parent dir */ )
                );

            UTF_CHECK_EQUAL(
                isValid && ! isDotDir && ! isAbsolute,
                bl::fs::isValidPath( path, false /* don't allow current/parent dir */ )
                );

            UTF_CHECK_EQUAL(
                isPortable && ! isAbsolute,
                bl::fs::isPortablePath( path, true /* allow current/parent dir */ )
                );

            UTF_CHECK_EQUAL(
                isValid && ! isAbsolute,
                bl::fs::isValidPath( path, true /* allow current/parent dir */ )
                );

            UTF_CHECK_EQUAL(
                isValid,
                bl::fs::isValidPath( path, true /* allow current/parent dir */, true /* allowAbsolutePath */ )
                );
        }
    }
}

/************************************************************************
 * BoxedValueObject tests
 */

UTF_AUTO_TEST_CASE( BaseLib_BoxedValueObjectTests )
{
    {
        typedef bl::om::ObjectImpl< bl::om::BoxedValueObject< bl::fs::file_status > > status_obj_t;

        bl::fs::file_status s1( bl::fs::symlink_file );
        bl::fs::file_status s2( bl::fs::socket_file );

        UTF_REQUIRE_EQUAL( s1.type(), bl::fs::symlink_file );
        UTF_REQUIRE_EQUAL( s2.type(), bl::fs::socket_file  );

        auto obj1 = status_obj_t::createInstance();
        obj1 -> moveOrCopyNothrow( std::move( s1 ) );

        auto obj2 = status_obj_t::createInstance();
        obj2 -> moveOrCopyNothrow( std::move( s2 ) );

        UTF_REQUIRE_EQUAL( s1.type(), bl::fs::symlink_file );
        UTF_REQUIRE_EQUAL( s2.type(), bl::fs::socket_file  );

        UTF_REQUIRE_EQUAL( obj1 -> value().type(), bl::fs::symlink_file );
        UTF_REQUIRE_EQUAL( obj2 -> value().type(), bl::fs::socket_file );
    }

    {
        typedef bl::om::ObjectImpl< bl::om::BoxedValueObject< bl::fs::path > > entry_obj_t;

        bl::fs::path p1( "bar" );
        bl::fs::path p2( "baz" );

        UTF_REQUIRE_EQUAL( p1, bl::fs::path( "bar" ) );
        UTF_REQUIRE_EQUAL( p2, bl::fs::path( "baz" ) );

        const auto obj1 = entry_obj_t::createInstance();
        const auto obj2 = entry_obj_t::createInstance();

        obj1 -> swapValue( std::move( p1 ) );
        obj2 -> swapValue( std::move( p2 ) );

        UTF_REQUIRE_EQUAL( p1, bl::fs::path( "" ) );
        UTF_REQUIRE_EQUAL( p2, bl::fs::path( "" ) );

        UTF_REQUIRE_EQUAL( obj1 -> value(), bl::fs::path( "bar" ) );
        UTF_REQUIRE_EQUAL( obj2 -> value(), bl::fs::path( "baz" ) );
    }

}

/************************************************************************
 * EndpointSelector tests
 */

UTF_AUTO_TEST_CASE( BaseLib_SimpleEndpointSelectorImplTests )
{
    {
        const auto maxRetryCount = bl::EndpointCircularIterator::DEFAULT_MAX_RETRY_COUNT / 2;
        const auto retryTimeout =
            time::seconds( bl::EndpointCircularIterator::DEFAULT_RETRY_TIMEOUT_IN_SECONDS / 2 );

        const auto selector =
            bl::SimpleEndpointSelectorImpl::createInstance< bl::EndpointSelector >(
            "my.host.com",
            1234,
            maxRetryCount,
            retryTimeout
            );

        UTF_REQUIRE_EQUAL( selector -> count(), 1U );

        const auto iterator = selector -> createIterator();

        UTF_REQUIRE_EQUAL( iterator -> maxRetryCount(), maxRetryCount );
        UTF_REQUIRE_EQUAL( iterator -> retryTimeout(), retryTimeout );
    }

    const auto selector =
        bl::SimpleEndpointSelectorImpl::createInstance< bl::EndpointSelector >( "my.host.com", 1234 );
    UTF_REQUIRE_EQUAL( selector -> count(), 1U );

    {
        const auto iterator = selector -> createIterator();

        UTF_REQUIRE_EQUAL(
            iterator -> maxRetryCount(),
            bl::EndpointCircularIterator::DEFAULT_MAX_RETRY_COUNT
            );

        UTF_REQUIRE_EQUAL(
            iterator -> retryTimeout(),
            time::seconds( bl::EndpointCircularIterator::DEFAULT_RETRY_TIMEOUT_IN_SECONDS )
            );

        UTF_REQUIRE_EQUAL( iterator -> host(), "my.host.com" );
        UTF_REQUIRE_EQUAL( iterator -> port(), 1234 );
        UTF_REQUIRE_EQUAL( iterator -> count(), 1U );

        UTF_REQUIRE( iterator -> canRetry() );
        UTF_REQUIRE( iterator -> canRetryNow() );

        /*
         * The loop must run past the exhaustion threshold, otherwise the else block
         * below is never executed
         */

        for( std::size_t i = 0; i < ( iterator -> maxRetryCount() + 2 ); ++i )
        {
            if( i < ( iterator -> maxRetryCount() - 1 ) )
            {
                UTF_REQUIRE( iterator -> selectNext() );
                UTF_REQUIRE( iterator -> canRetry() );
            }
            else
            {
                UTF_REQUIRE( ! iterator -> selectNext() );
                UTF_REQUIRE( ! iterator -> canRetry() );
                UTF_REQUIRE( ! iterator -> canRetryNow() );
            }

            UTF_REQUIRE_EQUAL( iterator -> host(), "my.host.com" );
            UTF_REQUIRE_EQUAL( iterator -> port(), 1234 );
        }

        /*
         * The iterator is exhausted now; resetRetry() must make it usable again and it
         * must also clear the retry time gate - otherwise a transfer which reconnected
         * successfully would remain gated by the retry timeout forever
         */

        UTF_REQUIRE( ! iterator -> canRetry() );

        iterator -> resetRetry();

        UTF_REQUIRE( iterator -> canRetry() );
        UTF_REQUIRE( iterator -> canRetryNow() );
        UTF_REQUIRE( iterator -> selectNext() );

        UTF_REQUIRE_EQUAL( iterator -> count(), 1U );
    }

    {
        const auto iterator = selector -> createIterator();

        bl::time::time_duration timeout;

        UTF_REQUIRE( iterator -> canRetry() );
        UTF_REQUIRE( iterator -> canRetryNow( &timeout ) );
        UTF_REQUIRE_EQUAL( timeout, bl::time::milliseconds( 0 ) );

        UTF_REQUIRE( ! iterator -> canRetryNow( &timeout ) );
        UTF_REQUIRE( timeout != bl::time::milliseconds( 0 ) );
        UTF_REQUIRE( ! timeout.is_special() );

        /*
         * TODO: turn off part of the test which is fragile
         *
         * It will be re-enabled once we figure a better way
         * to do this
         */

        /*
        os::sleep( timeout );

        UTF_REQUIRE( iterator -> canRetryNow( &timeout ) );
        UTF_REQUIRE_EQUAL( timeout, time::milliseconds( 0 ) );
        UTF_REQUIRE( ! iterator -> canRetryNow( &timeout ) );
        */
    }
}

/************************************************************************
 * EndpointSelectorImpl tests
 */

UTF_AUTO_TEST_CASE( BaseLib_EndpointSelectorImplTests )
{
    /*
     * Just use our real cassandra nodes dev cluster
     */

    const char* hosts[] =
    {
        "host1.domain.net",
        "host2.domain.net",
        "host3.domain.net",
        "host4.domain.net",
    };

    const auto cb = [ &hosts ]( SAA_in const bl::om::ObjPtr< bl::EndpointSelectorImpl >& selector ) -> void
    {
        {
            UTF_REQUIRE_EQUAL( selector -> count(), 4U );

            const auto iterator = selector -> createIterator();
            UTF_REQUIRE_EQUAL( iterator -> count(), 4U );


            /*
             * The iteration count must exceed the exhaustion threshold, which is
             * BL_ARRAY_SIZE( hosts ) * ( maxRetryCount() - 1 ), otherwise the else block
             * below is never executed
             */

            const std::size_t iterationsCount =
                BL_ARRAY_SIZE( hosts ) * ( iterator -> maxRetryCount() + 2 );

            for( std::size_t i = 0; i < iterationsCount; ++i )
            {
                UTF_REQUIRE_EQUAL( iterator -> host(), hosts[ i % BL_ARRAY_SIZE( hosts ) ] );
                UTF_REQUIRE_EQUAL( iterator -> port(), 1234 );

                const auto maxIterations =
                    BL_ARRAY_SIZE( hosts ) * ( iterator -> maxRetryCount() - 1 );

                if( i < maxIterations )
                {
                    UTF_REQUIRE( iterator -> selectNext() );
                    UTF_REQUIRE( iterator -> canRetry() );
                }
                else
                {
                    UTF_REQUIRE( ! iterator -> selectNext() );
                    UTF_REQUIRE( ! iterator -> canRetry() );
                    UTF_REQUIRE( ! iterator -> canRetryNow() );
                }
            }

            /*
             * The iterator is exhausted now; resetRetry() must zero the retry counters
             * *and* clear the retry time gate, but it must not rewind the index - that
             * would silently re-pin the endpoint which has just failed
             */

            UTF_REQUIRE( ! iterator -> canRetry() );

            iterator -> resetRetry();

            UTF_REQUIRE( iterator -> canRetry() );
            UTF_REQUIRE( iterator -> canRetryNow() );
            UTF_REQUIRE( iterator -> selectNext() );

            UTF_REQUIRE_EQUAL(
                iterator -> host(),
                hosts[ ( iterationsCount + 1 ) % BL_ARRAY_SIZE( hosts ) ]
                );
        }

        {
            const auto iterator = selector -> createIterator();

            bl::time::time_duration timeout;

            UTF_REQUIRE( iterator -> canRetry() );
            UTF_REQUIRE( iterator -> canRetryNow( &timeout ) );
            UTF_REQUIRE_EQUAL( timeout, bl::time::milliseconds( 0 ) );

            UTF_REQUIRE( ! iterator -> canRetryNow( &timeout ) );
            UTF_REQUIRE( timeout != bl::time::milliseconds( 0 ) );
            UTF_REQUIRE( ! timeout.is_special() );

            /*
             * TODO: turn off part of the test which is fragile
             *
             * It will be re-enabled once we figure a better way
             * to do this
             */

            /*
            os::sleep( timeout );

            UTF_REQUIRE( iterator -> canRetryNow( &timeout ) );
            UTF_REQUIRE_EQUAL( timeout, time::milliseconds( 0 ) );
            UTF_REQUIRE( ! iterator -> canRetryNow( &timeout ) );
            */
        }
    };

    {
        const auto selector = bl::EndpointSelectorImpl::createInstance( 1234 );
        UTF_REQUIRE_EQUAL( selector -> count(), 0U );

        /*
         * An empty selector cannot shell out an iterator - chkIndex() in the iterator
         * constructor is what prevents an out of range read on the entries vector
         */

        UTF_REQUIRE_THROW_MESSAGE(
            selector -> createIterator(),
            bl::UnexpectedException,
            "Endpoint selector is empty"
            );

        for( std::size_t i = 0; i < BL_ARRAY_SIZE( hosts ); ++i )
        {
            selector -> addHost( hosts[ i ] );
        }

        cb( selector );
    }

    {
        const auto selector = bl::EndpointSelectorImpl::createInstance(
            1234,
            hosts,
            hosts + BL_ARRAY_SIZE( hosts )
            );
        UTF_REQUIRE_EQUAL( selector -> count(), 4U );

        cb( selector );
    }

    {
        std::vector< std::string > v( hosts, hosts + BL_ARRAY_SIZE( hosts ) );

        const auto selector = bl::EndpointSelectorImpl::createInstance(
            1234,
            v.begin(),
            v.end()
            );
        UTF_REQUIRE_EQUAL( selector -> count(), 4U );

        cb( selector );
    }
}

/************************************************************************
 * UuidIteratorImpl tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TestUuidIteratorImpl )
{
    std::set< bl::uuid_t > unique;

    auto pv = bl::bo::uuid_vector::createInstance();

    pv -> lvalue().push_back( bl::uuids::create() );
    pv -> lvalue().push_back( bl::uuids::create() );
    pv -> lvalue().push_back( bl::uuids::create() );

    const auto single = bl::uuids::create();
    const auto beginSingle = &single;

    const auto i1 = bl::UuidIteratorImpl::createInstance< bl::UuidIterator >( beginSingle, beginSingle + 1 );

    const auto i2 = bl::UuidIteratorImpl::createInstance< bl::UuidIterator >(
        pv -> value(),
        bl::om::qi< bl::om::Object >( pv )
        );

    /*
     * Reset vector to ensure the iterator holds a reference to it
     */

    pv.reset();

    UTF_REQUIRE( i1 -> hasCurrent() );
    UTF_REQUIRE( unique.insert( i1 -> current() ).second );
    i1 -> loadNext();
    UTF_REQUIRE( ! i1 -> hasCurrent() );

    /*
     * The out of range guard in loadNext() is unreachable from a
     * for( ; hasCurrent(); loadNext() ) traversal, which is the only shape used anywhere
     * else - without it m_pos would walk past m_end, hasCurrent() would stay true and
     * every consumer would loop indefinitely over out of bounds memory
     */

    UTF_REQUIRE_THROW( i1 -> loadNext(), bl::UnexpectedException );

    i1 -> reset();

    UTF_REQUIRE( i1 -> hasCurrent() );
    UTF_REQUIRE_EQUAL( i1 -> current(), single );

    {
        /*
         * An empty pointer pair range
         */

        const auto empty = bl::UuidIteratorImpl::createInstance< bl::UuidIterator >( beginSingle, beginSingle );

        UTF_REQUIRE( ! empty -> hasCurrent() );
        UTF_REQUIRE_THROW( empty -> loadNext(), bl::UnexpectedException );

        empty -> reset();

        UTF_REQUIRE( ! empty -> hasCurrent() );
    }

    {
        /*
         * The std::vector overload with an empty vector - this exercises the
         * data.empty() ? nullptr : &data.front() branch, which a naive &data.front()
         * rewrite would turn into undefined behavior
         */

        const std::vector< bl::uuid_t > none;

        const auto empty = bl::UuidIteratorImpl::createInstance< bl::UuidIterator >( none );

        UTF_REQUIRE( ! empty -> hasCurrent() );
        UTF_REQUIRE_THROW( empty -> loadNext(), bl::UnexpectedException );

        empty -> reset();

        UTF_REQUIRE( ! empty -> hasCurrent() );
    }

    UTF_REQUIRE( i2 -> hasCurrent() );
    i2 -> loadNext();
    UTF_REQUIRE( i2 -> hasCurrent() );
    i2 -> loadNext();
    UTF_REQUIRE( i2 -> hasCurrent() );
    i2 -> loadNext();
    UTF_REQUIRE( ! i2 -> hasCurrent() );

    i2 -> reset();
    UTF_REQUIRE( i2 -> hasCurrent() );

    for( ; i2 -> hasCurrent(); i2 -> loadNext() )
    {
        UTF_REQUIRE( unique.insert( i2 -> current() ).second );
    }

    UTF_REQUIRE_EQUAL( ( std::size_t ) 4, unique.size() );
}

/************************************************************************
 * ScopeGuard tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TestScopeGuard )
{
    {
        bool called1 = false;
        bool called2 = false;

        {
            auto g1 = BL_SCOPE_GUARD( { called1 = true; } );
            auto g2 = BL_SCOPE_GUARD( { called2 = true; } );

            UTF_REQUIRE( ! called1 );
            UTF_REQUIRE( ! called2 );

            g2.dismiss();
        }

        UTF_REQUIRE( called1 );
        UTF_REQUIRE( ! called2 );
    }

    {
        bool called1 = false;
        bool called2 = false;

        {
            BL_SCOPE_EXIT( { called1 = true; } );
            BL_SCOPE_EXIT( { called2 = true; } );

            UTF_REQUIRE( ! called1 );
            UTF_REQUIRE( ! called2 );
        }

        UTF_REQUIRE( called1 );
        UTF_REQUIRE( called2 );
    }

    /*
     * runNow() disables the guard before it invokes the callback, so the destructor
     * which follows cannot run the callback a second time
     */

    {
        int count = 0;

        {
            auto g = BL_SCOPE_GUARD( { ++count; } );

            g.runNow();

            UTF_REQUIRE_EQUAL( 1, count );
        }

        UTF_REQUIRE_EQUAL( 1, count );
    }

    /*
     * The move constructor dismisses the source, so responsibility for the callback
     * transfers exactly once - ScopeGuard::create returns by value, so every
     * BL_SCOPE_GUARD goes through this path
     */

    {
        int count = 0;

        {
            auto g1 = BL_SCOPE_GUARD( { ++count; } );

            {
                auto g2( std::move( g1 ) );

                UTF_REQUIRE_EQUAL( 0, count );
            }

            UTF_REQUIRE_EQUAL( 1, count );
        }

        UTF_REQUIRE_EQUAL( 1, count );
    }

    /*
     * Move assignment runs the cleanup the target is still holding before it takes over the
     * source's - an armed guard always runs exactly once, which is the same rule the
     * destructor implements
     */

    {
        int a = 0;
        int b = 0;

        {
            auto g1 = BL_SCOPE_GUARD( { ++a; } );
            auto g2 = BL_SCOPE_GUARD( { ++b; } );

            g1 = std::move( g2 );

            UTF_REQUIRE_EQUAL( 1, a );
            UTF_REQUIRE_EQUAL( 0, b );
        }

        UTF_REQUIRE_EQUAL( 1, a );
        UTF_REQUIRE_EQUAL( 1, b );
    }

    /*
     * A dismissed target has nothing to run, and self assignment is a no-op rather than a
     * cleanup which fires while the guard is still armed
     */

    {
        int a = 0;
        int b = 0;

        {
            auto g1 = BL_SCOPE_GUARD( { ++a; } );
            auto g2 = BL_SCOPE_GUARD( { ++b; } );

            g1.dismiss();

            g1 = std::move( g2 );

            UTF_REQUIRE_EQUAL( 0, a );
            UTF_REQUIRE_EQUAL( 0, b );
        }

        UTF_REQUIRE_EQUAL( 0, a );
        UTF_REQUIRE_EQUAL( 1, b );
    }

    {
        int count = 0;

        {
            auto g = BL_SCOPE_GUARD( { ++count; } );

            /*
             * Through a pointer, so the compiler's own self-move diagnostic does not reject
             * the very expression under test
             */

            auto* const self = &g;

            g = std::move( *self );

            UTF_REQUIRE_EQUAL( 0, count );
        }

        UTF_REQUIRE_EQUAL( 1, count );
    }
}

/************************************************************************
 * FsUtils tests
 */

UTF_AUTO_TEST_CASE( FsUtils_TestMakeHidden )
{
    bl::fs::path tmpPath;
    BL_SCOPE_EXIT( { bl::fs::safeDeletePathNothrow( tmpPath ); } );

    bl::fs::path pathSave;

    bl::fs::createTempDir( tmpPath );
    pathSave = tmpPath;

    tmpPath = bl::fs::makeHidden( tmpPath );
    UTF_REQUIRE( pathSave != tmpPath );
    UTF_REQUIRE( 0U == tmpPath.filename().string().find( "." ) );
    pathSave = tmpPath;

    tmpPath = bl::fs::makeHidden( tmpPath );
    UTF_REQUIRE( pathSave == tmpPath );

    if( bl::os::onWindows() )
    {
        UTF_REQUIRE( bl::fs::safeGetFileAttributes( tmpPath ) & bl::os::FileAttributeHidden );
    }

    UTF_REQUIRE_THROW( bl::fs::makeHidden( tmpPath / ".non-existent" ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( bl::fs::makeHidden( "." ), bl::UnexpectedException );
    UTF_REQUIRE_THROW( bl::fs::makeHidden( ".." ), bl::UnexpectedException );

    bl::fs::safeUpdateFileAttributes( tmpPath, bl::os::FileAttributeHidden, true /* remove */ );

    if( bl::os::onWindows() )
    {
        UTF_REQUIRE( ! ( bl::fs::safeGetFileAttributes( tmpPath ) & bl::os::FileAttributeHidden ) );
    }

    bl::fs::safeUpdateFileAttributes( tmpPath, bl::os::FileAttributeHidden, true /* remove */ );

    bl::fs::safeUpdateFileAttributes( tmpPath, bl::os::FileAttributeHidden, false /* remove */ );

    if( bl::os::onWindows() )
    {
        UTF_REQUIRE( bl::fs::safeGetFileAttributes( tmpPath ) & bl::os::FileAttributeHidden );
    }

    bl::fs::safeUpdateFileAttributes( tmpPath, bl::os::FileAttributeHidden, false /* remove */ );

    if( bl::os::onWindows() )
    {
        /*
         * os::unsafe::updateFileAttributes rejects a zero attribute set and any bit
         * outside FileAttributesMask before it reaches SetFileAttributesW, and reports a
         * missing path as a system error rather than silently doing nothing
         *
         * On UNIX the implementation is an empty function (OSImplUNIX.h), which is why
         * this is Windows only - it still has to compile everywhere
         *
         * Note the exception types are those the production macros actually throw:
         * BL_CHK gives UnexpectedException, not ArgumentException, and the
         * GetFileAttributesW failure comes through createException( ... ) as a
         * SystemException
         */

        UTF_REQUIRE_THROW(
            bl::os::unsafe::updateFileAttributes( tmpPath, bl::os::FileAttributeNone ),
            bl::UnexpectedException
            );

        UTF_REQUIRE_THROW(
            bl::os::unsafe::updateFileAttributes(
                tmpPath,
                static_cast< bl::os::FileAttributes >( ~static_cast< std::uint32_t >( bl::os::FileAttributesMask ) )
                ),
            bl::UnexpectedException
            );

        UTF_REQUIRE_THROW(
            bl::os::unsafe::updateFileAttributes( tmpPath / "no-such-file", bl::os::FileAttributeHidden ),
            bl::SystemException
            );

        /*
         * The positive control - without it the three rejections above could all pass
         * vacuously if the API stopped working altogether
         */

        bl::os::unsafe::updateFileAttributes( tmpPath, bl::os::FileAttributeHidden, false /* remove */ );

        UTF_REQUIRE( bl::fs::safeGetFileAttributes( tmpPath ) & bl::os::FileAttributeHidden );
    }
}

UTF_AUTO_TEST_CASE( FsUtils_TestCreateTempDirAndCreateDirectory )
{
    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        UTF_REQUIRE( bl::fs::exists( tmpDir.path() ) );

        for( std::size_t i = 0; i < 2000; i++ )
        {
            bl::fs::safeCreateDirectory( tmpDir.path() );
        }
    }

    UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );

    /*
     * The TmpDirT( rootTemp ) branch - every other TmpDir in the suite is default
     * constructed, so this branch has no coverage at all
     *
     * A non empty rootTemp creates rootTemp / ( "bl-temp-dir-" + uuid ) with
     * safeCreateDirectory and NOT safeMkdirs, so rootTemp itself must already exist; the
     * result then goes through makeHidden, which renames it to a leading dot name on
     * every platform
     */

    {
        bl::fs::TmpDir outer;

        bl::fs::path savedInnerPath;

        {
            bl::fs::TmpDir inner( outer.path() );

            savedInnerPath = inner.path();

            UTF_REQUIRE( bl::fs::is_directory( inner.path() ) );
            UTF_REQUIRE_EQUAL( inner.path().parent_path(), outer.path() );
            UTF_REQUIRE( 0U == inner.path().filename().string().find( "." ) );
        }

        /*
         * Only the inner directory is removed - the caller supplied root survives
         */

        UTF_REQUIRE( ! bl::fs::path_exists( savedInnerPath ) );
        UTF_REQUIRE( bl::fs::is_directory( outer.path() ) );

        /*
         * A missing root is not created for the caller - safeCreateDirectory reports it
         * through BL_CHK_EC_USER_FRIENDLY, which throws SystemException
         */

        UTF_REQUIRE_THROW( bl::fs::TmpDir( outer.path() / "no-such-root" ), bl::SystemException );
    }
}

UTF_AUTO_TEST_CASE( FsUtils_TestMkdirs )
{
    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        UTF_REQUIRE( bl::fs::exists( tmpPath ) );

        /*
         * Just attempt to create a lot of directories in parallel
         */

        bl::tasks::scheduleAndExecuteInParallel(
            [ &tmpPath ]( SAA_in const bl::om::ObjPtr< bl::tasks::ExecutionQueue >& eq ) -> void
            {
                const auto cb = []( SAA_in const bl::om::ObjPtrCopyable< bl::bo::path >& nestedDirs ) -> void
                {
                    bl::fs::safeMkdirs( nestedDirs -> value() );
                    UTF_REQUIRE( bl::fs::exists( nestedDirs -> value() ) );

                    bl::fs::safeMkdirs( nestedDirs -> value() );
                    UTF_REQUIRE( bl::fs::exists( nestedDirs -> value() ) );
                };

                std::set< std::string > allPaths;

                for( std::size_t i = 0; i < 500; i++ )
                {
                    auto nestedDirs =
                        tmpPath /
                        ( "foo" + bl::utils::lexical_cast< std::string >( i % 5 ) ) /
                        ( "bar" + bl::utils::lexical_cast< std::string >( i % 5 ) ) /
                        ( "baz" + bl::utils::lexical_cast< std::string >( i % 20 ) );

                    const auto pair = allPaths.insert( nestedDirs.string() );

                    if( pair.second )
                    {
                        /*
                         * This is a new path; it should not exists
                         */

                        UTF_REQUIRE( ! bl::fs::exists( nestedDirs ) );
                    }

                    bl::om::ObjPtrCopyable< bl::bo::path > pathObj( bl::bo::path::createInstance() );

                    pathObj -> lvalue().swap( nestedDirs );

                    eq -> push_back( bl::cpp::bind< void >( cb, pathObj ) );
                    eq -> push_back( bl::cpp::bind< void >( cb, pathObj ) );
                    eq -> push_back( bl::cpp::bind< void >( cb, pathObj ) );
                }

                eq -> flush();

                BL_LOG(
                    bl::Logging::debug(),
                    BL_MSG()
                        << "Number of unique paths created: "
                        << allPaths.size()
                    );
            }
            );
    }
}

UTF_AUTO_TEST_CASE( FsUtils_JunctionsTests )
{
    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        const auto subDir = tmpPath / "sub-dir";
        const auto junctionDir = tmpPath / "junction-dir";
        const auto junctionDir2 = tmpPath / "junction-dir2";
        const auto filePath = subDir / "file.txt";
        const auto filePathViaJunction = junctionDir / "file.txt";

        bl::fs::ensurePathDoesNotExist( subDir );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::ensurePathExists( subDir ),
            bl::UnexpectedException,
            " does not exist"
            );

        bl::fs::safeMkdirs( subDir );

        bl::fs::ensurePathExists( subDir );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::fs::ensurePathDoesNotExist( subDir ),
            bl::UnexpectedException,
            " already exists"
            );

        UTF_REQUIRE( ! bl::fs::isDirectoryJunction( subDir ) );
        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir ), bl::UnexpectedException )
        bl::fs::createDirectoryJunction( subDir, junctionDir );
        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir ) );

        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir2 ), bl::UnexpectedException )
        bl::fs::createDirectoryJunction( subDir, junctionDir2 );
        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir2 ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir2 ) );

        {
            const auto file = bl::os::fopen( filePath, "wb" );
            UTF_REQUIRE( bl::fs::exists( filePath ) );
        }

        UTF_REQUIRE( bl::fs::exists( filePathViaJunction ) );

        bl::fs::deleteDirectoryJunction( junctionDir );
        UTF_REQUIRE( ! bl::fs::exists( junctionDir ) );
        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir ), bl::UnexpectedException )

        UTF_REQUIRE( ! bl::fs::exists( filePathViaJunction ) );
        UTF_REQUIRE( bl::fs::exists( filePath ) );

        /*
         * Now delete the directory to test the broken junction case
         */

        bl::eh::error_code ec;

        UTF_REQUIRE( bl::fs::path_exists( junctionDir2 ) );
        ec.clear();
        UTF_REQUIRE( bl::fs::path_exists( junctionDir2, ec ) );
        UTF_REQUIRE( ! ec );

        bl::fs::safeDeletePathNothrow( subDir );
        UTF_REQUIRE( ! bl::fs::exists( filePath ) );

        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir2 ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir2 ) );

        UTF_REQUIRE( bl::fs::path_exists( junctionDir2 ) );
        ec.clear();
        UTF_REQUIRE( bl::fs::path_exists( junctionDir2, ec ) );
        UTF_REQUIRE( ! ec );

        bl::fs::deleteDirectoryJunction( junctionDir2 );
        UTF_REQUIRE( ! bl::fs::exists( junctionDir2 ) );
        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir2 ), bl::UnexpectedException )

        UTF_REQUIRE( ! bl::fs::path_exists( junctionDir2 ) );
        ec.clear();
        UTF_REQUIRE( ! bl::fs::path_exists( junctionDir2, ec ) );
        UTF_REQUIRE( ec );
    }

    UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );
}

UTF_AUTO_TEST_CASE( FsUtils_TestCopyErrorCodeOverload )
{
    bl::fs::TmpDir tmpDir;

    const auto restrictedDir = tmpDir.path() / "restricted";
    const auto sourcePath = restrictedDir / "source.txt";
    const auto targetPath = tmpDir.path() / "target.txt";

    bl::fs::safeMkdirs( restrictedDir );

    {
        const auto file = bl::os::fopen( sourcePath, "wb" );
    }

    /*
     * Make the parent directory inaccessible, so obtaining the status of the source path
     * fails with a real error
     *
     * Note that a merely missing path is not an error for the filesystem status APIs, so
     * it would not exercise the non-throwing contract of the error code overload here
     */

    bl::fs::permissions( restrictedDir, bl::fs::perms::no_perms );

    bool statusQueryThrows = false;

    try
    {
        ( void ) bl::fs::is_directory( sourcePath );
    }
    catch( std::exception& )
    {
        statusQueryThrows = true;
    }

    bl::eh::error_code ec;
    bool copyWithErrorCodeThrows = false;

    if( statusQueryThrows )
    {
        try
        {
            bl::fs::copy( sourcePath, targetPath, ec );
        }
        catch( std::exception& )
        {
            copyWithErrorCodeThrows = true;
        }
    }

    /*
     * Restore the permissions before checking the results, so the temporary directory
     * can always be cleaned up
     */

    bl::fs::permissions( restrictedDir, bl::fs::perms::owner_all );

    if( statusQueryThrows )
    {
        /*
         * The platform enforces the restriction, so the error code overload must have
         * reported the failure via 'ec' instead of throwing
         */

        UTF_REQUIRE( ! copyWithErrorCodeThrows );
        UTF_REQUIRE( ec );
        UTF_REQUIRE( ! bl::fs::path_exists( targetPath ) );
    }

    /*
     * The error code overload must also succeed and leave 'ec' clear for a valid source
     */

    const auto validSourcePath = tmpDir.path() / "source-dir";
    const auto validTargetPath = tmpDir.path() / "target-dir";

    bl::fs::safeMkdirs( validSourcePath );

    ec.clear();

    UTF_CHECK_NO_THROW( bl::fs::copy( validSourcePath, validTargetPath, ec ) );

    UTF_REQUIRE( ! ec );
    UTF_REQUIRE( bl::fs::is_directory( validTargetPath ) );
}

UTF_AUTO_TEST_CASE( FsUtils_TestSafeRemove )
{
    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        const auto subDir = tmpPath / "sub-dir";
        const auto junctionDir = tmpPath / "junction-dir";
        const auto junctionDir2 = tmpPath / "junction-dir2";
        const auto filePath = subDir / "file.txt";
        const auto filePathViaJunction = junctionDir / "file.txt";

        bl::fs::safeRemoveIfExists( subDir );
        bl::fs::safeRemoveIfExists( filePathViaJunction );
        BL_CHK_EC_NM( bl::fs::trySafeRemoveIfExists( subDir ) );
        BL_CHK_EC_NM( bl::fs::trySafeRemoveIfExists( filePathViaJunction ) );

        bl::fs::safeMkdirs( subDir );

        UTF_REQUIRE( ! bl::fs::isDirectoryJunction( subDir ) );
        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir ), bl::UnexpectedException )
        bl::fs::createDirectoryJunction( subDir, junctionDir );
        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir ) );

        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir2 ), bl::UnexpectedException )
        bl::fs::createDirectoryJunction( subDir, junctionDir2 );
        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir2 ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir2 ) );

        {
            const auto file = bl::os::fopen( filePath, "wb" );
            UTF_REQUIRE( bl::fs::exists( filePath ) );
            UTF_REQUIRE( bl::fs::exists( filePathViaJunction ) );
        }

        bl::fs::safeRemove( filePathViaJunction );
        UTF_REQUIRE( ! bl::fs::exists( filePath ) );
        UTF_REQUIRE( ! bl::fs::exists( filePathViaJunction ) );

        bl::fs::safeRemove( subDir );
        UTF_REQUIRE( ! bl::fs::exists( subDir ) );
        UTF_REQUIRE( ! bl::fs::exists( junctionDir ) );
        UTF_REQUIRE( ! bl::fs::exists( junctionDir2 ) );
        UTF_REQUIRE( bl::fs::path_exists( junctionDir ) );
        UTF_REQUIRE( bl::fs::path_exists( junctionDir2 ) );

        bl::fs::safeRemove( junctionDir );
        UTF_REQUIRE( ! bl::fs::path_exists( junctionDir ) );
        UTF_REQUIRE( bl::fs::path_exists( junctionDir2 ) );
        UTF_REQUIRE( bl::fs::exists( tmpPath ) );

        bl::fs::safeRemove( junctionDir2 );
        UTF_REQUIRE( ! bl::fs::path_exists( junctionDir ) );
        UTF_REQUIRE( ! bl::fs::path_exists( junctionDir2 ) );
        UTF_REQUIRE( bl::fs::exists( tmpPath ) );
    }

    UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );
}

UTF_AUTO_TEST_CASE( FsUtils_TestSafeRemoveAll )
{
    bl::fs::path tmpPath;

    {
        bl::fs::TmpDir tmpDir;
        tmpPath = tmpDir.path();

        const auto subDir = tmpPath / "sub-dir";
        const auto subDir2 = subDir / "sub-dir2";
        const auto junctionDir = tmpPath / "junction-dir";
        const auto junctionDir2 = tmpPath / "junction-dir2";
        const auto filePath = subDir / "file.txt";
        const auto filePathViaJunction = junctionDir / "file.txt";

        bl::fs::safeRemoveAllIfExists( subDir );
        bl::fs::safeRemoveAllIfExists( filePathViaJunction );
        BL_CHK_EC_NM( bl::fs::trySafeRemoveAllIfExists( subDir ) );
        BL_CHK_EC_NM( bl::fs::trySafeRemoveAllIfExists( filePathViaJunction ) );

        bl::fs::safeMkdirs( subDir );
        bl::fs::safeMkdirs( subDir2 );

        UTF_REQUIRE( ! bl::fs::isDirectoryJunction( subDir ) );
        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir ), bl::UnexpectedException )
        bl::fs::createDirectoryJunction( subDir, junctionDir );
        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir ) );

        UTF_CHECK_THROW( bl::fs::isDirectoryJunction( junctionDir2 ), bl::UnexpectedException )
        bl::fs::createDirectoryJunction( subDir, junctionDir2 );
        UTF_REQUIRE( bl::fs::isDirectoryJunction( junctionDir2 ) );
        UTF_REQUIRE_EQUAL( subDir, bl::fs::getDirectoryJunctionTarget( junctionDir2 ) );

        {
            const auto file = bl::os::fopen( filePath, "wb" );
            UTF_REQUIRE( bl::fs::exists( filePath ) );
            UTF_REQUIRE( bl::fs::exists( filePathViaJunction ) );
        }

        bl::fs::safeRemoveAll( tmpPath );
        UTF_REQUIRE( ! bl::fs::exists( tmpPath ) );
    }
}

#define UTF_TEST_NORMALIZE( input, expected )                   \
    {                                                           \
        const auto normalized( bl::fs::normalize( input ) );    \
                                                                \
        BL_LOG(                                                 \
            bl::Logging::debug(),                               \
            BL_MSG()                                            \
                << "input: "                                    \
                << input                                        \
                << "\nnormalized: "                             \
                << normalized                                   \
                << "\nexpected: "                               \
                << expected                                     \
            );                                                  \
                                                                \
        UTF_CHECK( bl::fs::path( expected ).compare( normalized ) == 0 ); \
    }

UTF_AUTO_TEST_CASE( FsUtils_TestNormalize )
{
    /*
     * absolute path
     */

    UTF_TEST_NORMALIZE(
        bl::fs::absolute( test::UtfArgsParser::argv0() ),
        bl::fs::normalize( test::UtfArgsParser::argv0() )
        );

    /*
     * absolute non-existent path
     */

    UTF_TEST_NORMALIZE(
        bl::fs::absolute( bl::fs::path( test::UtfArgsParser::argv0() ) / "foobaztestfoofaz" ),
        bl::fs::normalize( bl::fs::path( test::UtfArgsParser::argv0() ) / "foobaztestfoofaz" )
        );

    /*
     * relative path
     */

    UTF_TEST_NORMALIZE(
        bl::fs::path( test::UtfArgsParser::argv0() ).filename(),
        bl::fs::path( bl::fs::current_path() / ( bl::fs::path( test::UtfArgsParser::argv0() ).filename() ) )
        );

    /*
     * relative non-existent path
     */

    UTF_TEST_NORMALIZE(
        "foobaztestfoofaz/foobaztestfoofaz",
        bl::fs::normalize( bl::fs::path( "foobaztestfoofaz" ) / "foobaztestfoofaz" )
        );

    /*
     * single dotted non-existent path
     */

    UTF_TEST_NORMALIZE(
        "./foobaztestfoofaz",
        bl::fs::normalize( bl::fs::path( "." ) ) / "foobaztestfoofaz"
        );

    /*
     * twice dotted non-existent path
     */

    UTF_TEST_NORMALIZE(
        "../foobaztestfoofaz",
        bl::fs::normalize( bl::fs::path( ".." ) ) / "foobaztestfoofaz"
        );

    /*
     * twice dotted non-existent path
     */

    UTF_TEST_NORMALIZE(
        "./foobaztestfoofaz/../foobaztestfoofaz2",
        bl::fs::normalize( bl::fs::path( "foobaztestfoofaz2" ) )
        );

    if( bl::os::onUNIX() )
    {
        /*
         * escaped paths
         */

        UTF_TEST_NORMALIZE(
            bl::fs::normalize( "\\foobaztestfoofaz1\\foobaztestfoofaz2" ),
            bl::fs::normalize( bl::fs::path( "\\foobaztestfoofaz1\\foobaztestfoofaz2" ) )
            );

        /*
         * Windows-style UNC/LFN paths are not supported on Unix
         */

        UTF_CHECK_THROW(
            bl::fs::normalize( "\\\\host\\directoryname" ),
            bl::ArgumentException
            );

        /*
         * A parent directory reference must be clamped at the root - POSIX defines
         * "/.." as "/", so the expectations below are spelled out as literals rather
         * than computed by calling bl::fs::normalize( ... ) again
         *
         * Without the clamp "/.." becomes an empty path and everything appended to it
         * turns the result into a current directory relative path
         */

        UTF_TEST_NORMALIZE(
            "/../etc/passwd",
            bl::fs::path( "/etc/passwd" )
            );

        UTF_TEST_NORMALIZE(
            "/..",
            bl::fs::path( "/" )
            );

        UTF_TEST_NORMALIZE(
            "/../../..",
            bl::fs::path( "/" )
            );

        UTF_TEST_NORMALIZE(
            "/a/../../b",
            bl::fs::path( "/b" )
            );

        UTF_TEST_NORMALIZE(
            "/a/./b/../c",
            bl::fs::path( "/a/c" )
            );

        UTF_REQUIRE( bl::fs::normalize( "/../etc/passwd" ).is_absolute() );

        /*
         * normalizePathCliParameter( ... ) resolves ".." through normalize( ... ); there
         * is no LFN prefix to remove on this platform
         */

        UTF_REQUIRE_EQUAL( bl::fs::normalizePathCliParameter( "/a/b/../c" ), std::string( "/a/c" ) );
    }

    if( bl::os::onWindows() )
    {
        /*
         * using regular slashes
         */

        UTF_TEST_NORMALIZE(
            std::string( "foobaztestfoofaz1/foobaztestfoofaz2/foobaztestfoofaz3" ),
            bl::fs::absolute( bl::fs::path( "foobaztestfoofaz1\\foobaztestfoofaz2\\foobaztestfoofaz3" ) )
            );

        /*
         * UNC path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\host\\directoryname" ),
            bl::fs::path( "\\\\?\\UNC\\host\\directoryname" )
            );

        /*
         * UNC dotted path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\host\\directoryname\\." ),
            bl::fs::path( "\\\\?\\UNC\\host\\directoryname" )
            );

        /*
         * UNC twice dotted path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\host\\directoryname\\.." ),
            bl::fs::path( "\\\\?\\UNC\\host" )
            );

        /*
         * LFN path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\?\\D:\\very long path" ),
            bl::fs::path( "\\\\?\\D:\\very long path" )
            );

        /*
         * LFN path dotted path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\?\\D:\\very long path\\.\\another level" ),
            bl::fs::path( "\\\\?\\D:\\very long path\\another level" )
            );

        /*
         * LFN path twice dotted path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\?\\D:\\very long path\\..\\another level" ),
            bl::fs::path( "\\\\?\\D:\\another level" )
            );

        /*
         * LFN path multi-dotted path
         */

        UTF_TEST_NORMALIZE(
            std::string( "\\\\?\\D:\\very long path\\.\\another level\\..\\yet another level" ),
            bl::fs::path( "\\\\?\\D:\\very long path\\yet another level" )
            );

        /*
         * The parent directory reference is clamped at the root here too; note that
         * the first path element on Windows is the root *name* ( "C:" ) and only
         * becomes the root path once the root directory element is appended, which is
         * why the clamp compares against root_path() and not against the seed value
         */

        UTF_TEST_NORMALIZE(
            std::string( "C:\\..\\Windows" ),
            bl::fs::path( "C:\\Windows" )
            );

        UTF_TEST_NORMALIZE(
            std::string( "C:\\a\\..\\..\\b" ),
            bl::fs::path( "C:\\b" )
            );

        UTF_REQUIRE( bl::fs::normalize( "C:\\..\\Windows" ).is_absolute() );

        /*
         * bl::fs::path adds the \\?\ LFN prefix in its constructor on Windows, and
         * normalizePathCliParameter( ... ) exists precisely to take it back off again -
         * it is what JvmHelpers uses to build the JVM class path and the -D options, so a
         * change which stopped removing the prefix would hand the JVM \\?\C:\... and it
         * would fail to load
         */

        const auto lfnInput = bl::fs::path( "c:\\some\\dir" ).string();

        UTF_REQUIRE( 0 == lfnInput.find( "\\\\?\\" ) );

        const auto lfnResult = bl::fs::normalizePathCliParameter( lfnInput );

        UTF_REQUIRE( 0 != lfnResult.find( "\\\\?\\" ) );
        UTF_REQUIRE( bl::str::iends_with( lfnResult, "some\\dir" ) );
    }

    /*
     * normalizePathCliParameter( ... ) ground truth - every existing assertion about this
     * function ( utf_baselib_jni/TestJni.h:753/765/766 ) computes its expectation by
     * calling the function under test, so it holds for ANY implementation, including one
     * which returns the empty string
     */

    UTF_REQUIRE_EQUAL( bl::fs::normalizePathCliParameter( std::string() ), std::string() );

    {
        const auto cwd = bl::fs::current_path();

        const auto resolved = bl::fs::normalizePathCliParameter( "some/relative/leaf" );

        UTF_REQUIRE( bl::fs::path( resolved ).is_absolute() );

        UTF_REQUIRE(
            bl::fs::normalize( cwd / "some" / "relative" / "leaf" ).compare( bl::fs::path( resolved ) ) == 0
            );
    }
}

UTF_AUTO_TEST_CASE( FsUtils_TestCreateLockFile )
{
    bl::fs::TmpDir tmpDir;
    const auto tmpPath = tmpDir.path();

    const auto lockFile = tmpPath / "lockfile.lock";

    std::atomic< int > numCreated( 0 );

    std::vector< bl::os::thread > threads;

    for ( int i = 0; i < 10; ++i ) {
        threads.push_back(
            bl::os::thread([ & ]()
                {
                    if( bl::fs::createLockFile( lockFile.string() ) )
                    {
                        ++numCreated;
                    }
                })
            );
    }

    for( auto& thread : threads )
    {
        thread.join();
    }

    UTF_CHECK_EQUAL( 1, numCreated );

    UTF_REQUIRE( bl::fs::exists( lockFile ) );

    auto expectedFileContent = std::to_string( bl::os::getPid() );
    expectedFileContent += '\n';

    UTF_CHECK_EQUAL(
        expectedFileContent,
        bl::encoding::readTextFile( lockFile )
        );
}

/************************************************************************
 * StringUtils tests
 */

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsJoinFormattedTests )
{
    std::vector< std::string > empty;
    std::vector< std::string > vec;

    vec.push_back( "one" );
    vec.push_back( "two" );
    vec.push_back( "three" );
    vec.push_back( "four" );
    vec.push_back( "five" );

    UTF_CHECK_EQUAL( "", bl::str::joinFormatted( empty, " ", " " ) );

    UTF_CHECK_EQUAL( "onetwothreefourfive", bl::str::joinFormatted( vec, "", "" ) );
    UTF_CHECK_EQUAL( "one, two, three, four and five", bl::str::joinFormatted( vec, ", ", " and " ) );
    UTF_CHECK_EQUAL( "one\ntwo\nthree\nfour\nfive", bl::str::joinNewLineFormatted( vec ) );
    UTF_CHECK_EQUAL( "'one', 'two', 'three', 'four' and 'five'", bl::str::joinQuoteFormatted( vec ) );

    const auto formatter1 =
        [](
            SAA_in      std::ostream&           stream,
            SAA_in      const std::string&      value
        )
        {
            stream << '(' << bl::str::to_upper_copy( value ) << ')';
        };

    UTF_CHECK_EQUAL( "", bl::str::joinFormatted< std::string >( empty, ", ", ", ", formatter1 ) );

    UTF_CHECK_EQUAL( "(ONE)|(TWO)|(THREE)|(FOUR)=>(FIVE)", bl::str::joinFormatted< std::string >( vec, "|", "=>", formatter1 ) );

    int n = 0;
    const auto formatter2 =
        [ &n ](
            SAA_in      std::ostream&           stream,
            SAA_in      const std::string&      value
        )
        {
            stream << "[" << ++n << "]=" << value;
        };

    UTF_CHECK_EQUAL( "[1]=one;[2]=two;[3]=three;[4]=four;[5]=five", bl::str::joinFormatted< std::string >( vec, ";", ";", formatter2 ) );
    UTF_CHECK_EQUAL( "[6]=one+[7]=two+[8]=three+[9]=four>[10]=five", bl::str::joinFormatted< std::string >( vec, "+", ">", formatter2 ) );
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsSplitTests )
{
    std::vector< std::string > vec;

    /*
     * Beware, an empty string will be split into a vector of ONE empty element!
     */

    bl::str::split( vec, "", bl::str::is_any_of( ",;" ) );
    UTF_REQUIRE_EQUAL( 1U, vec.size() );
    UTF_CHECK_EQUAL( "", vec[ 0 ] );

    /*
     * This works as expected
     */

    bl::str::split( vec, "abc", bl::str::is_any_of( ",;" ) );
    UTF_REQUIRE_EQUAL( 1U, vec.size() );
    UTF_CHECK_EQUAL( "abc", vec[ 0 ] );

    bl::str::split( vec, "abc;def,;ghij", bl::str::is_any_of( ",;" ) );
    UTF_REQUIRE_EQUAL( 4U, vec.size() );
    UTF_CHECK_EQUAL( "abc", vec[ 0 ] );
    UTF_CHECK_EQUAL( "def", vec[ 1 ] );
    UTF_CHECK_EQUAL( "", vec[ 2 ] );
    UTF_CHECK_EQUAL( "ghij", vec[ 3 ] );

    bl::str::split( vec, "   ", bl::str::is_space() );
    UTF_REQUIRE_EQUAL( 4U, vec.size() );
    UTF_CHECK_EQUAL( "", vec[ 0 ] );
    UTF_CHECK_EQUAL( "", vec[ 1 ] );
    UTF_CHECK_EQUAL( "", vec[ 2 ] );
    UTF_CHECK_EQUAL( "", vec[ 3 ] );

    bl::str::split( vec, " abc ", bl::str::is_space() );
    UTF_REQUIRE_EQUAL( 3U, vec.size() );
    UTF_CHECK_EQUAL( "", vec[ 0 ] );
    UTF_CHECK_EQUAL( "abc", vec[ 1 ] );
    UTF_CHECK_EQUAL( "", vec[ 2 ] );

    bl::str::split( vec, "abc/def//ghij/", bl::str::is_from_range( '/', '/' ) );
    UTF_REQUIRE_EQUAL( 5U, vec.size() );
    UTF_CHECK_EQUAL( "abc", vec[ 0 ] );
    UTF_CHECK_EQUAL( "def", vec[ 1 ] );
    UTF_CHECK_EQUAL( "", vec[ 2 ] );
    UTF_CHECK_EQUAL( "ghij", vec[ 3 ] );
    UTF_CHECK_EQUAL( "", vec[ 4 ] );

    bl::str::split( vec, "12345-6789-0", bl::str::is_equal_to( '-' ) );
    UTF_REQUIRE_EQUAL( 3U, vec.size() );
    UTF_CHECK_EQUAL( "12345", vec[ 0 ] );
    UTF_CHECK_EQUAL( "6789", vec[ 1 ] );
    UTF_CHECK_EQUAL( "0", vec[ 2 ] );

    /*
     * Empty string is tokenized into an empty sequence (as expected)
     */

    typedef bl::str::escaped_list_separator< char > separator_t;

    separator_t sep( "\\", ":", "\"\'");

    std::string empty;
    bl::str::tokenizer< separator_t > tokens1( empty, sep );

    UTF_CHECK_NO_THROW( vec.assign( tokens1.begin(), tokens1.end() ) );
    UTF_REQUIRE_EQUAL( 0U, vec.size() );

    /*
     * Check various escape characters (quoted text cannot be properly nested)
     */

    std::string input = "/bin:/opt/a\\:b/c:'http://abc:7890':\"xyz:'ne\"st\"ed':/abc:def\\\\ghi\":end";
    bl::str::tokenizer< separator_t > tokens2( input, sep );

    UTF_CHECK_NO_THROW( vec.assign( tokens2.begin(), tokens2.end() ) );
    UTF_REQUIRE_EQUAL( 5U, vec.size() );
    UTF_CHECK_EQUAL( "/bin", vec[ 0 ] );
    UTF_CHECK_EQUAL( "/opt/a:b/c", vec[ 1 ] );
    UTF_CHECK_EQUAL( "http://abc:7890", vec[ 2 ] );
    UTF_CHECK_EQUAL( "xyz:nested:/abc:def\\ghi", vec[ 3 ] );
    UTF_CHECK_EQUAL( "end", vec[ 4 ] );

    input = "missing closing quote 'is ok: doh";
    bl::str::tokenizer< separator_t > tokens3( input, sep );

    UTF_CHECK_NO_THROW( vec.assign( tokens3.begin(), tokens3.end() ) );
    UTF_REQUIRE_EQUAL( 1U, vec.size() );
    UTF_CHECK_EQUAL( "missing closing quote is ok: doh", vec[ 0 ] );

    input = "dangling escape\\";
    bl::str::tokenizer< separator_t > tokens4( input, sep );

    UTF_CHECK_THROW( vec.assign( tokens4.begin(), tokens4.end() ), boost::escaped_list_error );

    {
        const std::string str1 = "abcd";
        const std::string str2 = "";
        const std::string str3 = "abcdabcd";
        const std::string str4 = "abcd_middle_abcd";
        const std::string str5 = "left_abcdabcd_right";
        const std::string str6 = "left_abcd_middle_abcd_right";

        const std::string sep = "abcd";
        const std::string sep1 = "abcdef";
        const std::string sep2 = "";

        {
            UTF_REQUIRE_THROW( bl::str::splitString( str1, sep1, str1.length(), 0U ), bl::ArgumentException );
            UTF_REQUIRE_THROW( bl::str::splitString( str2, sep1, 0U, str1.length() ), bl::ArgumentException );
            UTF_REQUIRE_THROW( bl::str::splitString( str3, sep2, 0U, str1.length() ), bl::ArgumentException );
        }

        {
            const auto result = bl::str::splitString( str1, sep, 0U, str1.length() );
            UTF_REQUIRE_EQUAL( result.size(), 2U );
        }

        {
            const auto result = bl::str::splitString( str1, sep );
            UTF_REQUIRE_EQUAL( result.size(), 2U );
        }

        {
            const auto result = bl::str::splitString( str1, sep1, 0U, str1.length() );
            UTF_REQUIRE_EQUAL( result.size(), 1U );
        }

        {
            const auto result = bl::str::splitString( str3, sep, 0U, str3.length() );
            UTF_REQUIRE_EQUAL( result.size(), 3U );
        }

        {
            const auto result = bl::str::splitString( str3, sep, 4U, str3.length() );
            UTF_REQUIRE_EQUAL( result.size(), 2U );
        }

        {
            const auto result = bl::str::splitString( str4, sep, 0U, str4.length() );
            UTF_REQUIRE_EQUAL( result.size(), 3U );
        }

        {
            const auto result = bl::str::splitString( str4, sep1, 0U, str4.length() );
            UTF_REQUIRE_EQUAL( result.size(), 1U );
        }

        {
            const auto result = bl::str::splitString( str5, sep, 0U, str5.length() );
            UTF_REQUIRE_EQUAL( result.size(), 3U );
        }

        {
            const auto result = bl::str::splitString( str5, sep, 0U, str5.find("_right") );
            UTF_REQUIRE_EQUAL( result.size(), 3U );
        }

        {
            const auto result = bl::str::splitString( str6, sep, 0U, str6.length() );
            UTF_REQUIRE_EQUAL( result.size(), 3U );
        }

        /*
         * The cases below pin the RANGE handling of splitString - every assertion above
         * checks element counts only, and neither of the two range fixes changes any of
         * those counts
         *
         * httpserver/Parser.h splits an untrusted network buffer with an explicit
         * endPos, so a token which is allowed to extend past it reads into the body of
         * the request
         *
         * These inputs need their own literals - none of the strings above has a
         * separator which crosses a useful endPos
         */

        {
            /*
             * The separator cannot fit in the requested window - the window itself is
             * the only element, and not the whole text
             */

            const auto result =
                bl::str::splitString( std::string( "left_abcd_right" ), std::string( "abcdef" ), 5U, 9U );

            UTF_REQUIRE_EQUAL( result.size(), 1U );
            UTF_CHECK_EQUAL( "abcd", result[ 0 ] );
        }

        {
            /*
             * The separator straddles endPos - it must not match, so the window is
             * returned whole rather than split around a token which lies outside it
             */

            const auto result =
                bl::str::splitString( std::string( "ab--cd" ), std::string( "--" ), 0U, 3U );

            UTF_REQUIRE_EQUAL( result.size(), 1U );
            UTF_CHECK_EQUAL( "ab-", result[ 0 ] );
        }

        {
            /*
             * The separator ends exactly at endPos - it must match and leave an empty
             * trailing element
             */

            const auto result =
                bl::str::splitString( std::string( "ab--cd" ), std::string( "--" ), 0U, 4U );

            UTF_REQUIRE_EQUAL( result.size(), 2U );
            UTF_CHECK_EQUAL( "ab", result[ 0 ] );
            UTF_CHECK_EQUAL( "", result[ 1 ] );
        }

        {
            /*
             * Empty leading and trailing elements inside a window
             */

            const auto result =
                bl::str::splitString( std::string( "xx--yy--zz" ), std::string( "--" ), 2U, 8U );

            UTF_REQUIRE_EQUAL( result.size(), 3U );
            UTF_CHECK_EQUAL( "", result[ 0 ] );
            UTF_CHECK_EQUAL( "yy", result[ 1 ] );
            UTF_CHECK_EQUAL( "", result[ 2 ] );
        }

        {
            /*
             * Content assertions for two of the size-only inputs above
             */

            const auto result = bl::str::splitString( str6, sep, 0U, str6.length() );

            UTF_REQUIRE_EQUAL( result.size(), 3U );
            UTF_CHECK_EQUAL( "left_", result[ 0 ] );
            UTF_CHECK_EQUAL( "_middle_", result[ 1 ] );
            UTF_CHECK_EQUAL( "_right", result[ 2 ] );
        }

        {
            const auto result = bl::str::splitString( str3, sep, 4U, str3.length() );

            UTF_REQUIRE_EQUAL( result.size(), 2U );
            UTF_CHECK_EQUAL( "", result[ 0 ] );
            UTF_CHECK_EQUAL( "", result[ 1 ] );
        }
    }
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsParsePropertiesListTests )
{
    {
        const auto properties = str::parsePropertiesList( "" );

        UTF_REQUIRE( properties.empty() );
    }

    {
        const auto properties = str::parsePropertiesList( ";;" );

        UTF_REQUIRE( properties.empty() );
    }

    {
        const auto properties = str::parsePropertiesList( "name1=value1" );

        UTF_REQUIRE_EQUAL( 1U, properties.size() );
        UTF_REQUIRE_EQUAL( std::string( "value1" ), properties.at( "name1" ) );
    }

    {
        const auto properties = str::parsePropertiesList( ";name1=value1; empty= " );

        UTF_REQUIRE_EQUAL( 2U, properties.size() );
        UTF_REQUIRE_EQUAL( std::string( "value1" ), properties.at( "name1" ) );
        UTF_REQUIRE_EQUAL( std::string( "" ), properties.at( "empty" ) );
    }

    {
        const auto properties =
            str::parsePropertiesList( "name1=value1; name2=value2;name3=value3;" );

        UTF_REQUIRE_EQUAL( 3U, properties.size() );
        UTF_REQUIRE_EQUAL( std::string( "value1" ), properties.at( "name1" ) );
        UTF_REQUIRE_EQUAL( std::string( "value2" ), properties.at( "name2" ) );
        UTF_REQUIRE_EQUAL( std::string( "value3" ), properties.at( "name3" ) );
    }

    {
        const auto properties =
            str::parsePropertiesList( std::vector< std::string >{ "name1=value1", "name2=value2", "name3=value3" } );

        UTF_REQUIRE_EQUAL( 3U, properties.size() );
        UTF_REQUIRE_EQUAL( std::string( "value1" ), properties.at( "name1" ) );
        UTF_REQUIRE_EQUAL( std::string( "value2" ), properties.at( "name2" ) );
        UTF_REQUIRE_EQUAL( std::string( "value3" ), properties.at( "name3" ) );
    }

    {
        const auto properties =
            str::parsePropertiesText( " #comment\nname1=value1\n name2=value2\nname3=value3\n\n" );

        UTF_REQUIRE_EQUAL( 3U, properties.size() );
        UTF_REQUIRE_EQUAL( std::string( "value1" ), properties.at( "name1" ) );
        UTF_REQUIRE_EQUAL( std::string( "value2" ), properties.at( "name2" ) );
        UTF_REQUIRE_EQUAL( std::string( "value3" ), properties.at( "name3" ) );
    }

    {
        const auto properties =
            str::parseLines( " #comment\nname1=value1\n name2=value2\nname3=value3\n\n" );

        UTF_REQUIRE_EQUAL( 3U, properties.size() );
        UTF_REQUIRE_EQUAL( std::string( "name1=value1" ), properties[ 0U ] );
        UTF_REQUIRE_EQUAL( std::string( "name2=value2" ), properties[ 1U ] );
        UTF_REQUIRE_EQUAL( std::string( "name3=value3" ), properties[ 2U ] );
    }

    UTF_REQUIRE_THROW_MESSAGE(
        str::parsePropertiesList( ";value" ),
        bl::InvalidDataFormatException,
        "Cannot parse property in 'name=value' format"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        str::parsePropertiesList( ";=value" ),
        bl::InvalidDataFormatException,
        "Cannot parse property in 'name=value' format"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        str::parsePropertiesList( ";name1=value1; name1=value2" ),
        bl::InvalidDataFormatException,
        "Duplicate property"
        );
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsGetBeginning )
{
    const std::string text = "global file system";

    UTF_CHECK_EQUAL( ""                  , bl::str::getBeginning( text, 0 ) );
    UTF_CHECK_EQUAL( "g"                 , bl::str::getBeginning( text, 1 ) );
    UTF_CHECK_EQUAL( "global"            , bl::str::getBeginning( text, 6 ) );
    UTF_CHECK_EQUAL( "global file"       , bl::str::getBeginning( text, 11 ) );
    UTF_CHECK_EQUAL( "global file syste" , bl::str::getBeginning( text, 17 ) );
    UTF_CHECK_EQUAL( "global file system", bl::str::getBeginning( text, 18 ) );

    UTF_CHECK_EQUAL( "global file system", bl::str::getBeginning( text, 19 ) );
    UTF_CHECK_EQUAL( "global file system", bl::str::getBeginning( text, 20 ) );
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsGetEnding )
{
    const std::string text = "global file system";

    UTF_CHECK_EQUAL( ""                  , bl::str::getEnding( text, 0 ) );
    UTF_CHECK_EQUAL( "m"                 , bl::str::getEnding( text, 1 ) );
    UTF_CHECK_EQUAL( "system"            , bl::str::getEnding( text, 6 ) );
    UTF_CHECK_EQUAL( "file system"       , bl::str::getEnding( text, 11 ) );
    UTF_CHECK_EQUAL( "lobal file system" , bl::str::getEnding( text, 17 ) );
    UTF_CHECK_EQUAL( "global file system", bl::str::getEnding( text, 18 ) );

    UTF_CHECK_EQUAL( "global file system", bl::str::getEnding( text, 19 ) );
    UTF_CHECK_EQUAL( "global file system", bl::str::getEnding( text, 20 ) );
}

namespace
{
    std::string testFormatMessage(
        SAA_in      const char*                                 format,
        SAA_in                                                  ...
        )
    {
        va_list va;
        va_start( va, format );

        auto result = bl::str::formatMessage( format, va );

        va_end( va );

        return result;
    }
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsFormatMessageTests )
{
    UTF_CHECK_EQUAL( "", testFormatMessage( "" ) );
    UTF_CHECK_EQUAL( "test", testFormatMessage( "test" ) );

    UTF_CHECK_EQUAL( "test: ok", testFormatMessage( "test: %s", "ok" ) );
    UTF_CHECK_EQUAL( "test: (null)", testFormatMessage( "test: %s", nullptr ) );

    UTF_CHECK_EQUAL( "test:       data, -42, 3.1415\n", testFormatMessage( "test: %10s, %d, %g\n", "data", -42, 3.1415, "extra", 999 ) );

    UTF_CHECK_THROW( testFormatMessage( nullptr ), bl::ArgumentNullException );
    UTF_CHECK_THROW( testFormatMessage( nullptr, 1, 2, 3 ), bl::ArgumentNullException );
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsFormatPercentTests )
{
    UTF_CHECK_EQUAL( "25.2%", bl::str::formatPercent( 25.23, 100 ) );
    UTF_CHECK_EQUAL( "0.1%", bl::str::formatPercent( 1, 1000 ) );
    UTF_CHECK_EQUAL( "0%", bl::str::formatPercent( 1, 1000, 0 /* precision */ ) );
    UTF_CHECK_EQUAL( "0.00%", bl::str::formatPercent( 0, 1000, 2 /* precision */ ) );
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsWipe )
{
    {
        std::string s1( "abc" );

        {
            auto& s1Ref = s1;
            BL_WIPE_ON_EXIT( s1Ref );
        }

        UTF_CHECK_EQUAL( "000", s1 );
    }

    {
        std::vector< std::string > v1( 1, "abc" );

        {
            auto& v1Ref = v1;
            BL_WIPE_ON_EXIT( v1Ref );
        }

        UTF_CHECK_EQUAL( "000", v1.back() );
    }

    {
        std::map< int, std::string > m1{ { 1, "abc" } };

        {
            auto& m1Ref = m1;
            BL_WIPE_ON_EXIT( m1Ref );
        }

        UTF_CHECK_EQUAL( "000", m1[ 1 ] );
    }

    {
        std::unordered_map< int, std::string > um1{ { 1, "abc" } };

        {
            auto& um1Ref = um1;
            BL_WIPE_ON_EXIT( um1Ref );
        }

        UTF_CHECK_EQUAL( "000", um1[ 1 ] );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsSecureStringWrapper )
{
    bl::str::SecureStringWrapper sec1;

    UTF_CHECK_EQUAL( sec1.size(), 0U );
    UTF_CHECK( sec1.empty() );

    {
        /*
         * Testing wipe functionality
         */

        std::string s( "test" );

        {
            bl::str::SecureStringWrapper sec2( std::move( s ) );

            UTF_CHECK( s.empty() );
            UTF_CHECK( ! sec2.empty() );
            UTF_CHECK_EQUAL( sec2.getAsNonSecureString(), "test" );
        }
    }

    {
        /*
         * Testing move assignment operator from string and move ctor from SecureStringWrapper
         */

        std::string s( "test" );
        bl::str::SecureStringWrapper sec2;

        sec2 = std::move( s );

        UTF_CHECK( s.empty() );
        UTF_CHECK( ! sec2.empty() );
        UTF_CHECK_EQUAL( sec2.getAsNonSecureString(), "test" );

        bl::str::SecureStringWrapper sec3( std::move( sec2 ) );

        UTF_CHECK( sec2.empty() );
        UTF_CHECK( ! sec3.empty() );
        UTF_CHECK_EQUAL( sec3.getAsNonSecureString(), "test" );

        sec3.clear();

        UTF_CHECK( sec3.empty() );
    }

    {
        /*
         * Testing move ctor from string + reallocation
         */

        std::string s( "test" );

        bl::str::SecureStringWrapper sec2( std::move( s ) );
        bl::str::SecureStringWrapper sec3;

        sec3 = std::move( sec2 );

        UTF_CHECK( sec2.empty() );
        UTF_CHECK( ! sec3.empty() );
        UTF_CHECK_EQUAL( sec3.getAsNonSecureString(), "test" );

        sec3.append( sec3 );

        UTF_CHECK_EQUAL( sec3.getAsNonSecureString(), "testtest" );

        sec3.append( '1' );

        UTF_CHECK_EQUAL( sec3.getAsNonSecureString(), "testtest1" );

        bl::str::SecureStringWrapper sec4( "long string, long string, long string, long string, " );
        sec4.append( "long string, long string, long string, long string, " );
        sec4.append( "long string, long string, long string, long string, " );
        sec4.append( "long string, long string, long string, long string, " );
        sec4.append( "long string, long string, long string, long string" );

        const char* dataPtr4 = sec4.getAsNonSecureString().c_str();

        const auto size4 = sec4.size();

        sec3 = std::move( sec4 );

        UTF_CHECK( sec4.empty() );
        UTF_CHECK( ! sec3.empty() );
        UTF_CHECK_EQUAL( sec3.size(), size4 );
        UTF_CHECK( dataPtr4 != sec3.getAsNonSecureString().c_str() );
    }

    {
        /*
         * Testing SecureStringWrapper management of external string
         */

        std::string s( "test" );

        {
            bl::str::SecureStringWrapper sec2( &s );

            UTF_CHECK( ! s.empty() );
            UTF_CHECK( ! sec2.empty() );
            UTF_CHECK_EQUAL( s, "test" );
            UTF_CHECK_EQUAL( sec2.getAsNonSecureString(), "test" );

            sec2.append( "long string, long string, long string, long string" );

            UTF_CHECK_EQUAL( s.c_str(), sec2.getAsNonSecureString().c_str() );
        }

        UTF_CHECK( s.empty() );
    }

    {
        /*
         * Initial capacity can only be specified for external and empty string
         */

        UTF_CHECK_THROW(
            bl::str::SecureStringWrapper sec1( nullptr, 1024 ),
            bl::UnexpectedException
            );

        std::string s( "test" );

        UTF_CHECK_THROW(
            bl::str::SecureStringWrapper sec2( &s, 1024 ),
            bl::UnexpectedException
            );

        std::string s2;

        UTF_CHECK_NO_THROW(
            bl::str::SecureStringWrapper sec3( &s2, 1024 )
            );
    }

    {
        /*
         * The copy ctor always binds m_implPtr to its own m_impl, so a copy has storage
         * which is fully independent from the source
         */

        bl::str::SecureStringWrapper src;
        src.append( "secret-value" );

        bl::str::SecureStringWrapper copy( src );

        UTF_CHECK_EQUAL( copy.getAsNonSecureString(), "secret-value" );
        UTF_CHECK_EQUAL( src.getAsNonSecureString(), "secret-value" );
        UTF_CHECK( copy.getAsNonSecureString().c_str() != src.getAsNonSecureString().c_str() );
    }

    {
        /*
         * A copy of an external string wrapper must not alias the external string -
         * if it did, the copy would wipe the original's string when it is destroyed
         */

        std::string ext( "ext-secret" );

        {
            bl::str::SecureStringWrapper w( &ext );
            bl::str::SecureStringWrapper copy( w );

            UTF_CHECK_EQUAL( copy.getAsNonSecureString(), "ext-secret" );
            UTF_CHECK( copy.getAsNonSecureString().c_str() != ext.c_str() );

            copy.clear();

            UTF_CHECK_EQUAL( ext, "ext-secret" );
        }

        UTF_CHECK( ext.empty() );
    }

    {
        /*
         * Copy assignment replaces the payload rather than appending to it, and it
         * leaves the source intact
         */

        bl::str::SecureStringWrapper a;
        a.append( "aaa" );

        bl::str::SecureStringWrapper b;
        b.append( "bbbbb" );

        b = a;

        UTF_CHECK_EQUAL( b.getAsNonSecureString(), "aaa" );
        UTF_CHECK_EQUAL( a.getAsNonSecureString(), "aaa" );

        /*
         * Both self assignment guards - without the 'this == &other' checks the clear()
         * at the top of each operator would wipe the payload before appending it back
         *
         * The assignment is routed through a reference, as BaseLib_StringUtilsWipe
         * already does, to keep the compiler's self assignment diagnostics quiet
         */

        auto& aRef = a;

        a = aRef;

        UTF_CHECK_EQUAL( a.getAsNonSecureString(), "aaa" );

        a = std::move( aRef );

        UTF_CHECK_EQUAL( a.getAsNonSecureString(), "aaa" );
    }

    {
        /*
         * Reallocation through grow(): INITIAL_CAPACITY is 16, so the first 20 bytes
         * reserve 32 and the following 100 bytes take the temporary copy path and
         * reserve 128 - which is where reserve() must re-sync m_dataPtr, or the clear()
         * below terminates the process through checkWrappedStringIntegrity()
         */

        bl::str::SecureStringWrapper w;

        w.append( std::string( 20U, 'x' ) );
        w.append( std::string( 100U, 'y' ) );

        UTF_CHECK_EQUAL( w.size(), 120U );
        UTF_CHECK_EQUAL( w.getAsNonSecureString(), std::string( 20U, 'x' ) + std::string( 100U, 'y' ) );

        w.clear();

        UTF_CHECK( w.empty() );
    }

    {
        /*
         * Self append across the capacity boundary
         */

        bl::str::SecureStringWrapper s;

        s.append( std::string( 16U, 'z' ) );
        s.append( s );

        UTF_CHECK_EQUAL( s.size(), 32U );
        UTF_CHECK_EQUAL( s.getAsNonSecureString(), std::string( 32U, 'z' ) );
    }
}

/************************************************************************
 * Utils tests
 */

UTF_AUTO_TEST_CASE( BaseLib_UtilsFindLastTests )
{
    const int array[] = { 0, 1, 2, 3, 4, 5, 4, 3, 2, 2, 1 };

    UTF_CHECK( bl::utils::find_last( std::begin( array ), std::end( array ), 999 ) == std::end( array ) );
    UTF_CHECK( bl::utils::find_last( std::begin( array ), std::end( array ), 0 ) == &array[ 0 ] );
    UTF_CHECK( bl::utils::find_last( std::begin( array ), std::end( array ), 5 ) == &array[ 5 ] );
    UTF_CHECK( bl::utils::find_last( std::begin( array ), std::end( array ), 2 ) == &array[ 9 ] );
    UTF_CHECK( bl::utils::find_last( std::begin( array ), std::end( array ), 1 ) == &array[ 10 ] );
    UTF_CHECK( bl::utils::find_last( std::begin( array ), std::end( array ), INT_MIN ) == std::end( array ) );

    std::vector< std::string > vec;
    vec.push_back( "one" );
    vec.push_back( "two" );
    vec.push_back( "three" );
    vec.push_back( "four" );
    vec.push_back( "five" );
    vec.push_back( "two" );

    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.end(), "forty two" ) == vec.end() );
    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.end(), "one" )  == vec.begin() + 0 );
    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.end(), "four" ) == vec.begin() + 3 );
    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.end(), "five" ) == vec.begin() + 4 );
    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.end(), "two" )  == vec.begin() + 5 );
    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.begin() + 4, "two" ) == vec.begin() + 1 );
    UTF_CHECK( bl::utils::find_last( vec.begin(), vec.end(), "zero" ) == vec.end() );

    std::list< short > empty;

    UTF_REQUIRE( empty.begin() == empty.end() );
    UTF_CHECK( bl::utils::find_last( empty.begin(), empty.end(), 123 ) == empty.end() );
}

/************************************************************************
 * Tagged pool tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TaggedPoolTests )
{
    typedef bl::om::ObjectImpl
        <
            bl::TaggedPool< std::string, bl::om::ObjPtr< bl::data::DataBlock > >
        >
        datablocks_tagged_pool_t;

    const auto pool = datablocks_tagged_pool_t::createInstance();

    auto block = bl::data::DataBlock::createInstance( 16 );

    {
        char* data = reinterpret_cast< char* >( block -> pv() );

        data[ 0 ] = 1;
        data[ 1 ] = 2;
        data[ 2 ] = 3;
        data[ 3 ] = 4;
    }

    const auto key1 = "key1";

    {
        {
            const auto value = pool -> tryGet( key1 );
            UTF_REQUIRE( ! value );
            pool -> put( key1, std::move( block ) );
        }

        {
            const auto value = pool -> tryGet( "key2" );
            UTF_REQUIRE( ! value );
        }

        {
            auto block = pool -> tryGet( key1 );
            UTF_REQUIRE( block );

            const char* data = reinterpret_cast< const char* >( block -> pv() );

            UTF_REQUIRE( data[ 0 ] == 1 );
            UTF_REQUIRE( data[ 1 ] == 2 );
            UTF_REQUIRE( data[ 2 ] == 3 );
            UTF_REQUIRE( data[ 3 ] == 4 );

            const auto value = pool -> tryGet( key1 );
            UTF_REQUIRE( ! value );

            pool -> put( key1, std::move( block ) );
        }

        {
            auto block = bl::data::DataBlock::createInstance( 16 );

            {
                char* data = reinterpret_cast< char* >( block -> pv() );

                data[ 0 ] = 5;
                data[ 1 ] = 6;
                data[ 2 ] = 7;
                data[ 3 ] = 8;
            }

            pool -> put( "key1", std::move( block ) );
        }

        {
            const auto value = pool -> tryGet( "key2" );
            UTF_REQUIRE( ! value );
        }

        {
            for( auto i = 0; i < 2; ++i )
            {
                const auto block = pool -> tryGet( key1 );
                UTF_REQUIRE( block );

                const char* data = reinterpret_cast< const char* >( block -> pv() );

                UTF_REQUIRE( data[ 0 ] == 1 || data[ 0 ] == 5 );

                if( data[ 0 ] == 1 )
                {
                    UTF_REQUIRE( data[ 1 ] == 2 );
                    UTF_REQUIRE( data[ 2 ] == 3 );
                    UTF_REQUIRE( data[ 3 ] == 4 );
                }
                else
                {
                    UTF_REQUIRE( data[ 1 ] == 6 );
                    UTF_REQUIRE( data[ 2 ] == 7 );
                    UTF_REQUIRE( data[ 3 ] == 8 );
                }
            }

            const auto value = pool -> tryGet( key1 );
            UTF_REQUIRE( ! value );

            {
                auto block = bl::data::DataBlock::createInstance( 16 );
                pool -> put( key1, std::move( block ) );
            }

            {
                auto block = bl::data::DataBlock::createInstance( 16 );
                pool -> put( "key2", std::move( block ) );
            }
        }

        {
            UTF_REQUIRE( pool -> tryGet( key1 ) );
            UTF_REQUIRE( pool -> tryGet( "key2" ) );
        }
    }
}

/************************************************************************
 * Utils tests
 */

UTF_AUTO_TEST_CASE( BaseLib_RetryOnErrorTests )
{
    std::size_t successCalled = 0;
    std::size_t failLessCalled = 0;
    std::size_t failMoreCalled = 0;

    const std::size_t retryCount = 4;

    const auto cbTestSuccess = [ & ]() -> void
    {
        ++successCalled;
    };

    const auto cbTestAlwaysFail = [ & ]() -> void
    {
        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "This is fail always exception"
            );
    };

    const auto cbTestFailLess = [ & ]() -> void
    {
        ++failLessCalled;

        if( failLessCalled < retryCount )
        {
            BL_THROW(
                bl::UnexpectedException(),
                BL_MSG()
                    << "This is fail less exception"
                );
        }
    };

    const auto cbTestFailMore = [ & ]() -> void
    {
        ++failMoreCalled;

        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "This is fail more exception"
            );
    };

    auto startTime = bl::time::microsec_clock::universal_time();

    UTF_REQUIRE_EQUAL( successCalled, 0U );
    bl::utils::retryOnError< bl::UnexpectedException >( cbTestSuccess, retryCount );
    UTF_REQUIRE_EQUAL( successCalled, 1U );

    auto elapsedTime = bl::time::microsec_clock::universal_time() - startTime;
    UTF_REQUIRE( elapsedTime < bl::time::seconds( 3 ) );

    startTime = bl::time::microsec_clock::universal_time();

    try
    {
        bl::utils::retryOnError< bl::UnexpectedException >(
            cbTestAlwaysFail,
            5U /* retryCount */,
            bl::time::milliseconds( 500 ) /* retryTimeout */
            );

        UTF_FAIL( "Exception must be thrown" );
    }
    catch( bl::UnexpectedException& )
    {
    }

    elapsedTime = bl::time::microsec_clock::universal_time() - startTime;
    UTF_REQUIRE( elapsedTime > bl::time::seconds( 2 ) );

    UTF_REQUIRE_EQUAL( successCalled, 1U );
    bl::utils::retryOnAllErrors( cbTestSuccess, retryCount, bl::time::seconds( 2 ) );
    UTF_REQUIRE_EQUAL( successCalled, 2U );

    UTF_REQUIRE_EQUAL( failLessCalled, 0U );
    bl::utils::retryOnError< bl::UnexpectedException >( cbTestFailLess, retryCount );
    UTF_REQUIRE_EQUAL( failLessCalled, retryCount );

    try
    {
        failLessCalled = 0U;
        bl::utils::retryOnError< bl::SystemException >( cbTestFailLess, retryCount );
        UTF_FAIL( "Exception must be thrown" );
    }
    catch( bl::UnexpectedException& e )
    {
        UTF_REQUIRE_EQUAL( e.what(), "This is fail less exception" );
        UTF_REQUIRE_EQUAL( failLessCalled, 1U );
    }

    try
    {
        UTF_REQUIRE_EQUAL( failMoreCalled, 0U );
        bl::utils::retryOnError< bl::UnexpectedException >( cbTestFailMore, retryCount );
        UTF_FAIL( "Exception must be thrown" );
    }
    catch( bl::UnexpectedException& e )
    {
        UTF_REQUIRE_EQUAL( e.what(), "This is fail more exception" );

        /*
         * The expected count is the normal call + the retry count
         */

        const std::size_t expectedCalls = retryCount + 1;

        UTF_REQUIRE_EQUAL( failMoreCalled, expectedCalls );
    }

    try
    {
        failMoreCalled = 0U;
        bl::utils::retryOnError< bl::SystemException >( cbTestFailMore, retryCount );
        UTF_FAIL( "Exception must be thrown" );
    }
    catch( bl::UnexpectedException& e )
    {
        UTF_REQUIRE_EQUAL( e.what(), "This is fail more exception" );
        UTF_REQUIRE_EQUAL( failMoreCalled, 1U );
    }

    /*
     * The value returning overloads are separate instantiations of
     * detail::Utils::retryOnError< R, EXCEPTION > and none of the checks above reach
     * them; the default retryTimeout is timeoutNoDelay(), so nothing below sleeps
     */

    std::size_t intCalled = 0U;

    const auto cbIntFailsTwiceThenReturns7 = [ & ]() -> int
    {
        ++intCalled;

        if( intCalled < 3U )
        {
            BL_THROW(
                bl::UnexpectedException(),
                BL_MSG()
                    << "This is fail twice exception"
                );
        }

        return 7;
    };

    const auto cbIntSucceeds = [ & ]() -> int
    {
        ++intCalled;

        return 42;
    };

    const auto cbIntAlwaysFails = [ & ]() -> int
    {
        ++intCalled;

        BL_THROW(
            bl::UnexpectedException(),
            BL_MSG()
                << "This is int fail always exception"
            );
    };

    intCalled = 0U;

    UTF_REQUIRE_EQUAL(
        ( bl::utils::retryOnError< int, bl::UnexpectedException >( cbIntFailsTwiceThenReturns7, retryCount ) ),
        7
        );

    UTF_REQUIRE_EQUAL( intCalled, 3U );

    intCalled = 0U;

    UTF_REQUIRE_EQUAL( bl::utils::retryOnAllErrors< int >( cbIntSucceeds, retryCount ), 42 );

    UTF_REQUIRE_EQUAL( intCalled, 1U );

    intCalled = 0U;

    UTF_REQUIRE_THROW(
        ( bl::utils::retryOnError< int, bl::UnexpectedException >( cbIntAlwaysFails, 2U ) ),
        bl::UnexpectedException
        );

    /*
     * The normal call plus the retry count, exactly as for the void overload
     */

    UTF_REQUIRE_EQUAL( intCalled, 3U );

    {
        /*
         * On failure tryRetryOnAllErrors< R > must report false and a default
         * constructed R, not an uninitialized one, and must not let the exception escape
         */

        intCalled = 0U;

        const auto result = bl::utils::tryRetryOnAllErrors< int >( cbIntAlwaysFails, 2U );

        UTF_REQUIRE( ! result.second );
        UTF_REQUIRE_EQUAL( result.first, 0 );
        UTF_REQUIRE_EQUAL( intCalled, 3U );
    }

    {
        intCalled = 0U;

        const auto result = bl::utils::tryRetryOnAllErrors< int >( cbIntSucceeds, retryCount );

        UTF_REQUIRE( result.second );
        UTF_REQUIRE_EQUAL( result.first, 42 );
        UTF_REQUIRE_EQUAL( intCalled, 1U );
    }

    /*
     * The void tryRetryOnAllErrors overload in both outcomes
     */

    UTF_REQUIRE( bl::utils::tryRetryOnAllErrors( cbTestSuccess, retryCount ) );
    UTF_REQUIRE( ! bl::utils::tryRetryOnAllErrors( cbTestAlwaysFail, 1U ) );
}

/************************************************************************
 * UniqueHandle tests
 */

UTF_AUTO_TEST_CASE( BaseLib_UniqueHandleTests )
{
    static int g_counter = 0U;
    static int g_outstanding = 0U;
    static std::map< int, bl::cpp::ScalarTypeIniter< std::size_t > > g_handleTable;

    class MyFakeResourceImpl
    {
    public:

        /*
         * Helpers to create and destroy fake resources
         */

        static int create()
        {
            ++g_counter;

            auto& refs = g_handleTable[ g_counter ];
            ++refs.lvalue();
            ++g_outstanding;
            return g_counter;
        }

        static int dup( SAA_in const int handle )
        {
            auto& refs = g_handleTable[ handle ];
            UTF_REQUIRE( refs );
            ++refs.lvalue();
            ++g_outstanding;
            return handle;
        }

        static int createFailed()
        {
            return get_null();
        }

        static void destroy( SAA_in const int handle )
        {
            auto& refs = g_handleTable[ handle ];
            UTF_REQUIRE( refs );
            --refs.lvalue();
            --g_outstanding;
        }

        static int get_null() NOEXCEPT
        {
            return -1;
        }
    };

    class MyDeleter
    {
    public:

        void operator()( SAA_in const int handle ) NOEXCEPT
        {
            MyFakeResourceImpl::destroy( handle );
        }

        int get_null() const NOEXCEPT
        {
            return MyFakeResourceImpl::get_null();
        }
    };

    typedef bl::cpp::UniqueHandle< int, MyDeleter > my_resource_ref;

    my_resource_ref nil;
    UTF_REQUIRE( nil == MyFakeResourceImpl::get_null() );

    if( nil )
    {
        UTF_FAIL( "Must be false" );
    }

    {
        my_resource_ref v1( MyFakeResourceImpl::create() );
        UTF_REQUIRE( v1 != MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 1U == g_outstanding );

        if( ! v1 )
        {
            UTF_FAIL( "Must be true" );
        }

        int i1 = v1.get();
        UTF_REQUIRE( i1 );
        UTF_REQUIRE( i1 == v1.get() );

        my_resource_ref v2( MyFakeResourceImpl::create() );
        UTF_REQUIRE( v2 != MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 2U == g_outstanding );

        auto v3 = my_resource_ref::attach( MyFakeResourceImpl::dup( v1.get() ) );
        UTF_REQUIRE( v3 != MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 3U == g_outstanding );

        const my_resource_ref v4( MyFakeResourceImpl::createFailed() );
        UTF_REQUIRE( v4 == MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 3U == g_outstanding );

        UTF_REQUIRE( v2.get() );
        const auto rawHandle = v2.release();
        UTF_REQUIRE( v2 == MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( rawHandle != MyFakeResourceImpl::get_null() );
        MyFakeResourceImpl::destroy( rawHandle );
        UTF_REQUIRE( 2U == g_outstanding );

        UTF_REQUIRE( v3.get() );
        v3.reset();
        UTF_REQUIRE( v3 == MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 1U == g_outstanding );
    }

    UTF_REQUIRE( 0U == g_outstanding );

    {
        my_resource_ref v1;
        UTF_REQUIRE( 0U == g_outstanding );
        v1 = MyFakeResourceImpl::create();
        UTF_REQUIRE( 1U == g_outstanding );
        UTF_REQUIRE( v1 != MyFakeResourceImpl::get_null() );

        my_resource_ref v2;
        v2 = MyFakeResourceImpl::create();
        UTF_REQUIRE( v2.get() != MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 2U == g_outstanding );

        my_resource_ref v3( std::move( v1 ) );
        UTF_REQUIRE( v3 != MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( v1 == MyFakeResourceImpl::get_null() );
        UTF_REQUIRE( 2U == g_outstanding );

        my_resource_ref v4;
        v4 = std::move( v3 );
        UTF_REQUIRE( MyFakeResourceImpl::get_null() != v4 );
        UTF_REQUIRE( MyFakeResourceImpl::get_null() == v3 );
        UTF_REQUIRE( v3 == v1 );
        UTF_REQUIRE( v4 != v1 );
        UTF_REQUIRE( 2U == g_outstanding );

        my_resource_ref v5( MyFakeResourceImpl::create() );
        v4 = std::move( v5 );

        v4.reset();
        UTF_REQUIRE( 1U == g_outstanding );

        v4.reset( MyFakeResourceImpl::create() );
        UTF_REQUIRE( 2U == g_outstanding );
    }

    UTF_REQUIRE( 0U == g_outstanding );

    for( const auto& pair : g_handleTable )
    {
        UTF_REQUIRE( 0U == pair.second.value() );
    }
}

/************************************************************************
 * Long file names LFN UNC path test
 */

UTF_AUTO_TEST_CASE( BaseLib_LfnPrefixesTests )
{
    /*
     * chk2RemovePrefix( ... ) is pure string manipulation with no platform specific API
     * and it is called from fs::normalizePathParameterForPrint( ... ) - i.e. from every
     * filesystem error message - on every platform, so it must be verified everywhere
     */

    {
        const auto cbCheckRemovePrefix = [](
            SAA_in      const std::string&                          input,
            SAA_in      const std::string&                          expected
            ) -> void
        {
            const auto stripped =
                bl::fs::detail::WinLfnUtils::chk2RemovePrefix( bl::fs::path( input ) ).string();

            UTF_REQUIRE_EQUAL( expected, stripped );

            /*
             * Removing the prefix must be idempotent
             */

            const auto strippedTwice =
                bl::fs::detail::WinLfnUtils::chk2RemovePrefix( bl::fs::path( stripped ) ).string();

            UTF_REQUIRE_EQUAL( stripped, strippedTwice );
        };

        cbCheckRemovePrefix( "\\\\?\\c:\\foo", "c:\\foo" );
        cbCheckRemovePrefix( "\\\\?\\UNC\\server\\share", "\\\\server\\share" );

        /*
         * The bare prefix and a UNC prefix without its trailing separator - the latter
         * does not match g_lfnUncPrefix, so only the plain prefix is stripped
         */

        cbCheckRemovePrefix( "\\\\?\\", "" );
        cbCheckRemovePrefix( "\\\\?\\UNC", "UNC" );

        /*
         * A path which merely contains the prefix away from position 0 must be untouched
         */

        cbCheckRemovePrefix( "c:\\already\\\\?\\inside", "c:\\already\\\\?\\inside" );

        /*
         * The expectation is spelled through fs::path rather than as a literal because on
         * Windows the separators are normalized when the path is constructed - see the note
         * in chk2AddPrefix( ... ) - so this input round-trips as 'relative\path' there and as
         * 'relative/path' everywhere else. What the case pins is that removing the prefix
         * does not disturb a path which never had one
         */

        cbCheckRemovePrefix( "relative/path", bl::fs::path( "relative/path" ).string() );
        cbCheckRemovePrefix( "", "" );
    }

    if( ! bl::os::onWindows() )
    {
        /*
         * The rest of the test covers chk2AddPrefix( ... ), which depends on
         * path::is_absolute() and is therefore Windows only
         */

        return;
    }

    std::string pathStrNormal( "c:\\" );
    std::string pathStrUnc( "\\\\foo\\bar" );

    bl::fs::path pathNormal( pathStrNormal );
    bl::fs::path pathUnc( pathStrUnc );

    UTF_REQUIRE_EQUAL( bl::fs::path( pathNormal ), pathNormal );
    UTF_REQUIRE_EQUAL( bl::fs::path( pathUnc ), pathUnc );

    const auto pathStrNormalPrefixed = pathNormal.string();
    const auto pathStrUncPrefixed = pathUnc.string();

    const std::string lfnRoot = bl::fs::detail::WinLfnUtils::g_lfnRootPath.string();
    const std::string lfnUncRoot = bl::fs::detail::WinLfnUtils::g_lfnUncRootPath.string();

    UTF_REQUIRE( 0 == pathStrNormalPrefixed.find( lfnRoot ) );
    UTF_REQUIRE( 0 == pathStrUncPrefixed.find( lfnUncRoot ) );

    {
        const auto pathStrNormalReconstructed = pathStrNormalPrefixed.substr( lfnRoot.size() );
        UTF_REQUIRE_EQUAL( pathStrNormal, pathStrNormalReconstructed );

        const auto pathStrUncReconstructed = "\\\\" + pathStrUncPrefixed.substr( lfnUncRoot.size() );
        UTF_REQUIRE_EQUAL( pathStrUnc, pathStrUncReconstructed );
    }

    {
        const auto pathStrNormalReconstructed =
            bl::fs::detail::WinLfnUtils::chk2RemovePrefix( bl::cpp::copy( pathNormal ) ).string();
        UTF_REQUIRE_EQUAL( pathStrNormal, pathStrNormalReconstructed );

        const auto pathStrUncReconstructed =
            bl::fs::detail::WinLfnUtils::chk2RemovePrefix( bl::cpp::copy( pathUnc ) ).string();
        UTF_REQUIRE_EQUAL( pathStrUnc, pathStrUncReconstructed );
    }

    {
        const auto pathStrNormalReconstructed =
            bl::fs::detail::WinLfnUtils::chk2RemovePrefix(
                bl::fs::detail::WinLfnUtils::chk2RemovePrefix( bl::cpp::copy( pathNormal ) )
                ).string();
        UTF_REQUIRE_EQUAL( pathStrNormal, pathStrNormalReconstructed );

        const auto pathStrUncReconstructed =
            bl::fs::detail::WinLfnUtils::chk2RemovePrefix(
                bl::fs::detail::WinLfnUtils::chk2RemovePrefix( bl::cpp::copy( pathUnc ) )
                ).string();
        UTF_REQUIRE_EQUAL( pathStrUnc, pathStrUncReconstructed );
    }

    {
        /*
         * chk2AddPrefix( ... ) boundaries - note that on Windows fs::path itself routes
         * through chk2AddPrefix( ... ), so the explicit call below is the second (and
         * idempotent) application of it
         */

        const auto cbAddPrefix = []( SAA_in const std::string& input ) -> std::string
        {
            return bl::fs::detail::WinLfnUtils::chk2AddPrefix( bl::fs::path( input ) ).string();
        };

        UTF_REQUIRE(
            bl::fs::detail::WinLfnUtils::chk2AddPrefix( bl::fs::path() ).empty()
            );

        UTF_REQUIRE_EQUAL( std::string( "relative\\path" ), cbAddPrefix( "relative\\path" ) );

        UTF_REQUIRE_EQUAL( std::string( "\\\\?\\c:\\foo" ), cbAddPrefix( "\\\\?\\c:\\foo" ) );

        UTF_REQUIRE_EQUAL( std::string( "\\\\?\\c:\\foo" ), cbAddPrefix( "c:\\foo" ) );

        UTF_REQUIRE_EQUAL(
            std::string( "\\\\?\\UNC\\server\\share" ),
            cbAddPrefix( "\\\\server\\share" )
            );

        /*
         * Forward slashes are legal separators on Windows everywhere except under the long
         * file name prefix, which switches path parsing off - so they have to be normalized
         * before the prefix is applied, or the result names nothing. A package produced on a
         * UNIX host stores its relative paths that way, which is how this reaches production
         */

        UTF_REQUIRE_EQUAL( std::string( "\\\\?\\c:\\foo" ), cbAddPrefix( "c:/foo" ) );

        UTF_REQUIRE_EQUAL( std::string( "relative\\path" ), cbAddPrefix( "relative/path" ) );

        /*
         * The UNC detection tests for backslashes only, so a forward-slash share was not
         * recognised before the normalization and came out as \\?\//server/share
         *
         * Note this holds for BOOST_FILESYSTEM_VERSION 3, which is what consumers get by
         * default and what this repository builds against; the version 4 make_preferred( )
         * deliberately leaves the root name alone, which would defeat it
         */

        UTF_REQUIRE_EQUAL(
            std::string( "\\\\?\\UNC\\server\\share" ),
            cbAddPrefix( "//server/share" )
            );

        /*
         * Three or more leading backslashes are parsed by Boost.Filesystem as a root
         * directory followed by redundant separators - there is no root name, so such a
         * path is not absolute and it is returned unchanged
         *
         * What matters is that it is never mistaken for a UNC share
         */

        UTF_REQUIRE_EQUAL( std::string( "\\\\\\weird" ), cbAddPrefix( "\\\\\\weird" ) );

        /*
         * Adding and then removing the prefix must be lossless for both forms
         */

        const auto cbCheckRoundTrip = []( SAA_in const std::string& original ) -> void
        {
            UTF_REQUIRE_EQUAL(
                original,
                bl::fs::detail::WinLfnUtils::chk2RemovePrefix(
                    bl::fs::detail::WinLfnUtils::chk2AddPrefix( bl::fs::path( original ) )
                    ).string()
                );
        };

        cbCheckRoundTrip( "c:\\foo" );
        cbCheckRoundTrip( "\\\\server\\share" );
    }
}

UTF_AUTO_TEST_CASE( BaseLib_LfnUNCPathTests )
{
    if( ! bl::os::onWindows() )
    {
        /*
         * This is Windows only test
         */

        return;
    }

    if( test::UtfArgsParser::path().empty() )
    {
        /*
         * This test requires command line parameter to
         * verify manually UNC paths work
         */

        return;
    }

    bl::fs::path rootDir( test::UtfArgsParser::path() );

    for( bl::fs::directory_iterator end, it( rootDir ) ; it != end; ++it  )
    {
        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "Path "
                << it -> path()
            );
    }
}

/************************************************************************
 * Container helper tests
 */

UTF_AUTO_TEST_CASE( BaseLib_ContainerHelperTests )
{
    const std::string value = "str-1";

    {
        std::unordered_set< std::string > set;

        UTF_REQUIRE( ! bl::cpp::contains( set, value ) );

        set.insert( value );

        UTF_REQUIRE( bl::cpp::contains( set, value ) );
    }

    {
        std::set< std::string > set;

        UTF_REQUIRE( ! bl::cpp::contains( set, value ) );

        set.insert( value );

        UTF_REQUIRE( bl::cpp::contains( set, value ) );
    }

    {
        std::unordered_map< std::string, std::string > map;

        UTF_REQUIRE( ! bl::cpp::contains( map, value ) );

        map[ value ] = value;

        UTF_REQUIRE( bl::cpp::contains( map, value ) );
    }

    {
        std::map< std::string, std::string > map;

        UTF_REQUIRE( ! bl::cpp::contains( map, value ) );

        map[ value ] = value;

        UTF_REQUIRE( bl::cpp::contains( map, value ) );
    }
}

/************************************************************************
 * Base64 encoding / decoding tests
 */

UTF_AUTO_TEST_CASE( BaseLib_Base64Tests )
{
    UTF_MESSAGE( "***************** BaseLib_Base64Tests tests *****************\n" );

    {
        /*
         * Encoding and decoding with padding basic tests
         */

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64EncodeString(
                "Lorem ipsum dolor sit amet, consectetur adipisicing elit,"
                ),
            "TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2ljaW5nIGVsaXQs"
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64DecodeString(
                "TG9yZW0gaXBzdW0gZG9sb3Igc2l0IGFtZXQsIGNvbnNlY3RldHVyIGFkaXBpc2ljaW5nIGVsaXQs"
                ),
            "Lorem ipsum dolor sit amet, consectetur adipisicing elit,"
            );

        /*
         * encoding/decoding - ground truth for padding cases
         */

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64EncodeString( "" ),
            ""
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64DecodeString( "" ),
            ""
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64EncodeString( "a" ),
            "YQ=="
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64DecodeString( "YQ==" ),
            "a"
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64EncodeString( "aa" ),
            "YWE="
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64DecodeString( "YWE=" ),
            "aa"
            );

        UTF_CHECK_EQUAL(
            bl::SerializationUtils::base64DecodeString( "TG9y" ),
            "Lor"
            );

        {
            /*
             * Malformed input must be rejected before it is handed to the Boost iterators
             *
             * A data length of 1 modulo 4 can't be produced by the encoder and it would
             * make the transform read past the end of the string
             */

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "TG9yZ" ),
                bl::ArgumentException
                );

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "TG9yZ=" ),
                bl::ArgumentException
                );

            /*
             * At most two padding characters are legal
             */

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "YQ===" ),
                bl::ArgumentException
                );

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "====" ),
                bl::ArgumentException
                );

            /*
             * Characters outside of the base64 alphabet - note that '=' only counts as
             * padding when it is trailing, so here it is rejected as an invalid character
             */

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "TG=y" ),
                bl::ArgumentException
                );

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "TG9\n" ),
                bl::ArgumentException
                );

            /*
             * The base64url alphabet must not be accepted by the base64 decoder
             */

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "TG-y" ),
                bl::ArgumentException
                );

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeString( "TG_y" ),
                bl::ArgumentException
                );

            /*
             * The validation lives in the template and not in the std::string wrapper, so
             * decoding into a vector of bytes must fail in exactly the same way
             */

            UTF_REQUIRE_THROW(
                bl::SerializationUtils::base64DecodeVector( "TG9yZ" ),
                bl::ArgumentException
                );

            /*
             * Positive control to ensure the accepted alphabet can't be narrowed later -
             * '+' is 62 (111110) and '/' is 63 (111111), so the first octet is 0xFB
             */

            UTF_REQUIRE_EQUAL(
                bl::SerializationUtils::base64DecodeString( "+/==" ),
                std::string( "\xFB" )
                );

            /*
             * The rejected input must never be echoed back in the exception
             */

            try
            {
                bl::SerializationUtils::base64DecodeString( "TG9yZ" );
                UTF_FAIL( BL_MSG() << "base64DecodeString must throw" );
            }
            catch( bl::ArgumentException& e )
            {
                UTF_REQUIRE( ! bl::eh::get_error_info< bl::eh::errinfo_string_value >( e ) );
                UTF_REQUIRE( ! bl::cpp::contains( std::string( e.what() ), "TG9yZ" ) );
            }
        }

        {
            /*
             * Test to ensure we can deal with encoding zeros in std::string
             */

            std::string original( "Lor" );

            original.append( 2U, '\0' );
            UTF_REQUIRE_EQUAL( original.size(), 5U );

            const auto encoded = bl::SerializationUtils::base64EncodeString( original );
            const auto decoded = bl::SerializationUtils::base64DecodeString( encoded );

            UTF_REQUIRE_EQUAL( decoded.size(), original.size() );
            UTF_REQUIRE_EQUAL( decoded[ 3 ], '\0' );
            UTF_REQUIRE_EQUAL( decoded[ 4 ], '\0' );
            UTF_REQUIRE_EQUAL( decoded, original );
        }

        {
            /*
             * Test the encoding and decoding of vector of bytes which also includes zero
             */

            std::vector< unsigned char > original;

            for( std::size_t i = 0U; i < 13; ++i )
            {
                original.push_back( ( unsigned char ) i );
            }

            const auto encoded = bl::SerializationUtils::base64Encode( original.data(), original.size() );
            const auto decoded = bl::SerializationUtils::base64DecodeVector( encoded );

            UTF_REQUIRE_EQUAL( decoded.size(), original.size() );

            for( std::size_t i = 0U, count = original.size(); i < count; ++i )
            {
                UTF_REQUIRE_EQUAL( decoded[ i ], original[ i ] );
            }
        }

        {
            /*
             * Encoding and decoding with varying lengths
             */

            std::string input;

            for( std::size_t i = 0; i < 1025; ++i )
            {
                UTF_CHECK_EQUAL(
                    bl::SerializationUtils::base64DecodeString(
                        bl::SerializationUtils::base64EncodeString( input )
                        ),
                        input
                    );

                input += static_cast< char >( i );
            }
        }
    }
}

UTF_AUTO_TEST_CASE( BaseLib_Base64EncodingTests )
{
    const auto currentExecutablePath =
        bl::fs::path( bl::os::getCurrentExecutablePath() );

    bl::fs::TmpDir tmpDir;
    const auto tmpPath = tmpDir.path();
    const auto outputPath = tmpPath / "parentDir" / currentExecutablePath.filename().string();

    std::size_t fileSize;

    const auto encodedString =
        bl::SerializationUtils::encodeFromFileToBase64String( currentExecutablePath, &fileSize );

    UTF_REQUIRE( fileSize > 0U );

    const auto encodedString2 =
        bl::SerializationUtils::encodeFromFileToBase64String( currentExecutablePath );

    UTF_REQUIRE_EQUAL( encodedString2, encodedString );

    UTF_REQUIRE_THROW(
        bl::SerializationUtils::encodeFromFileToBase64String(
            currentExecutablePath,
            &fileSize,
            1L /* maxSize */
            ),
        bl::UnexpectedException
        );

    UTF_REQUIRE( ! encodedString.empty() );

    /*
     * Note that the input is validated before it is handed to the Boost iterators, so an
     * invalid character is reported as a bl::ArgumentException (and the rejected input is
     * deliberately not embedded in the exception)
     */

    UTF_REQUIRE_THROW(
        bl::SerializationUtils::decodeFromBase64StringToFile(
            "invalidBase64String%",
            outputPath
            ),
        bl::ArgumentException
        );

    UTF_REQUIRE( ! fs::path_exists( outputPath ) );
    UTF_REQUIRE( ! fs::path_exists( outputPath.parent_path() ) );

    bl::SerializationUtils::decodeFromBase64StringToFile( encodedString, outputPath );

    UTF_REQUIRE(
        utest::TestFsUtils::compareFileContents(
            currentExecutablePath,
            outputPath,
            true /* ignoreTimestamp */,
            true /* ignoreName */
            )
        );

    {
        /*
         * The maxSize check is "fileSize < maxSize", i.e. a STRICT less-than, so a file of
         * exactly maxSize bytes is rejected. The extreme maxSize == 1 assertion above
         * cannot tell that apart from an implementation which always rejects whenever a
         * maxSize is supplied, nor from a flipped comparison; a 10 byte file tested at
         * both 10 and 11 can
         *
         * Note also that the is.read( ... ) below the check is not followed by a gcount()
         * assertion, so a short read would silently base64 encode uninitialised heap
         * memory - that is not deterministically testable here, but it is worth knowing
         */

        bl::fs::TmpDir boundaryTmpDir;

        const auto& boundaryPath = boundaryTmpDir.path();

        const auto sized = boundaryPath / "sized.bin";

        utest::TestFsUtils::createDummyFile( sized, 10U );

        std::size_t sizedFileSize = 0U;

        UTF_REQUIRE_THROW(
            bl::SerializationUtils::encodeFromFileToBase64String( sized, &sizedFileSize, 10U ),
            bl::UnexpectedException
            );

        UTF_REQUIRE_NO_THROW(
            bl::SerializationUtils::encodeFromFileToBase64String( sized, &sizedFileSize, 11U )
            );

        UTF_REQUIRE_EQUAL( sizedFileSize, 10U );

        /*
         * The zero length round trip - the encoded form of an empty file is the empty
         * string, and decoding it must still create the parent directory and the ( empty )
         * output file rather than skipping the write altogether
         */

        const auto empty = boundaryPath / "empty.bin";

        utest::TestFsUtils::createDummyFile( empty, 0U );

        std::size_t emptyFileSize = 1U;

        UTF_REQUIRE_EQUAL(
            bl::SerializationUtils::encodeFromFileToBase64String( empty, &emptyFileSize ),
            std::string()
            );

        UTF_REQUIRE_EQUAL( emptyFileSize, 0U );

        const auto emptyCopy = boundaryPath / "out" / "empty-copy.bin";

        UTF_REQUIRE( ! bl::fs::path_exists( emptyCopy.parent_path() ) );

        bl::SerializationUtils::decodeFromBase64StringToFile( std::string(), emptyCopy );

        UTF_REQUIRE( bl::fs::path_exists( emptyCopy ) );
        UTF_REQUIRE_EQUAL( bl::fs::file_size( emptyCopy ), 0U );
    }
}

/************************************************************************
 * Base64Url encoding / decoding tests
 */

UTF_AUTO_TEST_CASE( BaseLib_Base64UrlTests )
{
    /*
     * The base64url encoding and decoding APIs are really just small wrappers on top
     * of the base64 encoding APIs which eliminate some characters that are not safe
     * to appear in fine names and URLs such as the padding chars ('=') and the '+'
     * and '/' chars
     *
     * So the tests for these APIs will not duplicate the logic of the base64 core
     * tests above, but we will simply encode / verify and decode / verify a bunch
     * of random buffers
     */

    const std::size_t noOfTestRuns = 1024U;
    const std::size_t maxBufferSize = 1025U;

    std::vector< unsigned char > buffer;
    buffer.resize( maxBufferSize );

    for( std::size_t i = 0U; i < noOfTestRuns; ++i )
    {
        bl::random::uniform_int_distribution< std::size_t > dist( 0U, maxBufferSize );

        const auto bufferSize = dist( bl::tlsData().random.urng() );

        if( bufferSize )
        {
            random::getRandomBytes( buffer.data(), bufferSize );
        }

        /*
         * Test both the string and the vector versions of the APIs
         */

        const auto cbVerifyEncoded = [ & ]( SAA_in const std::string& encoded ) -> void
        {
            if( bufferSize )
            {
                UTF_REQUIRE( encoded.size() );
            }

            UTF_REQUIRE( ! cpp::contains( encoded, '=' ) );
            UTF_REQUIRE( ! cpp::contains( encoded, '+' ) );
            UTF_REQUIRE( ! cpp::contains( encoded, '/' ) );

            {
                const auto decoded = bl::SerializationUtils::base64UrlDecodeVector( encoded );

                UTF_REQUIRE_EQUAL( decoded.size(), bufferSize );

                if( bufferSize )
                {
                    UTF_REQUIRE_EQUAL( 0, ::memcmp( decoded.data(), buffer.data(), bufferSize ) );
                }
            }

            {
                const auto decoded = bl::SerializationUtils::base64UrlDecodeString( encoded );

                UTF_REQUIRE_EQUAL( decoded.size(), bufferSize );

                if( bufferSize )
                {
                    UTF_REQUIRE_EQUAL( 0, ::memcmp( decoded.data(), buffer.data(), bufferSize ) );
                }
            }
        };

        {
            const auto encoded = bl::SerializationUtils::base64UrlEncode( buffer.data(), bufferSize );
            cbVerifyEncoded( encoded );
        }

        {
            const std::string text( buffer.begin(), buffer.begin() + bufferSize );
            const auto encoded = bl::SerializationUtils::base64UrlEncodeString( text );
            cbVerifyEncoded( encoded );
        }
    }

    /*
     * The assertions below are deliberately outside the random buffer loop, so a failure
     * names the offending input
     *
     * The loop above only ever feeds the output of base64UrlEncode, whose length is
     * never 1 mod 4, so the rejection branch and the difference between the padding arms
     * are never reached by it
     */

    {
        /*
         * A length of 1 mod 4 cannot be padded into valid base64 and must be rejected
         */

        const std::string bad( "TG9yZ" );
        const std::string badLonger( "TG9yZW1wc" );

        UTF_REQUIRE_EQUAL( 1U, bad.size() % 4U );
        UTF_REQUIRE_EQUAL( 1U, badLonger.size() % 4U );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::SerializationUtils::base64UrlDecodeString( bad ),
            bl::ArgumentException,
            "Invalid base64url encoded string"
            );

        UTF_REQUIRE_THROW( bl::SerializationUtils::base64UrlDecodeVector( bad ), bl::ArgumentException );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::SerializationUtils::base64UrlDecodeString( badLonger ),
            bl::ArgumentException,
            "Invalid base64url encoded string"
            );

        UTF_REQUIRE_THROW( bl::SerializationUtils::base64UrlDecodeVector( badLonger ), bl::ArgumentException );

        /*
         * The input is attacker supplied and it travels back to the client through
         * ExceptionProperties::stringValue, so it must not be attached to the exception
         * and it must not appear in the message either
         */

        bool caught = false;

        try
        {
            ( void ) bl::SerializationUtils::base64UrlDecodeString( bad );
        }
        catch( bl::ArgumentException& e )
        {
            caught = true;

            UTF_REQUIRE( ! bl::eh::get_error_info< bl::eh::errinfo_string_value >( e ) );
            UTF_REQUIRE( ! bl::cpp::contains( std::string( e.what() ), bad ) );
        }

        UTF_REQUIRE( caught );

        /*
         * The three padding arms of the switch must stay distinct - the round trip test
         * above cannot tell them apart, because it only ever supplies encoder output
         */

        UTF_REQUIRE_EQUAL( bl::SerializationUtils::base64UrlDecodeString( "YQ" ), std::string( "a" ) );
        UTF_REQUIRE_EQUAL( bl::SerializationUtils::base64UrlDecodeString( "YWE" ), std::string( "aa" ) );
        UTF_REQUIRE_EQUAL( bl::SerializationUtils::base64UrlDecodeString( "YQ==" ), std::string( "a" ) );
    }
}

/************************************************************************
 * str::utf8ToIso88591Simple tests
 */

UTF_AUTO_TEST_CASE( BaseLib_Utf8ToIso88591SimpleTests )
{
    using namespace bl::str;
    /*
     * Test utf8ToIso88591Simple:
     *
     * 1. ASCII only string
     * 2. UTF-8 string with ASCII only characters
     * 3. Invalid UTF-8 character sequence
     * 4. UTF-8 string with non-ASCII characters
     */

    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "Hello, world!" ) ), std::string( "Hello, world!" ) );
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xA2" ) ), std::string( "\xA2" ) );

    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC2\x70" ) ), bl::ArgumentException );
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC2\xA2\xC2" ) ), bl::ArgumentException );
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xF0\x9F\x98\x81" ) ), bl::ArgumentException );

    /*
     * Additional comprehensive tests
     */

    /* Test empty string */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "" ) ), std::string( "" ) );

    /* Test boundary characters */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\x7F" ) ), std::string( "\x7F" ) ); /* Highest ASCII */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\x80" ) ), std::string( "\x80" ) ); /* Lowest extended */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xBF" ) ), std::string( "\xBF" ) ); /* End of 0xC2 range */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC3\x80" ) ), std::string( "\xC0" ) ); /* Start of 0xC3 range */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC3\xBF" ) ), std::string( "\xFF" ) ); /* Highest ISO-8859-1 */

    /* Test common ISO-8859-1 extended characters */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xA0" ) ), std::string( "\xA0" ) ); /* Non-breaking space */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xA3" ) ), std::string( "\xA3" ) ); /* Pound sign £ */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xA9" ) ), std::string( "\xA9" ) ); /* Copyright © */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xAE" ) ), std::string( "\xAE" ) ); /* Registered ® */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC2\xB0" ) ), std::string( "\xB0" ) ); /* Degree ° */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC3\x80" ) ), std::string( "\xC0" ) ); /* À */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC3\xA9" ) ), std::string( "\xE9" ) ); /* é */
    UTF_CHECK_EQUAL( utf8ToIso88591Simple( std::string( "\xC3\xB1" ) ), std::string( "\xF1" ) ); /* ñ */

    /* Test mixed ASCII and extended characters */
    {
        const std::string utf8Input = std::string( "Hello " ) + std::string( "\xC2\xA9" ) + std::string( " 2025" );
        const std::string iso88591Expected = std::string( "Hello " ) + std::string( "\xA9" ) + std::string( " 2025" );
        UTF_CHECK_EQUAL( utf8ToIso88591Simple( utf8Input ), iso88591Expected );
    }
    {
        const std::string utf8Input = std::string( "\xC3\xA9" ) + std::string( "cole" );
        const std::string iso88591Expected = std::string( "\xE9" ) + std::string( "cole" );
        UTF_CHECK_EQUAL( utf8ToIso88591Simple( utf8Input ), iso88591Expected );
    }

    /* Test invalid UTF-8 start bytes (0x80-0xBF are continuation bytes, invalid as start) */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\x80" ) ), bl::ArgumentException );
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xBF" ) ), bl::ArgumentException );

    /* Test invalid UTF-8 overlong encodings (0xC0-0xC1 can encode ASCII as 2 bytes, which is invalid) */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC0\x80" ) ), bl::ArgumentException );
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC1\xBF" ) ), bl::ArgumentException );

    /* Test invalid continuation bytes */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC2\x00", 2 ) ), bl::ArgumentException ); /* NULL as continuation (explicit length: the literal would otherwise stop at the NUL) */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC2\x7F" ) ), bl::ArgumentException ); /* ASCII as continuation */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC2\xC0" ) ), bl::ArgumentException ); /* Start byte as continuation */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC3\xFF" ) ), bl::ArgumentException ); /* Invalid continuation */

    /* Test incomplete UTF-8 sequences at end of string */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "Hello\xC2" ) ), bl::ArgumentException );
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "Hello\xC3" ) ), bl::ArgumentException );

    /* Test 3-byte UTF-8 sequences (outside ISO-8859-1 range) */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xE2\x82\xAC" ) ), bl::ArgumentException ); /* Euro sign € */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xE4\xB8\xAD" ) ), bl::ArgumentException ); /* Chinese character */

    /* Test 4-byte UTF-8 sequences (outside ISO-8859-1 range) */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xF0\x9F\x98\x80" ) ), bl::ArgumentException ); /* Emoji 😀 */

    /* Test characters just outside ISO-8859-1 range */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC4\x80" ) ), bl::ArgumentException ); /* Ā (0x100) */
    UTF_REQUIRE_THROW( utf8ToIso88591Simple( std::string( "\xC5\x93" ) ), bl::ArgumentException ); /* œ (0x153) */

    /* Test mixed valid and invalid sequences */
    {
        const std::string invalidInput = std::string( "Hello " ) + std::string( "\xC2\xA9" ) +
                                         std::string( " World " ) + std::string( "\xF0\x9F\x98\x81" );
        UTF_REQUIRE_THROW( utf8ToIso88591Simple( invalidInput ), bl::ArgumentException );
    }

    /*
     * Compare utf8ToIso88591Simple against Boost.Locale to verify compatibility
     * The simple implementation should produce identical results to:
     * bl::str::from_utf( content, "ISO-8859-1", str::method_type::stop )
     */

    /* Test ASCII string matches Boost.Locale */
    {
        const std::string input = "Hello, world!";
        const std::string simpleResult = utf8ToIso88591Simple( input );
        const std::string localeResult = str::from_utf( input, "ISO-8859-1", str::method_type::stop );
        UTF_CHECK_EQUAL( simpleResult, localeResult );
    }

    /* Test all valid ISO-8859-1 extended characters match Boost.Locale */
    {
        /* Test range 0x80-0xBF (UTF-8: 0xC2 0x80 to 0xC2 0xBF) */
        for( unsigned char ch = 0x80; ch <= 0xBF; ++ch )
        {
            const std::string utf8Input = std::string( "\xC2" ) + std::string( 1, static_cast< char >( ch ) );
            const std::string simpleResult = utf8ToIso88591Simple( utf8Input );
            const std::string localeResult = str::from_utf( utf8Input, "ISO-8859-1", str::method_type::stop );
            UTF_CHECK_EQUAL( simpleResult, localeResult );
        }

        /* Test range 0xC0-0xFF (UTF-8: 0xC3 0x80 to 0xC3 0xBF) */
        for( unsigned char ch = 0x80; ch <= 0xBF; ++ch )
        {
            const std::string utf8Input = std::string( "\xC3" ) + std::string( 1, static_cast< char >( ch ) );
            const std::string simpleResult = utf8ToIso88591Simple( utf8Input );
            const std::string localeResult = str::from_utf( utf8Input, "ISO-8859-1", str::method_type::stop );
            UTF_CHECK_EQUAL( simpleResult, localeResult );
        }
    }

    /* Test mixed content matches Boost.Locale */
    {
        const std::string input = std::string( "Hello " ) + std::string( "\xC2\xA9" ) +
                                  std::string( " 2025 " ) + std::string( "\xC3\xA9" ) +
                                  std::string( "cole" );
        const std::string simpleResult = utf8ToIso88591Simple( input );
        const std::string localeResult = str::from_utf( input, "ISO-8859-1", str::method_type::stop );
        UTF_CHECK_EQUAL( simpleResult, localeResult );
    }

    /* Test that both implementations throw on invalid UTF-8 */
    {
        const std::string invalidUtf8 = std::string( "\xC2\x70" ); /* Invalid continuation byte */
        UTF_REQUIRE_THROW( utf8ToIso88591Simple( invalidUtf8 ), bl::ArgumentException );
        UTF_REQUIRE_THROW( str::from_utf( invalidUtf8, "ISO-8859-1", str::method_type::stop ), std::exception );
    }

    /* Test that both implementations throw on out-of-range characters */
    {
        const std::string outOfRange = std::string( "\xE2\x82\xAC" ); /* Euro sign € (U+20AC) */
        UTF_REQUIRE_THROW( utf8ToIso88591Simple( outOfRange ), bl::ArgumentException );
        UTF_REQUIRE_THROW( str::from_utf( outOfRange, "ISO-8859-1", str::method_type::stop ), std::exception );
    }
}

#if defined( BL_DEVENV_VERSION ) && BL_DEVENV_VERSION < 6
// TODO: Re-enable the dependency on Boost.Locale when it is fixed
/*
 * The dependency on Boost.Locale is removed fror devenv 5 and above
 */

/************************************************************************
 * bl::str::from_utf tests
 */

UTF_AUTO_TEST_CASE( BaseLib_StringUtilsFromUtfTests )
{
    /*
     * Test bl::str::from_utf:
     *
     * 1. ASCII only string
     * 2. UTF-8 string with ASCII only characters
     * 3. Invalid UTF-8 character sequence
     * 4. UTF-8 string with non-ASCII characters
     */

    UTF_CHECK_EQUAL( bl::str::from_utf( std::string( "Hello, world!" ), "ISO-8859-1", bl::str::method_type::stop ), std::string( "Hello, world!" ) );
    UTF_CHECK_EQUAL( bl::str::from_utf( std::string( "\xC2\xA2" ), "ISO-8859-1", bl::str::method_type::stop ), std::string( "\xA2" ) );

    /*
    UTF_REQUIRE_THROW( bl::str::from_utf( std::string( "\xC2\x70" ), "ISO-8859-1", str::method_type::stop ), bl::ArgumentException );
    UTF_REQUIRE_THROW( bl::str::from_utf( std::string( "\xC2\xA2\xC2" ), "ISO-8859-1", str::method_type::stop ), bl::ArgumentException );
    UTF_REQUIRE_THROW( bl::str::from_utf( std::string( "\xF0\x9F\x98\x81" ), "ISO-8859-1", str::method_type::stop ), bl::ArgumentException );
    */
}
#endif /* #if defined( BL_DEVENV_VERSION ) && BL_DEVENV_VERSION < 6 */

/************************************************************************
 * URI encoding / decoding tests
 */

UTF_AUTO_TEST_CASE( BaseLib_URIEncodeDecodeTests )
{
    using namespace bl::str;

    UTF_MESSAGE( "***************** BaseLib_URIEncodeDecodeTests tests *****************\n" );

    {

        /*
         * just an example of the encoding/decoding usage, where no percent
         * encoding is used
         */

        const std::string input( "\"Aardvarks lurk, OK? And they lurk in /dev/null!\"" );

        UTF_CHECK_EQUAL( uriDecode( uriEncode( input ) ), input );
    }

    {
        /*
         * Every byte value must round-trip - the lookup tables are indexed with the
         * character, so on a platform where char is signed the bytes from 0x80 to 0xFF
         * would index before the tables and the emitted hex digits would be garbage
         */

        std::string input;

        for( unsigned int i = 1U; i < 256U; ++i )
        {
            input.push_back( static_cast< char >( static_cast< unsigned char >( i ) ) );
        }

        const auto encoded = uriEncode( input );

        for( const auto ch : encoded )
        {
            const auto value = static_cast< unsigned char >( ch );

            UTF_REQUIRE( value >= 0x20U && value < 0x7FU );
        }

        UTF_REQUIRE_EQUAL( uriDecode( encoded ), input );

        const auto encodedUnsafeOnly = uriEncodeUnsafeOnly( input );

        UTF_REQUIRE_EQUAL( uriDecode( encodedUnsafeOnly ), input );

        /*
         * Inputs which are too short to hold an escape sequence must be returned as they are
         */

        UTF_REQUIRE_EQUAL( uriDecode( "" ), "" );
        UTF_REQUIRE_EQUAL( uriDecode( "a" ), "a" );
        UTF_REQUIRE_EQUAL( uriDecode( "%4" ), "%4" );
    }

    {

        /*
         * safe characters, which need not be encoded
         */

        const std::string input( "%41%42%43%44%45%46%47%48%49%4A%4B%4C%4D%4E%4F" );
        const std::string output( "ABCDEFGHIJKLMNO" );

        UTF_CHECK_EQUAL( uriDecode( input ), output );

        UTF_CHECK( uriEncode( uriDecode( input ) ) == output || uriEncode( uriDecode( input ) ) == input );
    }

    {

        /*
         * unsafe characters, which must be encoded
         */

        const std::string input( "%3A%3B%3C%3D%3E%3F%40" );
        const std::string output( ":;<=>?@" );

        UTF_CHECK_EQUAL( uriDecode( input ), output );

        UTF_CHECK_EQUAL( uriEncode( uriDecode( input ) ), input );
    }

    {

        /*
         * unsafe only encode testing
         *
         * "<>#%{}|\^~[]`
         * 0x22 0x3C 0x3E 0x23 0x25 0x7B 0x7D 0x7C 0x5C 0x5E 0x7E 0x5B 0x5D 0x60
         */

        const std::string input( "\"<>#%{}|\\^~[]`" );

        const std::string inputNoPercent( "\"<>#{}|\\^~[]`" );

        UTF_CHECK_EQUAL(
            uriEncodeUnsafeOnly( input ),
            std::string( "%22%3C%3E%23%%7B%7D%7C%5C%5E%7E%5B%5D%60" )
            );

        UTF_CHECK_EQUAL(
            uriEncodeUnsafeOnly( inputNoPercent ),
            std::string( "%22%3C%3E%23%7B%7D%7C%5C%5E%7E%5B%5D%60" )
            );

        UTF_CHECK_EQUAL(
            uriEncodeUnsafeOnly( input, true /* escapePercent */ ),
            std::string( "%22%3C%3E%23%25%7B%7D%7C%5C%5E%7E%5B%5D%60" )
            );

        UTF_CHECK_EQUAL( uriDecode( uriEncodeUnsafeOnly( input ) ), input );

        UTF_CHECK_EQUAL( uriDecode( uriEncodeUnsafeOnly( inputNoPercent ) ), inputNoPercent );

        const std::string inputMixed( "\"<>#%{}|\\^~[]`\"Aardvarks lurk, OK? And they lurk in /dev/null!\"" );

        UTF_CHECK_EQUAL( uriDecode( uriEncodeUnsafeOnly( inputMixed ) ), inputMixed );
    }
}

/************************************************************************
 * Text file encoding / decoding tests
 */

namespace
{
    void TestFileEncoding(
        SAA_in      const bl::fs::path&                     fileName,
        SAA_in      const std::string&                      content,
        SAA_in      const bl::encoding::TextFileEncoding    encoding,
        SAA_in      const std::uint64_t                     fileSize
        )
    {
        using namespace bl::encoding;

        if( bl::os::onUNIX() && ( encoding == TextFileEncoding::Utf16LE || encoding == TextFileEncoding::Utf16LE_NoPreamble ) )
        {
            UTF_REQUIRE_THROW( writeTextFile( fileName, content, encoding ), bl::NotSupportedException );

            return;
        }

        writeTextFile( fileName, content, encoding );

        UTF_CHECK_EQUAL( fileSize, bl::fs::file_size( fileName ) );

        TextFileEncoding resultEncoding;

        const auto resultContent = readTextFile( fileName, &resultEncoding );

        UTF_REQUIRE_EQUAL( resultEncoding, encoding );

        UTF_REQUIRE_EQUAL( resultContent, content );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( BaseLib_TextFilesEncodingTests )
{
    using namespace bl::encoding;

    UTF_MESSAGE( "***************** BaseLib_TextFilesEncodingTests tests *****************\n" );

    bl::fs::TmpDir tmpDir;

    const auto textFile = tmpDir.path() / "test.txt";

    const std::string asciiContent = "Test\n";
    const std::string utf8Content = "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82\n";
    const std::string emptyContent;

    TestFileEncoding( textFile, asciiContent, TextFileEncoding::Ascii, asciiContent.length() );
    TestFileEncoding( textFile, utf8Content, TextFileEncoding::Ascii, utf8Content.length() );
    TestFileEncoding( textFile, emptyContent, TextFileEncoding::Ascii, emptyContent.length() );

    TestFileEncoding( textFile, asciiContent, TextFileEncoding::Utf8, asciiContent.length() + 3 );
    TestFileEncoding( textFile, utf8Content, TextFileEncoding::Utf8, utf8Content.length() + 3 );
    TestFileEncoding( textFile, emptyContent, TextFileEncoding::Utf8, emptyContent.length() + 3 );

    TestFileEncoding( textFile, asciiContent, TextFileEncoding::Utf16LE, asciiContent.length() * 2 + 2 );
    TestFileEncoding( textFile, utf8Content, TextFileEncoding::Utf16LE, 12 );
    TestFileEncoding( textFile, emptyContent, TextFileEncoding::Utf16LE, 2 );

    /*
     * writeTextFile( ... ) is documented to create the parent path if it doesn't exist,
     * but every case above writes into a directory which already exists
     */

    const auto nestedDir = tmpDir.path() / "created" / "by" / "writeTextFile";
    const auto nested = nestedDir / "note.txt";

    writeTextFile( nested, "nested", TextFileEncoding::Utf8_NoPreamble );

    UTF_REQUIRE( bl::fs::is_directory( nestedDir ) );
    UTF_REQUIRE_EQUAL( readTextFile( nested ), std::string( "nested" ) );

    /*
     * No preamble was written, so the file holds the content and nothing else
     */

    UTF_REQUIRE_EQUAL( bl::fs::file_size( nested ), 6U );

    /*
     * The default: arm of the encoding validation; TextFileEncoding::Unknown is the only
     * unreachable enumerator, since Ascii and Utf8_NoPreamble are the same value
     *
     * The encoding is validated BEFORE the target file is opened for writing, so a rejected
     * call leaves the existing file exactly as it was rather than truncating it
     */

    const std::string preservedContent( "must survive a rejected write" );

    writeTextFile( textFile, preservedContent, TextFileEncoding::Utf8_NoPreamble );

    UTF_REQUIRE_EQUAL( readTextFile( textFile ), preservedContent );

    UTF_REQUIRE_THROW_MESSAGE(
        writeTextFile( textFile, "x", static_cast< TextFileEncoding >( 0 ) ),
        bl::UnexpectedException,
        "Invalid TextFileEncoding"
        );

    UTF_REQUIRE_EQUAL( readTextFile( textFile ), preservedContent );

    UTF_REQUIRE_THROW_MESSAGE(
        writeTextFile( textFile, "x", static_cast< TextFileEncoding >( 4242 ) ),
        bl::UnexpectedException,
        "Invalid TextFileEncoding"
        );

    UTF_REQUIRE_EQUAL( readTextFile( textFile ), preservedContent );

    if( bl::os::onUNIX() )
    {
        /*
         * The unsupported UTF-16LE arm is validated in the same place, so it does not
         * truncate the file either
         */

        UTF_REQUIRE_THROW(
            writeTextFile( textFile, "x", TextFileEncoding::Utf16LE ),
            bl::NotSupportedException
            );

        UTF_REQUIRE_EQUAL( readTextFile( textFile ), preservedContent );
    }

    /*
     * Files shorter than the preambles - the size >= preamble.size() guard in
     * checkFilePreamble( ... ) and the ftell - buffer.size() rewind which puts the file
     * position back when the bytes read are not a preamble after all
     *
     * Utf8_NoPreamble makes the on disk bytes exactly the content, so a 1 byte file is
     * below both preambles and a 2 byte file is below the UTF-8 one and exactly the size
     * of the UTF-16LE one
     */

    {
        const char* const smallContents[] = { "a", "ab", "abc" };

        for( std::size_t i = 0; i < BL_ARRAY_SIZE( smallContents ); ++i )
        {
            const std::string content( smallContents[ i ] );

            /*
             * Not named 'small' - rpcndr.h, which the Windows SDK headers pull in, defines
             * 'small' as a macro for 'char', so the declaration does not compile on Windows
             */

            const auto smallPath = tmpDir.path() / ( "small" + std::to_string( i ) + ".txt" );

            writeTextFile( smallPath, content, TextFileEncoding::Utf8_NoPreamble );

            UTF_REQUIRE_EQUAL( bl::fs::file_size( smallPath ), content.size() );

            TextFileEncoding enc = TextFileEncoding::Unknown;

            const auto text = readTextFile( smallPath, &enc );

            UTF_REQUIRE_EQUAL( text, content );
            UTF_REQUIRE_EQUAL( enc, TextFileEncoding::Ascii );
        }
    }

    if( bl::os::onUNIX() )
    {
        /*
         * A 2 byte file whose bytes are exactly the UTF-16LE BOM is detected as UTF-16
         * and refused off Windows
         *
         * On Windows it takes the size % sizeof( wchar_t ) == 0 path instead and returns
         * an empty string with encoding Utf16LE, which is a different assertion
         */

        const auto bomOnly = tmpDir.path() / "bom-only.txt";

        writeTextFile( bomOnly, "\xFF\xFE", TextFileEncoding::Utf8_NoPreamble );

        UTF_REQUIRE_EQUAL( bl::fs::file_size( bomOnly ), 2U );

        UTF_REQUIRE_THROW( readTextFile( bomOnly ), bl::NotSupportedException );
    }
}

/************************************************************************
 * Some perf tests for unordered_map rehash function
 */

UTF_AUTO_TEST_CASE( BaseLib_UnorderedMapTests )
{
    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    const std::size_t noOfItems = 5000U * 1000U;

    typedef std::unordered_map< bl::uuid_t, std::size_t > map_t;

    map_t map;

    BL_LOG(
        bl::Logging::info(),
        BL_MSG()
            << "by default map has max load factor of "
            << map.max_load_factor()
        );

    map.max_load_factor( 0.5 );

    {
        bl::utils::ExecutionTimer timer( "unordered_map test no pre-allocate" );

        for( std::size_t i = 0; i < noOfItems; ++i )
        {
            map[ bl::uuids::create() ] = i;
        }

        BL_LOG(
            bl::Logging::info(),
            BL_MSG()
                << "map has "
                << map.bucket_count()
                << " buckets and load factor "
                << map.load_factor()
            );
    }

    {
        bl::utils::ExecutionTimer timer( "unordered_map test pre-allocated (0.5 load factor)" );

        map.rehash( noOfItems * 2U );

        for( std::size_t i = 0; i < noOfItems; ++i )
        {
            map[ bl::uuids::create() ] = i;
        }

        BL_LOG(
            bl::Logging::info(),
            BL_MSG()
                << "map has "
                << map.bucket_count()
                << " buckets and load factor "
                << map.load_factor()
            );
    }

    {
        bl::utils::ExecutionTimer timer( "unordered_map rehash only test" );

        map.rehash( 0 );

        BL_LOG(
            bl::Logging::info(),
            BL_MSG()
                << "map has "
                << map.bucket_count()
                << " buckets and load factor "
                << map.load_factor()
            );
    }
}

/************************************************************************
 * NumberUtils Tests
 */

UTF_AUTO_TEST_CASE( BaseLib_FloatingPointEqualTests )
{
    UTF_REQUIRE( bl::numbers::floatingPointEqual( 1.0, 1.0 ) );

    UTF_REQUIRE( ! bl::numbers::floatingPointEqual( 1.0, 1.0 + 2 * bl::numbers::getDefaultEpsilon< double >() ) );
    UTF_REQUIRE( ! bl::numbers::floatingPointEqual( 1.0f, 1.0f + 2 * bl::numbers::getDefaultEpsilon< float >() ) );

    UTF_REQUIRE( ! bl::numbers::floatingPointEqual( 1.0 + 2 * bl::numbers::getDefaultEpsilon< double >(), 1.0 ) );
    UTF_REQUIRE( ! bl::numbers::floatingPointEqual( 1.0f + 2 * bl::numbers::getDefaultEpsilon< float >(), 1.0f ) );
}

UTF_AUTO_TEST_CASE( BaseLib_SafeCoerceToTests )
{
    using namespace bl;

    std::int8_t i8 = 100;
    std::uint8_t ui8Large = 200U;
    std::uint8_t ui8Small = 100U;
    std::int16_t i16 = 1000;

    UTF_REQUIRE_EQUAL( numbers::safeCoerceTo< std::uint8_t >( i8 ), static_cast< std::uint8_t >( i8 ) );
    UTF_REQUIRE_EQUAL( numbers::safeCoerceTo< std::int8_t >( ui8Small ), static_cast< std::int8_t >( ui8Small ) );

    UTF_REQUIRE_THROW_MESSAGE(
        numbers::safeCoerceTo< std::int8_t >( ui8Large ),
        NumberCoerceException,
        "Cannot coerce number 200 into a smaller numeric type of size 1 (in bytes) "
        "which is also signed and can hold a maximum value of 127"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        numbers::safeCoerceTo< std::uint8_t >( i16 ),
        NumberCoerceException,
        "Cannot coerce number 1000 into a smaller numeric type of size 1 (in bytes) "
        "which is also unsigned and can hold a maximum value of 255"
        );

    const auto ehCallback = []() -> void
    {
        BL_THROW(
            ArgumentException(),
            BL_MSG()
                << "This is custom special message"
            );
    };

    UTF_REQUIRE_THROW_MESSAGE(
        numbers::safeCoerceTo< std::uint8_t >( i16, ehCallback ),
        ArgumentException,
        "This is custom special message"
        );

    UTF_REQUIRE_THROW(
        numbers::safeCoerceTo< std::int32_t >(
            static_cast< std::uint32_t >( std::numeric_limits< std::int32_t >::max() ) + 1U
            ),
        NumberCoerceException
        );

    UTF_REQUIRE_EQUAL(
        numbers::safeCoerceTo< std::uint32_t >( std::numeric_limits< std::int32_t >::max() ),
        static_cast< std::uint32_t >( std::numeric_limits< std::int32_t >::max() )
        );

    UTF_REQUIRE_EQUAL(
        numbers::safeCoerceTo< std::int32_t >( static_cast< std::uint32_t >( std::numeric_limits< std::int32_t >::max() ) ),
        std::numeric_limits< std::int32_t >::max()
        );

    /*
     * All the assertions above only ever coerce non-negative values, so the lower bound
     * check is entered but never violated - the assertions below cover the four dispatch
     * corners where a negative source must be rejected instead of silently wrapping
     */

    /*
     * Equal sizes (signed -> unsigned), i.e. NumberCoerceHelper< false >
     */

    UTF_REQUIRE_THROW_MESSAGE(
        numbers::safeCoerceTo< std::uint32_t >( static_cast< std::int32_t >( -1 ) ),
        NumberCoerceException,
        "Cannot coerce number -1 into a numeric type of size 4 (in bytes) "
        "which is unsigned and can hold a minimum value of 0"
        );

    /*
     * Widening into an unsigned type - this is the pattern used by os::detail::ftell
     * when it coerces a negative off_t into std::uint64_t
     */

    UTF_REQUIRE_THROW(
        numbers::safeCoerceTo< std::uint64_t >( static_cast< std::int32_t >( -1 ) ),
        NumberCoerceException
        );

    /*
     * Narrowing into an unsigned type, i.e. NumberCoerceHelper< true > - this also proves
     * the lower bound check runs before the maximum value check, because -1 does not
     * exceed the maximum of std::uint8_t and would otherwise wrap into 255
     */

    UTF_REQUIRE_THROW(
        numbers::safeCoerceTo< std::uint8_t >( static_cast< std::int16_t >( -1 ) ),
        NumberCoerceException
        );

    /*
     * Narrowing into a signed type which is below its minimum value
     */

    UTF_REQUIRE_THROW_MESSAGE(
        numbers::safeCoerceTo< std::int8_t >( static_cast< std::int32_t >( -200 ) ),
        NumberCoerceException,
        "can hold a minimum value of -128"
        );

    /*
     * The boundaries which must be accepted - the exact minimum value and zero
     */

    UTF_REQUIRE_EQUAL(
        numbers::safeCoerceTo< std::int8_t >( static_cast< std::int32_t >( -128 ) ),
        static_cast< std::int8_t >( -128 )
        );

    UTF_REQUIRE_EQUAL(
        numbers::safeCoerceTo< std::uint32_t >( static_cast< std::int32_t >( 0 ) ),
        0U
        );

    /*
     * Widening signed -> signed must not throw - os::detail::ftell depends on this when
     * it evaluates numbers::safeCoerceTo< off_t >( -1 ) as the expected error value
     */

    UTF_REQUIRE_EQUAL(
        numbers::safeCoerceTo< std::int64_t >( static_cast< std::int32_t >( -1 ) ),
        static_cast< std::int64_t >( -1 )
        );

    /*
     * A lower bound violation must also be routed through the error handling callback
     */

    UTF_REQUIRE_THROW_MESSAGE(
        numbers::safeCoerceTo< std::uint32_t >( static_cast< std::int32_t >( -1 ), ehCallback ),
        ArgumentException,
        "This is custom special message"
        );
}

/************************************************************************
 * os::< get user domain > tests
 */

namespace
{
    /*
     * An independent oracle for os::tryGetUserDomain() which is derived from the
     * documented contract rather than copied from the implementation
     *
     * USERDNSDOMAIN wins when it is set; otherwise USERDOMAIN is the user domain
     * unless it is empty or it is merely the computer name (i.e. a local account)
     */

    std::string expectedUserDomainFromEnvironment()
    {
        const auto dnsDomain = bl::os::tryGetEnvironmentVariable( "USERDNSDOMAIN" );

        if( dnsDomain )
        {
            return *dnsDomain;
        }

        const auto userDomain = bl::os::tryGetEnvironmentVariable( "USERDOMAIN" );

        if( ! userDomain || userDomain -> empty() )
        {
            return std::string();
        }

        const auto computerName = bl::os::tryGetEnvironmentVariable( "COMPUTERNAME" );

        if( computerName && ( *computerName == *userDomain ) )
        {
            return std::string();
        }

        return *userDomain;
    }

    bool isLocalUserOnWindows()
    {
        if( expectedUserDomainFromEnvironment().empty() )
        {
            BL_LOG(
                bl::Logging::debug(),
                BL_MSG()
                    << "Federated login not possible for local users."
                );

            return true;
        }

        return false;
    }
}

UTF_AUTO_TEST_CASE( BaseLib_GetUserDomainTests )
{
    const auto onWindows = bl::os::onWindows();

    if( onWindows )
    {
        const auto domain = bl::os::tryGetUserDomain();

        /*
         * The expectation is computed independently from the environment, so this is a
         * real oracle and not a comparison of the implementation against a copy of itself
         */

        UTF_REQUIRE_EQUAL( expectedUserDomainFromEnvironment(), domain );

        if( isLocalUserOnWindows() )
        {
            UTF_REQUIRE( domain.empty() );

            UTF_REQUIRE_THROW_MESSAGE(
                bl::os::getUserDomain(),
                bl::NotSupportedException,
                "User domain name is not available"
                );
        }
        else
        {
            UTF_REQUIRE( ! domain.empty() );

            const auto domainCopy = bl::os::getUserDomain();

            UTF_REQUIRE_EQUAL( domain, domainCopy );
        }

        /*
         * Now drive the two environment variables directly, so the fall-back path which
         * must read USERDOMAIN (and not USERDNSDOMAIN again) is actually exercised
         */

        {
            const auto dnsDomainSaved = bl::os::tryGetEnvironmentVariable( "USERDNSDOMAIN" );
            const auto userDomainSaved = bl::os::tryGetEnvironmentVariable( "USERDOMAIN" );

            BL_SCOPE_EXIT(
                {
                    if( dnsDomainSaved )
                    {
                        bl::os::setEnvironmentVariable( "USERDNSDOMAIN", *dnsDomainSaved );
                    }
                    else
                    {
                        bl::os::unsetEnvironmentVariable( "USERDNSDOMAIN" );
                    }

                    if( userDomainSaved )
                    {
                        bl::os::setEnvironmentVariable( "USERDOMAIN", *userDomainSaved );
                    }
                    else
                    {
                        bl::os::unsetEnvironmentVariable( "USERDOMAIN" );
                    }
                }
                );

            bl::os::unsetEnvironmentVariable( "USERDNSDOMAIN" );
            bl::os::setEnvironmentVariable( "USERDOMAIN", "SENTINELDOMAIN" );

            UTF_REQUIRE_EQUAL( std::string( "SENTINELDOMAIN" ), bl::os::tryGetUserDomain() );

            /*
             * With both variables unset the account is local and no domain is available
             */

            bl::os::unsetEnvironmentVariable( "USERDOMAIN" );

            UTF_REQUIRE( bl::os::tryGetUserDomain().empty() );
        }
    }
    else
    {
        UTF_REQUIRE_THROW_MESSAGE(
            bl::os::tryGetUserDomain(),
            bl::NotSupportedException,
            "tryGetUserDomain() is not implemented on Linux"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::os::getUserDomain(),
            bl::NotSupportedException,
            "tryGetUserDomain() is not implemented on Linux"
            );
    }
}

/************************************************************************
 * os::< get SPNEGO token > tests
 */

UTF_AUTO_TEST_CASE( BaseLib_GetSPNEGOTokenTests )
{
    const auto server = "testserver.com";

    if( bl::os::onWindows() )
    {
        if( isLocalUserOnWindows() )
        {
            /*
             * This test cannot run if the user is a local user on Windows
             */

            return;
        }

        const auto domain = bl::os::getUserDomain();

        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "SPNEGO token will be requested for server '"
                << server
                << "' and domain '"
                << domain
                << "'"
            );

        const auto tokenBuffer = bl::os::getSPNEGOtoken( server, domain );

        UTF_REQUIRE( 0U != tokenBuffer.size() );

        BL_LOG(
            bl::Logging::debug(),
            BL_MSG()
                << "SPNEGO token was created with size "
                << tokenBuffer.size()
            );

        BL_LOG_MULTILINE(
            bl::Logging::debug(),
            BL_MSG()
                << "SPNEGO token base64 encoded value is:\n"
                << bl::SerializationUtils::base64EncodeString( tokenBuffer )
            );
    }
    else
    {
        /*
         * Pass "fakedomain" so the exception is thrown by getSPNEGOtoken
         * but not by getUserDomain
         */

        UTF_REQUIRE_THROW_MESSAGE(
            bl::os::getSPNEGOtoken( server, "fakedomain" ),
            bl::NotSupportedException,
            "getSPNEGOtoken() is not implemented on Linux"
            );
    }
}

/************************************************************************
 * Random utils tests
 */

UTF_AUTO_TEST_CASE( BaseLib_RandomTests )
{
    const std::size_t maxValue = 1000U;

    const auto r1 = bl::random::getUniformRandomUnsignedValue< std::size_t >( maxValue );
    const auto r2 = bl::random::getUniformRandomUnsignedValue< std::size_t >( maxValue );
    const auto r3 = bl::random::getUniformRandomUnsignedValue< std::size_t >( maxValue );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Random values: "
            << r1
            << " "
            << r2
            << " "
            << r3
        );

    std::size_t count[ 2 ] = { 0 };

    for( std::size_t i = 0; i < 1000; ++i )
    {
        const auto r = bl::random::getUniformRandomUnsignedValue< std::size_t >( 1 );

        switch( r )
        {
            case 0:
            case 1:
                ++count[ r ];
                break;

            default:
                UTF_FAIL( std::to_string( r ) + " value different then 0 or 1 generated" );
        }
    }

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "1000 generations for [0, 1] interval hits for 0: "
            << count[ 0 ]
            << "; hits for 1: "
            << count[ 1 ]
        );

    /*
     * Expected after 1000 iterations the two possble values to be generated at least once
     */

    UTF_REQUIRE( count[ 0 ] );
    UTF_REQUIRE( count[ 1 ] );

    unsigned char buffer[ 13 ];
    std::memset( buffer, 0, BL_ARRAY_SIZE( buffer ) );

    bl::random::getRandomBytes( buffer, BL_ARRAY_SIZE( buffer ) );

    auto message = BL_MSG();
    message << "Random buffer values:";

    std::size_t sum = 0U;

    for( std::size_t i = 0; i < BL_ARRAY_SIZE( buffer ); ++i )
    {
        sum += buffer[ i ];
        message << " " << ( unsigned int ) buffer[ i ];
    }

    UTF_REQUIRE( sum != 0U );

    BL_LOG(
        Logging::debug(),
        message
        );

    {
        /*
         * Guard bands around the requested region - the 13 byte fill above still passes
         * if only the first byte is written, so these pin the exact extent of the fill
         */

        unsigned char buf[ 64 ];
        std::memset( buf, 0xCC, sizeof( buf ) );

        bl::random::getRandomBytes( buf + 8, 48 );

        for( std::size_t i = 0U; i < 8U; ++i )
        {
            UTF_REQUIRE_EQUAL( ( unsigned int ) buf[ i ], 0xCCU );
        }

        for( std::size_t i = 56U; i < 64U; ++i )
        {
            UTF_REQUIRE_EQUAL( ( unsigned int ) buf[ i ], 0xCCU );
        }
    }

    {
        /*
         * Every index of the requested region must really be written - accumulate the
         * distinct values observed per index over 32 fills; a single index which is
         * never written would hold the same zero on all 32 iterations
         */

        std::set< unsigned char > seen[ 48 ];

        for( std::size_t iteration = 0U; iteration < 32U; ++iteration )
        {
            unsigned char probe[ 48 ];
            std::memset( probe, 0, sizeof( probe ) );

            bl::random::getRandomBytes( probe, sizeof( probe ) );

            for( std::size_t i = 0U; i < BL_ARRAY_SIZE( probe ); ++i )
            {
                seen[ i ].insert( probe[ i ] );
            }
        }

        for( std::size_t i = 0U; i < 48U; ++i )
        {
            UTF_REQUIRE( seen[ i ].size() >= 2U );
        }
    }

    {
        /*
         * The bufferSize == 1 boundary
         */

        unsigned char one = 0xCC;

        bool changed = false;

        for( std::size_t i = 0U; i < 32U; ++i )
        {
            bl::random::getRandomBytes( &one, 1U );

            if( 0xCC != one )
            {
                changed = true;
            }
        }

        UTF_REQUIRE( changed );
    }

    {
        /*
         * maxValue == 0 is legal and must return 0 - the range is inclusive of maxValue
         * and production relies on that in RotatingMessagingClientDispatchBaseT
         */

        for( std::size_t i = 0U; i < 32U; ++i )
        {
            UTF_REQUIRE_EQUAL( bl::random::getUniformRandomUnsignedValue< std::size_t >( 0U ), 0U );
        }
    }

    {
        /*
         * A non power of two range, inclusive of its maximum
         */

        std::set< std::size_t > values;

        for( std::size_t i = 0U; i < 2000U; ++i )
        {
            const auto value = bl::random::getUniformRandomUnsignedValue< std::size_t >( 6U );

            UTF_REQUIRE( value <= 6U );

            values.insert( value );
        }

        UTF_REQUIRE_EQUAL( values.size(), 7U );
    }

    {
        /*
         * A type wider than the underlying 32 bit engine - at least one draw must exceed
         * the range of an std::uint32_t, which catches a truncating implementation such
         * as a modulo of a single draw
         */

        bool wide = false;

        for( std::size_t i = 0U; i < 200U; ++i )
        {
            const auto value = bl::random::getUniformRandomUnsignedValue< std::uint64_t >(
                std::numeric_limits< std::uint64_t >::max()
                );

            if( value > std::numeric_limits< std::uint32_t >::max() )
            {
                wide = true;
            }
        }

        UTF_REQUIRE( wide );
    }

    {
        /*
         * A type narrower than the engine
         */

        for( std::size_t i = 0U; i < 200U; ++i )
        {
            UTF_REQUIRE( bl::random::getUniformRandomUnsignedValue< std::uint16_t >( 3U ) <= 3U );
        }
    }
}

/************************************************************************
 * Test HTTP globals
 */

UTF_AUTO_TEST_CASE( BaseLib_HttpGlobalsTests )
{
    {
        const auto& statuses = bl::http::Parameters::emptyStatuses();
        UTF_REQUIRE( statuses.empty() );
    }

    {
        const auto& statuses = bl::http::Parameters::conflictStatuses();
        UTF_REQUIRE( ! statuses.empty() );
        UTF_REQUIRE_EQUAL( 1U, statuses.size() );
        UTF_REQUIRE( bl::cpp::contains( statuses, bl::http::Parameters::HTTP_CLIENT_ERROR_CONFLICT ) );
    }

    {
        const auto& statuses = bl::http::Parameters::unauthorizedStatuses();
        UTF_REQUIRE( ! statuses.empty() );
        UTF_REQUIRE_EQUAL( 1U, statuses.size() );
        UTF_REQUIRE( bl::cpp::contains( statuses, bl::http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED ) );
    }

    {
        const auto& statuses = bl::http::Parameters::redirectStatuses();
        UTF_REQUIRE( ! statuses.empty() );
        UTF_REQUIRE_EQUAL( 1U, statuses.size() );
        UTF_REQUIRE( bl::cpp::contains( statuses, bl::http::Parameters::HTTP_REDIRECT_TEMPORARILY ) );
    }

    {
        const auto& statuses = bl::http::Parameters::securityDefaultStatuses();
        UTF_REQUIRE( ! statuses.empty() );
        UTF_REQUIRE_EQUAL( 2U, statuses.size() );
        UTF_REQUIRE( bl::cpp::contains( statuses, bl::http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED ) );
        UTF_REQUIRE( bl::cpp::contains( statuses, bl::http::Parameters::HTTP_REDIRECT_TEMPORARILY ) );
    }
}

/************************************************************************
 * SafeStringStream tests
 */

#if defined( _WIN32 )
namespace
{
    /*
     * Implement the allocator interface based on the following MSVC example:
     * https://docs.microsoft.com/en-us/cpp/standard-library/allocators?view=vs-2019
     */

    template
    <
        typename T
    >
    struct BadAllocator
    {
        typedef T value_type;

        // default ctor not required by C++ Standard Library
        BadAllocator() NOEXCEPT {}

        // A converting copy constructor:
        template
        <
            class U
        >
        BadAllocator( const BadAllocator< U >& ) NOEXCEPT {}

        template
        <
            class U
        >
        bool operator==( const BadAllocator< U >& ) const NOEXCEPT
        {
            return true;
        }

        template
        <
            class U
        >
        bool operator!=( const BadAllocator< U >& ) const NOEXCEPT
        {
            return false;
        }

        T* allocate( const std::size_t ) const
        {
            throw std::bad_alloc();
        }
        
        void deallocate( T* const, std::size_t ) const NOEXCEPT {}
    };

    typedef std::basic_istringstream< char, std::char_traits< char >, BadAllocator< char > > BadInputStringStream;
    typedef std::basic_ostringstream< char, std::char_traits< char >, BadAllocator< char > > BadOutputStringStream;
}
#endif

UTF_AUTO_TEST_CASE( BaseLib_SafeStringStreamTests )
{
    #if defined( _WIN32 )
    if( bl::os::onWindows() )
    {
        UTF_REQUIRE_EXCEPTION(
            BadInputStringStream stream( "abc" ),
            std::bad_alloc,
            utest::TestUtils::logExceptionDetails
          );
        UTF_REQUIRE_EXCEPTION(
            BadOutputStringStream stream( "abc" ),
            std::bad_alloc,
            utest::TestUtils::logExceptionDetails
            );
    }
    #endif

    /*
     * TODO: due to a regression bug in GCC [5/6] std::ios_base::failure can't be caught
     * when thrown from the standard library as it is thrown with the old ABI signature
     *
     * For more details see the following links:
     *
     * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66145
     * http://stackoverflow.com/questions/38471518/how-to-get-io-error-messages-when-creating-a-file-in-c
     *
     * Apparently this bug will be fixed in GCC 7, but for now as a workaround we must
     * catch it as std::exception
     */

    {
        bl::cpp::SafeInputStringStream stream;
        std::ios& istream = stream;
        UTF_REQUIRE_EXCEPTION(
            istream.rdbuf( nullptr ),
            std::exception /* TODO: must be std::ios_base::failure - see comment above */,
            utest::TestUtils::logExceptionDetails
            );
        UTF_CHECK( stream.rdstate() == std::ios_base::badbit );
    }

    {
        bl::cpp::SafeOutputStringStream stream;
        UTF_REQUIRE_EXCEPTION(
            stream << static_cast< std::streambuf* >( nullptr ),
            std::exception /* TODO: must be std::ios_base::failure - see comment above */,
            utest::TestUtils::logExceptionDetails
            );
        UTF_CHECK( stream.rdstate() == std::ios_base::badbit );
    }

    {
        bl::cpp::SafeOutputStringStream stream;

        stream << "abc";
        UTF_CHECK_EQUAL( "abc", stream.str() );

        bl::cpp::secureWipe( stream );

        UTF_CHECK_EQUAL( "000", stream.str() );
    }

    /*
     * The exception masks below are the contract which uuids::string2uuid, str::toBool and
     * DateTimeValidationUtils::getDateTime all rely on - the input stream must mask badbit
     * only, so a failed extraction just sets failbit and these parsers can convert it into
     * their own exception types instead of leaking std::ios_base::failure to their callers
     */

    {
        bl::cpp::SafeInputStringStream is;
        bl::cpp::SafeOutputStringStream os;
        bl::cpp::SafeStringStream ss;

        UTF_REQUIRE_EQUAL( is.exceptions(), std::ios_base::badbit );
        UTF_REQUIRE_EQUAL( os.exceptions(), ( std::ios_base::failbit | std::ios_base::badbit ) );
        UTF_REQUIRE_EQUAL( ss.exceptions(), std::ios_base::badbit );
    }

    {
        /*
         * ... and the input mask is not merely cosmetic - a failed extraction must set
         * failbit without throwing
         */

        bl::cpp::SafeInputStringStream is( "not-a-number" );

        int value = 0;

        UTF_REQUIRE_NO_THROW( is >> value );
        UTF_REQUIRE( is.fail() );
    }

    {
        /*
         * The mirror image for the output stream - failbit is masked there, so an operation
         * which sets it does throw (the badbit case is already covered above)
         */

        bl::cpp::SafeOutputStringStream stream;

        UTF_REQUIRE_EXCEPTION(
            stream.setstate( std::ios_base::failbit ),
            std::exception /* TODO: must be std::ios_base::failure - see comment above */,
            utest::TestUtils::logExceptionDetails
            );
        UTF_CHECK( stream.rdstate() == std::ios_base::failbit );
    }
}

/************************************************************************
 * FsUtils_SafeFileStreamWrapper tests
 */

UTF_AUTO_TEST_CASE( FsUtils_SafeFileStreamWrapperTests )
{
    bl::fs::TmpDir tmpDir;
    const auto& tmpPath = tmpDir.path();

    {
        const auto noSuchFilePath = tmpPath / "no-such-file";
        UTF_REQUIRE_EXCEPTION(
            bl::fs::SafeInputFileStreamWrapper inputFile( noSuchFilePath ),
            bl::SystemException,
            utest::TestUtils::logExceptionDetails
            );
    }

    /*
     * TODO: due to a regression bug in GCC [5/6] std::ios_base::failure can't be caught
     * when thrown from the standard library as it is thrown with the old ABI signature
     *
     * For more details see the following links:
     *
     * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66145
     * http://stackoverflow.com/questions/38471518/how-to-get-io-error-messages-when-creating-a-file-in-c
     *
     * Apparently this bug will be fixed in GCC 7, but for now as a workaround we must
     * catch it as std::exception
     */

    {
        const auto filePath = tmpPath / "file-for-input-exception-test";
        bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );

        bl::fs::SafeInputFileStreamWrapper inputFile( filePath );
        auto& is = inputFile.stream();
        UTF_REQUIRE_EXCEPTION(
            is.rdbuf( nullptr ),
            std::exception /* TODO: must be std::ios_base::failure - see comment above */,
            utest::TestUtils::logExceptionDetails
            );
        UTF_CHECK( is.rdstate() == std::ios_base::badbit );
    }

    {
        const auto filePath = tmpPath / "file-for-output-exception-test";
        bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );
        auto& os = outputFile.stream();
        UTF_REQUIRE_EXCEPTION(
            os << static_cast< std::streambuf* >( nullptr ),
            std::exception /* TODO: must be std::ios_base::failure - see comment above */,
            utest::TestUtils::logExceptionDetails
            );
        UTF_CHECK( os.rdstate() == std::ios_base::badbit );
    }

    {
        const int N = 1000;
        const auto filePath = tmpPath / "read-write.txt";
        {
            bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );
            auto& os = outputFile.stream();
            for( int i = 0; i < N; ++i )
            {
                os << "Line " << i << '\n';
            }
        }

        {
            bl::fs::SafeInputFileStreamWrapper inputFile( filePath );
            auto& is = inputFile.stream();
            int i = 0;
            std::string line;
            while( std::getline( is, line) )
            {
                UTF_REQUIRE( line == "Line " + std::to_string( i ) );
                ++i;
            }
            UTF_REQUIRE ( i == N );
        }

        {
            bl::fs::SafeOutputFileStreamWrapper outputFile( filePath, bl::fs::SafeOutputFileStreamWrapper::OpenMode::APPEND );
            auto& os = outputFile.stream();
            for( int i = N; i < 2*N; ++i )
            {
                os << "Line " << i << '\n';
            }
        }

        {
            bl::fs::SafeInputFileStreamWrapper inputFile( filePath );
            auto& is = inputFile.stream();
            int i = 0;
            std::string line;
            while( std::getline( is, line) )
            {
                UTF_REQUIRE( line == "Line " + std::to_string( i ) );
                ++i;
            }
            UTF_REQUIRE ( i == 2*N );
        }
    }

    /*
     * flushAndCheck() must make the content visible to another reader while the writer
     * is still open - the destructor deliberately discards flush and close errors, so
     * this is the only way a caller can learn that the bytes really reached the file
     *
     * All the round trips above read the file back only after the writer was destroyed,
     * so the implicit flush there would hide a missing flush in flushAndCheck()
     */

    {
        const auto filePath = tmpPath / "flush-and-check.txt";

        bl::fs::SafeOutputFileStreamWrapper outputFile( filePath );
        outputFile.stream() << "flushed-content";

        UTF_REQUIRE_NO_THROW( outputFile.flushAndCheck() );

        bl::fs::SafeInputFileStreamWrapper inputFile( filePath );
        auto& is = inputFile.stream();

        std::string readBack;
        std::getline( is, readBack );

        UTF_REQUIRE_EQUAL( readBack, std::string( "flushed-content" ) );
    }

    /*
     * A failed stream must be reported as an error instead of being discarded
     *
     * Note that the stream is created with exceptions( badbit | failbit ) set, so the
     * mask has to be cleared first - otherwise flush() would throw std::ios_base::failure
     * from its sentry before flushAndCheck() ever gets to its own check
     */

    {
        const auto badPath = tmpPath / "flush-and-check-failure.txt";

        bl::fs::SafeOutputFileStreamWrapper badFile( badPath );
        auto& os = badFile.stream();

        os.exceptions( std::ios::goodbit );
        os.setstate( std::ios::failbit );

        UTF_REQUIRE_THROW_MESSAGE(
            badFile.flushAndCheck(),
            bl::UnexpectedException,
            "Failed to write the contents of a file"
            );
    }
}

/************************************************************************
 * BaseLib_copyDirectoryPermissions tests
 */

UTF_AUTO_TEST_CASE( BaseLib_copyDirectoryPermissions_Tests )
{
    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    bl::fs::path srcDirPath = test::UtfArgsParser::path();

    bl::fs::path tgtDirPath = test::UtfArgsParser::outputPath();

    bl::fs::safeMkdirs( tgtDirPath );

    bl::os::copyDirectoryPermissions( srcDirPath, tgtDirPath );
}

/************************************************************************
 * groupBy tests
 */

UTF_AUTO_TEST_CASE( BaseLib_GroupByTest )
{
    std::vector< int > v;
    v.push_back( 1 );
    v.push_back( 2 );
    v.push_back( 2 );
    v.push_back( 3 );
    v.push_back( 3 );
    v.push_back( 3 );
    v.push_back( 4 );
    v.push_back( 4 );
    v.push_back( 4 );
    v.push_back( 4 );

    std::map< double, std::vector< int > > expected;
    expected[ 1.0 ].push_back( 1 );
    expected[ 4.0 ].push_back( 2 );
    expected[ 4.0 ].push_back( 2 );
    expected[ 9.0 ].push_back( 3 );
    expected[ 9.0 ].push_back( 3 );
    expected[ 9.0 ].push_back( 3 );
    expected[ 16.0 ].push_back( 4 );
    expected[ 16.0 ].push_back( 4 );
    expected[ 16.0 ].push_back( 4 );
    expected[ 16.0 ].push_back( 4 );

    std::function< double ( SAA_in int ) > square = []( SAA_in int const i )
    {
        return 1.0 * i * i;
    };

    const auto actual = groupBy( v.begin(), v.end(), square );

    UTF_CHECK_EQUAL( actual, expected );
}

/************************************************************************
 * Tree tests
 */

UTF_AUTO_TEST_CASE( BaseLib_TreeTests )
{
    typedef bl::Tree< std::string > StringTree;

    StringTree stringTree( "root" );

    UTF_CHECK_EQUAL( stringTree.value(), "root" );
    UTF_CHECK_EQUAL( stringTree.hasChildren(), false );

    auto& ch1 = stringTree.addChild( "child 1" );
    auto& ch2 = stringTree.addChild( "child 2" );

    auto& ch1_1 = ch1.addChild( "grandchild 1-1" );
    auto& ch1_2 = ch1.addChild( "grandchild 1-2" );

    auto& ch1_2_1 = ch1_2.addChild( "grandgrandchild 1-2-1" );

    auto& ch2_1 = ch2.addChild( "grandchild 2-1" );

    UTF_CHECK_EQUAL( stringTree.value(), "root" );
    UTF_CHECK_EQUAL( stringTree.hasChildren(), true );
    UTF_CHECK_EQUAL( stringTree.children().size(), 2U );

    UTF_CHECK_EQUAL( ch1.value(), "child 1" );
    UTF_CHECK_EQUAL( ch1.hasChildren(), true );
    UTF_CHECK_EQUAL( ch1.children().size(), 2U );
    UTF_CHECK_EQUAL( ch2.value(), "child 2" );
    UTF_CHECK_EQUAL( ch2.hasChildren(), true );
    UTF_CHECK_EQUAL( ch2.children().size(), 1U );

    UTF_CHECK_EQUAL( ch1_1.value(), "grandchild 1-1" );
    UTF_CHECK_EQUAL( ch1_1.hasChildren(), false );
    UTF_CHECK_EQUAL( ch1_1.children().size(), 0U );
    UTF_CHECK_EQUAL( ch1_2.value(), "grandchild 1-2" );
    UTF_CHECK_EQUAL( ch1_2.hasChildren(), true );
    UTF_CHECK_EQUAL( ch1_2.children().size(), 1U );
    UTF_CHECK_EQUAL( ch1_2_1.value(), "grandgrandchild 1-2-1" );
    UTF_CHECK_EQUAL( ch1_2_1.hasChildren(), false );
    UTF_CHECK_EQUAL( ch1_2_1.children().size(), 0U );

    UTF_CHECK_EQUAL( ch2_1.value(), "grandchild 2-1" );
    UTF_CHECK_EQUAL( ch2_1.hasChildren(), false );
    UTF_CHECK_EQUAL( ch2_1.children().size(), 0U );

    UTF_CHECK_EQUAL( stringTree.child( 0 ).value(), "child 1" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).children().size(), 2U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 0 ).value(), "grandchild 1-1" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 0 ).children().size(), 0U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 1 ).value(), "grandchild 1-2" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 1 ).children().size(), 1U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 1 ).child( 0 ).value(), "grandgrandchild 1-2-1" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 1 ).child( 0 ).children().size(), 0U );

    UTF_CHECK_EQUAL( stringTree.child( 1 ).value(), "child 2" );
    UTF_CHECK_EQUAL( stringTree.child( 1 ).children().size(), 1U );
    UTF_CHECK_EQUAL( stringTree.child( 1 ).child( 0 ).value(), "grandchild 2-1" );
    UTF_CHECK_EQUAL( stringTree.child( 1 ).child( 0 ).children().size(), 0U );

    UTF_REQUIRE_THROW_MESSAGE( stringTree.child( 2 ).value(), bl::ArgumentException, "index 2 is out of range, max=1" );

    stringTree.childLvalue( 0 ).removeChild( 0 );

    UTF_CHECK_EQUAL( stringTree.child( 0 ).value(), "child 1" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).children().size(), 1U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 0 ).value(), "grandchild 1-2" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 0 ).children().size(), 1U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 0 ).child( 0 ).value(), "grandgrandchild 1-2-1" );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).child( 0 ).child( 0 ).children().size(), 0U );

    UTF_REQUIRE_THROW_MESSAGE( stringTree.removeChild( 3 ), bl::ArgumentException, "index 3 is out of range, max=1" );
    UTF_REQUIRE_THROW_MESSAGE( stringTree.childLvalue( 0 ).childLvalue( 0 ).childLvalue( 0 ).removeChild( 4 ), bl::ArgumentException, "index 4 is out of range, max=-1" );

    stringTree.childLvalue( 1 ).lvalue() = "another child";

    UTF_CHECK_EQUAL( stringTree.children().size(), 2U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).value(), "child 1" );
    UTF_CHECK_EQUAL( stringTree.child( 1 ).value(), "another child" );

    stringTree.removeChild( 0 );

    UTF_CHECK_EQUAL( stringTree.children().size(), 1U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).value(), "another child" );

    auto& ch3 = stringTree.addChild( "yet another child" );

    UTF_CHECK_EQUAL( ch3.value(), "yet another child" );
    UTF_CHECK_EQUAL( ch3.hasChildren(), false );
    UTF_CHECK_EQUAL( ch3.children().size(), 0U );

    UTF_CHECK_EQUAL( stringTree.children().size(), 2U );
    UTF_CHECK_EQUAL( stringTree.child( 0 ).value(), "another child" );
    UTF_CHECK_EQUAL( stringTree.child( 1 ).value(), "yet another child" );

    auto stringTree2 = std::move( stringTree );

    UTF_CHECK( stringTree.value() != "root" );
    UTF_CHECK_EQUAL( stringTree.hasChildren(), false );
    UTF_CHECK_EQUAL( stringTree2.value(), "root" );
    UTF_CHECK_EQUAL( stringTree2.hasChildren(), true );

    StringTree stringTree3( "test" );
    stringTree3 = std::move( stringTree2 );

    UTF_CHECK( stringTree2.value() != "root" );
    UTF_CHECK_EQUAL( stringTree2.hasChildren(), false );
    UTF_CHECK_EQUAL( stringTree3.value(), "root" );
    UTF_CHECK_EQUAL( stringTree3.hasChildren(), true );

    StringTree stringSubTree( "subtree" );
    stringSubTree.addChild( "subtree child 1" );
    stringSubTree.addChild( "subtree child 2" );

    UTF_CHECK_EQUAL( stringSubTree.hasChildren(), true );

    stringTree3.addTree( std::move( stringSubTree ) );

    UTF_CHECK( stringSubTree.value() != "subtree" );
    UTF_CHECK_EQUAL( stringSubTree.hasChildren(), false );

    UTF_CHECK_EQUAL( stringTree3.children().size(), 3U );
    UTF_CHECK_EQUAL( stringTree3.child( 0 ).value(), "another child" );
    UTF_CHECK_EQUAL( stringTree3.child( 1 ).value(), "yet another child" );
    UTF_CHECK_EQUAL( stringTree3.child( 2 ).value(), "subtree" );
    UTF_CHECK_EQUAL( stringTree3.child( 2 ).children().size(), 2U );
    UTF_CHECK_EQUAL( stringTree3.child( 2 ).child( 0 ).value(), "subtree child 1" );
    UTF_CHECK_EQUAL( stringTree3.child( 2 ).child( 1 ).value(), "subtree child 2" );
}

UTF_AUTO_TEST_CASE( BaseLib_TableTests )
{
    /*
     * ------------------------
     * Name  | Age | Title
     * -----------------------
     * Tom   | 25  | Analyst
     * Harry | 30  | Associate
     * -----------------------
     */

    typedef struct
    {
        std::string name;
        std::size_t size;
    } ColumnHeader;

    typedef bl::Table< std::string, ColumnHeader > Table;

    std::vector< ColumnHeader > columnHeaders;

    ColumnHeader column1 = { "Name", 10 };
    ColumnHeader column2 = { "Age", 10 };
    ColumnHeader column3 = { "Title", 10 };

    columnHeaders.emplace_back( column1 );
    columnHeaders.emplace_back( column2 );
    columnHeaders.emplace_back( column3 );

    Table table( columnHeaders );

    UTF_CHECK_EQUAL( table.getColumnCount(), columnHeaders.size() );

    table.addRow( "Tom", "25", "Analyst" );
    table.addRow( "Harry", "30", "Associate" );

    UTF_CHECK_THROW(
        table.addRow( "James", "35", "Associate", "London" ),
        bl::UnexpectedException
        );

    UTF_CHECK( table.getColumnHeaders().at( 0 ).name == "Name" );
    UTF_CHECK( table.getColumnHeaders().at( 1 ).name == "Age" );

    UTF_CHECK_EQUAL( table.getCell( 0, 0 ), "Tom" );
    UTF_CHECK_EQUAL( table.getCell( 1, 1 ), "30" );

    UTF_CHECK_EQUAL( table.getRowCount(), 2U );
    UTF_CHECK_EQUAL( table.getColumnCount(), 3U );
}

UTF_AUTO_TEST_CASE( BaseLib_SortedVectorHelperTests )
{
    using namespace bl;

    typedef cpp::SortedVectorHelper< std::size_t > helper_t;

    const std::size_t maxCount = 20000;

    {
        std::set< std::size_t > s;
        helper_t::vector_t v;
        helper_t helper;

        v.reserve( maxCount );

        {
            utils::ExecutionTimer timer(
                bl::resolveMessage(
                    BL_MSG()
                        << "Inserting "
                        << maxCount
                        << " elements in sorted vector"
                    )
                );

            while( v.size() < maxCount )
            {
                const auto value = random::getUniformRandomUnsignedValue< std::size_t >(
                    std::numeric_limits< std::size_t >::max() /* maxValue */
                    );

                helper.insert( v, value );
            }
        }

        {
            utils::ExecutionTimer timer(
                bl::resolveMessage(
                    BL_MSG()
                        << "Searching "
                        << maxCount
                        << " elements in sorted vector twice"
                    )
                );

            bool ok = true;

            for( std::size_t i = 0; i < maxCount; ++i )
            {
                if( v.cend() == helper.cfind( v, v[ i ] ) )
                {
                    ok = false;
                    break;
                }

                if( v.end() == helper.find( v, v[ i ] ) )
                {
                    ok = false;
                    break;
                }
            }

            UTF_REQUIRE( ok );
        }

        {
            utils::ExecutionTimer timer(
                bl::resolveMessage(
                    BL_MSG()
                        << "Inserting "
                        << maxCount
                        << " elements in a set"
                    )
                );

            while( s.size() < maxCount )
            {
                const auto value = random::getUniformRandomUnsignedValue< std::size_t >(
                    std::numeric_limits< std::size_t >::max() /* maxValue */
                    );

                s.insert( value );
            }
        }

        {
            utils::ExecutionTimer timer(
                bl::resolveMessage(
                    BL_MSG()
                        << "Searching "
                        << maxCount
                        << " elements in a set twice"
                    )
                );

            bool ok = true;

            for( const auto& element : s )
            {
                const auto& constSet = s;

                if( s.cend() == constSet.find( element ) )
                {
                    ok = false;
                    break;
                }

                if( s.end() == s.find( element ) )
                {
                    ok = false;
                    break;
                }
            }

            UTF_REQUIRE( ok );
        }
    }

    {
        std::set< std::size_t > s;
        helper_t::vector_t v;
        helper_t helper;

        v.reserve( maxCount );

        {
            utils::ExecutionTimer timer(
                bl::resolveMessage(
                    BL_MSG()
                        << "Inserting and searching "
                        << maxCount
                        << " elements in both sorted vector & set"
                    )
                );

            while( s.size() < maxCount )
            {
                const auto value = random::getUniformRandomUnsignedValue< std::size_t >(
                    std::numeric_limits< std::size_t >::max() /* maxValue */
                    );

                const auto pair1 = s.insert( value );

                if( pair1.second )
                {
                    /*
                     * It was inserted, so the value was unique
                     *
                     * Verify that it is not in the vector, then verify that insert returns as expected
                     * and then finally verify that the find functions work as expected
                     */

                    UTF_REQUIRE( v.cend() == helper.cfind( v, value ) );

                    UTF_REQUIRE( v.end() == helper.find( v, value ) );

                    UTF_REQUIRE( ! helper.erase( v, value ) );

                    const auto pair2 = helper.insert( v, value );

                    UTF_REQUIRE( pair2.second );
                    UTF_REQUIRE( v.end() != pair2.first );
                    UTF_REQUIRE( value == *pair2.first );

                    {
                        const auto pos = helper.cfind( v, value );

                        UTF_REQUIRE( v.cend() != pos && value == *pos );
                    }

                    {
                        const auto pos = helper.find( v, value );

                        UTF_REQUIRE( v.end() != pos && value == *pos );
                    }
                }
                else
                {
                    /*
                     * It is already inserted - just verify that it is present in the vector too
                     * and then insert returns as expected
                     */

                    {
                        const auto pos = helper.cfind( v, value );

                        UTF_REQUIRE( v.cend() != pos || value == *pos );
                    }

                    {
                        const auto pos = helper.find( v, value );

                        UTF_REQUIRE( v.end() != pos || value == *pos );
                    }

                    const auto pair2 = helper.insert( v, value );

                    UTF_REQUIRE( ! pair2.second && v.end() != pair2.first && value == *pair2.first );
                }
            }

            for( std::size_t i = 0; i < 100; ++i )
            {
                const auto index = random::getUniformRandomUnsignedValue< std::size_t >(
                    v.size() - 1 /* maxValue */
                    );

                const auto value = v[ index ];

                {
                    const auto pos = helper.cfind( v, value );

                    UTF_REQUIRE( v.cend() != pos || value == *pos );
                }

                {
                    const auto pos = helper.find( v, value );

                    UTF_REQUIRE( v.end() != pos || value == *pos );
                }

                UTF_REQUIRE( helper.erase( v, value ) );

                UTF_REQUIRE( v.cend() == helper.cfind( v, value ) );

                UTF_REQUIRE( v.end() == helper.find( v, value ) );

                UTF_REQUIRE( ! helper.erase( v, value ) );
            }
        }
    }
}

UTF_AUTO_TEST_CASE( BaseLib_OSRegistryValueTest )
{
    if( ! bl::os::onWindows() )
    {
        return;
    }

    UTF_CHECK(
        bl::os::tryGetRegistryValue( "Software\\foo", "bar" ) == nullptr
        );

    UTF_CHECK_THROW(
        bl::os::getRegistryValue( "Software\\foo", "bar" ),
        bl::UnexpectedException
        );

    #if defined( _WIN32 )
    {
        /*
         * A key and a value whose names contain a non-ASCII character are created through
         * the wide registry API and must be found back through the UTF-8 helpers: a
         * byte-widened name would look up a different, nonexistent key or value. A name
         * which is not valid UTF-8 must be rejected rather than converted silently (the
         * lone 0xE9 byte below, byte-widened, would have matched the value just created)
         */

        const std::string keyName =
            "Software\\swblocks-baselib-utf-\xC3\xA9-" + bl::uuids::uuid2string( bl::uuids::create() );
        const std::string valueName = "value-\xC3\xA9";
        const std::string data = "data-\xC3\xBC";

        bl::cpp::wstring_convert_t conv;

        const std::wstring wkeyName = conv.from_bytes( keyName );
        const std::wstring wvalueName = conv.from_bytes( valueName );
        const std::wstring wdata = conv.from_bytes( data );

        HKEY hkey = nullptr;

        UTF_REQUIRE_EQUAL(
            ERROR_SUCCESS,
            ::RegCreateKeyExW(
                HKEY_CURRENT_USER               /* hKey */,
                wkeyName.c_str()                /* lpSubKey */,
                0                               /* Reserved */,
                nullptr                         /* lpClass */,
                REG_OPTION_VOLATILE             /* dwOptions */,
                KEY_WRITE                       /* samDesired */,
                nullptr                         /* lpSecurityAttributes */,
                &hkey                           /* phkResult */,
                nullptr                         /* lpdwDisposition */
                )
            );

        BL_SCOPE_EXIT(
            {
                ::RegCloseKey( hkey );
                ::RegDeleteKeyW( HKEY_CURRENT_USER, wkeyName.c_str() );
            }
            );

        UTF_REQUIRE_EQUAL(
            ERROR_SUCCESS,
            ::RegSetValueExW(
                hkey                                                        /* hKey */,
                wvalueName.c_str()                                          /* lpValueName */,
                0                                                           /* Reserved */,
                REG_SZ                                                      /* dwType */,
                reinterpret_cast< const BYTE* >( wdata.c_str() )            /* lpData */,
                static_cast< DWORD >( ( wdata.size() + 1 ) * sizeof( wchar_t ) )   /* cbData */
                )
            );

        UTF_REQUIRE_EQUAL(
            data,
            bl::os::getRegistryValue( keyName, valueName, true /* currentUser */ )
            );

        UTF_CHECK_THROW(
            bl::os::tryGetRegistryValue( keyName, "value-\xE9" /* not UTF-8 */, true /* currentUser */ ),
            bl::SystemException
            );

        /*
         * PINNED LIMIT - tryGetRegistryValue( ... ) reads into a fixed WCHAR buffer[ 1024 ]
         * ( OSImplWindows.h:3585 ), so a REG_SZ longer than 1023 characters makes
         * RegGetValueW return ERROR_MORE_DATA and the function throws rather than growing
         * the buffer and retrying
         *
         * 1023 characters is therefore the documented maximum today. The value of pinning
         * it is that a partial read - i.e. a silent truncation - would be a very different
         * and much worse failure mode; if the buffer is ever made growable this is the
         * single place which flips to an equality check against the written value
         */

        const std::string longValueName = "long-value";
        const std::wstring wlongValueName = conv.from_bytes( longValueName );
        const std::wstring wlongData( 2000U, L'x' );

        UTF_REQUIRE_EQUAL(
            ERROR_SUCCESS,
            ::RegSetValueExW(
                hkey                                                            /* hKey */,
                wlongValueName.c_str()                                          /* lpValueName */,
                0                                                               /* Reserved */,
                REG_SZ                                                          /* dwType */,
                reinterpret_cast< const BYTE* >( wlongData.c_str() )            /* lpData */,
                static_cast< DWORD >( ( wlongData.size() + 1 ) * sizeof( wchar_t ) ) /* cbData */
                )
            );

        UTF_REQUIRE_THROW(
            bl::os::getRegistryValue( keyName, longValueName, true /* currentUser */ ),
            bl::SystemException
            );

        /*
         * The hive named by a failing diagnostic, and the handle lifetime on both failing
         * paths, are covered by BaseLib_OSRegistryHiveDiagnosticsWindowsTests in
         * TestBaselibDefault5.h
         *
         * Reaching the diagnostic under HKEY_LOCAL_MACHINE needs an open which fails with
         * something other than ERROR_FILE_NOT_FOUND ( which is returned as nullptr rather
         * than thrown ), and HKEY_LOCAL_MACHINE\SECURITY is readable by SYSTEM only, which
         * makes it deterministic without administrator rights
         */
    }
    #endif

    /*
     * Don't run for 32bit process as the below registry entry won't exist
     * in WOW registry hive for 32bit process running on 64bit OS.
     */

    if( sizeof(void*) == 4U )
    {
        return;
    }

    UTF_CHECK(
        bl::os::getRegistryValue(
            "Environment",
            "TEMP",
            true /* currentUser */
            ) != bl::str::empty()
        );
}

/************************************************************************
 * LoggableCounter class tests
 */

UTF_AUTO_TEST_CASE( BaseLib_LoggableCounterTests )
{
    using namespace bl;

    typedef LoggableCounterDefaultImpl::EventId EventId;

    const std::string counterName = "my counter";

    std::size_t updateCount = 0U;
    std::vector< std::size_t > updates;

    cpp::ScalarTypeIniter< bool > created;
    cpp::ScalarTypeIniter< bool > destroyed;

    const auto callback = [ & ](
        SAA_in          const EventId                           eventId,
        SAA_in          const std::size_t                       counterValue
        ) NOEXCEPT
    {
        BL_NOEXCEPT_BEGIN()

        switch( eventId )
        {
            default:
                BL_RIP_MSG( "Unexpected event id value" );
                break;

            case EventId::Create:
                {
                    created = true;

                    UTF_REQUIRE_EQUAL( counterValue, 0U );
                }
                break;

            case EventId::Update:
                {
                    ++updateCount;

                    updates.push_back( counterValue );

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Loggable counter '"
                            << counterName
                            << "' was updated; new counter value is "
                            << counterValue
                        );
                }
                break;

            case EventId::Destroy:
                {
                    destroyed = true;

                    UTF_REQUIRE_EQUAL( counterValue, 0U );
                }
                break;
        }

        BL_NOEXCEPT_END()
    };

    {
        const auto counter = LoggableCounterDefaultImpl::createInstance(
            cpp::copy( counterName ),
            10U /* minDeltaToLog */,
            cpp::copy( callback )
            );
    }

    UTF_REQUIRE( created );
    UTF_REQUIRE( destroyed );
    UTF_REQUIRE_EQUAL( updateCount, 0U );
    UTF_REQUIRE_EQUAL( updates.size(), 0U );

    created = false;
    destroyed = false;

    {
        const auto counter = LoggableCounterDefaultImpl::createInstance(
            cpp::copy( counterName ),
            10U /* minDeltaToLog */,
            cpp::copy( callback )
            );

        for( std::size_t i = 0U; i < 25U; ++i )
        {
            counter -> increment();
        }

        for( std::size_t i = 0U; i < 25U; ++i )
        {
            counter -> decrement();
        }
    }

    UTF_REQUIRE( created );
    UTF_REQUIRE( destroyed );
    UTF_REQUIRE_EQUAL( updateCount, 5U );
    UTF_REQUIRE_EQUAL( updates.size(), 5U );

    UTF_REQUIRE_EQUAL( updates[ 0 ], 1U );
    UTF_REQUIRE_EQUAL( updates[ 1 ], 11U );
    UTF_REQUIRE_EQUAL( updates[ 2 ], 21U );
    UTF_REQUIRE_EQUAL( updates[ 3 ], 11U );
    UTF_REQUIRE_EQUAL( updates[ 4 ], 1U );
}

UTF_AUTO_TEST_CASE( BaseLib_CopyDirectoryWithContentTests )
{
    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    bl::fs::path sourcePath = test::UtfArgsParser::path();
    bl::fs::path targetPath = test::UtfArgsParser::outputPath();

    BL_CHK(
        true,
        sourcePath.empty(),
        BL_MSG()
            << "The --path parameter cannot be empty"
        );

    BL_CHK(
        true,
        targetPath.empty(),
        BL_MSG()
            << "The --output-path parameter cannot be empty"
        );

    const auto getDirectoryContents = [ & ]( SAA_in const bl::fs::path& path ) -> std::vector< std::string >
    {
        std::vector< std::string > paths;

        for( bl::fs::recursive_directory_iterator i( path ), end; i != end; ++i )
        {
            paths.emplace_back( i -> path().string() );
        }

        return paths;
    };

    const auto sourceContents = getDirectoryContents( sourcePath );

    UTF_REQUIRE( ! bl::fs::path_exists( targetPath ) );

    bl::fs::copyDirectoryWithContents( sourcePath, targetPath );

    const auto targetContents = getDirectoryContents( targetPath );

    UTF_REQUIRE_EQUAL( sourceContents.size(), targetContents.size() );
}

UTF_AUTO_TEST_CASE( BaseLib_ProfilesDirectoryTests )
{
    const auto profilesDirectory = bl::os::getUsersDirectory();

    UTF_REQUIRE( bl::fs::path_exists( profilesDirectory ) );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Users directory: "
            << profilesDirectory.string()
        );
}
