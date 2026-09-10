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
#include <baselib/messaging/GraphQLErrorHelpers.h>

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
        /*
         * A category name this process cannot resolve does not reject the document - the error
         * code cannot be rebuilt, because an eh::error_category is a process local object, but
         * the name itself survives as data
         */

        auto serverErrorJson = dm::ServerErrorJson::createInstance();
        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );
        serverErrorJson -> result() -> exceptionType( "bl::ServerErrorException" );
        serverErrorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );
        serverErrorJson -> result() -> exceptionProperties() -> categoryName( "non-generic" );
        serverErrorJson -> result() -> exceptionProperties() -> errorCode( 42 );

        try
        {
            std::rethrow_exception(
                dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson )
                );

            UTF_FAIL( "The restored exception must be thrown" );
        }
        catch( ServerErrorException& e )
        {
            UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_category_name, "non-generic" )

            UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_error_code >( e ) );
        }
    }

    /*
     * The "system" arm of the categoryName chain and the bl::SystemException guard
     *
     * Windows OS errors arrive as eh::system_category(), so if the "system" arm were lost every
     * Windows-origin SystemException crossing the messaging boundary would be rejected at the
     * receiving end with "Unknown error category: 'system'" - on Windows only, with no Linux CI
     * signal at all. The guard is the only thing standing between a malformed peer document and
     * a null dereference of *errorCategory further down
     */

    const auto makeSystemExceptionDocument = [](
        SAA_in          const std::string&                          categoryName,
        SAA_in          const bool                                  setErrorCode,
        SAA_in          const std::string&                          exceptionMessage
        )
        -> om::ObjPtr< dm::ServerErrorJson >
    {
        auto serverErrorJson = dm::ServerErrorJson::createInstance();

        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );
        serverErrorJson -> result() -> exceptionType( "bl::SystemException" );
        serverErrorJson -> result() -> exceptionMessage( exceptionMessage );
        serverErrorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );
        serverErrorJson -> result() -> exceptionProperties() -> categoryName( categoryName );

        if( setErrorCode )
        {
            serverErrorJson -> result() -> exceptionProperties() -> errorCode( 5 );
        }

        return serverErrorJson;
    };

    try
    {
        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject(
            makeSystemExceptionDocument( "system", true /* setErrorCode */, "prefix: some system error" )
            );

        std::rethrow_exception( exceptionPtr );
    }
    catch( SystemException& e )
    {
        UTF_REQUIRE_EQUAL( e.fullTypeName(), "bl::SystemException" );

        UTEST_PROPERTY_REQUIRE_EQUAL( errinfo_category_name, "system" )

        const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

        UTF_REQUIRE( ec != nullptr );
        UTF_REQUIRE( ec -> category() == eh::system_category() );
        UTF_REQUIRE_EQUAL( ec -> value(), 5 );

        /*
         * A substring, because the trailing [category:value] form is Boost version dependent -
         * the same workaround the generic SystemException block above uses
         */

        UTF_REQUIRE( cpp::contains( std::string( e.what() ), "prefix" ) );
    }

    try
    {
        /*
         * An exceptionMessage with no ": " separator leaves whatPrefix empty rather than
         * promoting the whole message to the prefix
         */

        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject(
            makeSystemExceptionDocument( "system", true /* setErrorCode */, "no separator here" )
            );

        std::rethrow_exception( exceptionPtr );
    }
    catch( SystemException& e )
    {
        const std::string what( e.what() );

        UTF_REQUIRE( ! what.empty() );
        UTF_REQUIRE( std::string::npos == what.find( "no separator here" ) );
    }

    {
        /*
         * Both halves of the guard's || - no category, and a category but no error code
         */

        const auto requireGuardThrows = []( SAA_in const om::ObjPtr< dm::ServerErrorJson >& document ) -> void
        {
            UTF_REQUIRE_EXCEPTION(
                ( void )dm::ServerErrorHelpers::createExceptionFromObject( document ),
                ArgumentException,
                []( SAA_in const ArgumentException& e ) -> bool
                {
                    return std::string(
                        "errorCategory or errorCode properties are not set for SystemException"
                        ) == e.what();
                }
                );
        };

        requireGuardThrows(
            makeSystemExceptionDocument( bl::str::empty(), true /* setErrorCode */, "prefix: msg" )
            );

        requireGuardThrows(
            makeSystemExceptionDocument( "generic", false /* setErrorCode */, "prefix: msg" )
            );
    }

    try
    {
        /*
         * An EMPTY category name means the error-code block in exceptionFromProperties() is
         * skipped even when errorCode is set - and there is no name to carry as data either, so
         * errinfo_category_name must be absent as well
         */

        auto serverErrorJson = dm::ServerErrorJson::createInstance();

        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );
        serverErrorJson -> result() -> exceptionType( "bl::ArgumentException" );
        serverErrorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );
        serverErrorJson -> result() -> exceptionProperties() -> categoryName( bl::str::empty() );
        serverErrorJson -> result() -> exceptionProperties() -> errorCode( 13 );
        serverErrorJson -> result() -> exceptionProperties() -> message( "no-category" );

        const auto exceptionPtr = dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson );

        std::rethrow_exception( exceptionPtr );
    }
    catch( ArgumentException& e )
    {
        UTF_REQUIRE_EQUAL( e.fullTypeName(), "bl::ArgumentException" );
        UTF_CHECK_EQUAL( e.what(), "no-category" );

        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_error_code >( e ) );
        UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_category_name >( e ) );
    }

