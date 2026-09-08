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

#include <baselib/data/eh/ServerErrorHelpers.h>

#include <utests/baselib/UtfBaseLibCommon.h>
#include <utests/baselib/TestUtils.h>

namespace utest
{
    /*
     * Build an exception decorated with the full set of errinfo properties which the server
     * error model carries
     *
     * Every case in this file needs the same fixture and the block used to be spelled out twice,
     * ~40 near identical lines apart; only the exception type, the suffix which makes each value
     * recognisable in the assertions and the message actually differ between the call sites
     */

    template
    <
        typename EXCEPTION
    >
    inline std::exception_ptr createDecoratedException(
        SAA_in          const EXCEPTION&                            exception,
        SAA_in          const std::string&                          suffix,
        SAA_in          const std::string&                          message,
        SAA_in          const bl::eh::error_code&                   errorCode
        )
    {
        /*
         * Note that std::make_exception_ptr() is used on an object which was never thrown, so
         * the slicing caveat which applies to capturing a live exception does not apply here -
         * the static type is the type being stored
         */

        return std::make_exception_ptr(
            bl::eh::enable_current_exception( bl::eh::enable_error_info( exception ) )
                << bl::eh::throw_function( BOOST_CURRENT_FUNCTION )
                << bl::eh::throw_file( __FILE__ )
                << bl::eh::throw_line( __LINE__ )
                << bl::eh::errinfo_errno                       ( 1001 )
                << bl::eh::errinfo_file_name                   ( "file_name: " + suffix )
                << bl::eh::errinfo_file_open_mode              ( "file_open_mode: " + suffix )
                << bl::eh::errinfo_message                     ( message )
                << bl::eh::errinfo_time_thrown                 ( "2015-09-23T18:29:10.475824-04:00" )
                << bl::eh::errinfo_function_name               ( "function_name: " + suffix )
                << bl::eh::errinfo_system_code                 ( bl::http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST )
                << bl::eh::errinfo_category_name               ( errorCode.category().name() /* "generic" */ )
                << bl::eh::errinfo_error_code                  ( errorCode )
                << bl::eh::errinfo_error_code_message          ( errorCode.message() /* "Permission denied" */ )
                << bl::eh::errinfo_is_expected                 ( true )
                << bl::eh::errinfo_task_info                   ( "task_info: " + suffix )
                << bl::eh::errinfo_host_name                   ( bl::net::getShortHostName() )
                << bl::eh::errinfo_service_name                ( "service_name: " + suffix )
                << bl::eh::errinfo_endpoint_address            ( "endpoint_address: " + suffix + " address" )
                << bl::eh::errinfo_endpoint_port               ( 1002 )
                << bl::eh::errinfo_http_url                    ( "www.mytest.com" )
                << bl::eh::errinfo_http_redirect_url           ( "www.myredirectedtest.com" )
                << bl::eh::errinfo_http_status_code            ( 1003 )
                << bl::eh::errinfo_http_response_headers       ( "<header1><header2><header3>" )
                << bl::eh::errinfo_http_request_details        ( "_http_request_details: " + suffix )
                << bl::eh::errinfo_parser_file                 ( "Blablabla" )
                << bl::eh::errinfo_parser_line                 ( 1004U )
                << bl::eh::errinfo_parser_column               ( 1005U )
                << bl::eh::errinfo_parser_reason               ( "syntax error" )
                << bl::eh::errinfo_external_command_output     ( "external_command_output: " + suffix )
                << bl::eh::errinfo_external_command_exit_code  ( 1006 )
                << bl::eh::errinfo_string_value                ( "string_value: " + suffix )
                << bl::eh::errinfo_is_user_friendly            ( false )
                << bl::eh::errinfo_ssl_is_verify_failed        ( true )
                << bl::eh::errinfo_ssl_is_verify_error         ( 1007 )
                << bl::eh::errinfo_ssl_is_verify_error_message ( "ssl_is_verify_error_message: " + suffix )
                << bl::eh::errinfo_ssl_is_verify_error_string  ( "ssl_is_verify_error_string: " + suffix )
                << bl::eh::errinfo_ssl_is_verify_subject_name  ( "ssl_is_verify_subject_name: " + suffix )
            );
    }

} // utest

