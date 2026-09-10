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

    /**
     * @brief A backend which echoes the request body back as the response body
     *
     * The shared utest::http::TestHttpServerProcessingTask never looks at the body it was
     * sent, so a request whose body never left the client is indistinguishable from one
     * which arrived intact. This backend is deliberately file local - the shared one is
     * included by three test modules whose cases assert against its exact routing table
     */

    template
    <
        typename BACKENDSTATE
    >
    class EchoBodyProcessingTask :
        public bl::httpserver::HttpServerProcessingTaskDefault< BACKENDSTATE >
    {
        BL_DECLARE_OBJECT_IMPL( EchoBodyProcessingTask )

    protected:

        typedef bl::httpserver::HttpServerProcessingTaskDefault< BACKENDSTATE >     base_type;
        typedef bl::http::Parameters::HttpStatusCode                                HttpStatusCode;

        using base_type::m_statusCode;
        using base_type::m_request;
        using base_type::m_response;
        using base_type::m_responseHeaders;

        EchoBodyProcessingTask(
            SAA_in          bl::om::ObjPtr< bl::httpserver::Request >&&              request,
            SAA_in_opt      bl::om::ObjPtr< BACKENDSTATE >&&                         backendState = nullptr
            )
            :
            base_type( BL_PARAM_FWD( request ), BL_PARAM_FWD( backendState ) )
        {
        }

        virtual void requestProcessing() OVERRIDE
        {
            m_response = m_request -> body();
            m_statusCode = HttpStatusCode::HTTP_SUCCESS_OK;

            m_responseHeaders.clear();
        }
    };

    typedef bl::om::ObjectImpl
    <
        bl::httpserver::ServerBackendProcessingImplDefault
        <
            utest::http::DummyBackendStateImpl,
            EchoBodyProcessingTask
        >
    >
    EchoBackendImpl;

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

                        UTF_REQUIRE_THROW( eq -> waitForSuccess( task ), bl::HttpException );

                        UTF_REQUIRE( stask -> isFailed() );
                        UTF_REQUIRE( nullptr != stask -> exception() );

                        UTF_REQUIRE_EQUAL(
                            stask -> getHttpStatus(),
                            http::Parameters::HTTP_REDIRECT_PERMANENTLY
                            );

                        try
                        {
                            cpp::safeRethrowException( stask -> exception() );

                            UTF_FAIL( "A redirect must be reported as an HttpException" );
                        }
                        catch( HttpException& e )
                        {
                            UTF_REQUIRE( nullptr != e.httpStatusCode() );
                            UTF_REQUIRE_EQUAL( *e.httpStatusCode(), 301 );

                            /*
                             * Pinning the absence is what makes the href sub-block below
                             * meaningful - g_redirectedResult carries no href at all
                             */

                            UTF_REQUIRE( nullptr == e.httpRedirectUrl() );

                            UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_is_expected >( e ) );
                        }

                        UTF_REQUIRE( eq -> isEmpty() );
                    }

                    {
                        /*
                         * The redirect URL is extracted from the response body with a case
                         * insensitive regex which accepts either quoting style, so an upper
                         * case HREF in single quotes exercises both halves of it. There is no
                         * coverage of that regex anywhere else
                         */

                        const std::string body =
                            "<html><body>Go <a HREF='https://example.invalid/next'>here</a></body></html>";

                        RawHttpResponder responder(
                            RawHttpResponder::makeResponse(
                                "HTTP/1.0 302 Moved Temporarily",
                                {
                                    "Content-Type: text/html",
                                    "Content-Length: " + utils::lexical_cast< std::string >( body.size() )
                                },
                                body
                                )
                            );

                        const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                            std::string( "127.0.0.1" ),
                            responder.port(),
                            "/probe"
                            );

                        taskImpl -> setTimeout( time::seconds( 60 ) );

                        const auto task = om::qi< Task >( taskImpl );

                        eq -> push_back( task );

                        UTF_REQUIRE_THROW( eq -> waitForSuccess( task ), bl::HttpException );

                        UTF_REQUIRE_EQUAL( 302U, taskImpl -> getHttpStatus() );

                        try
                        {
                            cpp::safeRethrowException( taskImpl -> exception() );

                            UTF_FAIL( "A redirect must be reported as an HttpException" );
                        }
                        catch( HttpException& e )
                        {
                            UTF_REQUIRE( nullptr != e.httpRedirectUrl() );
                            UTF_REQUIRE_EQUAL( *e.httpRedirectUrl(), "https://example.invalid/next" );
                        }

                        UTF_REQUIRE( eq -> isEmpty() );
                    }

                    {
                        /*
                         * Anything other than exactly 200 makes the client fail the task, so
                         * "201 Created is a failure for this client" is a genuinely surprising
                         * contract which every REST caller hits - widening the comparison to a
                         * 2xx range check is a plausible "fix" which would silently change it
                         *
                         * addExpectedHttpStatuses does not make the task succeed either; all
                         * it does is make isExpectedException() return true, which suppresses
                         * the diagnostic dump in chk2DumpException. That dump is itself gated
                         * on a non-empty task name and SimpleHttpTask never sets one, so on
                         * this task the setter has no observable effect at all - which is
                         * what the two identical outcomes below record
                         */

                        const auto fnRun201Case = [ &eq ]( SAA_in const bool addExpectedStatus ) -> void
                        {
                            RawHttpResponder responder(
                                RawHttpResponder::makeResponse(
                                    "HTTP/1.0 201 Created",
                                    { "Content-Type: text/plain", "Content-Length: 2" },
                                    "ok"
                                    )
                                );

                            const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                                std::string( "127.0.0.1" ),
                                responder.port(),
                                "/probe"
                                );

                            taskImpl -> setTimeout( time::seconds( 60 ) );

                            if( addExpectedStatus )
                            {
                                http::StatusesList statuses;

                                statuses.insert( 201U );

                                taskImpl -> addExpectedHttpStatuses( statuses );
                            }

                            const auto task = om::qi< Task >( taskImpl );

                            eq -> push_back( task );

                            UTF_REQUIRE_THROW( eq -> waitForSuccess( task ), bl::HttpException );

                            UTF_REQUIRE( taskImpl -> isFailed() );
                            UTF_REQUIRE_EQUAL( 201U, taskImpl -> getHttpStatus() );

                            /*
                             * decodeContent() runs before throwHttpException(), so the body is
                             * still handed to the caller on an HTTP error - the opposite of the
                             * truncation and the response size cap paths, where it stays empty
                             */

                            UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), "ok" );

                            UTF_REQUIRE( eq -> isEmpty() );
                        };

                        fnRun201Case( false /* addExpectedStatus */ );
                        fnRun201Case( true /* addExpectedStatus */ );
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