#undef UTEST_PROPERTY_REQUIRE_EQUAL
}

UTF_AUTO_TEST_CASE( ErrorToJsonExceptionTypeMappingTests )
{
    using namespace bl;

    /*
     * createServerErrorResultObject() writes e.fullTypeName() into the exceptionType string and
     * createExceptionFromObject() reads it back through a chain of 24 bl:: arms plus the
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
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( BufferTooSmallException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( CacheException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ExternalCommandException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( HttpException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( HttpServerException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( TimeoutException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( JavaException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( JsonException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( NotFoundException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( NotSupportedException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( NumberCoerceException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ObjectDisconnectedException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( PrintableWrapperException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( SecurityException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ServerErrorException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( ServerNoConnectionException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( UnexpectedException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( XmlException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( InvalidDataFormatException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( UserAuthenticationException )
    UTEST_ROUNDTRIP_EXCEPTION_TYPE( UserMessageException )

    /*
     * The declared types which have NO arm in createExceptionFromObject() and therefore take the
     * fall-through to UnexpectedException. The table is empty now that the five types which used
     * to take it - BufferTooSmallException, NotFoundException, UserAuthenticationException,
     * NumberCoerceException and PrintableWrapperException - have arms of their own; the machinery
     * is kept because a newly declared type still lands here until someone adds its arm, and the
     * count assertions at the end are what makes that visible
     */

    /*
     * A type which is not declared by core/ErrorHandling.h at all, so it can never gain an arm -
     * this keeps the fall-through arm itself covered now that every declared type is mapped, and
     * it is also the shape of the case the fall-through exists for: a newer server naming a type
     * this client has never heard of
     */

    {
        ++fallThroughTypes;

        const auto errorJson = dm::ServerErrorJson::createInstance();

        errorJson -> result( dm::ServerErrorResult::createInstance() );
        errorJson -> result() -> message( "msg: a type from a newer peer" );
        errorJson -> result() -> exceptionType( "bl::SomeExceptionFromANewerPeer" );
        errorJson -> result() -> exceptionMessage( "msg: a type from a newer peer" );
        errorJson -> result() -> exceptionFullDump( "<none>" );
        errorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );

        try
        {
            cpp::safeRethrowException(
                dm::ServerErrorHelpers::createExceptionFromObject( errorJson )
                );

            UTF_FAIL( "createExceptionFromObject must produce a throwable exception" );
        }
        catch( bl::BaseExceptionDefault& e )
        {
            UTF_CHECK_EQUAL(
                std::string( e.fullTypeName() ),
                std::string( bl::UnexpectedException::fullTypeNameStatic() )
                );
        }
    }

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
     * :750-771, plus UserMessageException (:716) and SystemException (:779). 23 are round
     * tripped above and SystemException is covered by ErrorToJsonTests, so every declared type
     * now has an arm and the single fall-through case above uses an undeclared name. C++ has no
     * way to count the declarations, so the 24 below is the hand maintained half of the coupling
     * - it is what the maintenance note at ErrorHandling.h:745 asks a reader to keep in step,
     * and a newly declared type must be added to the table above together with it
     */

    UTF_REQUIRE_EQUAL( roundTrippedTypes, 23U );
    UTF_REQUIRE_EQUAL( fallThroughTypes, 1U );

    UTF_REQUIRE_EQUAL(
        roundTrippedTypes + 1U /* SystemException, see ErrorToJsonTests */,
        24U
        );
}

