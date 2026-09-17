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

#include <utests/baselib/TestTaskUtils.h>
#include <utests/baselib/MachineGlobalTestLock.h>
#include <utests/baselib/UtfCrypto.h>

#include <baselib/messaging/BlobServerFacade.h>
#include <baselib/messaging/TcpBlockServerMessageDispatcher.h>

#include <baselib/core/BaseIncludes.h>

namespace
{
    typedef bl::tasks::TcpBlockTransferClientConnectionImpl< bl::tasks::TcpSocketAsyncBase >    connection_t;
    typedef bl::tasks::TcpConnectionEstablisherConnectorImpl< bl::tasks::TcpSocketAsyncBase >   connector_t;

    /*
     * Functions below return values of the type int to make it simple
     * ( without passing references ) to ensure that the function was
     * actually called (asyncDataChunkStorage will return the return
     * value of callback or errorCallback depending on which was called).
     * Or it will return 0 if none of the callbacks was called
     */

    typedef bl::cpp::function<
            int (
                SAA_in const bl::om::ObjPtr< connection_t >& connection
                )
        >
        callback_t;

    typedef bl::cpp::function<
            int (
                std::exception& e
                )
        >
        error_callback_t;

    using bl::data::DataChunkStorage;
    using bl::AsyncDataChunkStorage;

    int asyncDataChunkStorageTest(
        SAA_in      const bl::om::ObjPtr< AsyncDataChunkStorage >&  asyncStorage,
        SAA_in      const callback_t&                               callback,
        SAA_in      const error_callback_t&                         errorCallback
        )
    {
        using bl::tasks::SimpleTaskControlTokenImpl;
        using bl::tasks::TaskControlTokenRW;

        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        test::MachineGlobalTestLock lock;

        using bl::data::datablocks_pool_type;

        std::atomic< int > returnValue( 0 );

        utest::TestTaskUtils::createAcceptorAndExecute< bl::tasks::TcpBlockServerDataChunkStorage >(
            controlToken,
            [ &callback, &errorCallback, &returnValue ]() -> void
            {
                const auto eq = bl::om::lockDisposable(
                    bl::tasks::ExecutionQueueImpl::createInstance(
                        bl::tasks::ExecutionQueue::OptionKeepAll
                        )
                    );

                const auto connector = connector_t::createInstance(
                    "localhost",
                    28100
                    );

                const auto taskConnector = bl::om::qi< bl::tasks::Task >( connector.get() );
                eq -> push_back( taskConnector );
                eq -> waitForSuccess( taskConnector );


                const auto stats = connection_t::createInstance(
                    connection_t::CommandId::ReceiveChunk,
                    bl::uuids::nil() /* chunkId */,
                    datablocks_pool_type::createInstance( "[client pool]" ),
                    bl::tasks::BlockTransferDefs::BlockType::ServerState
                    );

                stats -> setCommandInfo(
                    connection_t::CommandId::ReceiveChunk,
                    bl::uuids::nil() /* chunkId */,
                    nullptr /* chunkData */,
                    bl::tasks::BlockTransferDefs::BlockType::ServerState
                    );

                stats -> attachStream( connector -> detachStream() );
                const auto taskStats = bl::om::qi< bl::tasks::Task >( stats );
                eq -> push_back( taskStats );

                try
                {
                    eq -> waitForSuccess( taskStats );
                    returnValue = callback( stats );
                }
                catch( std::exception& e)
                {
                    returnValue = errorCallback( e );
                }

            },
            datablocks_pool_type::createInstance( "[server pool]" ),
            asyncStorage,
            std::string( test::UtfArgsParser::host() ),
            test::UtfArgsParser::port()
            );

        return returnValue;
    }
}

UTF_AUTO_TEST_CASE( Test_AsyncDataChunkStorageStats )
{
    const auto storage = bl::om::lockDisposable(
        utest::BackendImplTestImpl::createInstance< bl::data::DataChunkStorage >()
        );

    const auto asyncStorage =
        bl::AsyncDataChunkStorage::createInstance(
            storage /* writeStorage */,
            storage /* readStorage */,
            test::UtfArgsParser::threadsCount(),
            nullptr /* controlToken */,
            0U /* maxConcurrentTasks */,
            nullptr /* dataBlocksPool */,
            bl::AsyncDataChunkStorage::datablock_callback_t(),
            []( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) -> void
            {
                const std::string output = "Test output";

                const auto dataLength = output.size() + 1;

                BL_CHK(
                    false,
                    dataLength <= data -> capacity(),
                    BL_MSG()
                        << "Block size not sufficient"
                    );

                /*
                 * Chunk data will include the zero terminator character
                 */

                ::memcpy( data -> pv(), output.c_str(), dataLength );
                data -> setSize( dataLength );
            }
            );

    const int returnValue = asyncDataChunkStorageTest(
        asyncStorage,
        [] ( SAA_in const bl::om::ObjPtr< connection_t >& connection ) -> int
        {
            UTF_CHECK_EQUAL(
                "Test output",
                std::string(
                    connection -> getChunkData() -> begin(),
                    connection -> getChunkData() -> end() - 1
                    )
                );

            return 1;
        },
        [] ( std::exception& e )
        {
            BL_UNUSED( e );

            return 2;
        }
    );

    UTF_CHECK_EQUAL( 1, returnValue ); /* Make sure that callback was called */
}