UTF_AUTO_TEST_CASE( Client_SimpleHttpResponseSizeLimitTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpResponseSizeLimitTests ********************************\n"
        );

    /*
     * The client talks to servers it does not control, so it bounds both halves of what it
     * will buffer: the status line plus the headers by the maximum_size of m_response, and
     * the body by m_maxResponseSize. Neither bound is observed anywhere else - getMaxResponseSize
     * and setMaxResponseSize have no caller at all in the repository
     */

    /*
     * (1) The default body cap is what it claims to be and the setter round trips - this
     *     needs no server at all
     */

    {
        const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
            std::string( "127.0.0.1" ),
            static_cast< unsigned short >( 1U ),
            "/probe"
            );

        UTF_REQUIRE_EQUAL( taskImpl -> getMaxResponseSize(), static_cast< std::size_t >( 1U ) << 26 );

        taskImpl -> setMaxResponseSize( 4096U );

        UTF_REQUIRE_EQUAL( taskImpl -> getMaxResponseSize(), static_cast< std::size_t >( 4096U ) );
    }

    /*
     * (2) A body which exceeds the cap is rejected before it is handed to the caller
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 4096" },
                std::string( 4096U, 'x' )
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
                taskImpl -> setMaxResponseSize( 1024U );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::UnexpectedException,
                    "The HTTP response body is larger than the maximum of 1024 bytes"
                    );

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE( taskImpl -> getResponse().empty() );

                /*
                 * The cap is enforced with BL_CHK_USER_FRIENDLY, so the message may be shown
                 * to an end user as it is; losing that flag would bury it behind the generic
                 * "unexpected error" text
                 */

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "An oversized HTTP response body must be rejected" );
                }
                catch( bl::UnexpectedException& e )
                {
                    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_is_user_friendly >( e ) );
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }

    /*
     * (3) A server which never terminates its headers cannot make the client buffer without
     *     limit - the bounded m_response makes async_read_until fail with not_found once the
     *     65536 byte bound is reached. The 70 KiB below is comfortably past it and is sent
     *     after the status line, which is parsed normally
     */

    {
        RawHttpResponder responder(
            std::string( "HTTP/1.0 200 OK\r\n" ) + std::string( 70U * 1024U, 'a' )
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

                ( void ) eq -> pop( true );

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE( nullptr != taskImpl -> exception() );

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "An unterminated HTTP header block must be rejected" );
                }
                catch( eh::system_error& e )
                {
                    UTF_REQUIRE( e.code() == bl::asio::error::not_found );
                }

                /*
                 * The status line was consumed before the header flood started
                 */

                UTF_REQUIRE_EQUAL( 200U, taskImpl -> getHttpStatus() );
                UTF_REQUIRE( taskImpl -> getResponse().empty() );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpContentLengthValidationTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpContentLengthValidationTests ********************************\n"
        );

    /*
     * std::stoul( "-1" ) returns SIZE_MAX by wraparound and std::stoul( "12abc" ) returns 12,
     * so a Content-Length which is not a plain unsigned number must be rejected explicitly
     * rather than cast. The two failure modes deliberately produce different exception types:
     * the sign / emptiness guard goes through createException<>() and is enhanced, while a
     * value the cast cannot consume throws bad_lexical_cast, which reaches the task through
     * the catch( std::exception& ) arm and is not
     */

    const auto fnRunCase = [](
        SAA_in      const std::string&      headerValue,
        SAA_in      const bool              isUnexpectedException
        )
        -> void
    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                {
                    "Content-Type: text/plain",
                    std::string( "Content-Length: " ) + headerValue
                },
                "abcdefghij"
                )
            );

        scheduleAndExecuteInParallel(
            [ &responder, &headerValue, isUnexpectedException ](
                SAA_in const om::ObjPtr< ExecutionQueue >& eq
                ) -> void
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

                if( isUnexpectedException )
                {
                    UTF_REQUIRE_THROW_MESSAGE(
                        eq -> waitForSuccess( task ),
                        bl::UnexpectedException,
                        "returned invalid response"
                        );
                }
                else
                {
                    ( void ) eq -> pop( true );

                    UTF_REQUIRE_THROW(
                        cpp::safeRethrowException( taskImpl -> exception() ),
                        utils::bad_lexical_cast
                        );
                }

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE( nullptr != taskImpl -> exception() );
                UTF_REQUIRE( taskImpl -> getResponse().empty() );

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "An invalid Content-Length header must be rejected" );
                }
                catch( std::exception& e )
                {
                    /*
                     * Both paths end up enhanced, but for different reasons: the guard throws
                     * through createException<>(), which calls chk2EnhanceException() itself,
                     * while boost::lexical_cast raises its bad_lexical_cast through
                     * boost::throw_exception() - so what is actually thrown is a
                     * wrapexcept< bad_lexical_cast >, which IS a boost::exception and is
                     * therefore picked up by the catch( bl::eh::exception& ) arm of
                     * BL_TASKS_HANDLER_END_IMPL rather than by its catch( std::exception& ) one
                     */

                    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_http_url >( e ) );

                    const std::string message( e.what() );

                    if( isUnexpectedException )
                    {
                        UTF_REQUIRE( cpp::contains( message, "returned invalid response" ) );

                        if( ! headerValue.empty() )
                        {
                            /*
                             * The offending value is echoed back, which is what makes a
                             * misbehaving server diagnosable from the client side
                             */

                            UTF_REQUIRE( cpp::contains( message, headerValue ) );
                        }
                    }
                    else
                    {
                        /*
                         * The guard did not fire here - the cast is what rejected the value
                         */

                        UTF_REQUIRE( ! cpp::contains( message, "returned invalid response" ) );
                    }
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });
    };

    /*
     * A negative value, an explicitly signed value and an empty value are caught by the guard;
     * trailing garbage and a value past std::size_t are caught by the cast
     */

    fnRunCase( "-1", true /* isUnexpectedException */ );
    fnRunCase( "+10", true /* isUnexpectedException */ );
    fnRunCase( str::empty(), true /* isUnexpectedException */ );
    fnRunCase( "12abc", false /* isUnexpectedException */ );
    fnRunCase( "99999999999999999999999", false /* isUnexpectedException */ );
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpStatusLineParsingTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpStatusLineParsingTests ********************************\n"
        );

    /*
     * doReadStatus applies three guards to the status line - the version token must start with
     * "HTTP/", the status must parse and be non-zero, and the reason phrase must be non-empty.
     * The first one is what stops the client from parsing an SSH banner or any other TCP
     * service as an HTTP response, which is the first thing that happens when a client is
     * pointed at the wrong port
     */

    const auto fnRunInvalidCase = [](
        SAA_in      const std::string&      rawResponse,
        SAA_in      const std::string&      expectedInMessage
        )
        -> void
    {
        RawHttpResponder responder( cpp::copy( rawResponse ) );

        scheduleAndExecuteInParallel(
            [ &responder, &expectedInMessage ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
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
                    "returned invalid response"
                    );

                /*
                 * m_httpStatus is only assigned after all three guards pass
                 */

                UTF_REQUIRE_EQUAL(
                    static_cast< unsigned int >( http::Parameters::HTTP_STATUS_UNDEFINED ),
                    taskImpl -> getHttpStatus()
                    );

                if( ! expectedInMessage.empty() )
                {
                    try
                    {
                        cpp::safeRethrowException( taskImpl -> exception() );

                        UTF_FAIL( "An invalid HTTP status line must be rejected" );
                    }
                    catch( bl::UnexpectedException& e )
                    {
                        UTF_REQUIRE( cpp::contains( std::string( e.what() ), expectedInMessage ) );
                    }
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });
    };

    /*
     * (1) Not HTTP at all - the offending token is reported so a wrong-port misconfiguration
     *     is diagnosable
     */

    fnRunInvalidCase( "SSH-2.0-OpenSSH_9.6\r\n\r\n", "SSH-2.0-OpenSSH_9.6" );

    /*
     * (2) A zero status and (3) a status which does not parse both fail the second guard
     */

    fnRunInvalidCase( "HTTP/1.0 0 Zero\r\n\r\n", str::empty() );
    fnRunInvalidCase( "HTTP/1.0 abc Bad\r\n\r\n", str::empty() );

    /*
     * (4) The line below deliberately ends the status line with a bare LF rather than CRLF.
     *     async_read_until( ..., "\r\n" ) otherwise guarantees that std::getline leaves at
     *     least a "\r" in the reason phrase, so this is the only way to reach the third
     *     guard - do not "fix" the \n into \r\n, that would silently drop the coverage
     */

    fnRunInvalidCase( "HTTP/1.0 200\n\r\n\r\n", str::empty() );

    /*
     * (5) The version guard only checks the "HTTP/" prefix, so 1.1 is accepted
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.1 200 OK",
                { "Content-Length: 2" },
                "ok"
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

                UTF_REQUIRE_EQUAL( 200U, taskImpl -> getHttpStatus() );
                UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), "ok" );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }

    /*
     * (6) The status is not range checked to three digits - it is carried through as it is
     *     and reaches errinfo_http_status_code. This documents the behavior rather than
     *     endorsing it
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 99999 Weird",
                { "Content-Length: 2" },
                "ok"
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

                UTF_REQUIRE_THROW( eq -> waitForSuccess( task ), bl::HttpException );

                UTF_REQUIRE_EQUAL( 99999U, taskImpl -> getHttpStatus() );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpResponseHeaderParsingTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpResponseHeaderParsingTests ********************************\n"
        );

    /*
     * doReadHeaders splits every header line at the first ':', skips the line entirely when
     * there is no separator or when the separator is at position 0, trims both halves and
     * then either folds the value into the cookie buffer (when the name matches Set-Cookie
     * case insensitively) or lower-cases the name and emplaces it - so for a duplicate name
     * the first value wins. The accumulated cookies land under the single lower-cased
     * "cookie" key with a trailing separator
     *
     * Duplicate-header first-wins is a header smuggling relevant decision which no test
     * states today, and the cookie fold is the client's only cookie support, so its exact
     * shape is what an application would have to parse back
     *
     * One response carries every shape at once, built with explicit "\r\n" joins so the
     * malformed lines are unmistakable
     */

    const std::string body = "ok";

    RawHttpResponder responder(
        RawHttpResponder::makeResponse(
            "HTTP/1.0 200 OK",
            {
                "Content-Type: text/plain",
                "X-Mixed-CASE:   spaced value",
                "X-Dup: first",
                "X-Dup: second",
                "Set-Cookie: a=1; Path=/",
                "set-cookie: b=2",
                "Location: http://example.invalid:8080/x",
                "NoSeparatorLine",
                ":leading-colon-value",
                "Content-Length: " + utils::lexical_cast< std::string >( body.size() )
            },
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

            UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), "ok" );

            /*
             * The name is lower-cased and the value is trimmed. Dropping the lower-casing
             * would break every tryGetResponseHeader caller, including chk2EnhanceException
             * and decodeContent's own Content-Type lookup
             */

            {
                const auto value = taskImpl -> tryGetResponseHeader( "X-MIXED-case" );

                UTF_REQUIRE( value );
                UTF_REQUIRE_EQUAL( *value, "spaced value" );
            }

            UTF_REQUIRE_EQUAL( taskImpl -> getResponseHeaders().count( "x-mixed-case" ), 1U );
            UTF_REQUIRE_EQUAL( taskImpl -> getResponseHeaders().count( "X-Mixed-CASE" ), 0U );

            /*
             * emplace(), not operator[] - the first value of a duplicated header wins
             */

            {
                const auto value = taskImpl -> tryGetResponseHeader( "x-dup" );

                UTF_REQUIRE( value );
                UTF_REQUIRE_EQUAL( *value, "first" );
            }

            /*
             * Only the first colon separates, so a value which contains one survives intact
             */

            {
                const auto value = taskImpl -> tryGetResponseHeader( "location" );

                UTF_REQUIRE( value );
                UTF_REQUIRE_EQUAL( *value, "http://example.invalid:8080/x" );
            }

            /*
             * Both Set-Cookie values are folded into one "cookie" entry, in the order they
             * arrived and each followed by the separator. The trailing ';' is deliberate -
             * asserting it rather than trimming it is what would catch a change to the
             * separator handling
             */

            {
                const auto value = taskImpl -> tryGetResponseHeader( http::HttpHeader::g_cookie );

                UTF_REQUIRE( value );
                UTF_REQUIRE_EQUAL( *value, "a=1; Path=/;b=2;" );
            }

            UTF_REQUIRE( ! taskImpl -> tryGetResponseHeader( http::HttpHeader::g_setCookie ) );

            /*
             * A line with no separator and a line whose separator is at position 0 are both
             * skipped, so neither reaches the map - and no entry with an empty name is made
             */

            UTF_REQUIRE( ! taskImpl -> tryGetResponseHeader( "noseparatorline" ) );
            UTF_REQUIRE_EQUAL( taskImpl -> getResponseHeaders().count( str::empty() ), 0U );

            UTF_REQUIRE( nullptr == taskImpl -> tryGetResponseHeader( "x-absent" ) );

            UTF_REQUIRE( eq -> isEmpty() );
        });
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpContentCharsetTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpContentCharsetTests ********************************\n"
        );

    /*
     * Every response the rest of the suite receives is application/json; charset=UTF-8 with a
     * pure ASCII body, so the UTF-8 arm of decodeContent runs but is a no-op and cannot tell a
     * correct conversion from no conversion at all. isExpectUtf8Content is never set anywhere,
     * although bl-tool's HttpRequest sets it unconditionally
     *
     * Dropping the '! m_isExpectUtf8Content' guard would mangle every UTF-8 response bl-tool
     * fetches, and dropping the conversion would hand UTF-8 bytes to a caller which asked for
     * ISO-8859-1 - neither shows up in any assertion today
     *
     * The byte sequences are written as explicit escapes rather than as source literals: the
     * repository is compiled by MSVC as well and a raw non-ASCII character in the source would
     * be re-encoded by the compiler
     */

    const std::string utf8Body = "caf\xC3\xA9";                 /* 5 bytes, e-acute as UTF-8 */
    const std::string latin1Body = "caf\xE9";                   /* 4 bytes, e-acute as ISO-8859-1 */

    const auto fnRunCase = [](
        SAA_in      const std::string&      contentType,
        SAA_in      const std::string&      responseBody,
        SAA_in      const bool              expectUtf8Content,
        SAA_in      const std::string&      expectedResponse
        )
        -> void
    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                {
                    "Content-Type: " + contentType,
                    "Content-Length: " + utils::lexical_cast< std::string >( responseBody.size() )
                },
                responseBody
                )
            );

        scheduleAndExecuteInParallel(
            [ &responder, expectUtf8Content, &expectedResponse ](
                SAA_in const om::ObjPtr< ExecutionQueue >& eq
                ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
                    std::string( "127.0.0.1" ),
                    responder.port(),
                    "/probe"
                    );

                taskImpl -> setTimeout( time::seconds( 60 ) );

                if( expectUtf8Content )
                {
                    taskImpl -> isExpectUtf8Content( true );
                }

                UTF_REQUIRE_EQUAL( taskImpl -> isExpectUtf8Content(), expectUtf8Content );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                /*
                 * Asserted on every case so a decode failure can never be mistaken for a
                 * transport failure
                 */

                UTF_REQUIRE_EQUAL( 200U, taskImpl -> getHttpStatus() );

                UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), expectedResponse );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    };

    /*
     * (1) UTF-8 without isExpectUtf8Content - the body is converted to ISO-8859-1, which is
     *     the only assertion anywhere that the conversion actually happens
     */

    fnRunCase( "text/plain; charset=UTF-8", utf8Body, false /* expectUtf8Content */, latin1Body );

    /*
     * (2) ... and with it set the conversion is suppressed
     */

    fnRunCase( "text/plain; charset=UTF-8", utf8Body, true /* expectUtf8Content */, utf8Body );

    /*
     * (3) A pass-through charset, spelled in lower case - which also proves the captured
     *     charset is upper-cased before it is compared
     */

    fnRunCase( "text/plain; charset=iso-8859-1", latin1Body, false /* expectUtf8Content */, latin1Body );

    /*
     * (4) The other two pass-through charsets are in the same set
     */

    fnRunCase( "text/plain; charset=WINDOWS-1252", latin1Body, false /* expectUtf8Content */, latin1Body );

    /*
     * (5) No charset at all - the body is returned untouched
     */

    fnRunCase( "text/plain", utf8Body, false /* expectUtf8Content */, utf8Body );

    {
        /*
         * (6) An unsupported charset logs a warning and leaves the body untouched
         *
         * The warning is emitted from a thread pool thread and the test binaries route every
         * warning to a test error, so the level is raised globally
         */

        Logging::LevelPusher pushLevel( Logging::LL_ERROR, true /* global */ );

        fnRunCase( "text/plain; charset=KOI8-R", utf8Body, false /* expectUtf8Content */, utf8Body );
    }

    /*
     * (7) RFC 7231 permits the charset parameter value to be a quoted string, and the quotes
     *     are excluded from the capture - so this decodes exactly like case (1) rather than
     *     going down the unsupported-charset arm with a capture of '"UTF-8'
     */

    fnRunCase(
        "text/plain; charset=\"UTF-8\"; boundary=x",
        utf8Body,
        false                               /* expectUtf8Content */,
        latin1Body
        );

    {
        /*
         * (8) A body which is not representable in ISO-8859-1 makes the conversion fail, so
         *     the whole task fails - the status line had already been parsed, which is why
         *     the status is still asserted separately
         */

        const std::string emojiBody = "\xF0\x9F\x98\x81";       /* 4 bytes, not representable in ISO-8859-1 */

        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                {
                    "Content-Type: text/plain; charset=UTF-8",
                    "Content-Length: " + utils::lexical_cast< std::string >( emojiBody.size() )
                },
                emojiBody
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

                UTF_REQUIRE_THROW( eq -> waitForSuccess( task ), std::exception );

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE_EQUAL( 200U, taskImpl -> getHttpStatus() );

                /*
                 * The exception type is build dependent - with boost_locale linked the failure
                 * is a boost::locale::conv::conversion_error, which is a std::runtime_error and
                 * is not enhanced, so only the default build's own type can be named here
                 */

                #if defined( BL_NO_BOOST_LOCALE_LIB )
                UTF_REQUIRE_THROW(
                    cpp::safeRethrowException( taskImpl -> exception() ),
                    bl::ArgumentException
                    );
                #endif

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpRequestFramingTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpRequestFramingTests ********************************\n"
        );

    /*
     * Every other client case observes only the response - nothing in the repository has ever
     * looked at the bytes initRequest() puts on the wire, so the request line, the Host
     * rendering, the user agent emission, the custom header separator and the point at which
     * Content-Length is generated are all invisible today
     *
     * This pins the current rendering; it deliberately does not test request header validation
     * because initRequest() performs none - a caller supplied header value containing CRLF
     * splits the request, which is a production hardening question and not a test gap
     *
     * initRequest() renders the whole request into one streambuf which doRequest() then sends
     * with a single async_write, so a request of this size arrives at the responder as one
     * chunk and read_until leaves the body in the same buffer as the headers
     */

    const auto makeOkResponse = []() -> std::string
    {
        return RawHttpResponder::makeResponse(
            "HTTP/1.0 200 OK",
            { "Content-Type: text/plain", "Content-Length: 2" },
            "ok"
            );
    };

    const auto runTask = []( SAA_in const om::ObjPtr< Task >& task ) -> void
    {
        scheduleAndExecuteInParallel(
            [ &task ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    };

    {
        /*
         * (1) A GET with no content and no custom headers
         */

        RawHttpResponder responder( makeOkResponse() );

        const auto taskImpl = SimpleHttpGetTaskImpl::createInstance(
            std::string( "127.0.0.1" ),
            responder.port(),
            "/probe"
            );

        taskImpl -> setTimeout( time::seconds( 60 ) );

        runTask( om::qi< Task >( taskImpl ) );

        const auto request = responder.lastRequest();

        UTF_REQUIRE( 0U == request.find( "GET /probe HTTP/1.0\r\n" ) );

        /*
         * The port is not emitted with the host. That is legal for HTTP/1.0, but it breaks
         * name based virtual hosting on a non default port, so a reviewer should see it
         * stated rather than have to derive it
         */

        UTF_REQUIRE( cpp::contains( request, "Host: 127.0.0.1" ) );
        UTF_REQUIRE( ! cpp::contains( request, "Host: 127.0.0.1:" ) );

        UTF_REQUIRE( cpp::contains( request, "\r\nAccept: */*\r\nConnection: close" ) );

        /*
         * No user agent is configured, so the header is omitted entirely - the negative half
         * of the pair which the next sub-block completes
         */

        UTF_REQUIRE_EQUAL( http::Parameters::userAgentDefault(), str::empty() );
        UTF_REQUIRE( ! cpp::contains( request, "User-Agent:" ) );

        /*
         * There is no body, so no Content-Length is generated at all
         */

        UTF_REQUIRE( ! cpp::contains( request, "Content-Length:" ) );
    }

    {
        /*
         * (2) A PUT with content - the Content-Length is derived from m_contentIn.size(),
         *     never from a caller supplied value, and the body follows the blank line
         */

        RawHttpResponder responder( makeOkResponse() );

        const std::string content = "abcdefghij";

        http::Parameters::userAgentDefault( cpp::copy( http::HttpHeader::g_userAgentBotDefault ) );

        BL_SCOPE_EXIT(
            {
                http::Parameters::userAgentDefault( std::string() );
            }
            );

        const auto taskImpl = SimpleHttpPutTaskImpl::createInstance(
            std::string( "127.0.0.1" ),
            responder.port(),
            "/probe",
            content
            );

        taskImpl -> setTimeout( time::seconds( 60 ) );

        runTask( om::qi< Task >( taskImpl ) );

        const auto request = responder.lastRequest();

        UTF_REQUIRE( 0U == request.find( "PUT /probe HTTP/1.0\r\n" ) );

        UTF_REQUIRE(
            cpp::contains(
                request,
                "\r\nUser-Agent: " + http::Parameters::userAgentDefault() + "\r\n"
                )
            );

        UTF_REQUIRE( cpp::contains( request, "\r\nContent-Length: 10\r\n" ) );

        const std::string tail = "\r\n\r\n" + content;

        UTF_REQUIRE( request.size() >= tail.size() );
        UTF_REQUIRE_EQUAL( request.substr( request.size() - tail.size() ), tail );
    }

    {
        /*
         * (3) The custom headers are written with g_nameSeparator and nothing appended, so
         *     there is no space after the colon, and they all precede the generated
         *     Content-Length - which is why a caller supplied Content-Length would produce a
         *     duplicate rather than an override
         */

        RawHttpResponder responder( makeOkResponse() );

        http::HeadersMap headers;

        headers.emplace( "X-One", "1" );
        headers.emplace( "X-Two", "2" );

        const auto taskImpl = SimpleHttpPutTaskImpl::createInstance(
            std::string( "127.0.0.1" ),
            responder.port(),
            "/probe",
            std::string( "abcdefghij" ),
            std::move( headers )
            );

        taskImpl -> setTimeout( time::seconds( 60 ) );

        runTask( om::qi< Task >( taskImpl ) );

        const auto request = responder.lastRequest();

        UTF_REQUIRE( cpp::contains( request, "\r\nX-One:1\r\n" ) );
        UTF_REQUIRE( cpp::contains( request, "\r\nX-Two:2\r\n" ) );

        const auto posOne = request.find( "X-One:" );
        const auto posTwo = request.find( "X-Two:" );
        const auto posContentLength = request.find( "Content-Length:" );

        UTF_REQUIRE( posOne != std::string::npos );
        UTF_REQUIRE( posTwo != std::string::npos );
        UTF_REQUIRE( posContentLength != std::string::npos );

        UTF_REQUIRE( posOne < posContentLength );
        UTF_REQUIRE( posTwo < posContentLength );
    }
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpSecureModeRedactionTests )
{
    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpSecureModeRedactionTests ********************************\n"
        );

    /*
     * m_isSecureMode gates four independent redactions - the path inside createUrl(), the
     * offending fragment inside chkHttpResponse(), and both the response header dump and the
     * whole request details block inside chk2EnhanceException(). bl-tool turns secure mode on
     * for exactly the requests which carry credentials, so inverting or dropping any of the
     * four ternaries leaks a cookie bearing URL, the request body and the response body into
     * a debug log. Every negative assertion below is paired with the same lookup against the
     * non-secure form, so a needle which simply stopped being emitted cannot make it pass
     */

    struct RedactionInfo
    {
        unsigned short                                  port;
        std::string                                     url;
        std::string                                     what;
        bool                                            hasDetails;
        std::string                                     details;
        bool                                            hasHeaders;
        std::string                                     headers;

        RedactionInfo()
            :
            port( 0U ),
            hasDetails( false ),
            hasHeaders( false )
        {
        }
    };

    const auto fnRunCase = [](
        SAA_in      const std::string&      rawResponse,
        SAA_in      const bool              isSecureMode
        )
        -> RedactionInfo
    {
        RedactionInfo result;

        RawHttpResponder responder( cpp::copy( rawResponse ) );

        result.port = responder.port();

        scheduleAndExecuteInParallel(
            [ &responder, isSecureMode, &result ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                /*
                 * A POST so that m_contentIn is non-empty and can be observed in the dump
                 */

                const auto taskImpl = SimpleHttpPostTaskImpl::createInstance(
                    std::string( "127.0.0.1" ),
                    responder.port(),
                    "/secret-path",
                    "secret-body"
                    );

                taskImpl -> setTimeout( time::seconds( 60 ) );

                if( isSecureMode )
                {
                    taskImpl -> isSecureMode( true );
                }

                UTF_REQUIRE_EQUAL( taskImpl -> isSecureMode(), isSecureMode );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                ( void ) eq -> pop( true );

                UTF_REQUIRE( taskImpl -> isFailed() );
                UTF_REQUIRE( nullptr != taskImpl -> exception() );

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "The request must fail so that an enhanced exception is built" );
                }
                catch( std::exception& e )
                {
                    result.what = e.what();

                    const auto* url = eh::get_error_info< eh::errinfo_http_url >( e );

                    UTF_REQUIRE( nullptr != url );

                    result.url = *url;

                    const auto* details = eh::get_error_info< eh::errinfo_http_request_details >( e );

                    if( details )
                    {
                        result.hasDetails = true;
                        result.details = *details;
                    }

                    const auto* headers = eh::get_error_info< eh::errinfo_http_response_headers >( e );

                    if( headers )
                    {
                        result.hasHeaders = true;
                        result.headers = *headers;
                    }
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });

        return result;
    };

    /*
     * A malformed status line makes both chkHttpResponse() and chk2EnhanceException() run
     */

    const std::string malformedResponse( "NOT-HTTP garbage\r\n\r\n" );

    /*
     * A valid header block with a non-200 status, so that the exception is built only after
     * doReadHeaders() has populated m_responseHeaders
     */

    const auto errorResponse = RawHttpResponder::makeResponse(
        "HTTP/1.0 404 Not Found",
        { "Content-Type: text/plain", "Content-Length: 5" },
        "error"
        );

    /*
     * (1) The non-secure control - everything is reported
     */

    {
        const auto info = fnRunCase( malformedResponse, false /* isSecureMode */ );

        UTF_REQUIRE_EQUAL(
            info.url,
            "http://127.0.0.1:" + utils::lexical_cast< std::string >( info.port ) + "/secret-path"
            );

        UTF_REQUIRE( info.hasDetails );
        UTF_REQUIRE( cpp::contains( info.details, "HTTP action: POST" ) );
        UTF_REQUIRE( cpp::contains( info.details, "/secret-path" ) );
        UTF_REQUIRE( cpp::contains( info.details, "secret-body" ) );

        UTF_REQUIRE( cpp::contains( info.what, "NOT-HTTP" ) );
    }

    /*
     * (2) The same request in secure mode - the host and the port are deliberately kept, the
     *     path, the request details and the offending response fragment are not
     */

    {
        const auto info = fnRunCase( malformedResponse, true /* isSecureMode */ );

        UTF_REQUIRE_EQUAL(
            info.url,
            "http://127.0.0.1:" + utils::lexical_cast< std::string >( info.port ) + "[REDACTED]"
            );

        UTF_REQUIRE( info.hasDetails );
        UTF_REQUIRE_EQUAL( info.details, "[REDACTED]" );
        UTF_REQUIRE( ! cpp::contains( info.details, "secret-body" ) );
        UTF_REQUIRE( ! cpp::contains( info.details, "/secret-path" ) );

        UTF_REQUIRE( ! cpp::contains( info.what, "NOT-HTTP" ) );
        UTF_REQUIRE( cpp::contains( info.what, "[REDACTED]" ) );
    }

    /*
     * (3) The response header dump. It is driven by errorResponseHeaderNamesLvalue(), a
     *     process global which is empty by default and which has no caller anywhere in the
     *     repository - so with it left alone no errinfo_http_response_headers is attached at
     *     all in the non-secure case
     */

    {
        const auto info = fnRunCase( errorResponse, false /* isSecureMode */ );

        UTF_REQUIRE( ! info.hasHeaders );
    }

    {
        UTF_REQUIRE( http::Parameters::errorResponseHeaderNamesLvalue().empty() );

        http::Parameters::errorResponseHeaderNamesLvalue().push_back( "content-type" );

        /*
         * The vector is process global, so restoring it is mandatory - every later case in
         * this binary shares it
         */

        BL_SCOPE_EXIT(
            {
                http::Parameters::errorResponseHeaderNamesLvalue().clear();
            }
            );

        {
            const auto info = fnRunCase( errorResponse, false /* isSecureMode */ );

            UTF_REQUIRE( info.hasHeaders );
            UTF_REQUIRE( cpp::contains( info.headers, "content-type: " ) );
            UTF_REQUIRE( cpp::contains( info.headers, "text/plain" ) );
        }

        {
            const auto info = fnRunCase( errorResponse, true /* isSecureMode */ );

            UTF_REQUIRE( info.hasHeaders );
            UTF_REQUIRE_EQUAL( info.headers, "[REDACTED]" );
            UTF_REQUIRE( ! cpp::contains( info.headers, "text/plain" ) );
        }
    }

    UTF_REQUIRE( http::Parameters::errorResponseHeaderNamesLvalue().empty() );
}