UTF_AUTO_TEST_CASE( ErrorToJsonSystemCodeDerivationTests )
{
    using namespace bl;

    /*
     * populateExceptionResult() reads errinfo_error_code FIRST and, when it is present, writes
     * exceptionProperties -> systemCode( errorCode -> value() ). The macro line further down
     * then OVERWRITES systemCode from errinfo_system_code, but only when that errinfo is
     * present. So an exception which carries only an error code silently arrives on the wire
     * with systemCode holding the errc numeric value
     *
     * That is not a corner case: BL_THROW_SERVER_ERROR( errcondition, msg ) - the canonical way
     * this library raises a coded server error - attaches errinfo_error_code and nothing else.
     * Both existing cases in this file set errinfo_system_code explicitly as well, so the macro
     * line always wins there and the derivation is invisible
     */

    const auto propertiesOf = []( SAA_in const std::exception_ptr& eptr )
        -> om::ObjPtr< dm::ExceptionProperties >
    {
        const auto serverErrorJson = dm::ServerErrorHelpers::createServerErrorObject( eptr );

        UTF_REQUIRE( serverErrorJson -> result() );
        UTF_REQUIRE( serverErrorJson -> result() -> exceptionProperties() );

        return om::copy( serverErrorJson -> result() -> exceptionProperties() );
    };

    const auto roundTrip = []( SAA_in const std::exception_ptr& eptr ) -> std::exception_ptr
    {
        return dm::ServerErrorHelpers::createExceptionFromObject(
            dm::ServerErrorHelpers::createServerErrorObject( eptr )
            );
    };

    {
        /*
         * 1. errinfo_error_code only - the derived path
         */

        std::exception_ptr eptr;

        try
        {
            BL_THROW_SERVER_ERROR( eh::errc::permission_denied, "coded" );
        }
        catch( ServerErrorException& e )
        {
            /*
             * The positive control for the asymmetry below: the exception as thrown carries no
             * errinfo_system_code at all
             */

            UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_error_code >( e ) );
            UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_system_code >( e ) );

            eptr = std::current_exception();
        }

        UTF_REQUIRE( eptr );

        const auto props = propertiesOf( eptr );

        UTF_REQUIRE_EQUAL( props -> systemCode(), static_cast< int >( eh::errc::permission_denied ) );
        UTF_REQUIRE( props -> errorCodeIsSet() );

        try
        {
            cpp::safeRethrowException( roundTrip( eptr ) );

            UTF_FAIL( "The round tripped exception must be thrown" );
        }
        catch( ServerErrorException& e )
        {
            /*
             * ... and the round tripped copy has GAINED an errinfo the original never had.
             * cmdline/CmdLineAppBase.h reads errinfo_system_code to compute a process exit
             * code, so the original and its copy would exit with different codes
             */

            UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_system_code >( e ) );
        }
    }

    {
        /*
         * 2. Both - the explicit errinfo_system_code wins over the derived value. This is the
         * ORDERING assertion: moving the derivation after the macro block would silently
         * corrupt the HTTP status of every exception which carries both
         */

        const auto eptr = BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_error_code( eh::errc::make_error_code( eh::errc::permission_denied ) )
                << eh::errinfo_system_code( 400 ),
            "both"
            );

        const auto props = propertiesOf( eptr );

        UTF_REQUIRE_EQUAL( props -> systemCode(), 400 );
        UTF_REQUIRE( props -> errorCodeIsSet() );
    }

    {
        /*
         * 3. errinfo_system_code only - nothing to derive from, and no error code is
         * reconstructed on the way back
         */

        const auto eptr = BL_MAKE_EXCEPTION_PTR(
            ServerErrorException() << eh::errinfo_system_code( 400 ),
            "system code only"
            );

        const auto props = propertiesOf( eptr );

        UTF_REQUIRE_EQUAL( props -> systemCode(), 400 );
        UTF_REQUIRE( ! props -> errorCodeIsSet() );

        try
        {
            cpp::safeRethrowException( roundTrip( eptr ) );

            UTF_FAIL( "The round tripped exception must be thrown" );
        }
        catch( ServerErrorException& e )
        {
            UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_system_code >( e ) );
            UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_error_code >( e ) );
        }
    }

    {
        /*
         * 4. A category this process cannot name - the positive control for the relaxation of
         * the category chain, which used to reject the whole document with an ArgumentException
         *
         * The name and the numeric value survive as data on a non-SystemException type too; the
         * SystemException leg of the same behaviour is pinned by
         * CryptoErrorHandling_OpenSslCategoryDoesNotSurviveServerErrorRoundTrip in
         * utf_baselib_security against a real OpenSSL failure
         */

        const auto eptr = BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_category_name( "a category from a newer peer" )
                << eh::errinfo_system_code( 400 ),
            "unknown category"
            );

        const auto props = propertiesOf( eptr );

        UTF_REQUIRE_EQUAL( props -> categoryName(), "a category from a newer peer" );

        try
        {
            cpp::safeRethrowException( roundTrip( eptr ) );

            UTF_FAIL( "The round tripped exception must be thrown" );
        }
        catch( ServerErrorException& e )
        {
            const auto* categoryName = eh::get_error_info< eh::errinfo_category_name >( e );

            UTF_REQUIRE( nullptr != categoryName );
            UTF_REQUIRE_EQUAL( *categoryName, "a category from a newer peer" );

            const auto* systemCode = eh::get_error_info< eh::errinfo_system_code >( e );

            UTF_REQUIRE( nullptr != systemCode );
            UTF_REQUIRE_EQUAL( *systemCode, 400 );
        }
    }

    {
        /*
         * 5. A bl::SystemException whose category cannot be resolved AND which carries no
         * numeric value at all
         *
         * The relaxation above deliberately stops here: with nothing to rebuild the code from,
         * the only SystemException which could be produced is one whose code() reports success,
         * which is worse than refusing the document. The guard's message names the category, so
         * an operator can see which one this process could not resolve
         */

        auto serverErrorJson = dm::ServerErrorJson::createInstance();

        serverErrorJson -> result( dm::ServerErrorResult::createInstance() );
        serverErrorJson -> result() -> exceptionType( "bl::SystemException" );
        serverErrorJson -> result() -> exceptionMessage( "prefix: no code at all" );
        serverErrorJson -> result() -> exceptionProperties( dm::ExceptionProperties::createInstance() );
        serverErrorJson -> result() -> exceptionProperties() -> categoryName( "OpenSSL" );

        UTF_REQUIRE_THROW_MESSAGE(
            ( void ) dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson ),
            ArgumentException,
            "Neither systemCode nor errorCode is set"
            );

        /*
         * The positive control - the very same document with a numeric value does rehydrate,
         * so the guard above is about the missing value and not about the category name
         */

        serverErrorJson -> result() -> exceptionProperties() -> systemCode( 42 );

        try
        {
            cpp::safeRethrowException(
                dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson )
                );

            UTF_FAIL( "The restored exception must be thrown" );
        }
        catch( SystemException& e )
        {
            UTF_REQUIRE_EQUAL( e.code().value(), 42 );

            const auto* categoryName = eh::get_error_info< eh::errinfo_category_name >( e );

            UTF_REQUIRE( nullptr != categoryName );
            UTF_REQUIRE_EQUAL( *categoryName, "OpenSSL" );
        }
    }

    {
        /*
         * 6. A corrupt errorUuid is dropped rather than rejecting the whole document
         *
         * uuids::string2uuid throws for a value which is not a uuid, and letting one corrupt
         * field discard an otherwise well formed server error is the failure mode the category
         * chain above no longer has - so the field is gated on uuids::isUuid
         */

        const auto errorUuid = uuids::string2uuid( "0f5a3d1e-9c74-4c1a-8f2d-6c9b1a7e35d0" );

        const auto eptr = BL_MAKE_EXCEPTION_PTR(
            ServerErrorException() << eh::errinfo_error_uuid( errorUuid ),
            "uuid round trip"
            );

        const auto serverErrorJson = dm::ServerErrorHelpers::createServerErrorObject( eptr );

        /*
         * The positive control first - a well formed uuid does survive
         */

        try
        {
            cpp::safeRethrowException(
                dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson )
                );

            UTF_FAIL( "The restored exception must be thrown" );
        }
        catch( ServerErrorException& e )
        {
            const auto* restored = eh::get_error_info< eh::errinfo_error_uuid >( e );

            UTF_REQUIRE( nullptr != restored );
            UTF_REQUIRE_EQUAL( *restored, errorUuid );
        }

        serverErrorJson -> result() -> exceptionProperties() -> errorUuid( "not-a-uuid-at-all" );

        try
        {
            cpp::safeRethrowException(
                dm::ServerErrorHelpers::createExceptionFromObject( serverErrorJson )
                );

            UTF_FAIL( "The restored exception must be thrown" );
        }
        catch( ServerErrorException& e )
        {
            /*
             * The document still rehydrates and everything else on it survives; only the
             * corrupt field is gone
             */

            UTF_REQUIRE( nullptr == eh::get_error_info< eh::errinfo_error_uuid >( e ) );

            UTF_REQUIRE_EQUAL( std::string( e.fullTypeName() ), "bl::ServerErrorException" );
        }
    }
}