UTF_AUTO_TEST_CASE( Test_AsyncDataChunkStorageStatsException )
{
    const auto storage = bl::om::lockDisposable(
        utest::BackendImplTestImpl::createInstance()
        );

    const auto asyncStorage =
        bl::AsyncDataChunkStorage::createInstance(
            bl::om::copy< bl::data::DataChunkStorage >( storage ) /* writeStorage */,
            bl::om::copy< bl::data::DataChunkStorage >( storage ) /* readStorage */,
            test::UtfArgsParser::threadsCount(),
            nullptr /* controlToken */,
            0U /* maxConcurrentTasks */,
            nullptr /* dataBlocksPool */,
            bl::AsyncDataChunkStorage::datablock_callback_t(),
            []( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data )
            {
                BL_UNUSED( data );

                BL_THROW_EC(
                    bl::eh::errc::make_error_code( bl::eh::errc::no_message ),
                    "Simulated exception"
                    );
            }
            );

    const int returnValue = asyncDataChunkStorageTest(
        asyncStorage,
        [] ( SAA_in const bl::om::ObjPtr< connection_t >& connection ) -> int
        {
            BL_UNUSED( connection );

            return 1;
        },
        [] ( std::exception& e ) -> int
        {
            UTF_CHECK_EQUAL( bl::eh::errc::no_message, *bl::eh::get_error_info< bl::eh::errinfo_errno >( e ) );

            return 2;
        }
    );

    UTF_CHECK_EQUAL( 2, returnValue ); /* Make sure that errorCallback was called */
}