UTF_AUTO_TEST_CASE( Client_SimpleHttpTimeoutTests )
{
    /*
     * The same scenario can also be driven by hand against a netcat instance, which is what
     * this case used to require before it was made unconditional. Works on Linux only, unless
     * you use a netcat or similar port for Windows:
     * - Start local netcat instance and pass to it few headers required to be dumped as part of the exception:
     *     echo -e "HTTP/1.0 500 ERROR\r\nrequestId: 123\r\nVersion: 1.0\r\nline1\r\n\line2\r\n\r\n" | nc -l 4545 -q 10
     * - Point a SimpleHttpGetTaskImpl at localhost:4545
     */

    using namespace bl;
    using namespace bl::tasks;

    BL_LOG_MULTILINE(
        Logging::debug(),
        BL_MSG()
            << "\n******************************** Starting test: Client_SimpleHttpTimeoutTests ********************************\n"
        );

    /*
     * m_timeout is a whole request deadline - it is armed exactly once, from
     * continueAfterConnected(), and is never re-armed by progress. It used to be re-armed at
     * the end of doRequest, doReadStatus, doReadHeaders and doReadContent, which made it an
     * inactivity deadline instead and let a drip feeding server hold a request open forever
     *
     * --timeout-in-seconds rewrites http::Parameters::timeoutInSecondsGet/Other globally, so
     * every sub-case below sets the per-task timeout explicitly rather than relying on the
     * default
     */

    /*
     * (1) The deadline fires against a server which accepts the request and then stalls
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 2" },
                "ok"
                ),
            time::seconds( 6 )                              /* delayBeforeResponse */
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

                taskImpl -> setTimeout( time::seconds( 2 ) );

                UTF_REQUIRE_EQUAL( taskImpl -> getTimeout(), time::seconds( 2 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                const auto started = time::microsec_clock::universal_time();

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::TimeoutException,
                    "has timed out"
                    );

                const auto elapsed = time::microsec_clock::universal_time() - started;

                /*
                 * The responder does not answer for six seconds, so returning well before
                 * that is only possible because the deadline fired
                 */

                UTF_REQUIRE( elapsed < time::seconds( 5 ) );

                UTF_REQUIRE( taskImpl -> isTimedOut() );
                UTF_REQUIRE( taskImpl -> isFailed() );

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "A stalled HTTP request must time out" );
                }
                catch( bl::TimeoutException& e )
                {
                    /*
                     * errinfo_is_expected is what stops chk2DumpException from dumping every
                     * routine timeout, and the cancellation which actually stopped the task is
                     * chained rather than discarded
                     */

                    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_is_expected >( e ) );
                    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_nested_exception_ptr >( e ) );

                    UTF_REQUIRE( cpp::contains( std::string( e.what() ), "HTTP GET request to 'http://127.0.0.1:" ) );
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }

    /*
     * (2) The deadline is NOT re-armed by progress. The responder below hands over the whole
     *     response one byte at a time, 40 ms apart, so the client is making progress through
     *     every one of the four read handlers the whole time - and must still be interrupted
     *     at its three second deadline. Delivering the 105 byte response takes the responder
     *     at least 104 * 40 ms = 4.16 s, so a re-armed deadline would never fire and this
     *     sub-case would complete successfully instead of timing out
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 40" },
                std::string( 40U, 'y' )
                ),
            time::milliseconds( 0 )                         /* delayBeforeResponse */,
            1U                                              /* chunkSize */,
            time::milliseconds( 40 )                        /* delayBetweenChunks */
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

                taskImpl -> setTimeout( time::seconds( 3 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                const auto started = time::microsec_clock::universal_time();

                UTF_REQUIRE_THROW_MESSAGE(
                    eq -> waitForSuccess( task ),
                    bl::TimeoutException,
                    "has timed out"
                    );

                const auto elapsed = time::microsec_clock::universal_time() - started;

                UTF_REQUIRE( elapsed < time::seconds( 4 ) );

                UTF_REQUIRE( taskImpl -> isTimedOut() );
                UTF_REQUIRE( taskImpl -> isFailed() );

                try
                {
                    cpp::safeRethrowException( taskImpl -> exception() );

                    UTF_FAIL( "A drip fed HTTP request must still hit its whole request deadline" );
                }
                catch( bl::TimeoutException& e )
                {
                    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_is_expected >( e ) );
                    UTF_REQUIRE( nullptr != eh::get_error_info< eh::errinfo_nested_exception_ptr >( e ) );

                    UTF_REQUIRE( cpp::contains( std::string( e.what() ), "HTTP GET request to 'http://127.0.0.1:" ) );
                }

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }

    /*
     * (3) The positive control - the same drip pattern against a deadline which is generous
     *     enough completes normally, so the two sub-cases above are not merely proving that
     *     a dripped response can never be read
     */

    {
        RawHttpResponder responder(
            RawHttpResponder::makeResponse(
                "HTTP/1.0 200 OK",
                { "Content-Type: text/plain", "Content-Length: 40" },
                std::string( 40U, 'y' )
                ),
            time::milliseconds( 0 )                         /* delayBeforeResponse */,
            1U                                              /* chunkSize */,
            time::milliseconds( 20 )                        /* delayBetweenChunks */
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

                taskImpl -> setTimeout( time::seconds( 20 ) );

                UTF_REQUIRE_EQUAL( taskImpl -> getTimeout(), time::seconds( 20 ) );

                const auto task = om::qi< Task >( taskImpl );

                eq -> push_back( task );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                UTF_REQUIRE( ! taskImpl -> isTimedOut() );
                UTF_REQUIRE_EQUAL( taskImpl -> getResponse().size(), 40U );

                UTF_REQUIRE( eq -> isEmpty() );
            });
    }
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

                    /*
                     * The secure task binds a str::SecureStringWrapper to the base class's own
                     * m_contentIn buffer and assigns the caller's secure content into it, so
                     * initRequest() emits it as the request body exactly as the plain task
                     * would. Nothing observed that until now - the shared test backend never
                     * looks at the body it was sent, so dropping the assignment (or reordering
                     * the members so the wrapper binds to a buffer which is constructed after
                     * it) would have sent an empty body for every secure request and left the
                     * whole suite green. The file local echo backend closes that hole
                     */

                    {
                        const bl::str::SecureStringWrapper content( "Hidden content" );

                        const auto taskImpl = SimpleSecureHttpSslPutTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() ),
                            cpp::copy( test::UtfArgsParser::port() ),
                            utest::http::g_requestUri,
                            content
                            );

                        UTF_REQUIRE( taskImpl -> isSecureMode() );

                        const auto task = om::qi< Task >( taskImpl );

                        eq -> push_back( task );

                        UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                        UTF_REQUIRE_EQUAL( http::Parameters::HTTP_SUCCESS_OK, taskImpl -> getHttpStatus() );

                        UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), "Hidden content" );
                        UTF_REQUIRE_EQUAL( taskImpl -> getContent(), "Hidden content" );

                        UTF_REQUIRE( eq -> isEmpty() );
                    }

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

                        /*
                         * The GET-with-content form the header declares must carry its body too
                         */

                        UTF_REQUIRE_EQUAL( response, "Hidden content" );
                        UTF_REQUIRE_EQUAL( stask -> getContent(), "Hidden content" );

                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* begin HTTP response ******* \n" );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << response );
                        BL_LOG_MULTILINE( Logging::debug(), BL_MSG() << "\n******* end HTTP response ******* \n" );
                    }

                    /*
                     * The no-content verb pins the empty body form, so a regression which
                     * started sending something would be caught as well
                     */

                    {
                        const auto taskImpl = SimpleSecureHttpSslDeleteTaskImpl::createInstance(
                            cpp::copy( test::UtfArgsParser::host() ),
                            cpp::copy( test::UtfArgsParser::port() ),
                            utest::http::g_requestUri
                            );

                        UTF_REQUIRE( taskImpl -> isSecureMode() );

                        const auto task = om::qi< Task >( taskImpl );

                        eq -> push_back( task );

                        UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( task ) );

                        UTF_REQUIRE_EQUAL( http::Parameters::HTTP_SUCCESS_OK, taskImpl -> getHttpStatus() );

                        UTF_REQUIRE_EQUAL( taskImpl -> getResponse(), str::empty() );
                        UTF_REQUIRE( taskImpl -> getContent().empty() );

                        UTF_REQUIRE( eq -> isEmpty() );
                    }
                });
        },
        EchoBackendImpl::createInstance< bl::httpserver::ServerBackendProcessing >()
        );
}

