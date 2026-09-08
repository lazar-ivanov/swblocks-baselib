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

#include <utests/baselib/HttpServerHelpers.h>

namespace
{
    /**
     * @brief A one-shot canned-response raw TCP server for the HTTP client tests
     *
     * The real bl::httpserver::HttpServer is correct by construction - it derives
     * Content-Length from the body it holds, validates every custom header and always
     * emits a well formed status line - so it cannot produce the inputs the client's
     * defensive guards exist for. This responder serves a byte exact response instead,
     * on an ephemeral loopback port so no test needs a fixed port or a global lock
     */

    class RawHttpResponder
    {
        BL_NO_COPY_OR_MOVE( RawHttpResponder )

    private:

        bl::asio::io_service                    m_ioService;
        bl::asio::ip::tcp::acceptor             m_acceptor;
        const unsigned short                    m_port;
        const std::string                       m_response;
        const bl::time::time_duration           m_delayBeforeResponse;
        const std::size_t                       m_chunkSize;
        const bl::time::time_duration           m_delayBetweenChunks;
        std::atomic< bool >                     m_stopRequested;
        mutable bl::os::mutex                   m_lock;
        std::string                             m_request;
        bl::cpp::SafeUniquePtr< bl::os::thread > m_thread;

    public:

        explicit RawHttpResponder(
            SAA_in          std::string&&                       response,
            SAA_in_opt      const bl::time::time_duration&      delayBeforeResponse = bl::time::milliseconds( 0 ),
            SAA_in_opt      const std::size_t                   chunkSize = 0U /* 0 = write the response in one go */,
            SAA_in_opt      const bl::time::time_duration&      delayBetweenChunks = bl::time::milliseconds( 0 )
            )
            :
            m_acceptor(
                m_ioService,
                bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral port */ )
                ),
            m_port( m_acceptor.local_endpoint().port() ),
            m_response( BL_PARAM_FWD( response ) ),
            m_delayBeforeResponse( delayBeforeResponse ),
            m_chunkSize( chunkSize ),
            m_delayBetweenChunks( delayBetweenChunks ),
            m_stopRequested( false )
        {
            /*
             * The acceptor constructor above has already bound and started listening, so a
             * client which connects before the worker reaches accept() lands in the backlog
             */

            m_thread.reset( new bl::os::thread( bl::cpp::bind( &RawHttpResponder::run, this ) ) );
        }

        ~RawHttpResponder() NOEXCEPT
        {
            BL_NOEXCEPT_BEGIN()

            m_stopRequested = true;

            /*
             * Closing the acceptor does not reliably wake a worker which is already blocked
             * in accept(), so unblock it with one throwaway loopback connection while the
             * acceptor is still open and only then join - the acceptor is closed afterwards
             * by its own destructor. Without this a case which never connects would hang
             */

            {
                bl::eh::error_code ec;

                bl::asio::io_service ioService;
                bl::asio::ip::tcp::socket socket( ioService );

                socket.connect(
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                    ec
                    );

                socket.close( ec );
            }

            bl::os::safeThreadJoin( *m_thread );

            BL_NOEXCEPT_END()
        }

        unsigned short port() const NOEXCEPT
        {
            return m_port;
        }

        std::string lastRequest() const
        {
            BL_MUTEX_GUARD( m_lock );

            return m_request;
        }

        /**
         * @brief Assembles a raw response out of a status line, a list of headers and a body,
         * so the cases below read as data rather than as string concatenation
         */

        static std::string makeResponse(
            SAA_in          const std::string&                  statusLine,
            SAA_in          const std::vector< std::string >&   headers,
            SAA_in          const std::string&                  body
            )
        {
            bl::cpp::SafeOutputStringStream oss;

            oss << statusLine << "\r\n";

            for( const auto& header : headers )
            {
                oss << header << "\r\n";
            }

            oss << "\r\n" << body;

            return oss.str();
        }

    private:

        /**
         * @brief Sleeps in small slices and returns false if a shutdown was requested meanwhile
         *
         * The destructor joins the worker, so a long uninterruptible sleep here would make
         * every case which configures a delay pay for it in full
         */