UTF_AUTO_TEST_CASE( Test_AsyncDataChunkStorageStatsUncodedException )
{
    /*
     * Uncoded and non-generic-category backend errors are narrowed away on the wire
     *
     * TcpBlockTransferServerConnection::chk4ServerErrors() turns a backend failure into a
     * single std::uint32_t - errinfo_errno if present, else errinfo_error_code but only
     * when its category is generic. If neither yields a non-zero value the ErrBit is never
     * set, chkAsyncResult() returns true and the server carries on as if the operation had
     * succeeded; execution then reaches detail::chkChunkSize(...), which fails because the
     * callback never set a size, and the client is left with a dropped connection carrying
     * a transport error instead of the server's reason
     *
     * Every Windows OS error (system_category) and every OpenSSL-category error takes that
     * branch. The intended fixes are either to give GetServerState a 'defaultError' the way
     * AuthenticateClient has one, one line above it in AsyncExecutorWrapperBlocks.h, or to
     * make chk4ServerErrors fall back on a non-zero sentinel (eh::errc::io_error, say) when
     * it cannot narrow the code, so that 'a server error occurred' is never silently
     * promoted to success
     *
     * Note that the exact transport error the client observes (asio::error::eof,
     * connection_reset or the chunk size message) is platform and timing dependent, so only
     * the stable negative facts are asserted below
     */

    const auto cbRunWithServerStateCallback = [](
        SAA_in      bl::AsyncDataChunkStorage::datablock_callback_t&&        serverStateCallback,
        SAA_in      const error_callback_t&                                  errorCallback
        )
        -> int
    {
        const auto storage = bl::om::lockDisposable(
            utest::BackendImplTestImpl::createInstance()
            );

        const auto asyncStorage =
            bl::AsyncDataChunkStorage::createInstance(
                bl::om::copy< bl::data::DataChunkStorage >( storage )        /* writeStorage */,
                bl::om::copy< bl::data::DataChunkStorage >( storage )        /* readStorage */,
                test::UtfArgsParser::threadsCount(),
                nullptr                                                      /* controlToken */,
                0U                                                           /* maxConcurrentTasks */,
                nullptr                                                      /* dataBlocksPool */,
                bl::AsyncDataChunkStorage::datablock_callback_t()            /* authenticationCallback */,
                BL_PARAM_FWD( serverStateCallback )
                );

        return asyncDataChunkStorageTest(
            asyncStorage,
            []( SAA_in const bl::om::ObjPtr< connection_t >& connection ) -> int
            {
                BL_UNUSED( connection );

                return 1;
            },
            errorCallback
            );
    };

    /*
     * The assertion oracle is shared between the two narrowed-away sub-cases, so a change
     * which makes only one of them work is caught
     */

    const auto cbMakeNarrowedAwayCallback = []( SAA_in std::string&& serverMessage ) -> error_callback_t
    {
        const std::string messageAbsent( BL_PARAM_FWD( serverMessage ) );

        return [ messageAbsent ]( std::exception& e ) -> int
        {
            /*
             * Guarded, unlike Test_AsyncDataChunkStorageStatsException which dereferences
             * the errno unconditionally - here there is none to dereference
             */

            UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_errno >( e ) );
            UTF_REQUIRE( nullptr == dynamic_cast< bl::ServerErrorException* >( &e ) );

            /*
             * The server's own reason never reached the client - this is the assertion
             * which states the defect and the one a fix would flip
             */

            UTF_REQUIRE( std::string::npos == std::string( e.what() ).find( messageAbsent ) );

            return 2;
        };
    };

    {
        /*
         * (1) An uncoded exception - no errno and no error code at all
         */

        const int returnValue = cbRunWithServerStateCallback(
            []( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) -> void
            {
                BL_UNUSED( data );

                BL_THROW( bl::UnexpectedException(), "Simulated uncoded exception" );
            },
            cbMakeNarrowedAwayCallback( "Simulated uncoded exception" )
            );

        /*
         * The client did fail - a regression which started reporting success outright
         * would return 1 here
         */

        UTF_CHECK_EQUAL( 2, returnValue );
    }

    {
        /*
         * (2) A system-category coded exception - the shape every Windows OS failure takes
         */

        const int returnValue = cbRunWithServerStateCallback(
            []( SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data ) -> void
            {
                BL_UNUSED( data );

                BL_THROW_EC(
                    bl::eh::error_code( 5, bl::eh::system_category() ),
                    "Simulated system-category exception"
                    );
            },
            cbMakeNarrowedAwayCallback( "Simulated system-category exception" )
            );

        UTF_CHECK_EQUAL( 2, returnValue );
    }

    {
        /*
         * (3) A coded server error which does take the good branch, carrying the
         * 'is expected' flag - the flag is not part of the wire representation at all,
         * because chk4ServerErrorsClient rebuilds the exception from the uint32 with only
         * errinfo_errno and errinfo_error_code (TcpBlockTransferClient.h:272-316)
         */

        const auto ecEnoent =
            bl::eh::errc::make_error_code( bl::eh::errc::no_such_file_or_directory );

        std::atomic< bool > serverFlagWasSet( false );

        const int returnValue = cbRunWithServerStateCallback(
            [ &ecEnoent, &serverFlagWasSet ](
                SAA_in const bl::om::ObjPtr< bl::data::DataBlock >& data
                ) -> void
            {
                BL_UNUSED( data );

                auto exception =
                    bl::ServerErrorException()
                        << bl::eh::errinfo_errno( ecEnoent.value() )
                        << bl::eh::errinfo_error_code( ecEnoent )
                        << bl::eh::errinfo_is_expected( true );

                /*
                 * The mirror positive control, recorded here and asserted on the main test
                 * thread below, so the client side assertion cannot become vacuous
                 */

                const auto* isExpected = bl::eh::get_error_info< bl::eh::errinfo_is_expected >( exception );

                serverFlagWasSet = ( nullptr != isExpected && *isExpected );

                BL_THROW( exception, "Simulated expected server error" );
            },
            [ &ecEnoent ]( std::exception& e ) -> int
            {
                UTF_REQUIRE( nullptr != dynamic_cast< bl::ServerErrorException* >( &e ) );

                const auto* errNo = bl::eh::get_error_info< bl::eh::errinfo_errno >( e );

                UTF_REQUIRE( nullptr != errNo );
                UTF_REQUIRE_EQUAL( ecEnoent.value(), *errNo );

                /*
                 * The 'is expected' flag did not cross the wire
                 */

                UTF_REQUIRE( nullptr == bl::eh::get_error_info< bl::eh::errinfo_is_expected >( e ) );

                /*
                 * The client's message is synthesised by chk4ServerErrorsClient
                 * (TcpBlockTransferClient.h:272-316) and is not the server's own
                 */

                UTF_REQUIRE( bl::cpp::contains( std::string( e.what() ), "Server error has occurred" ) );

                return 2;
            }
            );

        UTF_CHECK_EQUAL( 2, returnValue );

        UTF_REQUIRE( serverFlagWasSet.load() );
    }
}

namespace
{
    /**
     * @brief A messaging backend processing mock whose processing task always fails with an
     * exception which is not a ServerErrorException
     *
     * This is the same shape as utf_baselib_io's ThrowingBackendProcessing; the two live in
     * different translation-unit-local namespaces so the code is necessarily duplicated
     */

    class ThrowingMessagingBackend : public bl::messaging::BackendProcessingBase
    {
    protected:

        typedef ThrowingMessagingBackend                                        this_type;

        std::atomic< std::size_t >                                              m_calls;