UTF_AUTO_TEST_CASE( ErrorToJsonUnmappedErrorInfoTests )
{
    using namespace bl;

    /*
     * ServerErrorHelpers maps the errinfo_* types onto ExceptionProperties in a hand written
     * macro list, mirrored in reverse in exceptionFromProperties(). There is no coupling of any
     * kind - compiler, test or otherwise - between the declaration list in core/ErrorHandling.h
     * and the serialisation list
     *
     * The eight errinfos below post-date the model and had NO wire representation at all until
     * they were added to it; three groups of them are attached by production code to exception
     * types which ARE explicitly wire mapped:
     *
     *   errinfo_hint, errinfo_original_type, errinfo_original_thread_name,
     *   errinfo_original_stack_trace          - jni/JniEnvironment.h, on bl::JavaException
     *
     *   errinfo_service_status, errinfo_service_status_category,
     *   errinfo_service_status_message        - security/AuthorizationServiceRest.h,
     *                                           on bl::SecurityException
     *
     *   errinfo_error_uuid                    - messaging/MessagingClientImpl.h and others, on
     *                                           bl::NotSupportedException / bl::TimeoutException
     *                                           and the storage exceptions
     *
     * This case pins their round trip end to end - through the JSON text and back into a live
     * exception - so a mapping removed from either half of the pair fails loudly here. The only
     * errinfos still deliberately without a property of their own are the two structural ones:
     * errinfo_full_type_name, which travels as the exceptionType field, and
     * errinfo_nested_exception_ptr, which is a process local pointer
     *
     * A reviewer who adds a new errinfo is expected to update this case deliberately rather than
     * have the behaviour change silently. ErrorToJsonTests enumerates the mapped ones, which
     * cannot see a gap at all, because the list under test IS the list under implementation
     */

    const auto stackTraceValue = std::string( "at Foo.bar(Foo.java:42)" );

    /*
     * A literal uuid rather than one of the BL_UUID_DECLARE'd values in
     * messaging/BrokerErrorCodes.h - that header must not be pulled into utf_baselib_data
     */

    const auto errorUuid = uuids::string2uuid( "0f5a3d1e-9c74-4c1a-8f2d-6c9b1a7e35d0" );

    const auto javaEptr = BL_MAKE_EXCEPTION_PTR(
        JavaException()
            << eh::errinfo_original_type( "java.lang.IllegalStateException" )
            << eh::errinfo_original_thread_name( "utest-thread" )
            << eh::errinfo_original_stack_trace( stackTraceValue )
            << eh::errinfo_hint( "hint: ErrorToJsonUnmappedErrorInfoTests" )
            << eh::errinfo_message( "java failure" ),
        "java failure"
        );

    const auto securityEptr = BL_MAKE_EXCEPTION_PTR(
        SecurityException()
            << eh::errinfo_service_status( 401 )
            << eh::errinfo_service_status_category( 7 )
            << eh::errinfo_service_status_message( "token expired" )
            << eh::errinfo_message( "authz failure" ),
        "authz failure"
        );

    const auto timeoutEptr = BL_MAKE_EXCEPTION_PTR(
        TimeoutException()
            << eh::errinfo_error_uuid( errorUuid )
            << eh::errinfo_message( "timeout failure" ),
        "timeout failure"
        );

    /*
     * Every "the value survived" assertion below is paired with a positive control on the SOURCE
     * exception, so it cannot silently become vacuous if a producer stops attaching the errinfo
     */

#define UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo, source, restored ) \
        { \
            const auto* sourceInfo = eh::get_error_info< eh::errinfo >( source ); \
            const auto* restoredInfo = eh::get_error_info< eh::errinfo >( restored ); \
            \
            UTF_REQUIRE( nullptr != sourceInfo ); \
            UTF_REQUIRE( nullptr != restoredInfo ); \
            \
            UTF_REQUIRE( *sourceInfo == *restoredInfo ); \
        } \

    {
        try
        {
            cpp::safeRethrowException( javaEptr );

            UTF_FAIL( "The source exception must be thrown" );
        }
        catch( JavaException& source )
        {
            const auto json = dm::ServerErrorHelpers::getServerErrorAsJson( javaEptr );

            const auto parsed = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( json );

            UTF_REQUIRE( parsed -> result() );
            UTF_REQUIRE( parsed -> result() -> exceptionProperties() );

            UTF_REQUIRE_EQUAL( parsed -> result() -> exceptionType(), JavaException::fullTypeNameStatic() );
            UTF_REQUIRE_EQUAL( parsed -> result() -> exceptionProperties() -> message(), "java failure" );

            try
            {
                cpp::safeRethrowException(
                    dm::ServerErrorHelpers::createExceptionFromObject( parsed )
                    );

                UTF_FAIL( "The restored exception must be thrown" );
            }
            catch( BaseExceptionDefault& restored )
            {
                UTF_REQUIRE_EQUAL( restored.fullTypeName(), JavaException::fullTypeNameStatic() );
                UTF_REQUIRE_EQUAL( restored.what(), "java failure" );

                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_original_type, source, restored )
                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_original_thread_name, source, restored )
                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_original_stack_trace, source, restored )
                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_hint, source, restored )
            }

            /*
             * The stack trace reaches the full document twice - in its own property and inside
             * exceptionFullDump - and getRedactedServerErrorAsJson(), the variant the default
             * HTTP server backend returns to a client, has to remove both: it replaces the dump
             * with "<redacted>" and blanks originalStackTrace and originalThreadName
             */

            UTF_REQUIRE(
                std::string::npos != parsed -> result() -> exceptionFullDump().find( stackTraceValue )
                );

            UTF_REQUIRE_EQUAL(
                parsed -> result() -> exceptionProperties() -> originalStackTrace(),
                stackTraceValue
                );

            UTF_REQUIRE(
                std::string::npos ==
                    dm::ServerErrorHelpers::getRedactedServerErrorAsJson( javaEptr ).find( stackTraceValue )
                );
        }
    }

    {
        try
        {
            cpp::safeRethrowException( securityEptr );

            UTF_FAIL( "The source exception must be thrown" );
        }
        catch( SecurityException& source )
        {
            const auto json = dm::ServerErrorHelpers::getServerErrorAsJson( securityEptr );

            const auto parsed = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( json );

            UTF_REQUIRE_EQUAL( parsed -> result() -> exceptionType(), SecurityException::fullTypeNameStatic() );

            try
            {
                cpp::safeRethrowException(
                    dm::ServerErrorHelpers::createExceptionFromObject( parsed )
                    );

                UTF_FAIL( "The restored exception must be thrown" );
            }
            catch( BaseExceptionDefault& restored )
            {
                UTF_REQUIRE_EQUAL( restored.fullTypeName(), SecurityException::fullTypeNameStatic() );
                UTF_REQUIRE_EQUAL( restored.what(), "authz failure" );

                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_service_status, source, restored )
                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_service_status_category, source, restored )
                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_service_status_message, source, restored )
            }
        }
    }

    {
        try
        {
            cpp::safeRethrowException( timeoutEptr );

            UTF_FAIL( "The source exception must be thrown" );
        }
        catch( TimeoutException& source )
        {
            const auto json = dm::ServerErrorHelpers::getServerErrorAsJson( timeoutEptr );

            const auto parsed = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >( json );

            UTF_REQUIRE_EQUAL( parsed -> result() -> exceptionType(), TimeoutException::fullTypeNameStatic() );

            try
            {
                cpp::safeRethrowException(
                    dm::ServerErrorHelpers::createExceptionFromObject( parsed )
                    );

                UTF_FAIL( "The restored exception must be thrown" );
            }
            catch( BaseExceptionDefault& restored )
            {
                UTF_REQUIRE_EQUAL( restored.fullTypeName(), TimeoutException::fullTypeNameStatic() );
                UTF_REQUIRE_EQUAL( restored.what(), "timeout failure" );

                UTEST_REQUIRE_ERRINFO_ROUND_TRIPS( errinfo_error_uuid, source, restored )
            }
        }
    }