        bool sleepUnlessStopping( SAA_in const bl::time::time_duration& duration )
        {
            if( duration.total_milliseconds() > 0 )
            {
                bl::os::interruptibleSleep(
                    bl::cpp::copy( duration ),
                    bl::time::milliseconds( 250 ),
                    [ this ]() -> bool
                    {
                        return m_stopRequested;
                    }
                    );
            }

            return ! m_stopRequested;
        }

        void run()
        {
            BL_NOEXCEPT_BEGIN()

            bl::eh::error_code ec;

            bl::asio::ip::tcp::socket socket( m_ioService );

            m_acceptor.accept( socket, ec );

            if( ec )
            {
                return;
            }

            {
                bl::asio::streambuf buffer( 64U * 1024U );

                bl::asio::read_until( socket, buffer, "\r\n\r\n", ec );

                if( buffer.size() > 0 )
                {
                    /*
                     * The capture idiom and the non-empty guard are the production ones from
                     * SimpleHttpTask.h - inserting an empty streambuf would set failbit, which
                     * cpp::SafeOutputStringStream turns into an exception
                     */

                    bl::cpp::SafeOutputStringStream oss;

                    oss << &buffer;

                    BL_MUTEX_GUARD( m_lock );

                    m_request = oss.str();
                }
            }

            if( ec || ! sleepUnlessStopping( m_delayBeforeResponse ) )
            {
                return;
            }

            if( 0U == m_chunkSize )
            {
                bl::asio::write( socket, bl::asio::buffer( m_response ), ec );
            }
            else
            {
                for( std::size_t offset = 0U; offset < m_response.size(); offset += m_chunkSize )
                {
                    if( 0U != offset && ! sleepUnlessStopping( m_delayBetweenChunks ) )
                    {
                        break;
                    }

                    bl::asio::write(
                        socket,
                        bl::asio::buffer(
                            m_response.c_str() + offset,
                            std::min( m_chunkSize, m_response.size() - offset )
                            ),
                        ec
                        );

                    if( ec )
                    {
                        break;
                    }
                }
            }

            socket.shutdown( bl::asio::ip::tcp::socket::shutdown_both, ec );
            socket.close( ec );

            BL_NOEXCEPT_END()
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( Client_SimpleHttpTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::transfer;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "\n******************************** Starting test: Client_SimpleHttpTests ********************************\n"
                );

            scheduleAndExecuteInParallel(
                []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    /*
                     * Success test case
                     */

                    {
                        http::HeadersMap headers;

                        headers[ "MyHeader" ] = "MyValue";

                        const auto stask = SimpleHttpGetTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() ),
                            cpp::copy( test::UtfArgsParser::port() ),
                            utest::http::g_requestUri,
                            std::move( headers )
                            );

                        const auto task = om::qi< Task >( stask );
                        UTF_REQUIRE_EQUAL( Task::Created, task -> getState() );

                        eq -> push_back( task );
                        const auto executedTask = eq -> pop( true );

                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* HTTP task executed ******* \n" );

                        UTF_REQUIRE( executedTask );
                        UTF_REQUIRE( om::areEqual( task, executedTask ) );
                        UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                        UTF_REQUIRE( eq -> isEmpty() );

                        if( stask -> isFailed() )
                        {
                            UTF_REQUIRE( nullptr != stask -> exception() );
                            cpp::safeRethrowException( stask -> exception() );
                        }

                        // Check the response code is 200, and content was received
                        UTF_REQUIRE( nullptr == stask -> exception() );
                        UTF_REQUIRE_EQUAL( 200U, stask -> getHttpStatus() );

                        const auto contentType = stask -> tryGetResponseHeader( http::Parameters::HttpHeader::g_contentType );

                        UTF_REQUIRE( contentType );
                        UTF_REQUIRE( str::istarts_with( *contentType, "application/json;" ) );

                        const auto userAgentValue = stask -> tryGetResponseHeader( "request-user-agent-id" );
                        UTF_REQUIRE( userAgentValue == nullptr );

                        const auto& response = stask -> getResponse();

                        UTF_REQUIRE( response.size() );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* begin HTTP response ******* \n" );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << response );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* end HTTP response ******* \n" );
                    }

                    /*
                     * Success test cases with user agent string provided. We have 4 cases:
                     *
                     * 1. User agent string is provided locally
                     * 2. User agent string is provided globally
                     * 3. User agent string is not provided at all
                     * 4. User agent string is provided locally and globally (local overrides global)
                     */

                    const auto fnUserAgentTest = [ & ](
                        SAA_in      const bool          useGlobal,
                        SAA_in      const bool          useLocal
                        )
                        -> void
                    {
                        if( useGlobal )
                        {
                            UTF_REQUIRE( str::icontains( http::HttpHeader::g_userAgentBotDefault, "bot" ) );

                            UTF_REQUIRE_EQUAL( http::Parameters::userAgentDefault(), str::empty() );
                            http::Parameters::userAgentDefault( cpp::copy( http::HttpHeader::g_userAgentBotDefault ) );
                        }

                        {
                            BL_SCOPE_EXIT(
                                {
                                    if( useGlobal )
                                    {
                                        http::Parameters::userAgentDefault( std::string() );
                                    }
                                }
                                );

                            http::HeadersMap headers;

                            headers[ "MyHeader" ] = "MyValue";

                            const std::string userAgentLocalValue = "swblocks-baselib-client-bot/MyUserAgent";

                            const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                                cpp::copy( test::UtfArgsParser::host() ),
                                cpp::copy( test::UtfArgsParser::port() ),
                                utest::http::g_requestUri,
                                std::move( headers )
                                );

                            if( useLocal )
                            {
                                taskImpl -> userAgent( cpp::copy( userAgentLocalValue ) );
                            }

                            const auto task = om::qi< Task >( taskImpl );
                            UTF_REQUIRE_EQUAL( Task::Created, task -> getState() );

                            eq -> push_back( task );
                            const auto executedTask = eq -> pop( true );

                            BL_LOG_MULTILINE(
                                Logging::debug(),
                                BL_MSG()
                                    << "\n******* HTTP task with user agent value executed ******* \n"
                                    );

                            UTF_REQUIRE( executedTask );
                            UTF_REQUIRE( om::areEqual( task, executedTask ) );
                            UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                            UTF_REQUIRE( eq -> isEmpty() );

                            if( taskImpl -> isFailed() )
                            {
                                UTF_REQUIRE( nullptr != taskImpl -> exception() );
                                cpp::safeRethrowException( taskImpl -> exception() );
                            }

                            UTF_REQUIRE( nullptr == taskImpl -> exception() );
                            UTF_REQUIRE_EQUAL( http::Parameters::HTTP_SUCCESS_OK, taskImpl -> getHttpStatus() );

                            const auto userAgentValue = taskImpl -> tryGetResponseHeader( "request-user-agent-id" );

                            if( useLocal )
                            {
                                UTF_REQUIRE( userAgentValue );
                                UTF_REQUIRE_EQUAL( userAgentLocalValue, *userAgentValue );
                            }
                            else if( useGlobal )
                            {
                                UTF_REQUIRE( userAgentValue );
                                UTF_REQUIRE_EQUAL( http::HttpHeader::g_userAgentBotDefault, *userAgentValue );
                            }
                            else
                            {
                                UTF_REQUIRE( ! userAgentValue );
                            }

                            if( userAgentValue )
                            {
                                BL_LOG_MULTILINE(
                                    Logging::debug(),
                                    BL_MSG()
                                        << "\n******* user agent value: "
                                        << *userAgentValue
                                        );
                            }
                        }
                    };

                    fnUserAgentTest( false /* useGlobal */, false /* useLocal */ );
                    fnUserAgentTest( true /* useGlobal */, false /* useLocal */ );
                    fnUserAgentTest( false /* useGlobal */, true /* useLocal */ );
                    fnUserAgentTest( true /* useGlobal */, true /* useLocal */ );

                    /*
                     * Failure test case
                     */

                    {
                        const auto stask = SimpleHttpGetTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() ),
                            cpp::copy( test::UtfArgsParser::port() ),
                            "/invalid_page"
                            );

                        const auto task = om::qi< Task >( stask );
                        UTF_REQUIRE_EQUAL( Task::Created, task -> getState() );

                        eq -> push_back( task );
                        const auto executedTask = eq -> pop( true );

                        UTF_REQUIRE( executedTask );
                        UTF_REQUIRE( om::areEqual( task, executedTask ) );
                        UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                        UTF_REQUIRE( eq -> isEmpty() );

                        // Check the response code is not 200 the request has failed
                        UTF_REQUIRE( stask -> isFailed() );
                        UTF_REQUIRE( ! stask -> isTimedOut() );
                        UTF_REQUIRE( nullptr != stask -> exception() );
                        UTF_REQUIRE( 200 != stask -> getHttpStatus() );

                        const auto contentType = stask -> tryGetResponseHeader( http::Parameters::HttpHeader::g_contentType );

                        UTF_REQUIRE( contentType );
                        UTF_REQUIRE( str::istarts_with( *contentType, bl::http::HttpHeader::g_contentTypeDefault ) );

                        UTF_REQUIRE( ! stask -> getRemoteEndpointId().empty() );

                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* begin HTTP task exception ******* \n" );
                        try
                        {
                            cpp::safeRethrowException( stask -> exception() );
                        }
                        catch( std::exception& e )
                        {
                            BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << eh::diagnostic_information( e ) );
                        }
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* end HTTP task exception ******* \n" );
                    }

                    /*
                     * Redirect test case
                     */

                    {
                        const auto stask = SimpleHttpGetTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() ),
                            cpp::copy( test::UtfArgsParser::port() ),
                            utest::http::g_redirectedRequestUri
                            );

                        const auto task = om::qi< Task >( stask );

                        eq -> push_back( task );

                        const std::string* redirectUrl = nullptr;

                        try
                        {
                            eq -> waitForSuccess( task );
                        }
                        catch ( HttpException& e )
                        {
                            redirectUrl = e.httpRedirectUrl();
                        }

                        if( test::UtfArgsParser::host() != "localhost" )
                        {
                            UTF_REQUIRE(
                                stask -> getHttpStatus() >= http::Parameters::HTTP_REDIRECT_START_RANGE &&
                                stask -> getHttpStatus() <= http::Parameters::HTTP_REDIRECT_END_RANGE
                                );

                            UTF_REQUIRE( nullptr != redirectUrl );
                        }
                    }
                });
        }
        );
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpTruncatedResponseTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpTruncatedResponseTests ********************************\n"
        );

    /*
     * Each sub-block below delivers exactly these ten body bytes and then closes the
     * connection - what varies is only the length the server announces for them.
     *
     * The tasks are given an explicit timeout because the default for GET is 30 minutes;
     * a loopback exchange which does not complete within a minute means a broken fixture
     * and should fail the test rather than hang it
     */

    const std::string body( "0123456789" );

    /*
     * (1) A body which stops short of the announced Content-Length must be rejected
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 100" },
                body
                )
            );

        UTF_REQUIRE( 0U != responder.port() );

        scheduleAndExecuteInParallel(
            [ &responder ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                    std::string( "127.0.0.1" ),
                    responder.port(),
                    "/probe"
                    );

                taskImpl -> setTimeout( time::seconds( 60 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "The HTTP response was truncated"
                    );

                UTF_REQUIRE( taskImpl -> isFailed() );

                /*
                 * The status line was parsed before the body was found to be short, and the
                 * short body is not handed to the caller as if it were the whole response
                 */

                UTF_REQUIRE_EQUAL( 200U, taskImpl -> getHttpStatus() );
                UTF_REQUIRE( taskImpl -> getResponse().empty() );

                /*
                 * waitForSuccess() unlinks the task it waited on, so the flush at the end of
                 * scheduleAndExecuteInParallel does not rethrow what was just asserted here
                 */

                UTF_REQUIRE( eq -> isEmpty() );
            });

        /*
         * The only direct check anywhere that initRequest() emits the request line it claims
         */

        UTF_REQUIRE( 0U == responder.lastRequest().find( "GET /probe HTTP/1.0\r\n" ) );
    }

    /*
     * (2) A body which matches the announced Content-Length exactly is accepted
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 10" },
                body
                )
            );

        scheduleAndExecuteInParallel(
            [ &responder ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                    std::string( "127.0.0.1" ),
                    responder.port(),
                    "/probe"
                    );

                taskImpl -> setTimeout( time::seconds( 60 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                UTF_REQUIRE_EQUAL( taskImpl -> getResponse().size(), 10U );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }

    /*
     * (3) When nothing is announced the m_responseLength sentinel keeps the check off by
     *     design - a length-less HTTP/1.0 response which simply ends at EOF is complete
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain" },
                body
                )
            );

        scheduleAndExecuteInParallel(
            [ &responder ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                    std::string( "127.0.0.1" ),
                    responder.port(),
                    "/probe"
                    );

                taskImpl -> setTimeout( time::seconds( 60 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                UTF_REQUIRE_EQUAL( taskImpl -> getResponse().size(), 10U );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }

    /*
     * (4) The guard is an inequality, so a body which overruns the announced
     *     Content-Length is rejected just the same
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 5" },
                body
                )
            );

        scheduleAndExecuteInParallel(
            [ &responder ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                    std::string( "127.0.0.1" ),
                    responder.port(),
                    "/probe"
                    );

                taskImpl -> setTimeout( time::seconds( 60 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "The HTTP response was truncated"
                    );

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE( taskImpl -> getResponse().empty() );

                /*
                 * The message must name both the ten bytes which arrived and the five which
                 * were announced, i.e. the counts were compared and not merely bounded
                 */

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "An over-long HTTP response must be rejected" );
                }
                catch( bl::UnexpectedException& e )
                {
                    const std::string message( e.what() );

                    UTF_REQUIRE( cpp::contains( message, "10" ) );
                    UTF_REQUIRE( cpp::contains( message, "5" ) );
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpTimeoutTests )
{
    /*
     * Manual HTTP timeout test case. Works on Linux only, unless you use a
     * netcat or similar port for Windows. Follow these steps in order to run it:
     * - Start local netcat instance and pass to it few headers required to be dumped as part of the exception:
     *     echo -e "HTTP/1.0 500 ERROR\r\nrequestId: 123\r\nVersion: 1.0\r\nline1\r\n\line2\r\n\r\n" | nc -l 4545 -q 10
     * - Pass --is-client argument to the utest binary:
     *     ./utf-baselib-http --log_level=message --run_test=Client_SimpleHttpTimeoutTests --is-client
     * - After the test finished netcat instance will exit. Run another one to repeat the test
     */

    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::transfer;

    if( ! test::UtfArgsParser::isClient() )
    {
        return;
    }

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "\n******************************** Starting test: Client_SimpleHttpTimeoutTests ********************************\n"
                );

            scheduleAndExecuteInParallel(
                []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    const auto stask = SimpleHttpGetTaskImpl::createInstance(
                        "localhost",
                        4545,
                        "/invalid_page"
                        );

                    stask -> setTimeout( time::seconds( 5 ) );

                    const auto task = om::qi< Task >( stask );
                    UTF_REQUIRE_EQUAL( Task::Created, task -> getState() );

                    eq -> push_back( task );
                    const auto executedTask = eq -> pop( true );

                    UTF_REQUIRE( executedTask );
                    UTF_REQUIRE( om::areEqual( task, executedTask ) );
                    UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                    UTF_REQUIRE( eq -> isEmpty() );

                    // Check the response code is not 200 the request has failed
                    UTF_REQUIRE( stask -> isFailed() );
                    UTF_REQUIRE( stask -> isTimedOut() );
                    UTF_REQUIRE( nullptr != stask -> exception() );
                    UTF_REQUIRE( 200 != stask -> getHttpStatus() );

                    UTF_REQUIRE( ! stask -> getRemoteEndpointId().empty() );

                    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* begin HTTP task timeout exception ******* \n" );
                    try
                    {
                        cpp::safeRethrowException( stask -> exception() );
                    }
                    catch( std::exception& e )
                    {
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << eh::diagnostic_information( e ) );

                        const auto* headers = bl::eh::get_error_info< bl::eh::errinfo_http_response_headers >( e );
                        UTF_REQUIRE( headers );
                        UTF_REQUIRE( ! headers -> empty() );
                    }
                    BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* end HTTP task timeout exception ******* \n" );
                });
        }
        );
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpPerfTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::transfer;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback(
        []() -> void
        {
            const std::size_t count = 50;

            const auto t1 = bl::time::microsec_clock::universal_time();

            {
                BL_LOG_MULTILINE(
                    Logging::debug(),
                    BL_MSG()
                        << "\n******************************** Starting test: Client_SimpleHttpPerfTests ********************************\n"
                    );

                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        eq -> setOptions( ExecutionQueue::OptionKeepFailed );

                        /*
                         * Simple perf test
                         */
                        for( std::size_t i = 0; i < count; ++i )
                        {
                            const auto stask = SimpleHttpGetTaskImpl::createInstance(
                                cpp::copy( test::UtfArgsParser::host() ),
                                cpp::copy( test::UtfArgsParser::port() ),
                                utest::http::g_requestPerfUri
                                );

                            eq -> push_back( om::qi< Task >( stask ) );
                        }

                        executeQueueAndCancelOnFailure( eq );
                    });
            }

            const auto duration = bl::time::microsec_clock::universal_time() - t1;

            const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Executing "
                    << count
                    << " HTTP requests took "
                    << durationInSeconds
                    << " seconds"
                );
        }
        );
}