        ThrowingMessagingBackend()
            :
            m_calls( 0U )
        {
        }

        void rejectTheMessage()
        {
            ++m_calls;

            BL_THROW(
                bl::ArgumentException(),
                BL_MSG()
                    << "Simulated fatal messaging backend error"
                );
        }

    public:

        auto calls() const NOEXCEPT -> std::size_t
        {
            return m_calls.load();
        }

        virtual bool autoBlockDispatching() const NOEXCEPT OVERRIDE
        {
            return true;
        }

        virtual auto createBackendProcessingTask(
            SAA_in                  const OperationId                           operationId,
            SAA_in                  const CommandId                             commandId,
            SAA_in                  const bl::uuid_t&                           sessionId,
            SAA_in                  const bl::uuid_t&                           chunkId,
            SAA_in_opt              const bl::uuid_t&                           sourcePeerId,
            SAA_in_opt              const bl::uuid_t&                           targetPeerId,
            SAA_in_opt              const bl::om::ObjPtr< bl::data::DataBlock >& data
            )
            -> bl::om::ObjPtr< bl::tasks::Task > OVERRIDE
        {
            BL_UNUSED( operationId );
            BL_UNUSED( commandId );
            BL_UNUSED( sessionId );
            BL_UNUSED( chunkId );
            BL_UNUSED( sourcePeerId );
            BL_UNUSED( targetPeerId );
            BL_UNUSED( data );

            return bl::tasks::SimpleTaskImpl::createInstance< bl::tasks::Task >(
                bl::cpp::bind(
                    &this_type::rejectTheMessage,
                    bl::om::ObjPtrCopyable< this_type >::acquireRef( this )
                    )
                );
        }
    };

    typedef bl::om::ObjectImpl< ThrowingMessagingBackend > ThrowingMessagingBackendImpl;

    /**
     * @brief Polls the acceptor state, bounded, until the predicate holds or the retries
     * are exhausted; it returns the state which was observed last
     */

    auto pollAcceptorState(
        SAA_in          const bl::om::ObjPtr< bl::tasks::Task >&                acceptorTask,
        SAA_in          const bool                                              waitForCompleted,
        SAA_in_opt      const std::size_t                                       maxRetries = 60U
        )
        -> bl::tasks::Task::State
    {
        auto state = acceptorTask -> getState();

        for( std::size_t retries = 0U; retries < maxRetries; ++retries )
        {
            state = acceptorTask -> getState();

            if( waitForCompleted == ( bl::tasks::Task::Completed == state ) )
            {
                break;
            }

            bl::os::sleep( bl::time::seconds( 1 ) );
        }

        return state;
    }

} // __unnamed