#undef UTEST_REQUIRE_ERRINFO_ROUND_TRIPS
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
     * The redaction list is a hand maintained enumeration of thirteen of the properties, with
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

    /*
     * The two errinfos below are not part of the shared decoration helper because only a JNI
     * rethrow attaches them, but they are the two most disclosing properties of the model - a
     * stack trace of the server's own code and the name of the thread which ran it - so the
     * redaction has to cover them
     */

    const auto eptr = utest::createDecoratedException(
        HttpServerException()
            << eh::errinfo_original_stack_trace( "at Foo.bar(Foo.java:42)" )
            << eh::errinfo_original_thread_name( "original_thread_name: ServerErrorHelpersRedactionTests" ),
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
    UTEST_REQUIRE_REDACTED_AWAY( originalStackTrace )
    UTEST_REQUIRE_REDACTED_AWAY( originalThreadName )

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
     * The raw exception text of an error which is not user friendly is redacted too - BL_MSG()
     * text in this codebase routinely contains file paths - and it is replaced rather than
     * emptied, because exceptionMessage is a required property of the model
     *
     * The friendly message the model computes is what takes its place, so a client still gets a
     * diagnostic; the user-friendly positive control at the end of this case is what keeps the
     * redaction from swallowing the text of an error which was written for the caller
     */

    UTF_REQUIRE( full -> result() -> exceptionMessage() != redacted -> result() -> exceptionMessage() );

    UTF_REQUIRE_EQUAL(
        redacted -> result() -> exceptionMessage(),
        redacted -> result() -> message()
        );

    UTF_REQUIRE( ! fullProperties -> message().empty() );
    UTF_REQUIRE( redactedProperties -> message().empty() );

    /*
     * Preserved - the over-redaction guard
     */

#define UTEST_REQUIRE_PRESERVED( property ) \
        { \
            UTF_REQUIRE_EQUAL( fullProperties -> property(), redactedProperties -> property() ); \
        } \

    UTF_REQUIRE_EQUAL( full -> result() -> message(), redacted -> result() -> message() );
    UTF_REQUIRE_EQUAL( full -> result() -> exceptionType(), redacted -> result() -> exceptionType() );

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

    /*
     * The user-friendly positive control for the message redaction above: an error which was
     * raised to be shown to the caller keeps its text on both sides, and the assertions above
     * would pass vacuously against an implementation which simply blanked every message
     */

    {
        const auto friendlyMessage = std::string( "message: please retry with a smaller payload" );

        const auto friendlyEptr = BL_MAKE_EXCEPTION_PTR(
            UserMessageException() << eh::errinfo_message( friendlyMessage ),
            friendlyMessage
            );

        const auto friendlyRedacted = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorJson >(
            dm::ServerErrorHelpers::getRedactedServerErrorAsJson( friendlyEptr )
            );

        UTF_REQUIRE( friendlyRedacted -> result() );
        UTF_REQUIRE( friendlyRedacted -> result() -> exceptionProperties() );

        UTF_REQUIRE_EQUAL( friendlyRedacted -> result() -> exceptionMessage(), friendlyMessage );
        UTF_REQUIRE_EQUAL( friendlyRedacted -> result() -> message(), friendlyMessage );

        UTF_REQUIRE_EQUAL(
            friendlyRedacted -> result() -> exceptionProperties() -> message(),
            friendlyMessage
            );
    }
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

UTF_AUTO_TEST_CASE( ErrorToGraphQLTests )
{
    using namespace bl;

    /*
     * GraphQLErrorHelpers::getServerErrorAsGraphQL() is the ONLY renderer of error responses
     * for GraphQL clients and nothing in the repository exercises it - the three
     * BaseRestServerProcessingContext construction sites all pass isGraphQLServer == false
     *
     * The envelope carries exactly one GraphQLErrorMessage; errorType is the exception type and
     * message is BrokerErrorCodes::tryGetExpectedErrorMessage() when that returns a non-empty
     * string and the server's own message otherwise, with " (error code N)" appended whenever
     * the code is not success
     *
     * Note that createServerErrorResultObject() sets message to e.what() ONLY when the
     * exception is user friendly, and to BL_GENERIC_FRIENDLY_UNEXPECTED_MSG otherwise - so
     * every message assertion below is written for a known polarity
     */

    const auto renderAsGraphQL = []( SAA_in const std::exception_ptr& eptr )
        -> om::ObjPtr< dm::ServerErrorGraphQL >
    {
        const auto json = messaging::GraphQLErrorHelpers::getServerErrorAsGraphQL( eptr );

        auto graphQL = dm::DataModelUtils::loadFromJsonText< dm::ServerErrorGraphQL >( json );

        UTF_REQUIRE( graphQL );
        UTF_REQUIRE_EQUAL( 1U, graphQL -> errors().size() );
        UTF_REQUIRE( ! graphQL -> errors()[ 0 ] -> message().empty() );

        return graphQL;
    };

    {
        /*
         * 1. The friendly substitution plus the numeric suffix - and the server's own message
         *    must not reach the client
         */

        const auto eptr = BL_MAKE_EXCEPTION_PTR(
            ServerErrorException()
                << eh::errinfo_error_code(
                    eh::errc::make_error_code( messaging::BrokerErrorCodes::TargetPeerNotFound )
                    ),
            "some internal detail"
            );

        const auto graphQL = renderAsGraphQL( eptr );

        UTF_REQUIRE_EQUAL(
            graphQL -> errors()[ 0 ] -> message(),
            "The server is currently unavailable (error code 99)"
            );

        UTF_REQUIRE_EQUAL( graphQL -> errors()[ 0 ] -> errorType(), "bl::ServerErrorException" );

        UTF_REQUIRE(
            std::string::npos == graphQL -> errors()[ 0 ] -> message().find( "some internal detail" )
            );
    }

    {
        /*
         * 2. No error code at all - errorCode() defaults to 0, i.e. eh::errc::success, so there
         *    is nothing to substitute and no suffix is appended in EITHER polarity
         */

        {
            const auto eptr = BL_MAKE_EXCEPTION_PTR( UnexpectedException(), "plain failure" );

            const auto graphQL = renderAsGraphQL( eptr );

            const auto& message = graphQL -> errors()[ 0 ] -> message();

            UTF_REQUIRE_EQUAL( message, std::string( BL_GENERIC_FRIENDLY_UNEXPECTED_MSG ) );
            UTF_REQUIRE( std::string::npos == message.find( "error code" ) );
        }

        {
            const auto eptr = BL_MAKE_EXCEPTION_PTR(
                UnexpectedException() << eh::errinfo_is_user_friendly( true ),
                "plain failure"
                );

            const auto graphQL = renderAsGraphQL( eptr );

            const auto& message = graphQL -> errors()[ 0 ] -> message();

            UTF_REQUIRE_EQUAL( message, "plain failure" );
            UTF_REQUIRE( std::string::npos == message.find( "error code" ) );
        }
    }

    {
        /*
         * 3. An error code which has no friendly text - the server's message survives (subject
         *    to the polarity) and the suffix IS appended
         *
         *    The two halves are asserted separately so the case is not tied to a platform errno
         *    value - no_space_on_device is 28 on Linux
         */

        {
            const auto eptr = BL_MAKE_EXCEPTION_PTR(
                ServerErrorException()
                    << eh::errinfo_error_code(
                        eh::errc::make_error_code( eh::errc::no_space_on_device )
                        ),
                "disk failure"
                );

            const auto graphQL = renderAsGraphQL( eptr );

            const auto& message = graphQL -> errors()[ 0 ] -> message();

            UTF_REQUIRE_EQUAL( 0U, message.find( BL_GENERIC_FRIENDLY_UNEXPECTED_MSG ) );
            UTF_REQUIRE( std::string::npos != message.find( "(error code " ) );
        }

        {
            const auto eptr = BL_MAKE_EXCEPTION_PTR(
                ServerErrorException()
                    << eh::errinfo_error_code(
                        eh::errc::make_error_code( eh::errc::no_space_on_device )
                        )
                    << eh::errinfo_is_user_friendly( true ),
                "disk failure"
                );

            const auto graphQL = renderAsGraphQL( eptr );

            const auto& message = graphQL -> errors()[ 0 ] -> message();

            UTF_REQUIRE_EQUAL( 0U, message.find( "disk failure" ) );
            UTF_REQUIRE( std::string::npos != message.find( "(error code " ) );
        }
    }

    {
        /*
         * 4. The optional callback is forwarded all the way down to
         *    createServerErrorResultObject()
         */

        bool invoked = false;

        const eh::void_exception_callback_t cb = [ &invoked ]( SAA_inout std::exception& e ) -> void
        {
            BL_UNUSED( e );

            invoked = true;
        };

        const auto eptr = BL_MAKE_EXCEPTION_PTR( UnexpectedException(), "callback probe" );

        const auto json = messaging::GraphQLErrorHelpers::getServerErrorAsGraphQL( eptr, cb );

        UTF_REQUIRE( ! json.empty() );
        UTF_REQUIRE( invoked );
    }

    {
        /*
         * 5. The fully populated exception this file already builds for the JSON envelope - the
         *    error code is permission_denied, i.e. BrokerErrorCodes::AuthorizationFailed, whose
         *    friendly text is the error code's own message()
         *
         *    Asserted as halves rather than as the literal "Permission denied (error code 13)",
         *    which is a platform dependent string
         */

        const eh::error_code errorCode( eh::errc::permission_denied, eh::generic_category() );

        const auto eptr = utest::createDecoratedException(
            ServerErrorException(),
            "ErrorToGraphQLTests",
            "message: ErrorToGraphQLTests",
            errorCode
            );

        const auto graphQL = renderAsGraphQL( eptr );

        UTF_REQUIRE_EQUAL( graphQL -> errors()[ 0 ] -> errorType(), "bl::ServerErrorException" );

        const auto& message = graphQL -> errors()[ 0 ] -> message();

        UTF_REQUIRE_EQUAL( 0U, message.find( errorCode.message() ) );
        UTF_REQUIRE( std::string::npos != message.find( "(error code " ) );
    }
}