UTF_AUTO_TEST_CASE( ErrorToJsonTests )
{
    using namespace bl;

#define UTEST_PROPERTY_REQUIRE_EQUAL( property, value ) \
        { \
            const auto errorProperty = eh::get_error_info< eh::property >( e ); \
            UTF_REQUIRE( errorProperty != nullptr ); \
            UTF_CHECK_EQUAL( *errorProperty, value ); \
        } \

    const eh::error_code errorCode(
        eh::errc::permission_denied,
        eh::generic_category()
        );

    const auto validateException = [ &errorCode ]( SAA_in const BaseExceptionDefault& e )
    {
        UTF_REQUIRE_EQUAL( e.fullTypeName(),  "bl::ServerErrorException" );
        UTF_REQUIRE_EQUAL( e.what(),  "message: ErrorToJsonTests" );
        UTF_REQUIRE( ! e.details().empty() );

        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_errno                       , 1001 )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_file_name                   , "file_name: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_file_open_mode              , "file_open_mode: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_message                     , "message: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_time_thrown                 , "2015-09-23T18:29:10.475824-04:00" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_function_name               , "function_name: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_system_code                 , static_cast< int >( http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST ) )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_category_name               , "generic" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_error_code                  , errorCode )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_error_code_message          , "Permission denied" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_is_expected                 , true )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_task_info                   , "task_info: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_host_name                   , net::getShortHostName() )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_service_name                , "service_name: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_endpoint_address            , "endpoint_address: ErrorToJsonTests address" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_endpoint_port               , 1002 )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_http_url                    , "www.mytest.com" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_http_redirect_url           , "www.myredirectedtest.com" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_http_status_code            , 1003 )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_http_response_headers       , "<header1><header2><header3>" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_http_request_details        , "_http_request_details: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_parser_file                 , "Blablabla" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_parser_line                 , 1004u )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_parser_column               , 1005u )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_parser_reason               , "syntax error" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_external_command_output     , "external_command_output: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_external_command_exit_code  , 1006 )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_string_value                , "string_value: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_is_user_friendly            , false )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_ssl_is_verify_failed        , true )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_ssl_is_verify_error         , 1007 )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_ssl_is_verify_error_message , "ssl_is_verify_error_message: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_ssl_is_verify_error_string  , "ssl_is_verify_error_string: ErrorToJsonTests" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_ssl_is_verify_subject_name  , "ssl_is_verify_subject_name: ErrorToJsonTests" )
    };

    /*
     * Create and throw ServerErrorException test
     */

    try
    {
        const auto serverErrorExceptionPtr = utest::createDecoratedException(
            ServerErrorException(),
            "ErrorToJsonTests",
            "message: ErrorToJsonTests",
            errorCode
            );

        const auto serverErrorJson = dm::ServerErrorHelpers::createServerErrorObject( serverErrorExceptionPtr );

        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson );

        std::rethrow_exception( exceptionPtr );
    }
    catch( BaseExceptionDefault& e )
    {
        validateException( e );
    }

    /*
     * Create and throw std::exception test
     */

    try
    {
        const auto stdExceptionPtr = std::make_exception_ptr( std::runtime_error( "std::exception: ErrorToJsonTests") );

        const auto serverErrorJson = dm::ServerErrorHelpers::createServerErrorObject( stdExceptionPtr );

        /*
         * An exception which is not a BaseException leaves exceptionType empty, and the default
         * applied here is the discriminator createExceptionFromObject() keys on to return a
         * plain std::runtime_error rather than falling through to UnexpectedException. A raw
         * std::runtime_error carries no errinfo_is_user_friendly, so the user facing message is
         * the generic one
         */

        UTF_CHECK_EQUAL( serverErrorJson -> result() -> exceptionType(), "std::exception" );

        UTF_CHECK_EQUAL(
            serverErrorJson -> result() -> message(),
            std::string( BL_GENERIC_FRIENDLY_UNEXPECTED_MSG )
            );

        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson );

        std::rethrow_exception( exceptionPtr );
    }
    catch( std::exception& e )
    {
        UTF_REQUIRE_EQUAL( e.what(),  "std::exception: ErrorToJsonTests" );
    }

    /*
     * Create and throw SystemException test
     */

    try
    {
        const auto systemException = SystemException::create( errorCode, "It's a what() prefix" );
        systemException << eh::errinfo_message( "It's a message" );

        const auto stdExceptionPtr = std::make_exception_ptr( systemException );

        const auto serverErrorJson = dm::ServerErrorHelpers::createServerErrorObject( stdExceptionPtr );

        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson );

        std::rethrow_exception( exceptionPtr );
    }
    catch( SystemException& e )
    {
        UTF_REQUIRE_EQUAL( e.fullTypeName(), "bl::SystemException" );
        UTF_REQUIRE(
            std::string( e.what() ) == "It's a what() prefix: Permission denied" ||
            std::string( e.what() ) == "It's a what() prefix: Permission denied [generic:13]"
            );

        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_message, "It's a message" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_category_name, "generic" )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_error_code, errorCode )
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_error_code_message, "Permission denied" )
    }

    /*
     * Throw an exception of an unknown type test
     */

    try
    {
        auto serverErrorJson = dm::ServerErrorJson::createInstance();

        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );
        serverErrorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );
        serverErrorJson -> result() -> exceptionType( "Unknown-Exception-Type" );
        serverErrorJson -> result() -> exceptionProperties() -> message( "message: Unknown-Exception-Type" );

        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson );

        std::rethrow_exception( exceptionPtr );
    }
    catch( UnexpectedException& e )
    {
        UTF_REQUIRE_EQUAL( e.fullTypeName(), "bl::UnexpectedException" );
        UTF_CHECK_EQUAL( e.what(), "message: Unknown-Exception-Type" );
        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_message, "message: Unknown-Exception-Type" )
    }

    /*
     * Test that correct exceptions are thrown for invalid ServerErrorJson objects
     */

    {
        auto serverErrorJson = dm::ServerErrorJson::createInstance();

        UTF_REQUIRE_EXCEPTION(
            ( void )dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson ),
            ArgumentException,
            []( SAA_in const ArgumentException& e ) -> bool
            {
                return std::string( "ServerErrorJson: 'result' property is not set" ) == e.what();
            }
            );
    }

    {
        auto serverErrorJson = dm::ServerErrorJson::createInstance();
        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );

        UTF_REQUIRE_EXCEPTION(
            ( void )dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson ),
            ArgumentException,
            []( SAA_in const ArgumentException& e ) -> bool
            {
                return std::string("ServerErrorResult: 'exceptionProperties' property is not set" ) == e.what();
            }
            );
    }

    {
        auto serverErrorJson = dm::ServerErrorJson::createInstance();
        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );
        serverErrorJson -> result() -> exceptionType( "bl::ServerErrorException" );
        serverErrorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );
        serverErrorJson -> result() -> exceptionProperties() -> categoryName( "non-generic" );

        UTF_REQUIRE_EXCEPTION(
            ( void )dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson ),
            ArgumentException,
            []( SAA_in const ArgumentException& e ) -> bool
            {
                return std::string("Unknown error category: 'non-generic'" ) == e.what();
            }
            );
    }