UTF_AUTO_TEST_CASE( Test_BlobServerFatalBackendErrorBlastRadius )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;

    UTF_MESSAGE(
        "Fatal backend error classification: an exception which escapes the backend without "
        "being a ServerErrorException sets m_isFatalServerError on the connection, and the "
        "blast radius of that is decided solely by the async wrapper - "
        "AsyncDataChunkStorage inherits stopServerOnUnexpectedBackendError() == true and the "
        "whole blob server acceptor is cancelled, while AsyncMessageDispatcherWrapper "
        "overrides it to false so only the offending connection dies"
        );

    /*
     * The acceptor is built explicitly rather than through createAcceptorAndExecute(),
     * which hides it, so the callback can observe its state
     *
     * All the sub-blocks bind the same port and therefore take the machine global lock and
     * run sequentially; every wait is bounded, because the failure signature of a
     * regression is exactly that an acceptor completes (or does not) when it should not
     */

    const auto cbConnect = [](
        SAA_in          const om::ObjPtr< ExecutionQueue >&                     eq,
        SAA_in          const om::ObjPtr< datablocks_pool_type >&               dataBlocksPool
        )
        -> om::ObjPtr< connection_t >
    {
        const auto connector = connector_t::createInstance( std::string( "localhost" ), 28100 );

        const auto taskConnector = om::qi< Task >( connector.get() );
        eq -> push_back( taskConnector );
        eq -> waitForSuccess( taskConnector );

        auto transfer = connection_t::createInstance(
            connection_t::CommandId::NoCommand,
            uuids::create()                                     /* peerId */,
            dataBlocksPool
            );

        transfer -> attachStream( connector -> detachStream() );

        return transfer;
    };

    {
        /*
         * (a) The 'true' polarity - a blob server
         *
         * The storage backend is disabled, so save() raises a plain SystemException which
         * is never wrapped into a ServerErrorException on the way out (AsyncDataChunkStorage
         * calls writeStorage() -> save() directly), and chk4ServerErrors() classifies it as
         * fatal
         */

        test::MachineGlobalTestLock lock;

        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
        const auto dataBlocksPool = datablocks_pool_type::createInstance();

        const auto backendImpl = utest::BackendImplTestImpl::createInstance();

        backendImpl -> setStorageDisabled( true );

        const auto asyncStorage = om::lockDisposable(
            AsyncDataChunkStorage::createInstance(
                om::copy< DataChunkStorage >( backendImpl )         /* writeStorage */,
                om::copy< DataChunkStorage >( backendImpl )         /* readStorage */,
                test::UtfArgsParser::threadsCount(),
                om::qi< TaskControlToken >( controlToken ),
                0U                                                  /* maxConcurrentTasks */,
                dataBlocksPool
                )
            );

        UTF_REQUIRE( asyncStorage -> stopServerOnUnexpectedBackendError() );

        const auto acceptor = TcpBlockServerDataChunkStorage::createInstance<>(
            controlToken,
            dataBlocksPool,
            std::string( "localhost" ),
            28100,
            bl::str::empty()                                        /* privateKeyPem */,
            bl::str::empty()                                        /* certificatePem */,
            asyncStorage
            );

        UTF_REQUIRE( acceptor );

        const auto acceptorTask = om::qi< Task >( acceptor );

        utest::TestTaskUtils::startAcceptorAndExecuteCallback(
            [ & ]() -> void
            {
                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        const auto transfer = cbConnect( eq, dataBlocksPool );
                        const auto taskTransfer = om::qi< Task >( transfer );

                        transfer -> setChunkData( backendImpl -> getData() );
                        transfer -> setCommandId( connection_t::CommandId::SendChunk );
                        transfer -> setChunkId( uuids::create() );

                        eq -> push_back( taskTransfer );
                        eq -> wait( taskTransfer );

                        UTF_REQUIRE( taskTransfer -> isFailed() );

                        UTF_REQUIRE_EQUAL( 0U, backendImpl -> saveCalls() );

                        /*
                         * The fatal backend error must take the whole acceptor down, and it
                         * must do so gracefully - requestCancelInternal() is a shutdown, not
                         * a failure
                         */

                        UTF_REQUIRE_EQUAL(
                            Task::Completed,
                            pollAcceptorState( acceptorTask, true /* waitForCompleted */ )
                            );

                        UTF_REQUIRE( ! acceptorTask -> isFailed() );

                        eq -> forceFlushNoThrow();
                    }
                    );
            },
            acceptor,
            "localhost"                                             /* readinessHost */,
            28100U                                                  /* readinessPort */
            );

        backendImpl -> assertions().requireNone();
    }

    {
        /*
         * (b) The 'false' polarity - a messaging broker
         *
         * The very same classification is reached (the backend task throws an
         * ArgumentException, which is not a ServerErrorException), but the async wrapper
         * says the server must not be stopped, so only the offending connection dies
         *
         * This is the load bearing sub-block: a one line flip of
         * AsyncMessageDispatcherWrapper::stopServerOnUnexpectedBackendError() to true turns
         * any single misbehaving client into a broker wide outage
         */

        test::MachineGlobalTestLock lock;

        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
        const auto dataBlocksPool = datablocks_pool_type::createInstance();

        const auto backendImpl = om::lockDisposable( ThrowingMessagingBackendImpl::createInstance() );

        typedef TcpBlockServerMessageDispatcher::async_wrapper_t     async_wrapper_t;
        typedef async_wrapper_t::backend_interface_t                 backend_interface_t;

        const auto asyncWrapper = om::lockDisposable(
            async_wrapper_t::createInstance< async_wrapper_t >(
                om::qi< backend_interface_t >( backendImpl )        /* writeBackend */,
                om::qi< backend_interface_t >( backendImpl )        /* readBackend */,
                test::UtfArgsParser::threadsCount(),
                om::qi< TaskControlToken >( controlToken ),
                0U                                                  /* maxConcurrentTasks */,
                dataBlocksPool
                )
            );

        UTF_REQUIRE( ! asyncWrapper -> stopServerOnUnexpectedBackendError() );

        const auto acceptor = TcpBlockServerMessageDispatcher::createInstance<>(
            controlToken,
            dataBlocksPool,
            std::string( "localhost" ),
            28100,
            bl::str::empty()                                        /* privateKeyPem */,
            bl::str::empty()                                        /* certificatePem */,
            asyncWrapper
            );

        UTF_REQUIRE( acceptor );

        const auto acceptorTask = om::qi< Task >( acceptor );

        utest::TestTaskUtils::startAcceptorAndExecuteCallback(
            [ & ]() -> void
            {
                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        {
                            const auto transfer = cbConnect( eq, dataBlocksPool );
                            const auto taskTransfer = om::qi< Task >( transfer );

                            transfer -> setChunkData( utest::BackendImplTestImpl::initDataBlock() );
                            transfer -> setCommandId( connection_t::CommandId::SendChunk );
                            transfer -> setChunkId( uuids::create() );

                            eq -> push_back( taskTransfer );
                            eq -> wait( taskTransfer );

                            UTF_REQUIRE( taskTransfer -> isFailed() );

                            eq -> forceFlushNoThrow();
                        }

                        UTF_REQUIRE_EQUAL( 1U, backendImpl -> calls() );

                        /*
                         * The broker acceptor must survive - the poll is bounded and asks
                         * for the opposite outcome, so a regression turns into a clean
                         * assertion rather than a hang
                         */

                        UTF_REQUIRE(
                            Task::Completed !=
                                pollAcceptorState( acceptorTask, false /* waitForCompleted */, 5U )
                            );

                        /*
                         * ... and it must still be serving: a brand new connection which
                         * only negotiates the protocol version completes successfully
                         */

                        {
                            const auto transfer = cbConnect( eq, dataBlocksPool );
                            const auto taskTransfer = om::qi< Task >( transfer );

                            transfer -> setCommandInfo( connection_t::CommandId::NoCommand );

                            eq -> push_back( taskTransfer );
                            eq -> waitForSuccess( taskTransfer );

                            UTF_REQUIRE( transfer -> isClientVersionNegotiated() );

                            cancelAndWaitForSuccess( eq, taskTransfer );
                        }

                        UTF_REQUIRE( Task::Completed != acceptorTask -> getState() );
                    }
                    );
            },
            acceptor,
            "localhost"                                             /* readinessHost */,
            28100U                                                  /* readinessPort */
            );
    }

    {
        /*
         * (c) The benign contrast arm - a peer reachable guard which lands on the other
         * side of the split
         *
         * onChunkAllocated()'s 'size <= capacity()' check raises a plain UnexpectedException
         * *outside* chk4ServerErrors(), so m_isFatalServerError stays false: the offending
         * connection dies without an error acknowledgment and the acceptor and every
         * bystander connection survive
         *
         * Deleting or inverting that guard would be a heap overwrite of size - capacity
         * bytes into the receive block
         */

        test::MachineGlobalTestLock lock;

        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
        const auto dataBlocksPool = datablocks_pool_type::createInstance();

        const auto backendImpl = utest::BackendImplTestImpl::createInstance();

        const auto asyncStorage = om::lockDisposable(
            AsyncDataChunkStorage::createInstance(
                om::copy< DataChunkStorage >( backendImpl )         /* writeStorage */,
                om::copy< DataChunkStorage >( backendImpl )         /* readStorage */,
                test::UtfArgsParser::threadsCount(),
                om::qi< TaskControlToken >( controlToken ),
                0U                                                  /* maxConcurrentTasks */,
                dataBlocksPool
                )
            );

        const auto acceptor = TcpBlockServerDataChunkStorage::createInstance<>(
            controlToken,
            dataBlocksPool,
            std::string( "localhost" ),
            28100,
            bl::str::empty()                                        /* privateKeyPem */,
            bl::str::empty()                                        /* certificatePem */,
            asyncStorage
            );

        UTF_REQUIRE( acceptor );

        const auto acceptorTask = om::qi< Task >( acceptor );

        utest::TestTaskUtils::startAcceptorAndExecuteCallback(
            [ & ]() -> void
            {
                scheduleAndExecuteInParallel(
                    [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
                    {
                        /*
                         * The bystander connection, negotiated up front so it is a real
                         * established endpoint on the server
                         */

                        const auto bystander = cbConnect( eq, dataBlocksPool );
                        const auto taskBystander = om::qi< Task >( bystander );

                        bystander -> setCommandInfo( connection_t::CommandId::NoCommand );
                        eq -> push_back( taskBystander );
                        eq -> waitForSuccess( taskBystander );

                        const auto chunkIdBystander = uuids::create();

                        {
                            /*
                             * The offending connection announces a chunk size larger than
                             * the server's block capacity; the block is built outside the
                             * shared pool so the pool's own blocks stay at the default
                             * capacity
                             */

                            const auto bigBlock = DataBlock::createInstance( 2U * 1024U * 1024U );
                            bigBlock -> setSize( 2U * 1024U * 1024U );

                            const auto transfer = cbConnect( eq, dataBlocksPool );
                            const auto taskTransfer = om::qi< Task >( transfer );

                            transfer -> setCommandInfo(
                                connection_t::CommandId::SendChunk,
                                uuids::create()                     /* chunkId */,
                                om::copy( bigBlock )                /* chunkData */
                                );

                            eq -> push_back( taskTransfer );
                            eq -> wait( taskTransfer );

                            UTF_REQUIRE( taskTransfer -> isFailed() );

                            /*
                             * The guard fires before dispatch rather than after a partial
                             * write
                             */

                            UTF_REQUIRE_EQUAL( 0U, backendImpl -> saveCalls() );

                            eq -> forceFlushNoThrow();
                        }

                        UTF_REQUIRE(
                            Task::Completed !=
                                pollAcceptorState( acceptorTask, false /* waitForCompleted */, 5U )
                            );

                        /*
                         * The bystander connection must still be alive on the server and
                         * must still be able to complete a full round trip
                         */

                        UTF_REQUIRE( ! acceptor -> activeEndpoints().empty() );

                        bystander -> setChunkData( backendImpl -> getData() );
                        bystander -> setCommandId( connection_t::CommandId::SendChunk );
                        bystander -> setChunkId( chunkIdBystander );

                        eq -> push_back( taskBystander );
                        eq -> waitForSuccess( taskBystander );

                        UTF_REQUIRE_EQUAL( 1U, backendImpl -> saveCalls() );

                        /*
                         * ... and a brand new connection to the same port still succeeds
                         */

                        {
                            const auto transfer = cbConnect( eq, dataBlocksPool );
                            const auto taskTransfer = om::qi< Task >( transfer );

                            transfer -> setCommandInfo( connection_t::CommandId::NoCommand );

                            eq -> push_back( taskTransfer );
                            eq -> waitForSuccess( taskTransfer );

                            UTF_REQUIRE( transfer -> isClientVersionNegotiated() );

                            cancelAndWaitForSuccess( eq, taskTransfer );
                        }

                        UTF_REQUIRE( Task::Completed != acceptorTask -> getState() );

                        cancelAndWaitForSuccess( eq, taskBystander );
                    }
                    );
            },
            acceptor,
            "localhost"                                             /* readinessHost */,
            28100U                                                  /* readinessPort */
            );

        backendImpl -> assertions().requireNone();
    }
}