UTF_AUTO_TEST_CASE( Client_SimpleSecureHttpSslGetTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::transfer;

    utest::http::HttpServerHelpers::startHttpServerAndExecuteCallback< bl::httpserver::HttpSslServer >(
        []() -> void
        {
            BL_LOG_MULTILINE(
                Logging::debug(),
                BL_MSG()
                    << "\n******************************** Starting test: Client_SimpleSecureHttpSslGetTests ********************************\n"
                );

            scheduleAndExecuteInParallel(
                []( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                {
                    eq -> setOptions( ExecutionQueue::OptionKeepAll );

                    {
                        http::HeadersMap headers;

                        headers[ "MyHeader" ] = "MyValue";

                        bl::str::SecureStringWrapper content( "Hidden content" );

                        const auto stask = SimpleSecureHttpSslGetTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() ),
                            cpp::copy( test::UtfArgsParser::port() ),
                            utest::http::g_requestUri,
                            content,
                            std::move( headers )
                            );

                        UTF_REQUIRE_EQUAL( stask -> isSecureMode(), true );

                        const auto task = om::qi< Task >( stask );
                        UTF_REQUIRE_EQUAL( Task::Created, task -> getState() );

                        eq -> push_back( task );
                        const auto executedTask = eq -> pop( true );

                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* HTTP task executed ******* \n" );

                        UTF_REQUIRE( executedTask );
                        UTF_REQUIRE( om::areEqual( task, executedTask ) );
                        UTF_REQUIRE_EQUAL( Task::Completed, task -> getState() );
                        UTF_REQUIRE( eq -> isEmpty() );

                        if( stask -> isFailed() )
                        {
                            UTF_REQUIRE( nullptr != stask -> exception() );
                            cpp::safeRethrowException( stask -> exception() );
                        }

                        // Check the response code is 200, and content was received
                        UTF_REQUIRE( nullptr == stask -> exception() );
                        UTF_REQUIRE_EQUAL( 200U, stask -> getHttpStatus() );

                        const auto contentType = stask -> tryGetResponseHeader( http::Parameters::HttpHeader::g_contentType );

                        UTF_REQUIRE( contentType );
                        UTF_REQUIRE( str::istarts_with( *contentType, "application/json;" ) );

                        const auto& response = stask -> getResponse();

                        UTF_REQUIRE( response.size() );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* begin HTTP response ******* \n" );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << response );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* end HTTP response ******* \n" );
                    }

                });
        }
        );
}