#undef UTEST_PROPERTY_REQUIRE_EQUAL
}

UTF_AUTO_TEST_CASE( ErrorToJsonExceptionTypeMappingTests )
{
    using namespace bl;

    /*
     * createServerErrorResultObject() writes e.fullTypeName() into the exceptionType string and
     * createExceptionFromObject() reads it back through a chain of 19 bl:: arms plus the
     * "std::exception" one. The name written on the way out and the type constructed on the way
     * in are coupled only by two hand maintained string literals sitting ~180 lines apart, and
     * before this case only four of those arms were ever executed
     *
     * cb431f0 fixed exactly this class of bug: bl::InvalidDataFormatException had been
     * reconstructed as XmlException for years and nothing noticed, because nothing asserted the
     * TYPE of the reconstructed exception. In production the consequence is silent - a client's
     * catch( bl::TimeoutException& ) simply stops matching and a generic arm handles the error
     *
     * bl::SystemException is deliberately NOT in the table below: it needs categoryName and
     * errorCode to be set, and it is already round tripped by ErrorToJsonTests. "std::exception"
     * is out too - it comes back as a plain std::runtime_error, which is not a
     * BaseExceptionDefault, so the catch arm below would not fire for it
     */

    std::size_t roundTrippedTypes = 0U;
    std::size_t fallThroughTypes = 0U;

#define UTEST_EXCEPTION_TYPE_ROUNDTRIP_IMPL( exceptionClass, reconstructedClass, counter )      \
        {                                                                                       \
            ++counter;                                                                          \
                                                                                                \
            std::exception_ptr eptr;                                                            \
            const std::string message = "msg: " #exceptionClass;                                \
                                                                                                \
            try                                                                                 \
            {                                                                                   \
                BL_THROW( bl::exceptionClass(), message );                                      \
            }                                                                                   \
            catch( std::exception& )                                                            \
            {                                                                                   \
                eptr = std::current_exception();                                                \
            }                                                                                   \
                                                                                                \
            UTF_REQUIRE( eptr );                                                                \
                                                                                                \
            const auto errorJson = dm::ServerErrorHelpers::createServerErrorObject( eptr );     \
                                                                                                \
            UTF_REQUIRE_EQUAL(                                                                  \
                errorJson -> result() -> exceptionType(),                                       \
                std::string( bl::exceptionClass::fullTypeNameStatic() )                         \
                );                                                                              \
                                                                                                \
            const auto backPtr =                                                                \
                dm::ServerErrorHelpers::createExceptionFromObject( errorJson );                 \
                                                                                                \
            try                                                                                 \
            {                                                                                   \
                cpp::safeRethrowException( backPtr );                                           \
                                                                                                \
                UTF_FAIL( "createExceptionFromObject must produce a throwable exception" );     \
            }                                                                                   \
            catch( bl::BaseExceptionDefault& e )                                                \
            {                                                                                   \
                UTF_CHECK_EQUAL(                                                                \
                    std::string( e.fullTypeName() ),                                            \
                    std::string( bl::reconstructedClass::fullTypeNameStatic() )                 \
                    );                                                                          \
                                                                                                \
                UTF_CHECK_EQUAL( std::string( e.what() ), message );                            \
            }                                                                                   \
        }                                                                                       \

    /*
     * A mapped type must come back as ITSELF. The 'out' half of each pair breaks if a
     * BL_DECLARE_EXCEPTION name string is edited; the 'back' half is the one which would have
     * caught cb431f0; and the what() half proves errinfo_message survived
     * exceptionFromProperties()'s BL_DM_SET_EXCEPTION_STRING_PROPERTY
     */

#define UTEST_ROUNDTRIP_EXCEPTION_TYPE( exceptionClass )                                        \
        UTEST_EXCEPTION_TYPE_ROUNDTRIP_IMPL(                                                    \
            exceptionClass, exceptionClass, roundTrippedTypes                                   \
            )                                                                                   \

    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ArgumentException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ArgumentNullException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( CacheException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ExternalCommandException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( HttpException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( HttpServerException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( TimeoutException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( JavaException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( JsonException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( NotSupportedException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ObjectDisconnectedException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( SecurityException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ServerErrorException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ServerNoConnectionException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( UnexpectedException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( XmlException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( InvalidDataFormatException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( UserMessageException )

    /*
     * The declared types which have NO arm in createExceptionFromObject() and therefore take the
     * fall-through at ServerErrorHelpers.h:409-412. This is CHARACTERISING the current behaviour
     * and not endorsing it: a BufferTooSmallException raised in the broker
     * (messaging/BrokerBackendProcessing.h, messaging/MessagingUtils.h) reaches the client as a
     * bl::UnexpectedException today. The point of pinning it is that a SIXTH unmapped type
     * cannot be added tomorrow without this case failing - see the count assertions at the end
     *
     * When the production mapping is extended, move the entry to the table above
     */

#define UTEST_UNMAPPED_EXCEPTION_TYPE( exceptionClass )                                         \
        UTEST_EXCEPTION_TYPE_ROUNDTRIP_IMPL(                                                    \
            exceptionClass, UnexpectedException, fallThroughTypes                               \
            )                                                                                   \

    UTEST_UNMAPPED_EXCEPTION_TYPE( BufferTooSmallException )
    UTEST_UNMAPPED_EXCEPTION_TYPE( NotFoundException )
    UTEST_UNMAPPED_EXCEPTION_TYPE( UserAuthenticationException )
    UTEST_UNMAPPED_EXCEPTION_TYPE( NumberCoerceException )
    UTEST_UNMAPPED_EXCEPTION_TYPE( PrintableWrapperException )

#undef UTEST_UNMAPPED_EXCEPTION_TYPE

#undef UTEST_ROUNDTRIP_EXCEPTION_TYPE

#undef UTEST_EXCEPTION_TYPE_ROUNDTRIP_IMPL

    /*
     * UserMessageException must come back user friendly. Its constructor stamps
     * errinfo_is_user_friendly and exceptionFromProperties() also restores it from the
     * isUserFriendly property, so this pins that at least one of the two mechanisms works
     */

    {
        std::exception_ptr eptr;

        try
        {
            BL_THROW( UserMessageException(), "msg: user friendly round trip" );
        }
        catch( std::exception& )
        {
            eptr = std::current_exception();
        }

        UTF_REQUIRE( eptr );

        const auto backPtr = dm::ServerErrorHelpers::createExceptionFromObject(
            dm::ServerErrorHelpers::createServerErrorObject( eptr )
            );

        try
        {
            cpp::safeRethrowException( backPtr );
        }
        catch( UserMessageException& e )
        {
            UTF_REQUIRE( eh::isUserFriendly( e ) );
        }
    }

    /*
     * The guard which forces a newly declared exception to be added to one of the two tables
     *
     * core/ErrorHandling.h declares 24 bl:: exception types: 22 through BL_DECLARE_EXCEPTION* at
     * :750-771, plus UserMessageException (:716) and SystemException (:779). 18 are round
     * tripped above, 5 fall through, and SystemException is covered by ErrorToJsonTests. C++ has
     * no way to count the declarations, so the 24 below is the hand maintained half of the
     * coupling - it is what the maintenance note at ErrorHandling.h:745 asks a reader to keep in
     * step, and it must be bumped together with whichever table gains the new entry
     */

    UTF_REQUIRE_EQUAL( roundTrippedTypes, 18U );
    UTF_REQUIRE_EQUAL( fallThroughTypes, 5U );

    UTF_REQUIRE_EQUAL(
        roundTrippedTypes + fallThroughTypes + 1U /* SystemException, see ErrorToJsonTests */,
        24U
        );
}

UTF_AUTO_TEST_CASE( ServerErrorHelpersTests )
{
    using namespace bl;

    {
        /*
         * Let's test using UserMessageException
         */

        std::exception_ptr eptr;

        const std::string userMessage = "Test user exception";

        try
        {
            BL_THROW_USER( userMessage );
        }
        catch( std::exception& )
        {
            /*
             * Don't use std::make_exception_ptr() as it slices the exception object
             */

            eptr = std::current_exception();
        }

        UTF_REQUIRE( eptr );

        const auto serverError = dm::ServerErrorHelpers::createServerErrorObject( eptr );

        UTF_REQUIRE( serverError );
        UTF_REQUIRE( serverError -> result() );
        UTF_CHECK_EQUAL( serverError -> result() -> exceptionType(), "bl::UserMessageException" );
        UTF_CHECK_EQUAL( serverError -> result() -> exceptionMessage(), userMessage );
        UTF_CHECK( ! serverError -> result() -> exceptionFullDump().empty() );

        /*
         * result -> message() is the USER FACING string and it comes out of a ternary on
         * eh::isUserFriendly( e ) - GraphQLErrorHelpers.h uses it verbatim as the GraphQL
         * message and RestUtils puts it in the REST response body. This is the friendly branch:
         * the exception's own text is what the user is shown
         */

        UTF_CHECK_EQUAL( serverError -> result() -> message(), userMessage );

        const auto& properties = serverError -> result() -> exceptionProperties();

        UTF_REQUIRE( properties );
        UTF_CHECK_EQUAL( properties -> isUserFriendly(), true );
        UTF_CHECK_EQUAL( properties -> message(), userMessage );

        auto reconstructedEptr = dm::ServerErrorHelpers::createExceptionFromObject( serverError );

        UTF_REQUIRE( reconstructedEptr );
        UTF_REQUIRE_THROW(
            cpp::safeRethrowException( reconstructedEptr ),
            UserMessageException
            );

        try
        {
            cpp::safeRethrowException( reconstructedEptr );
        }
        catch( UserMessageException& e )
        {
            UTF_CHECK_EQUAL( e.what(), userMessage );
        }
    }

    const auto errorCode = eh::errc::make_error_code( eh::errc::permission_denied );

    const auto exception = utest::createDecoratedException(
        HttpServerException(),
        "ServerErrorHelpersTests",
        "message: Bad client request",
        errorCode
        );

    const auto serverErrorAsJson = dm::ServerErrorHelpers::getServerErrorAsJson( exception );

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\nThe JSON text for server error message is: \n "
            << serverErrorAsJson
            << "\n"
        );

    const auto serverError = dm::ServerErrorHelpers::createServerErrorObject( exception );

    UTF_REQUIRE_EQUAL( serverError -> result() -> exceptionType(), "bl::HttpServerException" );
    UTF_REQUIRE_EQUAL( serverError -> result() -> exceptionMessage(), "message: Bad client request" );
    UTF_REQUIRE( ! serverError -> result() -> exceptionFullDump().empty() );

    /*
     * The other branch of the same ternary: this exception carries is_user_friendly( false ),
     * so the user facing message must be the generic text and must NOT be the exception's own
     * message, which may name internal state. Collapsing the ternary to e.what() passes the
     * whole suite today and leaks the raw text on every REST and GraphQL error response
     */

    UTF_CHECK_EQUAL(
        serverError -> result() -> message(),
        std::string( BL_GENERIC_FRIENDLY_UNEXPECTED_MSG )
        );

    UTF_CHECK( serverError -> result() -> message() != serverError -> result() -> exceptionMessage() );

    const auto validateProperties = []( SAA_in const om::ObjPtr< dm::ExceptionProperties >& properties ) -> void
    {
        UTF_REQUIRE_EQUAL( properties -> errNo()                   , 1001 );
        UTF_REQUIRE_EQUAL( properties -> fileName()                , "file_name: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> fileOpenMode()            , "file_open_mode: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> message()                 , "message: Bad client request" );
        UTF_REQUIRE_EQUAL( properties -> timeThrown()              , "2015-09-23T18:29:10.475824-04:00" );
        UTF_REQUIRE_EQUAL( properties -> functionName()            , "function_name: ServerErrorHelpersTests" )
        UTF_REQUIRE_EQUAL( properties -> systemCode()              , static_cast< int >( http::Parameters::HTTP_CLIENT_ERROR_BAD_REQUEST ) );
        UTF_REQUIRE_EQUAL( properties -> categoryName()            , "generic" );
        UTF_REQUIRE_EQUAL( properties -> errorCode()               , eh::errc::permission_denied );
        UTF_REQUIRE_EQUAL( properties -> errorCodeMessage()        , "Permission denied" );
        UTF_REQUIRE_EQUAL( properties -> isExpected()              , true );
        UTF_REQUIRE_EQUAL( properties -> taskInfo()                , "task_info: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> hostName()                , net::getShortHostName() );
        UTF_REQUIRE_EQUAL( properties -> serviceName()             , "service_name: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> endpointAddress()         , "endpoint_address: ServerErrorHelpersTests address" );
        UTF_REQUIRE_EQUAL( properties -> endpointPort()            , 1002 );
        UTF_REQUIRE_EQUAL( properties -> httpUrl()                 , "www.mytest.com" );
        UTF_REQUIRE_EQUAL( properties -> httpRedirectUrl()         , "www.myredirectedtest.com" );
        UTF_REQUIRE_EQUAL( properties -> httpStatusCode()          , 1003 );
        UTF_REQUIRE_EQUAL( properties -> httpResponseHeaders()     , "<header1><header2><header3>" );
        UTF_REQUIRE_EQUAL( properties -> httpRequestDetails()      , "_http_request_details: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> parserFile()              , "Blablabla" );
        UTF_REQUIRE_EQUAL( properties -> parserLine()              , 1004 );
        UTF_REQUIRE_EQUAL( properties -> parserColumn()            , 1005 );
        UTF_REQUIRE_EQUAL( properties -> parserReason()            , "syntax error" );
        UTF_REQUIRE_EQUAL( properties -> externalCommandOutput()   , "external_command_output: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> externalCommandExitCode() , 1006 );
        UTF_REQUIRE_EQUAL( properties -> stringValue()             , "string_value: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> isUserFriendly()          , false );
        UTF_REQUIRE_EQUAL( properties -> sslIsVerifyFailed()       , true );
        UTF_REQUIRE_EQUAL( properties -> sslIsVerifyError()        , 1007 );
        UTF_REQUIRE_EQUAL( properties -> sslIsVerifyErrorMessage() , "ssl_is_verify_error_message: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> sslIsVerifyErrorString () , "ssl_is_verify_error_string: ServerErrorHelpersTests" );
        UTF_REQUIRE_EQUAL( properties -> sslIsVerifySubjectName () , "ssl_is_verify_subject_name: ServerErrorHelpersTests" );
    };

    validateProperties( serverError -> result() -> exceptionProperties() );

    /*
     * Get the part related to the exception properties
     */

    auto pos = serverErrorAsJson.find( "exceptionProperties" );

    auto exceptionProperties = serverErrorAsJson.substr( pos );

    pos = exceptionProperties.find( "{" );
    const auto posEnd = exceptionProperties.find( "}" );

    UTF_REQUIRE( pos != std::string::npos );
    UTF_REQUIRE( posEnd != std::string::npos );

    exceptionProperties = exceptionProperties.substr( pos, posEnd - pos + 1U );

    const auto properties =
        dm::DataModelUtils::loadFromJsonText< dm::ExceptionProperties >( exceptionProperties );

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            <<"\nThe exception properties of a server error JSON are: "
            << dm::DataModelUtils::getDocAsPrettyJsonString( properties )
            << "\n"
        );

    validateProperties( properties );
}

UTF_AUTO_TEST_CASE( ServerErrorHelpersRedactionTests )
{
    using namespace bl;

    /*
     * getRedactedServerErrorAsJson() is the only thing standing between a server side exception
     * dump and an untrusted HTTP client (its one production caller is
     * httpserver/ServerBackendProcessingImplDefault.h), and until now it had never been executed
     * with an assertion attached anywhere
     *
     * The redaction list is a hand maintained enumeration of eleven of the 34 properties, with
     * no compiler or test coupling to the property set: adding a disclosing property to
     * data/models/ErrorHandling.h and forgetting this list, or deleting one line from it, is an
     * information disclosure regression with no signal at all
     *
     * BaseLib_HttpServerStdErrorResponseRedactionTest in utf_baselib_http covers the same
     * machinery from the response body side, but a substring search cannot prove WHICH
     * properties differ, and a content.find( "1002" ) == npos style check passes equally whether
     * endpointPort was omitted or emitted as 0. This case parses both documents back into the
     * model and diffs them property by property instead
     */

    const auto errorCode = eh::errc::make_error_code( eh::errc::permission_denied );

    const auto eptr = utest::createDecoratedException(
        HttpServerException(),
        "ServerErrorHelpersRedactionTests",
        "message: Bad client request",
        errorCode
        );

    const auto fullJson = dm::ServerErrorHelpers::getServerErrorAsJson( eptr );
    const auto redactedJson = dm::ServerErrorHelpers::getRedactedServerErrorAsJson( eptr );

    /*
     * Loading the redacted document back must not throw, and that alone pins the "<redacted>"
     * literal: exceptionFullDump is a required string property, so emptying it instead of
     * stamping it would make BL_DM_DECLARE_STRING_PROPERTY_SERIALIZE throw
     * BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET at the getRedactedServerErrorAsJson() call above and
     * the HTTP server would produce no error body at all
     */

    const auto full = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( fullJson );
    const auto redacted = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( redactedJson );

    UTF_REQUIRE( full -> result() );
    UTF_REQUIRE( redacted -> result() );

    const auto& fullProperties = full -> result() -> exceptionProperties();
    const auto& redactedProperties = redacted -> result() -> exceptionProperties();

    UTF_REQUIRE( fullProperties );
    UTF_REQUIRE( redactedProperties );

    /*
     * Redacted away. The 'non-empty on the full document' half is the positive control - without
     * it a property which simply stopped being emitted at all would satisfy the negative half
     * vacuously
     */

#define UTEST_REQUIRE_REDACTED_AWAY( property ) \
        { \
            UTF_REQUIRE( ! fullProperties -> property().empty() ); \
            UTF_REQUIRE( redactedProperties -> property().empty() ); \
        } \

    UTEST_REQUIRE_REDACTED_AWAY( fileName )
    UTEST_REQUIRE_REDACTED_AWAY( fileOpenMode )
    UTEST_REQUIRE_REDACTED_AWAY( functionName )
    UTEST_REQUIRE_REDACTED_AWAY( taskInfo )
    UTEST_REQUIRE_REDACTED_AWAY( hostName )
    UTEST_REQUIRE_REDACTED_AWAY( serviceName )
    UTEST_REQUIRE_REDACTED_AWAY( endpointAddress )
    UTEST_REQUIRE_REDACTED_AWAY( httpUrl )
    UTEST_REQUIRE_REDACTED_AWAY( httpRedirectUrl )
    UTEST_REQUIRE_REDACTED_AWAY( externalCommandOutput )
    UTEST_REQUIRE_REDACTED_AWAY( parserFile )

#undef UTEST_REQUIRE_REDACTED_AWAY

    UTF_REQUIRE_EQUAL( redacted -> result() -> exceptionFullDump(), "<redacted>" );
    UTF_REQUIRE( full -> result() -> exceptionFullDump() != "<redacted>" );

    /*
     * endpointPort is the only scalar in the list and it is cleared with endpointPortReset(), so
     * the key is OMITTED rather than emitted as 0. This is the assertion a string search over
     * the response body cannot make
     */

    UTF_REQUIRE( fullProperties -> endpointPortIsSet() );
    UTF_REQUIRE( ! redactedProperties -> endpointPortIsSet() );

    /*
     * Preserved - the over-redaction guard. Blanking exceptionMessage or message would strip the
     * only diagnostic a client ever gets
     *
     * Known gap which is deliberately NOT asserted as a defect here: exceptionMessage and
     * properties -> message() are not redacted even though BL_MSG() text in this codebase
     * routinely contains file paths. If the maintainers decide those should be redacted, this
     * list needs updating with the decision
     */

#define UTEST_REQUIRE_PRESERVED( property ) \
        { \
            UTF_REQUIRE_EQUAL( fullProperties -> property(), redactedProperties -> property() ); \
        } \

    UTF_REQUIRE_EQUAL( full -> result() -> message(), redacted -> result() -> message() );
    UTF_REQUIRE_EQUAL( full -> result() -> exceptionType(), redacted -> result() -> exceptionType() );
    UTF_REQUIRE_EQUAL( full -> result() -> exceptionMessage(), redacted -> result() -> exceptionMessage() );

    UTEST_REQUIRE_PRESERVED( errNo )
    UTEST_REQUIRE_PRESERVED( timeThrown )
    UTEST_REQUIRE_PRESERVED( systemCode )
    UTEST_REQUIRE_PRESERVED( categoryName )
    UTEST_REQUIRE_PRESERVED( errorCode )
    UTEST_REQUIRE_PRESERVED( errorCodeMessage )
    UTEST_REQUIRE_PRESERVED( isExpected )
    UTEST_REQUIRE_PRESERVED( httpStatusCode )
    UTEST_REQUIRE_PRESERVED( httpResponseHeaders )
    UTEST_REQUIRE_PRESERVED( httpRequestDetails )
    UTEST_REQUIRE_PRESERVED( parserLine )
    UTEST_REQUIRE_PRESERVED( parserColumn )
    UTEST_REQUIRE_PRESERVED( parserReason )
    UTEST_REQUIRE_PRESERVED( externalCommandExitCode )
    UTEST_REQUIRE_PRESERVED( stringValue )
    UTEST_REQUIRE_PRESERVED( isUserFriendly )
    UTEST_REQUIRE_PRESERVED( sslIsVerifyFailed )
    UTEST_REQUIRE_PRESERVED( sslIsVerifyError )
    UTEST_REQUIRE_PRESERVED( sslIsVerifyErrorMessage )
    UTEST_REQUIRE_PRESERVED( sslIsVerifyErrorString )
    UTEST_REQUIRE_PRESERVED( sslIsVerifySubjectName )

#undef UTEST_REQUIRE_PRESERVED

    /*
     * Belt and braces on the raw text for the two values most likely to leak through a future
     * model change, each paired with the same lookup against the unredacted document so that the
     * negative half cannot pass vacuously
     */

    UTF_REQUIRE( fullJson.find( net::getShortHostName() ) != std::string::npos );
    UTF_REQUIRE( redactedJson.find( net::getShortHostName() ) == std::string::npos );

    UTF_REQUIRE( fullJson.find( "function_name: " ) != std::string::npos );
    UTF_REQUIRE( redactedJson.find( "function_name: " ) == std::string::npos );
}

UTF_AUTO_TEST_CASE( ServerErrorHelpersExceptionCallbackTests )
{
    using namespace bl;

    /*
     * The exceptionCallback parameter is forwarded by all four public entry points and is
     * invoked exactly once, from inside the catch block, AFTER populateExceptionResult() has
     * filled the model and BEFORE createServerErrorResultObject() returns. Every existing test
     * uses the one argument overload, so none of that is observable today
     *
     * The only production consumer is rest/RestUtils.h's formatEhResponseSimpleJson(), which
     * binds updateHttpStatusFromException( _1, cpp::ref( httpStatusCodeActual ) ) and reads
     * httpStatusCodeActual on the NEXT line to build the Response - so the whole HTTP status
     * mapping of the REST gateway depends on the callback running synchronously, before the
     * helper returns, against an exception which still carries its errinfo values.
     * messaging/GraphQLErrorHelpers.h forwards it the same way
     *
     * The callback body is deliberately non-throwing: a throwing callback escapes
     * createServerErrorResultObject() (nothing catches it) and destroys the error document,
     * which is an open production robustness question and must not be frozen by a test here
     */

    std::exception_ptr eptr;

    try
    {
        BL_THROW(
            ServerErrorException()
                << eh::errinfo_error_code( eh::errc::make_error_code( eh::errc::permission_denied ) ),
            "cb-test"
            );
    }
    catch( std::exception& )
    {
        eptr = std::current_exception();
    }

    UTF_REQUIRE( eptr );

    int callCount = 0;
    std::string seenWhat;
    int seenCode = 0;

    const eh::void_exception_callback_t cb = [ & ]( SAA_inout std::exception& e ) -> void
    {
        ++callCount;

        seenWhat = e.what();

        const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

        if( ec )
        {
            seenCode = ec -> value();
        }
    };

    /*
     * 1. Invocation and timing
     */

    {
        const auto errorJson = dm::ServerErrorHelpers::createServerErrorObject( eptr, cb );

        UTF_REQUIRE_EQUAL( callCount, 1 );

        /*
         * The callback receives a non-const reference to the LIVE exception object and not a
         * stripped copy - if it stopped seeing the errinfo values, updateHttpStatusFromException
         * would silently map every error to the caller's default status code
         */

        UTF_REQUIRE_EQUAL( seenWhat, "cb-test" );
        UTF_REQUIRE_EQUAL( seenCode, static_cast< int >( eh::errc::permission_denied ) );

        /*
         * ... and the document is fully populated, i.e. the callback runs after population
         */

        UTF_REQUIRE( errorJson -> result() );
        UTF_REQUIRE( errorJson -> result() -> exceptionProperties() );

        UTF_REQUIRE_EQUAL(
            errorJson -> result() -> exceptionProperties() -> errorCode(),
            eh::errc::permission_denied
            );
    }

    /*
     * 2. and 3. The string producing entry points forward it too - including the redacting one,
     * which is the variant the HTTP server uses
     */

    {
        callCount = 0;

        const auto text = dm::ServerErrorHelpers::getServerErrorAsJson( eptr, cb );

        UTF_REQUIRE_EQUAL( callCount, 1 );
        UTF_REQUIRE( ! text.empty() );
    }

    {
        callCount = 0;

        const auto text = dm::ServerErrorHelpers::getRedactedServerErrorAsJson( eptr, cb );

        UTF_REQUIRE_EQUAL( callCount, 1 );
        UTF_REQUIRE( ! text.empty() );
    }

    /*
     * 4. The default path must not invoke anything: defaultEhCallback() returns an empty
     * cpp::function and calling it would throw std::bad_function_call out of an error handler
     */

    {
        callCount = 0;

        ( void ) dm::ServerErrorHelpers::createServerErrorObject( eptr );

        UTF_REQUIRE_EQUAL( callCount, 0 );

        ( void ) dm::ServerErrorHelpers::createServerErrorObject(
            eptr,
            dm::ServerErrorHelpers::defaultEhCallback()
            );

        UTF_REQUIRE_EQUAL( callCount, 0 );
    }
}