UTF_AUTO_TEST_CASE( Test_BlobServerFacadeStartStop )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;

    UTF_MESSAGE(
        "BlobServerFacade::startForSyncStorage() never runs in a normal CI run - both of its "
        "call sites open with 'if( ! UtfArgsParser::isServer() ) return;' - and neither of them "
        "ever supplies a key, a certificate or either callback, so SslBlobServerFacade is not "
        "instantiated anywhere in the repository"
        );

    /*
     * IMPORTANT: an EXPLICIT control token is passed in both sub-blocks below
     *
     * With controlTokenIn == nullptr the facade creates the token internally and never exposes
     * it, so startAcceptor() blocks until that token - or an OS signal - cancels it and the
     * case would hang forever. That defaulting arm is NOT testable through this entry point and
     * is deliberately left uncovered; the dataBlocksPoolIn arm below is the one which stays
     * coverable
     *
     * Both sub-blocks bind a port, so they hold the machine global lock and use different ports
     */

    const unsigned short portPlain = 28100U;
    const unsigned short portSsl   = 28105U;

    /*
     * A bounded wait for the facade worker to return once the control token is cancelled - the
     * regression this guards against is exactly that it does NOT return
     */

    const auto requireFacadeReturned = []( SAA_in const om::ObjPtr< Task >& serverTask ) -> void
    {
        const auto state = pollAcceptorState( serverTask, true /* waitForCompleted */ );

        if( Task::Completed != state )
        {
            UTF_FAIL( "BlobServerFacade::startForSyncStorage() did not return after the control token was cancelled" );
        }

        UTF_REQUIRE( ! serverTask -> isFailed() );
    };

    {
        /*
         * (a) The plain TCP facade with dataBlocksPoolIn == nullptr - the defaulting arm - and a
         *     non-empty isAuthenticationRequiredCallback, which is the only in-repo
         *     instantiation point of TcpBlockServerDataChunkStorageImpl's
         *     isauthenticationrequired_callback_t parameter
         */

        test::MachineGlobalTestLock lock;

        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        const auto storage = om::lockDisposable( utest::BackendImplTestImpl::createInstance() );

        std::atomic< std::size_t > isAuthRequiredCalls( 0U );

        const auto isAuthenticationRequiredCallback =
            [ &isAuthRequiredCalls ](
                SAA_in      const BlockTransferDefs::BlockType                          blockType,
                SAA_in      const std::uint16_t                                         cntrlCode
                )
                -> bool
            {
                BL_UNUSED( blockType );
                BL_UNUSED( cntrlCode );

                ++isAuthRequiredCalls;

                return false;
            };

        const auto runServer = [ & ]() -> void
        {
            BlobServerFacade::startForSyncStorage(
                test::UtfArgsParser::threadsCount(),
                0U                                                  /* maxConcurrentTasks */,
                om::copy< DataChunkStorage >( storage )             /* writeSyncStorage */,
                om::copy< DataChunkStorage >( storage )             /* readSyncStorage */,
                bl::str::empty()                                    /* privateKeyPem */,
                bl::str::empty()                                    /* certificatePem */,
                portPlain,
                controlToken,
                nullptr                                             /* dataBlocksPoolIn - the defaulting arm */,
                AsyncDataChunkStorage::datablock_callback_t()       /* authenticationCallback */,
                BlobServerFacade::isauthenticationrequired_callback_t( isAuthenticationRequiredCallback )
                );
        };

        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto serverTask = SimpleTaskImpl::createInstance< Task >( cpp::copy( runServer ) );

                eq -> push_back( serverTask );

                BL_SCOPE_EXIT(
                    {
                        controlToken -> requestCancel();
                    }
                    );

                utest::TestTaskUtils::waitForAcceptorReady( "localhost", portPlain );

                {
                    /*
                     * Drive a real command through the acceptor - a bare connector connect sends
                     * no command at all, and isAuthenticationRequiredCallback would then never
                     * be invoked
                     */

                    const auto dataBlocksPool = datablocks_pool_type::createInstance();

                    const auto eqClient = om::lockDisposable(
                        ExecutionQueueImpl::createInstance( ExecutionQueue::OptionKeepAll )
                        );

                    const auto connector = connector_t::createInstance( std::string( "localhost" ), portPlain );

                    const auto taskConnector = om::qi< Task >( connector.get() );
                    eqClient -> push_back( taskConnector );
                    eqClient -> waitForSuccess( taskConnector );

                    const auto transfer = connection_t::createInstance(
                        connection_t::CommandId::SendChunk,
                        uuids::create()                             /* peerId */,
                        dataBlocksPool
                        );

                    transfer -> attachStream( connector -> detachStream() );
                    transfer -> setChunkData( om::copy( storage -> getData() ) );
                    transfer -> setChunkId( uuids::create() );

                    const auto taskTransfer = om::qi< Task >( transfer );

                    eqClient -> push_back( taskTransfer );
                    eqClient -> wait( taskTransfer );

                    UTF_REQUIRE_EQUAL( Task::Completed, taskTransfer -> getState() );
                    UTF_REQUIRE( ! taskTransfer -> isFailed() );

                    eqClient -> forceFlushNoThrow();
                }

                /*
                 * Only meaningful because a real command was driven above
                 */

                UTF_REQUIRE( isAuthRequiredCalls.load() > 0U );

                controlToken -> requestCancel();

                requireFacadeReturned( serverTask );

                eq -> forceFlushNoThrow();
            }
            );

        storage -> assertions().requireNone();
    }

    {
        /*
         * (b) The SSL facade - the first instantiation of the SslBlobServerFacade typedef in the
         *     repository - with an explicit control token, an explicit data blocks pool, a real
         *     key and certificate and a non-empty authenticationCallback
         *
         *     No command is driven here: there is no SSL block transfer client anywhere in the
         *     test tree, so authenticationCallback is deliberately not expected to be invoked.
         *     What this sub-block pins is that the typedef instantiates, that the acceptor binds
         *     the port, and that startForSyncStorage() returns once the control token is
         *     cancelled
         */

        test::MachineGlobalTestLock lock;

        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();

        const auto storage = om::lockDisposable( utest::BackendImplTestImpl::createInstance() );

        const auto dataBlocksPool = datablocks_pool_type::createInstance();

        std::atomic< std::size_t > authenticationCalls( 0U );

        const auto authenticationCallback =
            [ &authenticationCalls ]( SAA_in const om::ObjPtr< DataBlock >& dataBlock ) -> void
            {
                BL_UNUSED( dataBlock );

                ++authenticationCalls;
            };

        const auto runServer = [ & ]() -> void
        {
            SslBlobServerFacade::startForSyncStorage(
                test::UtfArgsParser::threadsCount(),
                0U                                                  /* maxConcurrentTasks */,
                om::copy< DataChunkStorage >( storage )             /* writeSyncStorage */,
                om::copy< DataChunkStorage >( storage )             /* readSyncStorage */,
                test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
                test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
                portSsl,
                controlToken,
                om::copy( dataBlocksPool )                          /* dataBlocksPoolIn */,
                AsyncDataChunkStorage::datablock_callback_t( authenticationCallback ),
                SslBlobServerFacade::isauthenticationrequired_callback_t()
                );
        };

        scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
            {
                eq -> setOptions( ExecutionQueue::OptionKeepAll );

                const auto serverTask = SimpleTaskImpl::createInstance< Task >( cpp::copy( runServer ) );

                eq -> push_back( serverTask );

                BL_SCOPE_EXIT(
                    {
                        controlToken -> requestCancel();
                    }
                    );

                /*
                 * The readiness probe is a plain TCP connect, which is enough to prove the SSL
                 * acceptor bound the port - the handshake happens after the TCP connect
                 */

                utest::TestTaskUtils::waitForAcceptorReady( "localhost", portSsl );

                controlToken -> requestCancel();

                requireFacadeReturned( serverTask );

                eq -> forceFlushNoThrow();
            }
            );
    }
}
