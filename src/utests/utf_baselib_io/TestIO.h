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

#include <baselib/reactive/Observer.h>

#include <baselib/messaging/BrokerFacade.h>
#include <baselib/messaging/BrokerDispatchingBackendProcessing.h>
#include <baselib/messaging/BackendProcessingBase.h>
#include <baselib/messaging/AsyncDataChunkStorage.h>
#include <baselib/messaging/AsyncMessageDispatcherWrapper.h>
#include <baselib/messaging/TcpBlockTransferClient.h>
#include <baselib/messaging/TcpBlockServerDataChunkStorage.h>
#include <baselib/messaging/TcpBlockServerMessageDispatcher.h>
#include <baselib/messaging/MessagingClientBlockDispatchLocal.h>
#include <baselib/messaging/MessagingClientBlockDispatch.h>
#include <baselib/messaging/MessagingClientBlock.h>
#include <baselib/messaging/MessagingClientFactory.h>
#include <baselib/messaging/BrokerErrorCodes.h>
#include <baselib/messaging/DataChunkStorageFilesystem.h>

#include <baselib/data/eh/ServerErrorHelpers.h>

#include <baselib/crypto/ErrorHandling.h>
#include <baselib/crypto/OpenSSLTypes.h>

#include <baselib/tasks/TasksUtils.h>
#include <baselib/tasks/Algorithms.h>
#include <baselib/tasks/ExecutionQueue.h>
#include <baselib/tasks/SimpleTaskControlToken.h>

#include <baselib/transfer/SendRecvContext.h>

#include <baselib/core/OS.h>
#include <baselib/core/ThreadPool.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/Random.h>
#include <baselib/core/BaseIncludes.h>
#include <baselib/core/EndpointSelectorImpl.h>

#include <utests/baselib/MachineGlobalTestLock.h>
#include <utests/baselib/TestTaskUtils.h>
#include <utests/baselib/UtfArgsParser.h>
#include <utests/baselib/UtfCrypto.h>
#include <utests/baselib/Utf.h>

#include <atomic>
#include <set>

/************************************************************************
 * I/O code tests
 */

namespace
{
    using namespace bl;

    typedef bl::tasks::TcpBlockTransferClientConnectionImpl< bl::tasks::TcpSocketAsyncBase >        connection_t;
    typedef bl::tasks::TcpConnectionEstablisherConnectorImpl< bl::tasks::TcpSocketAsyncBase >       connector_t;
    typedef bl::tasks::TcpConnectionEstablisherConnectorImpl< bl::tasks::TcpSslSocketAsyncBase >    ssl_connector_t;

    template
    <
        typename T
    >
    void chkTaskCompletedOkOrRunning( SAA_in const om::ObjPtr< T >& task )
    {
        if( bl::tasks::Task::Completed != task -> getState() )
        {
            return;
        }

        if( task -> isFailed() )
        {
            bl::cpp::safeRethrowException( task -> exception() );
        }
    }

    /*
     * A socket free probe over the block transfer client connection
     *
     * It re-exposes the two protected members which have no reachable call site in a test:
     * validatePayload(), whose only caller is BL_ASSERT( validatePayload() ) in scheduleTask()
     * - and BL_ASSERT expands to ((void)0) under NDEBUG without evaluating its argument, so in
     * a release build the invariant does not exist at all - and chk4ServerErrorsClient(), whose
     * "error code but no ErrBit" guard is only reachable from a malformed ack on the wire
     */

    class PayloadProbe : public bl::tasks::TcpBlockTransferClientConnectionT< bl::tasks::TcpSocketAsyncBase >
    {
        typedef bl::tasks::TcpBlockTransferClientConnectionT< bl::tasks::TcpSocketAsyncBase >   base_type;

    protected:

        PayloadProbe(
            SAA_in                  const base_type::CommandId                                  commandId,
            SAA_in                  const bl::uuid_t&                                           peerId,
            SAA_in                  const bl::om::ObjPtr< bl::data::datablocks_pool_type >&     dataBlocksPool,
            SAA_in_opt              const bl::tasks::BlockTransferDefs::BlockType               blockType =
                bl::tasks::BlockTransferDefs::BlockType::Normal
            )
            :
            base_type( commandId, peerId, dataBlocksPool, blockType )
        {
        }

    public:

        using base_type::validatePayload;
        using base_type::chk4ServerErrorsClient;

        /*
         * Install the state an ack would have left in the command buffer
         */

        void setAckState(
            SAA_in                  const std::uint16_t                                         flags,
            SAA_in                  const std::uint32_t                                         errorCode
            ) NOEXCEPT
        {
            base_type::m_cmdBuffer.flags = flags;
            base_type::m_cmdBuffer.errorCode = errorCode;
        }
    };

    typedef bl::om::ObjectImpl< PayloadProbe > PayloadProbeImpl;

    /*
     * Raw socket framing helpers for the blob transfer protocol
     *
     * These are the minimal client side of the wire protocol and are used by the tests which
     * have to send frames the shipped client will never emit - a command before the protocol
     * version was negotiated, a version newer than the server's, an out of range block type
     *
     * Note that the framing duplicates CommandBlock's layout deliberately: if its size ever
     * changes these tests break loudly instead of silently desynchronizing a stream
     */

    template
    <
        typename STREAM
    >
    void sendCommand(
        SAA_inout           STREAM&                                                     stream,
        SAA_in              bl::tasks::detail::CommandBlock                             command
        )
    {
        command.host2Network();

        bl::asio::write( stream, bl::asio::buffer( &command, sizeof( command ) ) );
    }

    template
    <
        typename STREAM
    >
    auto recvCommand( SAA_inout STREAM& stream ) -> bl::tasks::detail::CommandBlock
    {
        bl::tasks::detail::CommandBlock command;

        bl::asio::read( stream, bl::asio::buffer( &command, sizeof( command ) ) );

        command.network2Host();

        return command;
    }

    template
    <
        typename Acceptor
    >
    void basicAcceptorTest()
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::tasks;
        using namespace utest;

        typedef typename Acceptor::async_wrapper_t                                              async_wrapper_t;
        typedef typename async_wrapper_t::backend_interface_t                                   backend_interface_t;

        test::MachineGlobalTestLock lock;

        {
            const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
            const auto dataBlocksPool = datablocks_pool_type::createInstance();
            const auto backendImpl = BackendImplTestImpl::createInstance();

            const auto asyncWrapper = om::lockDisposable(
                async_wrapper_t::template createInstance<>(
                    om::qi< backend_interface_t >( backendImpl ) /* writeBackend */,
                    om::qi< backend_interface_t >( backendImpl ) /* readBackend */,
                    test::UtfArgsParser::threadsCount(),
                    om::qi< TaskControlToken >( controlToken ),
                    0U /* maxConcurrentTasks */,
                    dataBlocksPool
                    )
                );

            {
                const auto acceptor = Acceptor::template createInstance<>(
                    controlToken,
                    dataBlocksPool,
                    "localhost",
                    1234,
                    bl::str::empty() /* privateKeyPem */,
                    bl::str::empty() /* certificatePem */,
                    asyncWrapper
                    );

                UTF_REQUIRE( acceptor );

                {
                    const auto i = om::qi< tasks::Task >( acceptor );
                    UTF_REQUIRE( i );
                }
            }
        }
    }

    template
    <
        typename Acceptor,
        typename Connector
    >
    void simpleConnectAndTransmitDataTest(
        SAA_in      const bool          startConnector = false,
        SAA_in      const bool          isAuthenticationRequired = false
        )
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::tasks;
        using namespace utest;

        typedef typename Acceptor::async_wrapper_t                                              async_wrapper_t;
        typedef typename async_wrapper_t::backend_interface_t                                   backend_interface_t;
        typedef bl::tasks::TcpBlockTransferClientConnectionImpl< typename Acceptor::stream_t >  connection_t;

        test::MachineGlobalTestLock lock;

        tasks::scheduleAndExecuteInParallel(
            [ &startConnector, &isAuthenticationRequired ](
                SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq
                ) -> void
            {
                const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
                const auto dataBlocksPool = datablocks_pool_type::createInstance();
                const auto backendImpl = BackendImplTestImpl::createInstance();

                const auto backend = om::lockDisposable(
                    async_wrapper_t::template createInstance<>(
                        om::qi< backend_interface_t >( backendImpl ) /* writeBackend */,
                        om::qi< backend_interface_t >( backendImpl ) /* readBackend */,
                        test::UtfArgsParser::threadsCount(),
                        om::qi< TaskControlToken >( controlToken ),
                        0U /* maxConcurrentTasks */,
                        dataBlocksPool
                        )
                    );

                {
                    auto isAuthenticationRequiredCallback = [ isAuthenticationRequired ](
                        SAA_in      const typename BlockTransferDefs::BlockType         blockType,
                        SAA_in      const typename std::uint16_t                        cntrlCode
                        ) -> bool
                    {
                        auto result = false;

                        if( isAuthenticationRequired && blockType == BlockTransferDefs::BlockType::Normal )
                        {
                            switch( cntrlCode )
                            {
                                case tasks::detail::CommandBlock::CntrlCodePutDataBlock:
                                case tasks::detail::CommandBlock::CntrlCodeRemoveDataBlock:
                                    result = true;
                                    break;

                                default:
                                    break;
                            }
                        }

                        return result;
                    };

                    const bl::uuid_t remotePeerId = uuids::create();

                    const auto acceptor = Acceptor::template createInstance< Acceptor >(
                        controlToken,
                        dataBlocksPool,
                        "localhost",
                        28100,
                        test::UtfCrypto::getDefaultServerKey()          /* privateKeyPem */,
                        test::UtfCrypto::getDefaultServerCertificate()  /* certificatePem */,
                        backend,
                        remotePeerId,
                        std::move( isAuthenticationRequiredCallback )
                        );

                    UTF_REQUIRE( acceptor );

                    /*
                     * Start the acceptor and sleep for a couple of seconds to give it a chance to start
                     */

                    const auto taskAcceptor = om::qi< tasks::Task >( acceptor );
                    eq -> push_back( taskAcceptor );

                    try
                    {
                        os::sleep( time::milliseconds( 2000 ) );

                        if( startConnector )
                        {
                            typedef tasks::detail::CommandBlock CommandBlock;

                            const auto connector = Connector::template createInstance< Connector >( "localhost", 28100 );
                            const auto taskConnector = om::qi< tasks::Task >( connector.get() );
                            eq -> push_back( taskConnector );
                            eq -> waitForSuccess( taskConnector );

                            /*
                             * Wait for the server connection to be established
                             */

                            std::size_t retries = 0;
                            const std::size_t maxRetries = 30;
                            om::ObjPtr< typename Acceptor::connection_t > serverConnection;

                            for( ;; )
                            {
                                os::sleep( time::seconds( 1 ) );

                                chkTaskCompletedOkOrRunning( acceptor );
                                const auto serverEndpoints = acceptor -> activeEndpoints();

                                if( serverEndpoints.size() )
                                {
                                    UTF_REQUIRE_EQUAL( serverEndpoints.size(), 1U );

                                    serverConnection =
                                        om::qi< typename Acceptor::connection_t >( serverEndpoints.back() );
                                    chkTaskCompletedOkOrRunning( serverConnection );

                                    break;
                                }

                                if( retries > maxRetries )
                                {
                                    UTF_FAIL( "Connection with server can't be established" );
                                }

                                ++retries;
                            }

                            const auto isServerConnectionAuthenticated = [ & ]() -> bool
                            {
                                return serverConnection -> isClientAuthenticated();
                            };

                            UTF_REQUIRE_EQUAL( serverConnection -> peerId(), remotePeerId );
                            UTF_REQUIRE_EQUAL( serverConnection -> remotePeerId(), uuids::nil() );

                            /*
                             * Test the isExpectedException logic first
                             */

                            {
                                typedef TcpBlockTransferClientConnectionT< typename Acceptor::stream_t >
                                    connection_base_t;

                                class LocalClientConnection : public connection_base_t
                                {
                                    /*
                                     * clang-cl does not resolve a typedef from the enclosing function
                                     * scope when it is used as a member-initializer name, so the base
                                     * class is named through this class-scope alias in the constructor
                                     * below. clang does not count that member-initializer use as a use
                                     * of the alias and would report it unused under -WX, so the warning
                                     * is suppressed for just this declaration (cl.exe never sees the
                                     * clang pragma); this replaces the former global -Wno-unused-local-typedef.
                                     */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-local-typedef"
#endif
                                    using base_type = connection_base_t;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

                                protected:

                                    LocalClientConnection(
                                        SAA_in const om::ObjPtr< data::datablocks_pool_type >& dataBlocksPool
                                        )
                                        :
                                        base_type(
                                            base_type::CommandId::NoCommand,
                                            uuids::create() /* peerId */,
                                            dataBlocksPool
                                        )
                                    {
                                    }

                                public:

                                    void checkExpectedException(
                                        SAA_in                  const std::exception_ptr&                       eptr,
                                        SAA_in                  const std::exception&                           exception,
                                        SAA_in_opt              const eh::error_code*                           ec
                                        )
                                    {
                                        UTF_REQUIRE( this -> isExpectedException( eptr, exception, ec ) );
                                    }
                                };

                                typedef om::ObjectImpl< LocalClientConnection > local_connection_t;

                                const auto localConnection = local_connection_t::createInstance( dataBlocksPool );

                                /*
                                 * Test some error codes which are expected to be filtered out
                                 */

                                /*
                                 * On Windows the error codes for connection reset and aborted are different
                                 * and we need to handle these separately
                                 *
                                 * The values for WSAECONNRESET (10054), WSAECONNABORTED (10053) and
                                 * WSAETIMEDOUT (10060) are from here:
                                 * http://msdn.microsoft.com/en-us/library/windows/desktop/ms740668%28v=vs.85%29.aspx
                                 */

                                const auto ecConnectionReset =
                                    os::onUNIX() ?  eh::errc::connection_reset : 10054 /* WSAECONNRESET */;
                                const auto ecBrokenPipe =
                                    os::onUNIX() ?  eh::errc::broken_pipe : 10053 /* WSAECONNABORTED */;
                                const auto ecTimedOut =
                                    os::onUNIX() ?  eh::errc::timed_out : 10060 /* WSAETIMEDOUT */;
                                const auto ecHostUnreachable =
                                    os::onUNIX() ?  eh::errc::host_unreachable : 10065 /* WSAEHOSTUNREACH */;

                                UnexpectedException exception;

                                {
                                    const auto ec = eh::error_code( ecConnectionReset, eh::system_category() );

                                    if( os::onLinux() )
                                    {
                                        UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:104" ) );                                    }
                                    else
                                    {
                                        UTF_REQUIRE_EQUAL(
                                            eh::errorCodeToString( ec ),
                                            os::onUNIX() ? std::string( "system:54" ) : std::string( "system:10054" )
                                            );
                                    }

                                    localConnection -> checkExpectedException( nullptr /* eptr */, exception, &ec );
                                }

                                {
                                    const auto ec = eh::error_code( ecBrokenPipe, eh::system_category() );

                                    UTF_REQUIRE_EQUAL(
                                        eh::errorCodeToString( ec ),
                                        os::onUNIX() ? std::string( "system:32" ) : std::string( "system:10053" )
                                        );

                                    localConnection -> checkExpectedException( nullptr /* eptr */, exception, &ec );
                                }

                                {
                                    const auto ec = eh::error_code( ecTimedOut, eh::system_category() );

                                    if( os::onLinux() )
                                    {
                                        UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:110" ) );                                    }
                                    else
                                    {
                                        UTF_REQUIRE_EQUAL(
                                            eh::errorCodeToString( ec ),
                                            os::onUNIX() ? std::string( "system:60" ) : std::string( "system:10060" )
                                            );
                                    }

                                    localConnection -> checkExpectedException( nullptr /* eptr */, exception, &ec );
                                }

                                {
                                    const auto ec = eh::error_code( ecHostUnreachable, eh::system_category() );

                                    if( os::onLinux() )
                                    {
                                        UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:113" ) );                                    }
                                    else
                                    {
                                        UTF_REQUIRE_EQUAL(
                                            eh::errorCodeToString( ec ),
                                            os::onUNIX() ? std::string( "system:65" ) : std::string( "system:10065" )
                                            );
                                    }

                                    localConnection -> checkExpectedException( nullptr /* eptr */, exception, &ec );
                                }
                            }

                            /*
                             * Now create a client connection and negotiate the client version
                             *
                             * This will trigger the update of the remotePeerId() on both ends
                             */

                            const bl::uuid_t peerId = uuids::create();

                            const auto transfer =
                                connection_t::createInstance(
                                    connection_t::CommandId::NoCommand,
                                    peerId,
                                    dataBlocksPool
                                    );

                            UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                            UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), uuids::nil() );
                            UTF_REQUIRE_EQUAL( transfer -> targetPeerId(), uuids::nil() );

                            transfer -> attachStream( connector -> detachStream() );

                            const auto taskTransfer = om::qi< tasks::Task >( transfer.get() );
                            eq -> push_back( taskTransfer );
                            eq -> waitForSuccess( taskTransfer );

                            auto guard = BL_SCOPE_GUARD(
                                {
                                    if( connection_t::isProtocolHandshakeNeeded )
                                    {
                                        eq -> wait( taskTransfer );

                                        UTF_REQUIRE( transfer -> isShutdownNeeded() );

                                        transfer -> setCommandId( connection_t::CommandId::NoCommand );
                                        transfer -> protocolOperationsOnly( true );

                                        eq -> push_back( taskTransfer );
                                        eq -> waitForSuccess( taskTransfer );

                                        UTF_REQUIRE( transfer -> hasShutdownCompletedSuccessfully() );
                                        UTF_REQUIRE( ! transfer -> isShutdownNeeded() );
                                    }
                                }
                                );

                            UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                            UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), remotePeerId );
                            UTF_REQUIRE_EQUAL( transfer -> targetPeerId(), uuids::nil() );
                            UTF_REQUIRE_EQUAL( serverConnection -> peerId(), remotePeerId );
                            UTF_REQUIRE_EQUAL( serverConnection -> remotePeerId(), peerId );

                            /*
                             * Verify that resetting the version also resets the remotePeerId() and
                             * then after re-negotiating the version again the remotePeerId() is
                             * again obtained correctly
                             */

                            transfer -> clientVersion( CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1 );
                            UTF_REQUIRE( ! transfer -> isClientVersionNegotiated() );
                            UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                            UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), uuids::nil() );
                            UTF_REQUIRE_EQUAL( transfer -> targetPeerId(), uuids::nil() );

                            transfer -> setCommandInfo( connection_t::CommandId::NoCommand );
                            eq -> push_back( taskTransfer );
                            eq -> waitForSuccess( taskTransfer );
                            UTF_REQUIRE( transfer -> isClientVersionNegotiated() );
                            UTF_REQUIRE_EQUAL( transfer -> clientVersion(), CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1 );

                            UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                            UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), remotePeerId );
                            UTF_REQUIRE_EQUAL( transfer -> targetPeerId(), uuids::nil() );
                            UTF_REQUIRE_EQUAL( serverConnection -> peerId(), remotePeerId );
                            UTF_REQUIRE_EQUAL( serverConnection -> remotePeerId(), peerId );

                            const auto authenticateBackend = [ & ]() -> void
                                {
                                    backend -> authenticationCallback(
                                        [ &backendImpl ]( SAA_in const om::ObjPtr< data::DataBlock >& authenticationToken )
                                            -> om::ObjPtr< data::DataBlock >
                                        {
                                            if( ! BackendImplTestImpl::areBlocksEqual( authenticationToken, backendImpl -> getData() ) )
                                            {
                                                BL_THROW_EC(
                                                    eh::errc::make_error_code( eh::errc::permission_denied ),
                                                    BL_MSG()
                                                        << "Authentication failed"
                                                    );
                                            }

                                            return om::copy( authenticationToken );
                                        }
                                        );

                                    transfer -> clientVersion( CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2 );
                                    transfer -> setCommandInfo(
                                        connection_t::CommandId::SendChunk,
                                        uuids::nil() /* chunkId */,
                                        nullptr /* chunkData */,
                                        BlockTransferDefs::BlockType::Authentication
                                        );

                                    transfer -> setChunkData( backendImpl -> getData() );
                                    eq -> push_back( taskTransfer );
                                    eq -> waitForSuccess( taskTransfer );
                                };

                            /*
                             * Now verify send/recv/remove/flush commands
                             */

                            const auto runSendRecvRemoveFlush = [ & ]( SAA_in const bool storageEnabled ) -> void
                            {
                                transfer -> setChunkData( backendImpl -> getData() );

                                UTF_REQUIRE( 0U == backendImpl -> loadCalls() );
                                UTF_REQUIRE( 0U == backendImpl -> saveCalls() );
                                UTF_REQUIRE( 0U == backendImpl -> removeCalls() );
                                UTF_REQUIRE( 0U == backendImpl -> flushCalls() );

                                backendImpl -> assertions().requireNone();

                                if( isAuthenticationRequired && ! isServerConnectionAuthenticated() )
                                {
                                    /*
                                     * The commands which the 'is authentication required' callback
                                     * selects must be rejected by the server while the connection is
                                     * not authenticated yet - the rejection must be a recoverable per
                                     * command error which never reaches the backend and which leaves
                                     * the connection usable for the commands which follow
                                     */

                                    const auto testRejectedCommand = [ & ]() -> void
                                    {
                                        eq -> push_back( taskTransfer );

                                        try
                                        {
                                            eq -> waitForSuccess( taskTransfer );
                                            UTF_FAIL( "This is expected to throw" );
                                        }
                                        catch( bl::ServerErrorException& e )
                                        {
                                            BL_LOG_MULTILINE(
                                                bl::Logging::debug(),
                                                BL_MSG()
                                                    << "Expected permission denied exception:\n"
                                                    << bl::eh::diagnostic_information( e )
                                                );

                                            const auto* errNo = eh::get_error_info< eh::errinfo_errno >( e );
                                            const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );
                                            UTF_REQUIRE( errNo && ec );

                                            eh::error_code ecExpected( *errNo, eh::generic_category() );

                                            UTF_REQUIRE_EQUAL( *ec, ecExpected );

                                            UTF_REQUIRE_EQUAL(
                                                *ec,
                                                eh::errc::make_error_code( eh::errc::permission_denied )
                                                );
                                        }

                                        UTF_REQUIRE( ! isServerConnectionAuthenticated() );
                                    };

                                    /*
                                     * An unauthenticated put request must be rejected
                                     */

                                    transfer -> setCommandInfo(
                                        connection_t::CommandId::SendChunk,
                                        uuids::create() /* chunkId */,
                                        nullptr /* chunkData */,
                                        BlockTransferDefs::BlockType::Normal
                                        );
                                    transfer -> setChunkData( backendImpl -> getData() );

                                    testRejectedCommand();

                                    /*
                                     * ... and so must an unauthenticated remove request
                                     */

                                    transfer -> detachChunkData();

                                    transfer -> setCommandInfo(
                                        connection_t::CommandId::RemoveChunk,
                                        uuids::create() /* chunkId */,
                                        nullptr /* chunkData */,
                                        BlockTransferDefs::BlockType::Normal
                                        );

                                    testRejectedCommand();

                                    /*
                                     * Neither of the rejected commands should have reached the backend
                                     */

                                    UTF_REQUIRE_EQUAL( 0U, backendImpl -> saveCalls() );
                                    UTF_REQUIRE_EQUAL( 0U, backendImpl -> removeCalls() );

                                    /*
                                     * The gate is selective - the callback does not require authentication
                                     * for the peer sessions flush request, so this command must still be
                                     * executed normally on an unauthenticated connection
                                     */

                                    transfer -> detachChunkData();

                                    transfer -> setCommandInfo(
                                        connection_t::CommandId::FlushPeerSessions,
                                        uuids::nil() /* chunkId */,
                                        nullptr /* chunkData */,
                                        BlockTransferDefs::BlockType::Normal
                                        );

                                    eq -> push_back( taskTransfer );
                                    UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( taskTransfer ) );

                                    UTF_REQUIRE_EQUAL( 1U, backendImpl -> flushCalls() );

                                    UTF_REQUIRE( ! isServerConnectionAuthenticated() );

                                    backendImpl -> assertions().requireNone();

                                    /*
                                     * Restore the state which the code below expects - i.e. the backend
                                     * call counters at zero and the chunk data attached to the task
                                     */

                                    backendImpl -> resetStats();

                                    transfer -> setChunkData( backendImpl -> getData() );
                                }

                                if( isAuthenticationRequired )
                                {
                                    authenticateBackend();
                                }

                                UTF_REQUIRE_EQUAL( isAuthenticationRequired, isServerConnectionAuthenticated() );

                                const auto getExpected = [ & ]( SAA_in const std::size_t expected ) -> std::size_t
                                {
                                    return storageEnabled ? expected : 0U;
                                };

                                const auto blockType =
                                    storageEnabled ?
                                        BlockTransferDefs::BlockType::Normal :
                                        BlockTransferDefs::BlockType::TransferOnly;

                                const auto verifyBlock =
                                    [ & ]( SAA_in const om::ObjPtr< data::DataBlock >& dataBlock ) -> void
                                {
                                    if( blockType != BlockTransferDefs::BlockType::TransferOnly )
                                    {
                                        return;
                                    }

                                    /*
                                     * This is a TransferOnly block and its size should equal to
                                     * capacity and the content should be the secure fill byte
                                     */

                                    UTF_REQUIRE_EQUAL( dataBlock -> size(), dataBlock -> capacity() );

                                    const auto size = dataBlock -> size();
                                    const auto* data = dataBlock -> begin();

                                    bool blockIsValid = true;

                                    for( std::size_t i = 0; i < size; ++i )
                                    {
                                        if( data[ i ] != async_wrapper_t::SECURE_BLOCKS_FILL_BYTE )
                                        {
                                            blockIsValid = false;
                                            break;
                                        }
                                    }

                                    UTF_REQUIRE( blockIsValid );
                                };

                                if( BlockTransferDefs::BlockType::TransferOnly == blockType )
                                {
                                    /*
                                     * A TransferOnly PUT announces its chunk size, so the server's
                                     * 'no load operation to set the size' override in
                                     * onChunkAllocated() must not be applied on this path - if it
                                     * were, the server would wait for capacity() bytes while the
                                     * peer sends only the size it announced and both sides would
                                     * hang
                                     *
                                     * The full capacity block the rest of this lambda sends cannot
                                     * tell the two branches apart, because its size *is* its
                                     * capacity; a partial block can
                                     */

                                    const auto smallBlock = data::DataBlock::get( dataBlocksPool );

                                    smallBlock -> setSize( 1024U );

                                    UTF_REQUIRE( smallBlock -> size() < smallBlock -> capacity() );

                                    const auto blocksTransferredBefore = transfer -> noOfBlocksTransferred();

                                    transfer -> setCommandInfo(
                                        connection_t::CommandId::SendChunk,
                                        uuids::create() /* chunkId - forced to chunkIdDefault() for a non-Normal block */,
                                        om::copy( smallBlock ),
                                        blockType
                                        );

                                    eq -> push_back( taskTransfer );

                                    /*
                                     * The regression signature is a hang, so this must be a bounded
                                     * wait - eq -> waitForSuccess( ... ) would block forever and the
                                     * whole test module would time out with no diagnostic
                                     */

                                    const std::size_t maxWaitInSeconds = 30U;

                                    std::size_t waited = 0U;

                                    for( ; waited < maxWaitInSeconds; ++waited )
                                    {
                                        if( tasks::Task::Completed == taskTransfer -> getState() )
                                        {
                                            break;
                                        }

                                        os::sleep( time::seconds( 1 ) );
                                    }

                                    if( tasks::Task::Completed != taskTransfer -> getState() )
                                    {
                                        taskTransfer -> requestCancel();
                                        eq -> wait( taskTransfer );

                                        UTF_FAIL(
                                            "TransferOnly PUT of a partial block did not complete - "
                                            "the server overrode the announced chunk size"
                                            );
                                    }

                                    if( taskTransfer -> isFailed() )
                                    {
                                        cpp::safeRethrowException( taskTransfer -> exception() );
                                    }

                                    UTF_REQUIRE( ! taskTransfer -> isFailed() );

                                    /*
                                     * A TransferOnly PUT is a SecureDiscard - it must never reach
                                     * the storage backend - but the client does count it as a
                                     * transferred block (unlike an Authentication one)
                                     */

                                    UTF_REQUIRE_EQUAL( 0U, backendImpl -> saveCalls() );

                                    UTF_REQUIRE_EQUAL(
                                        transfer -> noOfBlocksTransferred(),
                                        blocksTransferredBefore + 1U
                                        );

                                    /*
                                     * Release the partial block and restore the state the rest of
                                     * this lambda expects - the full capacity block attached at
                                     * the top of it
                                     */

                                    transfer -> detachChunkData();
                                    transfer -> setChunkData( backendImpl -> getData() );
                                }

                                /*
                                 * Send the data twice in a row and then request flush
                                 *
                                 * For the TransferOnly configuration the first send below also
                                 * proves that the stream is still in sync after the partial one
                                 * above
                                 */

                                transfer -> setCommandId( connection_t::CommandId::SendChunk );
                                transfer -> setBlockType( blockType );
                                transfer -> setChunkId( uuids::create() );
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 1U ) == backendImpl -> saveCalls() );

                                transfer -> setCommandId( connection_t::CommandId::SendChunk );
                                transfer -> setBlockType( blockType );
                                transfer -> setChunkId( uuids::create() );
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> saveCalls() );

                                transfer -> setCommandId( connection_t::CommandId::FlushPeerSessions );
                                transfer -> setBlockType( blockType );
                                transfer -> setChunkId( uuids::nil() );
                                transfer -> detachChunkData();
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 1U ) == backendImpl -> flushCalls() );

                                /*
                                 * Let's now receive data twice in a row
                                 */

                                transfer -> setCommandId( connection_t::CommandId::ReceiveChunk );
                                transfer -> setBlockType( blockType );
                                transfer -> detachChunkData();
                                transfer -> setChunkId( uuids::create() );
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 1U ) == backendImpl -> loadCalls() );
                                verifyBlock( transfer -> getChunkData() );

                                transfer -> setCommandId( connection_t::CommandId::ReceiveChunk );
                                transfer -> setBlockType( blockType );
                                transfer -> detachChunkData();
                                transfer -> setChunkId( uuids::create() );
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> loadCalls() );
                                verifyBlock( transfer -> getChunkData() );

                                /*
                                 * Let's now delete data twice in a row and flush again
                                 */

                                transfer -> setCommandId( connection_t::CommandId::RemoveChunk );
                                transfer -> setBlockType( blockType );
                                transfer -> detachChunkData();
                                transfer -> setChunkId( uuids::create() );
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 1U ) == backendImpl -> removeCalls() );

                                transfer -> setCommandId( connection_t::CommandId::RemoveChunk );
                                transfer -> setBlockType( blockType );
                                transfer -> detachChunkData();
                                transfer -> setChunkId( uuids::create() );
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> removeCalls() );

                                transfer -> setCommandId( connection_t::CommandId::FlushPeerSessions );
                                transfer -> setBlockType( blockType );
                                transfer -> setChunkId( uuids::nil() );
                                transfer -> detachChunkData();
                                eq -> push_back( taskTransfer );
                                eq -> waitForSuccess( taskTransfer );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> flushCalls() );

                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> loadCalls() );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> saveCalls() );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> removeCalls() );
                                UTF_REQUIRE( getExpected( 2U ) == backendImpl -> flushCalls() );

                                backendImpl -> assertions().requireNone();

                                UTF_REQUIRE_EQUAL( isAuthenticationRequired, isServerConnectionAuthenticated() );
                            };

                            runSendRecvRemoveFlush( true /* storageEnabled */ );

                            /*
                             * Let's test the V2 protocol enable/disable logic for the new commands
                             */

                            if( ! isAuthenticationRequired )
                            {
                                UTF_REQUIRE_EQUAL(
                                    CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1,
                                    transfer -> clientVersion()
                                    );

                                UTF_REQUIRE_THROW_MESSAGE(
                                    transfer -> setBlockType( BlockTransferDefs::BlockType::TransferOnly ),
                                    ArgumentException,
                                    "This block type requires V2 of the blob server protocol"
                                    );

                                UTF_REQUIRE_THROW_MESSAGE(
                                    transfer -> setCommandInfo(
                                        connection_t::CommandId::SendChunk,
                                        uuids::nil() /* chunkId */,
                                        nullptr /* chunkData */,
                                        BlockTransferDefs::BlockType::Authentication
                                        ),
                                    ArgumentException,
                                    "This block type requires V2 of the blob server protocol"
                                    );

                                UTF_REQUIRE_THROW_MESSAGE(
                                    transfer -> setCommandInfoRawPtr(
                                        connection_t::CommandId::SendChunk,
                                        uuids::nil() /* chunkId */,
                                        nullptr /* dataRawPtr */,
                                        BlockTransferDefs::BlockType::Authentication
                                        ),
                                    ArgumentException,
                                    "This block type requires V2 of the blob server protocol"
                                    );

                                transfer -> clientVersion(
                                    CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2
                                    );
                            }

                            UTF_REQUIRE_EQUAL(
                                CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2,
                                transfer -> clientVersion()
                                );

                            /*
                             * Test disabling backend and then the TransferOnly block types
                             */

                            const auto testDisabledCommand = [ & ]( SAA_in const cpp::void_callback_t& callback )
                            {
                                try
                                {
                                    callback();
                                    UTF_FAIL( "This is expected to throw" );
                                }
                                catch( bl::SystemException& e )
                                {
                                    /*
                                     * This is now expected to throw because the backend is disabled
                                     */

                                    BL_LOG_MULTILINE(
                                        bl::Logging::debug(),
                                        BL_MSG()
                                            << "Expected not permitted exception:\n"
                                            << bl::eh::diagnostic_information( e )
                                        );

                                    const auto* errNo = eh::get_error_info< eh::errinfo_errno >( e );
                                    const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );
                                    UTF_REQUIRE( errNo && ec );

                                    eh::error_code ecExpected( *errNo, eh::generic_category() );

                                    UTF_REQUIRE_EQUAL( *ec, ecExpected );
                                    UTF_REQUIRE_EQUAL( *ec, eh::errc::make_error_code( eh::errc::operation_not_permitted ) );
                                }
                            };

                            backendImpl -> resetStats();
                            backendImpl -> setStorageDisabled( true );

                            testDisabledCommand(
                                [ & ]() -> void
                                {
                                    backendImpl -> load(
                                        uuids::nil()                    /* peerId */,
                                        uuids::create()                 /* chunkId */,
                                        backendImpl -> getData()        /* data */
                                        );
                                }
                                );

                            testDisabledCommand(
                                [ & ]() -> void
                                {
                                    backendImpl -> save(
                                        uuids::nil()                    /* peerId */,
                                        uuids::create()                 /* chunkId */,
                                        backendImpl -> getData()        /* data */
                                        );
                                }
                                );

                            testDisabledCommand(
                                [ & ]() -> void
                                {
                                    backendImpl -> remove(
                                        uuids::nil()                    /* peerId */,
                                        uuids::create()                 /* chunkId */
                                        );
                                }
                                );

                            testDisabledCommand(
                                [ & ]() -> void
                                {
                                    backendImpl -> flushPeerSessions( uuids::nil() /* peerId */ );
                                }
                                );

                            UTF_REQUIRE( 0U == backendImpl -> loadCalls() );
                            UTF_REQUIRE( 0U == backendImpl -> saveCalls() );
                            UTF_REQUIRE( 0U == backendImpl -> removeCalls() );
                            UTF_REQUIRE( 0U == backendImpl -> flushCalls() );

                            runSendRecvRemoveFlush( false /* storageEnabled */ );

                            UTF_REQUIRE( 0U == backendImpl -> loadCalls() );
                            UTF_REQUIRE( 0U == backendImpl -> saveCalls() );
                            UTF_REQUIRE( 0U == backendImpl -> removeCalls() );
                            UTF_REQUIRE( 0U == backendImpl -> flushCalls() );

                            backendImpl -> assertions().requireNone();

                            /*
                             * Re-enable the backend layer and then test the normal block types again
                             */

                            backendImpl -> resetStats();
                            backendImpl -> setStorageDisabled( false );

                            runSendRecvRemoveFlush( true /* storageEnabled */ );

                            if( isAuthenticationRequired )
                            {
                                backend -> authenticationCallback( typename async_wrapper_t::datablock_callback_t() );
                            }

                            /*
                             * Let's test client authentication here
                             */

                            transfer -> setCommandInfo(
                                connection_t::CommandId::SendChunk,
                                uuids::nil() /* chunkId */,
                                nullptr /* chunkData */,
                                BlockTransferDefs::BlockType::Authentication
                                );
                            transfer -> setChunkData( backendImpl -> getData() );

                            eq -> push_back( taskTransfer );

                            try
                            {
                                eq -> waitForSuccess( taskTransfer );
                                UTF_FAIL( "This is expected to throw" );
                            }
                            catch( bl::ServerErrorException& e )
                            {
                                UTF_REQUIRE( ! isServerConnectionAuthenticated() );

                                BL_LOG_MULTILINE(
                                    bl::Logging::debug(),
                                    BL_MSG()
                                        << "Expected authentication exception:\n"
                                        << bl::eh::diagnostic_information( e )
                                    );

                                const auto* errNo = eh::get_error_info< eh::errinfo_errno >( e );
                                const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );
                                UTF_REQUIRE( errNo && ec );

                                eh::error_code ecExpected( *errNo, eh::generic_category() );

                                UTF_REQUIRE_EQUAL( *ec, ecExpected );
                                UTF_REQUIRE_EQUAL( *ec, eh::errc::make_error_code( eh::errc::function_not_supported ) );
                            }

                            UTF_REQUIRE( ! isServerConnectionAuthenticated() );

                            const auto testFailedAuthentication = [ & ]( SAA_in const eh::error_code& ecThrown ) -> void
                            {
                                backend -> authenticationCallback(
                                    [ & ]( SAA_in const om::ObjPtr< data::DataBlock >& authenticationToken )
                                        -> om::ObjPtr< data::DataBlock >
                                    {
                                        if(
                                            BackendImplTestImpl::areBlocksEqual(
                                                authenticationToken,
                                                backendImpl -> getData()
                                                )
                                            )
                                        {
                                            if( ecThrown )
                                            {
                                                BL_THROW_EC(
                                                    ecThrown,
                                                    BL_MSG()
                                                        << "Authentication failed"
                                                    );
                                            }
                                            else
                                            {
                                                BL_THROW(
                                                    UnexpectedException(),
                                                    BL_MSG()
                                                        << "Unexpected exception during authentication call"
                                                    );
                                            }
                                        }

                                        return om::copy( authenticationToken );
                                    }
                                    );

                                transfer -> setCommandInfo(
                                    connection_t::CommandId::SendChunk,
                                    uuids::nil() /* chunkId */,
                                    nullptr /* chunkData */,
                                    BlockTransferDefs::BlockType::Authentication
                                    );
                                transfer -> setChunkData( backendImpl -> getData() );
                                eq -> push_back( taskTransfer );

                                try
                                {
                                    eq -> waitForSuccess( taskTransfer );
                                    UTF_FAIL( "This is expected to throw" );
                                }
                                catch( bl::ServerErrorException& e )
                                {
                                    BL_LOG_MULTILINE(
                                        bl::Logging::debug(),
                                        BL_MSG()
                                            << "Expected authentication exception:\n"
                                            << bl::eh::diagnostic_information( e )
                                        );

                                    const auto* errNo = eh::get_error_info< eh::errinfo_errno >( e );
                                    const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );
                                    UTF_REQUIRE( errNo && ec );

                                    eh::error_code ecExpected( *errNo, eh::generic_category() );

                                    /*
                                     * If the exception thrown has an error code it should be transmitted
                                     * to the client code properly
                                     *
                                     * If the exception thrown does not have an error code we should still
                                     * get eh::errc::permission_denied error code on the client side
                                     */

                                    if( ecThrown )
                                    {
                                        UTF_REQUIRE_EQUAL( *ec, ecThrown );
                                        UTF_REQUIRE_EQUAL( ecExpected, ecThrown );
                                    }
                                    else
                                    {
                                        const auto ecPermissionDenied =
                                            eh::errc::make_error_code( eh::errc::permission_denied );

                                        UTF_REQUIRE_EQUAL( *ec, ecPermissionDenied );
                                        UTF_REQUIRE_EQUAL( ecExpected, ecPermissionDenied );
                                    }
                                }

                                UTF_REQUIRE( ! isServerConnectionAuthenticated() );
                            };

                            testFailedAuthentication( eh::error_code() );
                            testFailedAuthentication( eh::errc::make_error_code( eh::errc::permission_denied ) );
                            testFailedAuthentication( eh::errc::make_error_code( eh::errc::filename_too_long ) );

                            authenticateBackend();

                            /*
                             * The channel should now be authenticated!
                             */

                            UTF_REQUIRE( isServerConnectionAuthenticated() );

                            /*
                             * Scheduling and executing CommandId::NoCommand command it should work and it
                             * should just re-negotiate the version again (and serve as ping basically)
                             */

                            transfer -> clientVersion( CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1 );
                            UTF_REQUIRE( ! transfer -> isClientVersionNegotiated() );
                            transfer -> setCommandInfo( connection_t::CommandId::NoCommand );
                            eq -> push_back( taskTransfer );
                            eq -> waitForSuccess( taskTransfer );
                            UTF_REQUIRE( transfer -> isClientVersionNegotiated() );
                            UTF_REQUIRE_EQUAL( transfer -> clientVersion(), CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1 );

                            transfer -> clientVersion( CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2 );
                            UTF_REQUIRE( ! transfer -> isClientVersionNegotiated() );
                            transfer -> setCommandInfo( connection_t::CommandId::NoCommand );
                            eq -> push_back( taskTransfer );
                            eq -> waitForSuccess( taskTransfer );
                            UTF_REQUIRE( transfer -> isClientVersionNegotiated() );
                            UTF_REQUIRE_EQUAL( transfer -> clientVersion(), CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2 );

                            guard.runNow();
                        }
                    }
                    catch( std::exception& )
                    {
                        BL_WARN_NOEXCEPT_BEGIN()

                        /*
                         * Shutdown the acceptor gracefully before we
                         * rethrow the exception
                         */

                        cancelAndWaitForSuccess( eq, taskAcceptor );

                        BL_WARN_NOEXCEPT_END( "simpleConnectAndTransmitDataTest" )

                        throw;
                    }

                    /*
                     * Shutdown the acceptor gracefully and exit
                     */

                    cancelAndWaitForSuccess( eq, taskAcceptor );
                }
            }
            );
    }

    template
    <
        typename Connector
    >
    void runClientPerfTest(
        SAA_in              const bl::om::ObjPtr< bl::data::datablocks_pool_type >&     dataBlocksPool,
        SAA_in_opt          std::string&&                                               host = "localhost",
        SAA_in_opt          const unsigned short                                        port = 28100U,
        SAA_in_opt          const std::size_t                                           connectionsCount = 10U,
        SAA_in_opt          const std::size_t                                           totalSizeInMB = 200U
        )
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::tasks;
        using namespace utest;

        const auto backendImpl = BackendImplTestImpl::createInstance();

        std::vector< om::ObjPtr< connection_t > > connections;

        const auto eqTransfers = om::lockDisposable(
            tasks::ExecutionQueueImpl::createInstance(
                tasks::ExecutionQueue::OptionKeepAll
                )
            );

        {
            {
                /*
                 * Testing the connection pooling logic in SendRecvContext class
                 */

                using namespace bl::transfer;

                const auto context = SendRecvContext::createInstance(
                    SimpleEndpointSelectorImpl::createInstance< EndpointSelector >( cpp::copy( host ), port )
                    );

                const auto connector = connector_t::createInstance( std::move( host ), port );
                const auto taskConnector = om::qi< tasks::Task >( connector.get() );
                eqTransfers -> push_back( taskConnector );
                eqTransfers -> waitForSuccess( taskConnector );

                auto connectedStream = connector -> detachStream();

                {
                    /*
                     * Test the configure socket logic and code
                     */

                    const auto ok = TcpSocketCommonBase::tryConfigureConnectedStream( *connectedStream );

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "The stream was "
                            << ( ok ? "" : "not " )
                            << "configured successfully"
                        );

                    tcp::socket invalid( ThreadPoolDefault::getDefault() -> aioService() );
                    UTF_REQUIRE( ! TcpSocketCommonBase::tryConfigureConnectedStream( invalid ) );
                }

                context -> putConnection( "key", std::move( connectedStream ) );
                os::sleep( time::seconds( 2 ) );

                {
                    auto socket = context -> tryGetConnection( "key" );
                    UTF_REQUIRE( socket );

                    UTF_REQUIRE( ! context -> tryGetConnection( "key" ) );

                    const auto originalTimeout = context -> maxIdleTimeoutInSeconds();

                    context -> maxIdleTimeoutInSeconds( 2L );
                    UTF_REQUIRE_EQUAL( context -> maxIdleTimeoutInSeconds(), 2L );

                    context -> putConnection( "key", std::move( socket ) );
                    os::sleep( time::seconds( 4 ) );

                    /*
                     * The connections would now be expired and discarded
                     */

                    UTF_REQUIRE( ! context -> tryGetConnection( "key" ) );

                    context -> maxIdleTimeoutInSeconds( originalTimeout );
                    UTF_REQUIRE_EQUAL( context -> maxIdleTimeoutInSeconds(), originalTimeout );
                }
            }

            /*
             * First we establish all connections
             */

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Establishing "
                    << connectionsCount
                    << " connections...."
                );

            const bl::uuid_t peerId = uuids::create();

            for( std::size_t i = 0; i< connectionsCount; ++i )
            {
                const auto connector = connector_t::createInstance( std::move( host ), port );
                const auto taskConnector = om::qi< tasks::Task >( connector.get() );
                eqTransfers -> push_back( taskConnector );
                eqTransfers -> waitForSuccess( taskConnector );

                auto transfer = connection_t::createInstance(
                    connection_t::CommandId::NoCommand,
                    peerId,
                    dataBlocksPool
                    );

                transfer -> attachStream( connector -> detachStream() );
                transfer -> setChunkData( backendImpl -> getData() );

                connections.push_back( std::move( transfer ) );
            }

            const auto t1 = bl::time::microsec_clock::universal_time();

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Sending "
                    << totalSizeInMB
                    << " MB ..."
                );

            try
            {
                /*
                 * Let's do simple sequential perf test
                 */

                const std::size_t oneMB = 1024 * 1024;

                BL_ASSERT( backendImpl -> getData() -> size() <= oneMB );
                BL_ASSERT( 0 == ( oneMB % backendImpl -> getData() -> size() ) );

                const std::size_t numberOfBlocks = totalSizeInMB * ( oneMB / backendImpl -> getData() -> size() );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Sending "
                        << numberOfBlocks
                        << " number of "
                        << backendImpl -> getData() -> size()
                        << " blocks"
                    );

                for( std::size_t i = 0; i < numberOfBlocks; ++i )
                {
                    if( ! connections.empty() )
                    {
                        connections.back() -> setCommandId( connection_t::CommandId::SendChunk );
                        connections.back() -> setChunkId( uuids::create() );
                        eqTransfers -> push_back( om::qi< tasks::Task >( connections.back().get() ) );
                        connections.erase( connections.end() - 1 );

                        continue;
                    }

                    const auto taskTransfer = eqTransfers -> top( true /* wait */ );

                    if( taskTransfer -> isFailed() )
                    {
                        cpp::safeRethrowException( taskTransfer -> exception() );
                    }

                    const auto transfer = om::qi< connection_t >( taskTransfer );
                    transfer -> setCommandId( connection_t::CommandId::SendChunk );
                    transfer -> setChunkId( uuids::create() );

                    eqTransfers -> push_back( taskTransfer );
                }

                {
                    eqTransfers -> flush();

                    const auto taskTransfer = eqTransfers -> top( false /* wait */ );
                    BL_ASSERT( taskTransfer );

                    if( taskTransfer -> isFailed() )
                    {
                        cpp::safeRethrowException( taskTransfer -> exception() );
                    }

                    /*
                     * Request flush
                     */

                    const auto transfer = om::qi< connection_t >( taskTransfer );
                    transfer -> setCommandId( connection_t::CommandId::FlushPeerSessions );
                    transfer -> setChunkId( uuids::nil() );
                    transfer -> detachChunkData();

                    eqTransfers -> push_back( taskTransfer );

                    eqTransfers -> flushAndDiscardReady();

                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Session was flushed on the server"
                        );
                }
            }
            catch( std::exception& )
            {
                eqTransfers -> forceFlushNoThrow();
                throw;
            }

            const auto duration = bl::time::microsec_clock::universal_time() - t1;
            const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

            BL_LOG(
                Logging::debug(),
                BL_MSG()
                    << "Sending "
                    << totalSizeInMB
                    << " MB took "
                    << durationInSeconds
                    << " seconds; "
                    << "speed is "
                    << ( totalSizeInMB / durationInSeconds )
                    << " MB/s"
                );
        }
    }

    template
    <
        typename Acceptor,
        typename Connector
    >
    void simplePerfTest(
        SAA_in_opt          std::string&&                                               host = "localhost",
        SAA_in_opt          const unsigned short                                        port = 28100U,
        SAA_in_opt          const std::size_t                                           connectionsCount = 10U,
        SAA_in_opt          const std::size_t                                           totalSizeInMB = 200U
        )
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::tasks;
        using namespace utest;

        typedef typename Acceptor::async_wrapper_t                                              async_wrapper_t;
        typedef typename async_wrapper_t::backend_interface_t                                   backend_interface_t;

        test::MachineGlobalTestLock lock;

        tasks::scheduleAndExecuteInParallel(
            [ &host, &port, &connectionsCount, &totalSizeInMB ](
                SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq
                ) -> void
            {
                const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
                const auto dataBlocksPool = datablocks_pool_type::createInstance();
                const auto backendImpl = BackendImplTestImpl::createInstance();

                const auto backend = om::lockDisposable(
                    async_wrapper_t::createInstance(
                        om::qi< backend_interface_t >( backendImpl ) /* writeBackend */,
                        om::qi< backend_interface_t >( backendImpl ) /* readBackend */,
                        test::UtfArgsParser::threadsCount(),
                        om::qi< TaskControlToken >( controlToken ),
                        0U /* maxConcurrentTasks */,
                        dataBlocksPool
                        )
                    );

                {
                    const auto acceptor = Acceptor::template createInstance< Acceptor >(
                        controlToken,
                        dataBlocksPool,
                        "localhost",
                        28100,
                        bl::str::empty() /* privateKeyPem */,
                        bl::str::empty() /* certificatePem */,
                        backend
                        );

                    UTF_REQUIRE( acceptor );

                    /*
                     * Start the acceptor and sleep for a couple of seconds to give it a chance to start
                     */

                    const auto taskAcceptor = om::qi< tasks::Task >( acceptor );
                    eq -> push_back( taskAcceptor );

                    BL_SCOPE_EXIT( cancelAndWaitForSuccess( eq, taskAcceptor ); );

                    /*
                     * For this particular test there is no reliable way except to
                     * use some arbitrary value which is large enough to not
                     * break in the CI and normally
                     */

                    os::sleep( time::milliseconds( 5000 ) );

                    runClientPerfTest< Connector >(
                        dataBlocksPool,
                        std::forward< std::string >( host ),
                        port,
                        connectionsCount,
                        totalSizeInMB
                        );
                }
            }
            );
    }

    template
    <
        typename Acceptor
    >
    void simplePerfStartServer()
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::tasks;
        using namespace utest;
        using namespace test;

        typedef typename Acceptor::async_wrapper_t                                              async_wrapper_t;
        typedef typename async_wrapper_t::backend_interface_t                                   backend_interface_t;

        tasks::scheduleAndExecuteInParallel(
            []( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
            {
                const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
                const auto dataBlocksPool = datablocks_pool_type::createInstance();

                const auto backendImpl = BackendImplTestImpl::createInstance();

                const auto backend = om::lockDisposable(
                    async_wrapper_t::createInstance(
                        om::qi< backend_interface_t >( backendImpl ) /* writeBackend */,
                        om::qi< backend_interface_t >( backendImpl ) /* readBackend */,
                        test::UtfArgsParser::threadsCount(),
                        om::qi< TaskControlToken >( controlToken ),
                        0U /* maxConcurrentTasks */,
                        dataBlocksPool
                        )
                    );

                {
                    const auto acceptor = Acceptor::template createInstance<>(
                        controlToken,
                        dataBlocksPool,
                        "localhost",
                        28100,
                        bl::str::empty() /* privateKeyPem */,
                        bl::str::empty() /* certificatePem */,
                        backend
                        );

                    UTF_REQUIRE( acceptor );

                    startAcceptor( acceptor, eq );
                }
            }
            );
    }

    namespace
    {
        typedef messaging::BackendProcessing::OperationId                               OperationId;
        typedef messaging::BackendProcessing::CommandId                                 CommandId;

        /**
         * @brief The outcome of the blocks scheduled on an auto push connection
         *
         * The completion callbacks run on the thread pool thread which executed
         * continuationTask(), so the counters have to be atomic and the state has to outlive
         * the enclosing scope - it is always held by a std::shared_ptr captured by value
         */

        struct BlockCallbackState
        {
            BL_NO_COPY_OR_MOVE( BlockCallbackState )

        public:

            std::atomic< std::size_t >                                                  invocations;
            std::atomic< std::size_t >                                                  withException;
            std::atomic< std::size_t >                                                  badExceptions;

            os::mutex                                                                   lock;
            std::exception_ptr                                                          lastException;

            BlockCallbackState()
                :
                invocations( 0U ),
                withException( 0U ),
                badExceptions( 0U )
            {
            }
        };

        inline void recordBlockCallback(
            SAA_in              const std::shared_ptr< BlockCallbackState >&            state,
            SAA_in              const std::exception_ptr&                               eptr
            ) NOEXCEPT
        {
            BL_NOEXCEPT_BEGIN()

            if( eptr )
            {
                ++state -> withException;

                /*
                 * A delivered exception must always be a real one - an empty or a garbage
                 * exception_ptr would be counted here
                 */

                try
                {
                    cpp::safeRethrowException( eptr );

                    ++state -> badExceptions;
                }
                catch( std::exception& )
                {
                    /*
                     * The expected shape
                     */
                }
                catch( ... )
                {
                    ++state -> badExceptions;
                }

                BL_MUTEX_GUARD( state -> lock );

                if( ! state -> lastException )
                {
                    state -> lastException = eptr;
                }
            }

            /*
             * The invocation counter is bumped last so a waiter which observes it can rely on
             * the exception having been recorded already
             */

            ++state -> invocations;

            BL_NOEXCEPT_END()
        }

        /**
         * @brief A one-shot rejecting decorator over a messaging backend
         *
         * BrokerErrorCodes classifies an expected broker error by its error code, and the only
         * per chunk failure utest::BackendImplTestT can inject is ENOENT, which is *not* an
         * expected broker error - so the expected-error arm of
         * TcpBlockTransferClientAutoPushConnectionT::continuationTask() needs a rejection of
         * its own. Every call is forwarded to the inner backend while the flag is clear, so the
         * fixture is otherwise unchanged
         */

        class OneShotRejectingBackend : public messaging::BackendProcessingBase
        {
        protected:

            typedef OneShotRejectingBackend                                             this_type;

            const om::ObjPtr< messaging::BackendProcessing >                            m_inner;
            std::atomic< bool >                                                         m_rejectNextBlock;

            OneShotRejectingBackend( SAA_in om::ObjPtr< messaging::BackendProcessing >&& inner )
                :
                m_inner( BL_PARAM_FWD( inner ) ),
                m_rejectNextBlock( false )
            {
            }

            static void rejectTheBlock()
            {
                /*
                 * It must be a ServerErrorException carrying a generic category error code,
                 * otherwise chk4ServerErrors() records errorValue = 0 and the client sees the
                 * generic "Unexpected server error has occurred" instead
                 */

                BL_THROW(
                    ServerErrorException()
                        << eh::errinfo_error_code(
                            eh::errc::make_error_code( messaging::BrokerErrorCodes::AuthorizationFailed )
                            ),
                    BL_MSG()
                        << "Injected authorization failure for the message"
                    );
            }

        public:

            void rejectNextBlock() NOEXCEPT
            {
                m_rejectNextBlock = true;
            }

            virtual bool autoBlockDispatching() const NOEXCEPT OVERRIDE
            {
                return m_inner -> autoBlockDispatching();
            }

            virtual void setHostServices( SAA_in om::ObjPtr< om::Proxy >&& hostServices ) NOEXCEPT OVERRIDE
            {
                m_inner -> setHostServices( BL_PARAM_FWD( hostServices ) );
            }

            virtual bool isConnected() const NOEXCEPT OVERRIDE
            {
                return m_inner -> isConnected();
            }

            virtual auto createBackendProcessingTask(
                SAA_in                  const OperationId                               operationId,
                SAA_in                  const CommandId                                 commandId,
                SAA_in                  const bl::uuid_t&                               sessionId,
                SAA_in                  const bl::uuid_t&                               chunkId,
                SAA_in_opt              const bl::uuid_t&                               sourcePeerId,
                SAA_in_opt              const bl::uuid_t&                               targetPeerId,
                SAA_in_opt              const om::ObjPtr< data::DataBlock >&            data
                )
                -> om::ObjPtr< tasks::Task > OVERRIDE
            {
                if( m_rejectNextBlock.exchange( false ) )
                {
                    return tasks::SimpleTaskImpl::createInstance< tasks::Task >( &this_type::rejectTheBlock );
                }

                return m_inner -> createBackendProcessingTask(
                    operationId,
                    commandId,
                    sessionId,
                    chunkId,
                    sourcePeerId,
                    targetPeerId,
                    data
                    );
            }
        };

        typedef om::ObjectImpl< OneShotRejectingBackend > OneShotRejectingBackendImpl;

        /**
         * @brief A backend processing mock whose processing task always fails
         *
         * It is what makes the security relevant half of
         * BrokerDispatchingBackendProcessing::createBackendProcessingTask() observable - a
         * rejected message must not reach the target peer just because the send task was
         * already constructed
         */

        class ThrowingBackendProcessing : public messaging::BackendProcessingBase
        {
        protected:

            typedef ThrowingBackendProcessing                                           this_type;

            std::atomic< bool >                                                         m_ran;
            bool                                                                        m_autoBlockDispatching;

            ThrowingBackendProcessing()
                :
                m_ran( false ),
                m_autoBlockDispatching( true )
            {
            }

            void rejectTheMessage()
            {
                m_ran = true;

                BL_THROW(
                    ArgumentException(),
                    BL_MSG()
                        << "backend processing rejected the message"
                    );
            }

        public:

            bool ran() const NOEXCEPT
            {
                return m_ran;
            }

            void setAutoBlockDispatching( SAA_in const bool autoBlockDispatching ) NOEXCEPT
            {
                m_autoBlockDispatching = autoBlockDispatching;
            }

            virtual bool autoBlockDispatching() const NOEXCEPT OVERRIDE
            {
                return m_autoBlockDispatching;
            }

            virtual auto createBackendProcessingTask(
                SAA_in                  const OperationId                               operationId,
                SAA_in                  const CommandId                                 commandId,
                SAA_in                  const bl::uuid_t&                               sessionId,
                SAA_in                  const bl::uuid_t&                               chunkId,
                SAA_in_opt              const bl::uuid_t&                               sourcePeerId,
                SAA_in_opt              const bl::uuid_t&                               targetPeerId,
                SAA_in_opt              const om::ObjPtr< data::DataBlock >&            data
                )
                -> om::ObjPtr< tasks::Task > OVERRIDE
            {
                BL_UNUSED( operationId );
                BL_UNUSED( commandId );
                BL_UNUSED( sessionId );
                BL_UNUSED( chunkId );
                BL_UNUSED( sourcePeerId );
                BL_UNUSED( targetPeerId );
                BL_UNUSED( data );

                return tasks::SimpleTaskImpl::createInstance< tasks::Task >(
                    cpp::bind(
                        &this_type::rejectTheMessage,
                        om::ObjPtrCopyable< this_type >::acquireRef( this )
                        )
                    );
            }
        };

        typedef om::ObjectImpl< ThrowingBackendProcessing > ThrowingBackendProcessingImpl;

        /*
         * This mock also implements messaging::AcceptorNotify - it is the hook a proxy broker
         * uses to reject an unauthorized peer, and no other implementation of that interface
         * exists in the test tree, so without it
         * BrokerDispatchingBackendProcessing::peerConnectedNotify() returns false (a
         * synchronous no-op), the notify task always succeeds and the only half of
         * notifyTaskCompletedContinuationCallback() which has any effect is never executed
         *
         * With m_rejectPeers unset it still returns false, so the existing users of this mock
         * are unaffected
         */

        class LocalBackendProcessing :
            public messaging::BackendProcessingBase,
            public messaging::AcceptorNotify
        {
            BL_QITBL_BEGIN()
                BL_QITBL_ENTRY_CHAIN_BASE( messaging::BackendProcessingBase )
                BL_QITBL_ENTRY( messaging::AcceptorNotify )
            BL_QITBL_END( messaging::BackendProcessing )

        protected:

            typedef LocalBackendProcessing                                              this_type;

            const std::size_t                                                           m_dataExpectedOffset;
            const std::string                                                           m_dataProcessed;
            const std::string                                                           m_dataUnprocessed;

            std::atomic< std::size_t >                                                  m_peerConnectedCalls;
            cpp::ScalarTypeIniter< bool >                                               m_rejectPeers;

            LocalBackendProcessing(
                SAA_in              const std::size_t                                   dataExpectedOffset,
                SAA_in              std::string&&                                       dataProcessed,
                SAA_in              std::string&&                                       dataUnprocessed
                )
                :
                m_dataExpectedOffset( dataExpectedOffset ),
                m_dataProcessed( BL_PARAM_FWD( dataProcessed ) ),
                m_dataUnprocessed( BL_PARAM_FWD( dataUnprocessed ) ),
                m_peerConnectedCalls( 0U )
            {
            }

            void processBlock( SAA_in const om::ObjPtrCopyable< data::DataBlock >& dataBlock )
            {
                const auto protocolDataOffsetIn = dataBlock -> offset1();

                UTF_REQUIRE_EQUAL( protocolDataOffsetIn, m_dataExpectedOffset );

                UTF_REQUIRE( ( m_dataExpectedOffset + m_dataProcessed.size() ) == dataBlock -> size() );

                UTF_REQUIRE_EQUAL(
                    0,
                    std::memcmp(
                        dataBlock -> begin() + m_dataExpectedOffset,
                        m_dataUnprocessed.c_str(),
                        m_dataUnprocessed.size()
                        )
                    );

                std::memcpy(
                    dataBlock -> begin() + m_dataExpectedOffset,
                    m_dataProcessed.c_str(),
                    m_dataProcessed.size()
                    );
            }

        public:

            virtual auto createBackendProcessingTask(
                SAA_in                  const OperationId                               operationId,
                SAA_in                  const CommandId                                 commandId,
                SAA_in                  const bl::uuid_t&                               sessionId,
                SAA_in                  const bl::uuid_t&                               chunkId,
                SAA_in_opt              const bl::uuid_t&                               sourcePeerId,
                SAA_in_opt              const bl::uuid_t&                               targetPeerId,
                SAA_in_opt              const om::ObjPtr< data::DataBlock >&            data
                )
                -> om::ObjPtr< tasks::Task > OVERRIDE
            {
                BL_UNUSED( operationId );
                BL_UNUSED( commandId );
                BL_UNUSED( sessionId );
                BL_UNUSED( chunkId );
                BL_UNUSED( sourcePeerId );
                BL_UNUSED( targetPeerId );

                /*
                 * The mock now has two om::Object base subobjects (BackendProcessingBase and
                 * AcceptorNotify), so the interface the reference is taken through has to be
                 * named explicitly - same idiom as utest::BackendImplTestT
                 */

                typedef om::ObjPtrCopyable
                <
                    this_type,
                    messaging::BackendProcessing        /* addRef() interface */
                >
                copyable_ptr_t;

                return tasks::SimpleTaskImpl::createInstance< tasks::Task >(
                    cpp::bind(
                        &this_type::processBlock,
                        copyable_ptr_t::acquireRef( this ),
                        om::ObjPtrCopyable< data::DataBlock >( data )
                        )
                    );
            }

            /*
             * AcceptorNotify implementation
             */

            std::size_t peerConnectedCalls() const NOEXCEPT
            {
                return m_peerConnectedCalls;
            }

            void rejectPeers( SAA_in const bool rejectPeers ) NOEXCEPT
            {
                m_rejectPeers = rejectPeers;
            }

            virtual bool peerConnectedNotify(
                SAA_in                  const bl::uuid_t&                               peerId,
                SAA_in_opt              tasks::CompletionCallback&&                     completionCallback
                )
                OVERRIDE
            {
                BL_UNUSED( completionCallback );

                ++m_peerConnectedCalls;

                if( m_rejectPeers )
                {
                    /*
                     * Any exception will do - the point is that the notify task must fail
                     */

                    BL_THROW(
                        SecurityException(),
                        BL_MSG()
                            << "The peer with id "
                            << peerId
                            << " is not authorized"
                        );
                }

                /*
                 * false means the operation completed synchronously
                 */

                return false;
            }

            virtual bool peerDisconnectedNotify(
                SAA_in                  const bl::uuid_t&                               peerId,
                SAA_in_opt              tasks::CompletionCallback&&                     completionCallback
                )
                OVERRIDE
            {
                BL_UNUSED( peerId );
                BL_UNUSED( completionCallback );

                return false;
            }
        };

        typedef om::ObjectImpl< LocalBackendProcessing > LocalBackendProcessingImpl;

    } // __unnamed

    template
    <
        typename DispatchingBackend,
        typename Connector,
        typename AsyncWrapper
    >
    void simpleConnectAndTransmitDataOutgoingTest()
    {
        using namespace bl;
        using namespace bl::data;
        using namespace bl::tasks;
        using namespace utest;

        typedef bl::tasks::detail::BlockTransferServerStateImpl< AsyncWrapper >                 BlockTransferServerState;
        typedef typename AsyncWrapper::backend_interface_t                                      backend_interface_t;

        typedef bl::tasks::TcpBlockTransferServerConnectionImpl
        <
            typename DispatchingBackend::acceptor_t::stream_t,
            AsyncWrapper
        >
        server_connection_t;

        typedef messaging::BrokerErrorCodes BrokerErrorCodes;

        test::MachineGlobalTestLock lock;

        tasks::scheduleAndExecuteInParallel(
            [ ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
            {
                const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
                const auto dataBlocksPool = datablocks_pool_type::createInstance();
                const auto backendImpl = BackendImplTestImpl::createInstance();

                /*
                 * The peer side backend is interposed so that exactly one delivered block can
                 * be made to fail with an *expected* broker error - see the sub-block below
                 * which verifies that such an error does not tear the connection down; while
                 * its flag is clear the decorator is fully transparent
                 */

                const auto rejectingBackend = OneShotRejectingBackendImpl::createInstance(
                    om::qi< messaging::BackendProcessing >( backendImpl )
                    );

                const auto backend = om::lockDisposable(
                    AsyncWrapper::template createInstance<>(
                        om::qi< backend_interface_t >( rejectingBackend ) /* writeBackend */,
                        om::qi< backend_interface_t >( rejectingBackend ) /* readBackend */,
                        test::UtfArgsParser::threadsCount(),
                        om::qi< TaskControlToken >( controlToken ),
                        0U /* maxConcurrentTasks */,
                        dataBlocksPool
                        )
                    );

                const auto serverState = BlockTransferServerState::createInstance( dataBlocksPool, backend );

                {
                    typedef tasks::detail::CommandBlock                                             CommandBlock;

                    typedef messaging::BackendProcessing::OperationId                               OperationId;
                    typedef messaging::BackendProcessing::CommandId                                 CommandId;

                    /*
                     * Define protocol data patterns for valid, invalid and unprocessed
                     */

                    const std::size_t protocolDataSize = 42U;
                    const std::size_t protocolDataOffset = DataBlock::defaultCapacity() - protocolDataSize;
                    const std::string protocolData( protocolDataSize, 'A' );

                    UTF_REQUIRE_EQUAL( protocolData.size(), protocolDataSize );

                    for( std::size_t i = 0U; i < protocolDataSize; ++i )
                    {
                        UTF_REQUIRE_EQUAL( protocolData[ i ], 'A' );
                    }

                    const std::string protocolDataInvalid( protocolDataSize, 'B' );
                    const std::string protocolDataUnprocessed( protocolDataSize, 'U' );

                    UTF_REQUIRE_EQUAL( protocolData.size(), protocolDataInvalid.size() );
                    UTF_REQUIRE_EQUAL( protocolData.size(), protocolDataUnprocessed.size() );

                    /*
                     * Define some local callbacks to be shared and used below:
                     *
                     * createBlock( ... ) to allocate new block from the pool initialize it accordingly
                     *
                     * onReady( ... ) callback which will mark data block with a special pattern and
                     *     return it to the pool
                     */

                    const auto createBlock = [ & ]( SAA_in const bool unprocessed ) -> om::ObjPtr< data::DataBlock >
                    {
                        auto dataBlock = DataBlock::get( dataBlocksPool );

                        UTF_REQUIRE_EQUAL( DataBlock::defaultCapacity(), dataBlock -> capacity() );

                        dataBlock -> setSize( dataBlock -> capacity() );

                        UTF_REQUIRE( ( protocolDataOffset + protocolData.size() ) == dataBlock -> size() );

                        const auto& dataToCopy = unprocessed ? protocolDataUnprocessed : protocolData;

                        std::memcpy(
                            dataBlock -> begin() + protocolDataOffset,
                            dataToCopy.c_str(),
                            dataToCopy.size()
                            );

                        dataBlock -> setOffset1( protocolDataOffset );

                        return dataBlock;
                    };

                    const auto onReady = [ & ](
                        SAA_in              const om::ObjPtrCopyable< DataBlock >&          dataBlock,
                        SAA_in              const std::exception_ptr&                       eptr
                        )
                        -> void
                    {
                        BL_NOEXCEPT_BEGIN()

                        if( eptr )
                        {
                            cpp::safeRethrowException( eptr );
                        }

                        UTF_REQUIRE_EQUAL( protocolDataOffset, dataBlock -> offset1() );

                        UTF_REQUIRE( ( protocolDataOffset + protocolData.size() ) == dataBlock -> size() );

                        UTF_REQUIRE_EQUAL(
                            0,
                            std::memcmp(
                                dataBlock -> begin() + protocolDataOffset,
                                protocolData.c_str(),
                                protocolData.size()
                                )
                            );

                        std::memcpy(
                            dataBlock -> begin() + protocolDataOffset,
                            protocolDataInvalid.c_str(),
                            protocolDataInvalid.size()
                            );

                        dataBlocksPool -> put( om::copy( dataBlock ) );

                        BL_NOEXCEPT_END()
                    };

                    /*
                     * Create the dispatching backend and run the tests
                     */

                    const long heartbeatIntervalInSeconds = 2L;

                    const auto processingBackend =
                        LocalBackendProcessingImpl::template createInstance< messaging::BackendProcessing >(
                            protocolDataOffset                          /* dataExpectedOffset */,
                            cpp::copy( protocolData )                   /* dataProcessed */,
                            cpp::copy( protocolDataUnprocessed )        /* dataUnprocessed */
                            );

                    const auto peerId = uuids::create();

                    const auto dispatchingBackendImpl = om::lockDisposable(
                        DispatchingBackend::template createInstance< DispatchingBackend >(
                            om::copy( processingBackend ),
                            controlToken,
                            dataBlocksPool,
                            "localhost",
                            28100,
                            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
                            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
                            peerId,
                            time::seconds( heartbeatIntervalInSeconds )         /* heartbeatInterval */
                            )
                        );

                    UTF_REQUIRE( dispatchingBackendImpl );

                    const auto& acceptor = dispatchingBackendImpl -> acceptor();

                    /*
                     * Sleep for a couple of seconds to give it a chance to start the server
                     */

                    os::sleep( time::milliseconds( 2000 ) );

                    {
                        {
                            /*
                             * Simply create a large # of connections first to observe the smooth
                             * logging and then shut them down immediately
                             */

                            tasks::scheduleAndExecuteInParallel(
                                [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eqLocal ) -> void
                                {
                                    const std::size_t maxConnections = 220U;

                                    for( std::size_t i = 0U; i < maxConnections; ++i )
                                    {
                                        const auto connector =
                                            Connector::template createInstance<>( "localhost", 28100 );

                                        const auto taskConnector = om::qi< tasks::Task >( connector );
                                        eqLocal -> push_back( taskConnector );
                                        eqLocal -> waitForSuccess( taskConnector );

                                        const auto peerId = uuids::create();

                                        const auto transfer =
                                            server_connection_t::createInstance( serverState, peerId );

                                        UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                                        UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), uuids::nil() );

                                        transfer -> attachStream( connector -> detachStream() );
                                        eqLocal -> push_back( om::qi< tasks::Task >( transfer ) );
                                    }

                                    os::sleep( time::seconds( 1L ) );

                                    eqLocal -> forceFlushNoThrow();

                                    /*
                                     * Note that after we shutdown the connections on the client side we need
                                     * to wait for at least heartbeatIntervalInSeconds + some extra timeout
                                     * to ensure that the server tasks have sent heartbeat messages and have
                                     * disconnected (after which point we should not have any server tasks
                                     * associated with the acceptor - see UTF_REQUIRE_EQUAL check below)
                                     */

                                    os::sleep( time::seconds( heartbeatIntervalInSeconds ) + time::seconds( 5L ) );

                                    UTF_REQUIRE_EQUAL( acceptor -> activeEndpoints().size(), 0U );
                                }
                                );
                        }

                        const auto connector = Connector::template createInstance< Connector >( "localhost", 28100 );
                        const auto taskConnector = om::qi< tasks::Task >( connector.get() );
                        eq -> push_back( taskConnector );
                        eq -> waitForSuccess( taskConnector );

                        const auto peerId = uuids::create();

                        const auto transfer = server_connection_t::createInstance( serverState, peerId );

                        /*
                         * This will allow to test for successful SSL shutdown as the
                         * default behavior is to not do SSL shutdown if task is canceled
                         */

                        transfer -> forceShutdownContinuation( true );

                        UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                        UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), uuids::nil() );

                        transfer -> attachStream( connector -> detachStream() );
                        const auto taskTransfer = om::qi< tasks::Task >( transfer );
                        eq -> push_back( taskTransfer );

                        /*
                         * The taskTransfer task is now a server style connection and will never terminate
                         * on its own, so we need to cancel it explicitly when we exit the scope
                         */

                        auto guard = BL_SCOPE_GUARD(
                            {
                                if( server_connection_t::isProtocolHandshakeNeeded )
                                {
                                    UTF_REQUIRE( transfer -> isShutdownNeeded() );
                                }

                                BL_NOEXCEPT_BEGIN()

                                try
                                {
                                    cancelAndWaitForSuccess( eq, taskTransfer );
                                }
                                catch( std::exception& e )
                                {
                                    const auto* errorCode = eh::get_error_info< eh::errinfo_error_code >( e );

                                    if(
                                        ! TcpSocketCommonBase::isExpectedSocketException(
                                            true /* isCancelExpected */,
                                            errorCode
                                            )
                                        )
                                    {
                                        throw;
                                    }
                                }

                                BL_NOEXCEPT_END()

                                /*
                                 * Since the task was cancelled by forcefully closing the socket
                                 * it would terminate without having a chance to execute proper
                                 * SSL shutdown sequence (and this is expected)
                                 *
                                 * However since the SSL tasks explicitly handles this
                                 * isShutdownNeeded() should still return false
                                 */

                                UTF_REQUIRE( ! transfer -> hasShutdownCompletedSuccessfully() );
                                UTF_REQUIRE( ! transfer -> isShutdownNeeded() );
                            }
                            );

                        /*
                         * Wait for the server connection to be established
                         */

                        typedef typename DispatchingBackend::acceptor_t             acceptor_t;
                        typedef typename acceptor_t::connection_t                   connection_t;

                        std::size_t retries = 0;
                        const std::size_t maxRetries = 2 * 60;
                        om::ObjPtr< connection_t > serverTask;

                        for( ;; )
                        {
                            os::sleep( time::seconds( 1 ) );

                            chkTaskCompletedOkOrRunning( acceptor );
                            chkTaskCompletedOkOrRunning( transfer );

                            const auto serverEndpoints = acceptor -> activeEndpoints();

                            if( serverEndpoints.size() )
                            {
                                UTF_REQUIRE_EQUAL( serverEndpoints.size(), 1U );

                                serverTask = om::qi< connection_t >( serverEndpoints.back() );

                                chkTaskCompletedOkOrRunning( serverTask );
                                if( serverTask -> lastSuccessfulHeartbeat() != time::neg_infin )
                                {
                                    break;
                                }
                            }

                            if( retries > maxRetries )
                            {
                                UTF_FAIL( "Connection with server can't be established" );
                            }

                            ++retries;
                        }

                        UTF_REQUIRE( serverTask -> lastSuccessfulHeartbeat() != time::neg_infin );

                        const auto& serverConnection = serverTask -> connection();
                        UTF_REQUIRE( serverConnection -> isClientVersionNegotiated() );

                        UTF_REQUIRE_EQUAL( transfer -> peerId(), peerId );
                        UTF_REQUIRE_EQUAL( transfer -> remotePeerId(), serverConnection -> peerId() );
                        UTF_REQUIRE_EQUAL( serverConnection -> remotePeerId(), transfer -> peerId() );

                        UTF_REQUIRE(
                            serverConnection -> targetPeerId() == uuids::nil() ||
                            serverConnection -> targetPeerId() == transfer -> peerId()
                            );

                        UTF_REQUIRE_EQUAL(
                            serverConnection -> clientVersion(),
                            CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2
                            );

                        /*
                         * Wait until the last heartbeat changes and ensure it has moved forward
                         */

                        const auto waitForNewHeartbeat = [ & ](
                            SAA_in              const time::ptime&                              referenceTimestamp
                            ) -> time::ptime
                        {
                            retries = 0;
                            time::ptime newHeartbeat;

                            for( ;; )
                            {
                                newHeartbeat = serverTask -> lastSuccessfulHeartbeat();

                                if( newHeartbeat > referenceTimestamp )
                                {
                                    break;
                                }

                                os::sleep( time::seconds( 1 ) );

                                ++retries;
                            }

                            return newHeartbeat;
                        };

                        const auto lastSuccessfulHeartbeat = serverTask -> lastSuccessfulHeartbeat();
                        UTF_REQUIRE( waitForNewHeartbeat( lastSuccessfulHeartbeat ) > lastSuccessfulHeartbeat );

                        /*
                         * Send a data block and then verify that the targetPeerId and sourcePeerId
                         * are propagated correctly
                         */

                        auto targetPeerId = uuids::create();
                        const auto sourcePeerId = serverConnection -> peerId();

                        const auto waitForBlocks = [ & ]( SAA_in const std::size_t blocksNo ) -> void
                        {
                            retries = 0;

                            for( ;; )
                            {
                                chkTaskCompletedOkOrRunning( acceptor );
                                chkTaskCompletedOkOrRunning( transfer );
                                chkTaskCompletedOkOrRunning( serverTask );

                                os::sleep( time::seconds( 1 ) );

                                BL_ASSERT( backendImpl -> saveCalls() <= blocksNo );

                                if( backendImpl -> saveCalls() == blocksNo )
                                {
                                    break;
                                }

                                if( retries > maxRetries )
                                {
                                    UTF_FAIL( "Connection with server can't be established" );
                                }

                                ++retries;
                            }
                        };

                        const auto scheduleBlocks = [ & ]( SAA_in const std::size_t noOfBlocks ) -> std::size_t
                        {
                            for( std::size_t i = 0U; i < noOfBlocks; ++i )
                            {
                                const auto dataBlock = createBlock( false /* unprocessed */ );

                                try
                                {
                                    serverTask -> scheduleBlock(
                                        targetPeerId,
                                        om::copy( dataBlock ),
                                        cpp::bind< void /* result_type */ >(
                                            onReady,
                                            om::ObjPtrCopyable< DataBlock >( dataBlock ),
                                            _1 /* onReady - the NOEXCEPT completion callback */
                                            )
                                        );
                                }
                                catch( ServerErrorException& e )
                                {
                                    const auto* ec = e.errorCode();

                                    UTF_REQUIRE( ec );

                                    UTF_REQUIRE(
                                        eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) == *ec
                                        );

                                    return i;
                                }
                            }

                            return noOfBlocks;
                        };

                        std::size_t totalBlocksScheduled = 0U;

                        backendImpl -> setExpectRealData( true );

                        UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                        UTF_REQUIRE_EQUAL( scheduleBlocks( 1 ), 1U );
                        totalBlocksScheduled += 1;
                        waitForBlocks( totalBlocksScheduled );

                        UTF_REQUIRE_EQUAL( targetPeerId, backendImpl -> targetPeerId() );
                        UTF_REQUIRE_EQUAL( sourcePeerId, backendImpl -> sourcePeerId() );
                        UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                        targetPeerId = uuids::create();

                        UTF_REQUIRE_EQUAL( scheduleBlocks( 1 ), 1U );
                        totalBlocksScheduled += 1;
                        waitForBlocks( totalBlocksScheduled );

                        UTF_REQUIRE_EQUAL( targetPeerId, backendImpl -> targetPeerId() );
                        UTF_REQUIRE_EQUAL( sourcePeerId, backendImpl -> sourcePeerId() );
                        UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                        {
                            /*
                             * An expected broker error must not tear down the outbound connection
                             *
                             * When the block in flight fails with a ServerErrorException whose code
                             * is one of BrokerErrorCodes::{AuthorizationFailed,
                             * ProtocolValidationFailed, TargetPeerNotFound, TargetPeerQueueFull},
                             * continuationTask() pops that one block, invokes its callback with the
                             * exception and keeps going. Collapsing that expected / fatal split
                             * would make one rejected message drop the whole broker to peer
                             * connection, unregister the peer and fail every other queued block -
                             * a cascading outage from a single bad message
                             */

                            const auto rejectedState = std::make_shared< BlockCallbackState >();
                            const auto rejectedBlock = createBlock( false /* unprocessed */ );

                            rejectingBackend -> rejectNextBlock();

                            serverTask -> scheduleBlock(
                                targetPeerId,
                                om::copy( rejectedBlock ),
                                cpp::bind< void /* result_type */ >(
                                    &recordBlockCallback,
                                    rejectedState,
                                    _1 /* the NOEXCEPT completion callback */
                                    )
                                );

                            retries = 0;

                            while( 0U == rejectedState -> invocations.load() )
                            {
                                if( retries > maxRetries )
                                {
                                    UTF_FAIL( "The rejected block callback was never invoked" );

                                    break;
                                }

                                os::sleep( time::seconds( 1 ) );

                                ++retries;
                            }

                            UTF_REQUIRE_EQUAL( rejectedState -> invocations.load(), 1U );
                            UTF_REQUIRE_EQUAL( rejectedState -> withException.load(), 1U );
                            UTF_REQUIRE_EQUAL( rejectedState -> badExceptions.load(), 0U );
                            UTF_REQUIRE( rejectedState -> lastException );

                            try
                            {
                                cpp::safeRethrowException( rejectedState -> lastException );

                                UTF_FAIL( "The rejected block must have failed" );
                            }
                            catch( ServerErrorException& e )
                            {
                                const auto* ec = e.errorCode();

                                UTF_REQUIRE( ec );

                                UTF_REQUIRE(
                                    eh::errc::make_error_code( BrokerErrorCodes::AuthorizationFailed ) == *ec
                                    );
                            }

                            /*
                             * The outbound connection survived, no Unregister was delivered and
                             * the registry still routes to this peer
                             */

                            UTF_REQUIRE( Task::Completed != serverTask -> getState() );

                            UTF_REQUIRE(
                                dispatchingBackendImpl -> getAllActiveQueuesIds().count( transfer -> peerId() )
                                );

                            UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                            dataBlocksPool -> put( om::copy( rejectedBlock ) );

                            /*
                             * The next block is delivered normally - the task returned to its
                             * normal cycle
                             */

                            const auto followUpState = std::make_shared< BlockCallbackState >();
                            const auto followUpBlock = createBlock( false /* unprocessed */ );

                            serverTask -> scheduleBlock(
                                targetPeerId,
                                om::copy( followUpBlock ),
                                cpp::bind< void /* result_type */ >(
                                    &recordBlockCallback,
                                    followUpState,
                                    _1 /* the NOEXCEPT completion callback */
                                    )
                                );

                            ++totalBlocksScheduled;
                            waitForBlocks( totalBlocksScheduled );

                            retries = 0;

                            while( 0U == followUpState -> invocations.load() )
                            {
                                if( retries > maxRetries )
                                {
                                    UTF_FAIL( "The follow up block callback was never invoked" );

                                    break;
                                }

                                os::sleep( time::seconds( 1 ) );

                                ++retries;
                            }

                            UTF_REQUIRE_EQUAL( followUpState -> invocations.load(), 1U );
                            UTF_REQUIRE_EQUAL( followUpState -> withException.load(), 0U );
                            UTF_REQUIRE( ! followUpState -> lastException );

                            UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                            dataBlocksPool -> put( om::copy( followUpBlock ) );

                            const auto heartbeatBeforeReject = serverTask -> lastSuccessfulHeartbeat();

                            UTF_REQUIRE(
                                waitForNewHeartbeat( heartbeatBeforeReject ) > heartbeatBeforeReject
                                );

                            backendImpl -> assertions().requireNone();
                        }

                        auto noOfBlocksToSchedule = connection_t::BLOCK_QUEUE_SIZE / 2;

                        UTF_REQUIRE_EQUAL( scheduleBlocks( noOfBlocksToSchedule ), noOfBlocksToSchedule );
                        totalBlocksScheduled += noOfBlocksToSchedule;
                        waitForBlocks( totalBlocksScheduled );

                        UTF_REQUIRE_EQUAL( targetPeerId, backendImpl -> targetPeerId() );
                        UTF_REQUIRE_EQUAL( sourcePeerId, backendImpl -> sourcePeerId() );
                        UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                        os::sleep( time::seconds( 2L * heartbeatIntervalInSeconds ) );

                        UTF_REQUIRE_EQUAL( targetPeerId, backendImpl -> targetPeerId() );
                        UTF_REQUIRE_EQUAL( sourcePeerId, backendImpl -> sourcePeerId() );
                        UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                        typedef cpp::function< std::size_t () > blocks_schedule_callback_t;

                        const auto perfTest = [ & ]( SAA_in const blocks_schedule_callback_t& callback ) -> void
                        {
                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "Start sending data ..."
                                );

                            const auto t1 = bl::time::microsec_clock::universal_time();

                            const auto noOfBlocksScheduled = callback();

                            /*
                             * Note that to measure accurately we need to remember the last heartbeat
                             * before we start sending and then measure from t1 until a new heartbeat
                             * but not until the time after the wait because the wait can add up a
                             * second rounding time which can skew the measurement significantly
                             */

                            const auto duration = waitForNewHeartbeat( t1 /* referenceTimestamp */ ) - t1;
                            const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                            const auto totalSizeInMB =
                                ( noOfBlocksScheduled * DataBlock::defaultCapacity() ) / ( 1024 * 1024.0 );

                            BL_LOG(
                                Logging::debug(),
                                BL_MSG()
                                    << "Sending "
                                    << totalSizeInMB
                                    << " MB ("
                                    << noOfBlocksScheduled
                                    << " messages) took "
                                    << durationInSeconds
                                    << " seconds; "
                                    << "speed is "
                                    << ( totalSizeInMB / durationInSeconds )
                                    << " MB/s"
                                );

                            UTF_REQUIRE_EQUAL( targetPeerId, backendImpl -> targetPeerId() );
                            UTF_REQUIRE_EQUAL( sourcePeerId, backendImpl -> sourcePeerId() );
                            UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                            os::sleep( time::seconds( 2 * heartbeatIntervalInSeconds ) );

                            UTF_REQUIRE_EQUAL( totalBlocksScheduled, backendImpl -> saveCalls() );

                            backendImpl -> assertions().requireNone();
                        };

                        /*
                         * Schedule some blocks directly on the acceptor to test raw performance
                         */

                        const auto noOfPerfTestBlocks = connection_t::BLOCK_QUEUE_SIZE * 5;

                        perfTest(
                            [ & ]() -> std::size_t
                            {
                                noOfBlocksToSchedule = noOfPerfTestBlocks;
                                const auto noOfBlocksScheduled = scheduleBlocks( noOfBlocksToSchedule );

                                UTF_REQUIRE( noOfBlocksScheduled <= noOfBlocksToSchedule );
                                totalBlocksScheduled += noOfBlocksScheduled;
                                waitForBlocks( totalBlocksScheduled );

                                return noOfBlocksScheduled;
                            }
                            );

                        /*
                         * Schedule some blocks via the dispatching backend interface to test
                         * this execution path
                         */

                        perfTest(
                            [ & ]() -> std::size_t
                            {
                                const std::size_t noOfBlocksToSchedule = noOfPerfTestBlocks;
                                std::size_t noOfBlocksScheduled = 0U;

                                tasks::scheduleAndExecuteInParallel(
                                    [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eqLocal ) -> void
                                    {
                                        eqLocal -> setOptions( ExecutionQueue::OptionKeepAll );

                                        const auto dispatchingBackend =
                                            om::qi< messaging::BackendProcessing >( dispatchingBackendImpl );

                                        const auto sessionId = uuids::create();
                                        const auto chunkId = uuids::create();

                                        targetPeerId = transfer -> peerId();

                                        std::unordered_map< const Task*, om::ObjPtr< DataBlock > > dataBlocksInProgress;

                                        const auto scheduleNewBlock = [ & ]() -> void
                                        {
                                            const auto dataBlock = createBlock( true /* unprocessed */ );

                                            const auto task = dispatchingBackend -> createBackendProcessingTask(
                                                OperationId::Put,
                                                CommandId::None,
                                                sessionId,
                                                chunkId,
                                                sourcePeerId,
                                                targetPeerId,
                                                dataBlock
                                                );

                                            const auto pair =
                                                dataBlocksInProgress.emplace( task.get(), om::copy( dataBlock ) );

                                            UTF_REQUIRE( pair.second );

                                            eqLocal -> push_back( task );
                                        };

                                        /*
                                         * We should be able to schedule safely at least
                                         * connection_t::BLOCK_QUEUE_SIZE / 2 workers before filling up the queue
                                         */

                                        const std::size_t noOfWorkers = connection_t::BLOCK_QUEUE_SIZE / 2;

                                        for( std::size_t i = 0U; i < noOfWorkers; ++i )
                                        {
                                            scheduleNewBlock();
                                        }

                                        noOfBlocksScheduled += noOfWorkers;

                                        const auto popTask = [ & ]() -> void
                                        {
                                            const auto task = eqLocal -> pop();

                                            const auto eptr = task -> exception();

                                            if( eptr )
                                            {
                                                cpp::safeRethrowException( eptr );
                                            }

                                            const auto pos = dataBlocksInProgress.find( task.get() );
                                            UTF_REQUIRE( pos != std::end( dataBlocksInProgress ) );

                                            /*
                                             * onReady will check the block has the correct
                                             * pattern and then will mark it with a different pattern
                                             * before it gets returned in the data blocks pool
                                             */

                                            onReady( pos -> second, eptr );
                                            dataBlocksInProgress.erase( pos );
                                        };

                                        while( noOfBlocksScheduled < noOfBlocksToSchedule )
                                        {
                                            popTask();

                                            scheduleNewBlock();

                                            ++noOfBlocksScheduled;
                                        }

                                        while( ! eqLocal -> isEmpty() )
                                        {
                                            popTask();
                                        }

                                        totalBlocksScheduled += noOfBlocksScheduled;
                                    }
                                    );

                                return noOfBlocksScheduled;
                            }
                            );

                        os::sleep( time::seconds( 4 ) );

                        /*
                         * Fill the pending queue and then drop the socket under the auto push
                         * connection while blocks are still queued
                         *
                         * When the connection terminates for any reason other than an expected
                         * broker error, every block still sitting in m_pendingQueue is swapped
                         * out under the lock and each of its callbacks is invoked *after* the
                         * lock is released. A lost callback is a hang in MessagingClientImpl,
                         * whose ExternalCompletionTasks wait on exactly these callbacks - the
                         * worst possible regression signature - and the only latent protection
                         * is the destructor's BL_RIP, which fires only if the swap itself is
                         * lost, so a regression which swapped but stopped invoking the
                         * callbacks would be completely silent
                         *
                         * A separate trivial callback is used here on purpose - onReady returns
                         * the block to the pool and asserts its content, which is not
                         * appropriate for a block which was never delivered
                         */

                        const auto drainState = std::make_shared< BlockCallbackState >();

                        std::size_t accepted = 0U;

                        for( std::size_t i = 0U; i < connection_t::BLOCK_QUEUE_SIZE; ++i )
                        {
                            try
                            {
                                serverTask -> scheduleBlock(
                                    targetPeerId,
                                    createBlock( false /* unprocessed */ ),
                                    cpp::bind< void /* result_type */ >(
                                        &recordBlockCallback,
                                        drainState,
                                        _1 /* the NOEXCEPT completion callback */
                                        )
                                    );
                            }
                            catch( ServerErrorException& e )
                            {
                                const auto* ec = e.errorCode();

                                UTF_REQUIRE( ec );

                                UTF_REQUIRE(
                                    eh::errc::make_error_code( BrokerErrorCodes::TargetPeerQueueFull ) == *ec
                                    );

                                break;
                            }

                            ++accepted;
                        }

                        UTF_REQUIRE( accepted > 0U );

                        guard.runNow();

                        retries = 0;

                        for( ;; )
                        {
                            os::sleep( time::seconds( 1 ) );

                            chkTaskCompletedOkOrRunning( acceptor );

                            if( Task::Completed == serverTask -> getState() )
                            {
                                break;
                            }

                            if( retries > maxRetries )
                            {
                                UTF_FAIL( "The server task did not finish in the expected time" );
                            }

                            ++retries;
                        }

                        /*
                         * Every accepted block's callback must have run exactly once
                         */

                        retries = 0;

                        while( drainState -> invocations.load() < accepted )
                        {
                            if( retries > maxRetries )
                            {
                                UTF_FAIL( "Not every pending block callback was invoked on teardown" );

                                break;
                            }

                            os::sleep( time::seconds( 1 ) );

                            ++retries;
                        }

                        BL_LOG(
                            Logging::debug(),
                            BL_MSG()
                                << "Teardown drain: accepted "
                                << accepted
                                << " blocks; callbacks invoked "
                                << drainState -> invocations.load()
                                << "; of which with an exception "
                                << drainState -> withException.load()
                            );

                        UTF_REQUIRE_EQUAL( drainState -> invocations.load(), accepted );

                        /*
                         * Deliberately a check and not a requirement - the accepted blocks may
                         * all have been delivered over loopback before the cancel landed
                         */

                        UTF_CHECK( drainState -> withException.load() > 0U );

                        UTF_REQUIRE_EQUAL( drainState -> badExceptions.load(), 0U );

                        UTF_REQUIRE( Task::Completed == serverConnection -> getState() );

                        {
                            /*
                             * Once the task is terminated every further block is rejected
                             * outright with a user friendly NotSupportedException carrying
                             * ErrorUuidNotConnectedToBroker - the uuid
                             * MessagingUtils::isRetryableMessagingBrokerError() keys its retry
                             * decision off, so a bare NotSupportedException, a
                             * ServerErrorException or a different flag would make the messaging
                             * client stop retrying after a broker connection drop and surface a
                             * hard failure instead of reconnecting
                             *
                             * A rejected block's callback must not fire - the caller learns of
                             * the failure through the throw
                             */

                            const auto rejectedOnTeardownState = std::make_shared< BlockCallbackState >();

                            const auto saveCallsBefore = backendImpl -> saveCalls();

                            const auto chkRejected = [ & ]( SAA_in const bool useTryScheduleBlock ) -> void
                            {
                                const auto dataBlock = createBlock( false /* unprocessed */ );

                                try
                                {
                                    if( useTryScheduleBlock )
                                    {
                                        ( void ) serverTask -> tryScheduleBlock(
                                            targetPeerId,
                                            om::copy( dataBlock ),
                                            cpp::bind< void /* result_type */ >(
                                                &recordBlockCallback,
                                                rejectedOnTeardownState,
                                                _1 /* the NOEXCEPT completion callback */
                                                )
                                            );
                                    }
                                    else
                                    {
                                        serverTask -> scheduleBlock(
                                            targetPeerId,
                                            om::copy( dataBlock ),
                                            cpp::bind< void /* result_type */ >(
                                                &recordBlockCallback,
                                                rejectedOnTeardownState,
                                                _1 /* the NOEXCEPT completion callback */
                                                )
                                            );
                                    }

                                    UTF_FAIL( "Scheduling a block on a terminated connection must throw" );
                                }
                                catch( bl::NotSupportedException& e )
                                {
                                    const auto* errorUuid = eh::get_error_info< eh::errinfo_error_uuid >( e );

                                    UTF_REQUIRE( errorUuid );

                                    UTF_REQUIRE_EQUAL(
                                        *errorUuid,
                                        messaging::uuiddefs::ErrorUuidNotConnectedToBroker()
                                        );
                                }

                                dataBlocksPool -> put( om::copy( dataBlock ) );
                            };

                            chkRejected( false /* useTryScheduleBlock */ );
                            chkRejected( true /* useTryScheduleBlock */ );

                            UTF_REQUIRE_EQUAL( rejectedOnTeardownState -> invocations.load(), 0U );
                            UTF_REQUIRE_EQUAL( backendImpl -> saveCalls(), saveCallsBefore );
                        }

                        backendImpl -> assertions().requireNone();

                        /*
                         * Since the remote peer task is cancelled by closing the socket
                         * this task would terminate without having a chance to execute
                         * proper shutdown (likely with one of the expected
                         * error codes which happen when the socket is closed abruptly)
                         *
                         * However since shutdown was attempted isShutdownNeeded() should
                         * still return false
                         */

                        UTF_REQUIRE( ! serverConnection -> hasShutdownCompletedSuccessfully() );
                        UTF_REQUIRE( ! serverConnection -> isShutdownNeeded() );
                    }
                }
            }
            );
    }

} // __unnamed

UTF_AUTO_TEST_CASE( IO_BasicTests )
{
    basicAcceptorTest< bl::tasks::TcpBlockServerDataChunkStorage >();
}

UTF_AUTO_TEST_CASE( IO_BasicMessageDispatcherTests )
{
    basicAcceptorTest< bl::tasks::TcpBlockServerMessageDispatcher >();
}

UTF_AUTO_TEST_CASE( IO_SimpleAcceptorStartStopTests )
{
    simpleConnectAndTransmitDataTest< bl::tasks::TcpBlockServerDataChunkStorage, connector_t >();
}

UTF_AUTO_TEST_CASE( IO_SimpleAcceptorStartStopMessageDispatcherTests )
{
    simpleConnectAndTransmitDataTest< bl::tasks::TcpBlockServerMessageDispatcher, connector_t >();
}

UTF_AUTO_TEST_CASE( IO_SimpleConnectAndTransmitDataTests )
{
    simpleConnectAndTransmitDataTest< bl::tasks::TcpBlockServerDataChunkStorage, connector_t >(
        true /* startConnector */
        );
}

UTF_AUTO_TEST_CASE( IO_AuthenticatedConnectAndTransmitDataTests )
{
    simpleConnectAndTransmitDataTest< bl::tasks::TcpBlockServerDataChunkStorage, connector_t >(
        true /* startConnector */,
        true /* isAuthenticationRequired */
        );
}

UTF_AUTO_TEST_CASE( IO_SimpleConnectAndTransmitDataMessageDispatcherTests )
{
    simpleConnectAndTransmitDataTest< bl::tasks::TcpBlockServerMessageDispatcher, connector_t >(
        true /* startConnector */
        );
}

UTF_AUTO_TEST_CASE( IO_SslSimpleConnectAndTransmitDataMessageDispatcherTests )
{
    simpleConnectAndTransmitDataTest< bl::tasks::TcpSslBlockServerMessageDispatcher, ssl_connector_t >(
        true /* startConnector */
        );
}

UTF_AUTO_TEST_CASE( IO_SimpleConnectAndTransmitDataMessageDispatcherOutgoingTests )
{
    simpleConnectAndTransmitDataOutgoingTest<
        bl::messaging::BrokerDispatchingBackendProcessingImpl               /* DispatchingBackend */,
        connector_t,                                                        /* Connector */
        bl::tasks::TcpBlockServerMessageDispatcher::async_wrapper_t         /* AsyncWrapper */
        >();
}

UTF_AUTO_TEST_CASE( IO_SslSimpleConnectAndTransmitDataMessageDispatcherOutgoingTests )
{
    simpleConnectAndTransmitDataOutgoingTest<
        bl::messaging::SslBrokerDispatchingBackendProcessingImpl            /* DispatchingBackend */,
        ssl_connector_t,                                                    /* Connector */
        bl::tasks::TcpSslBlockServerMessageDispatcher::async_wrapper_t      /* AsyncWrapper */
        >();
}

/************************************************************************
 * Chunk level failure injection over the TCP block transfer stack
 *
 * Two holes are covered here. The harness backend used to validate a received chunk by
 * checking only its first 16 bytes out of a 1 MB block, so a framing or truncation
 * regression past that prefix - or a recycled block whose head happened to match - was
 * invisible; and there was no way to make one specific chunk fail, so the per chunk error
 * propagation from the storage backend back to the client was untested from this module
 */

UTF_AUTO_TEST_CASE( Io_TcpBlockTransferChunkFailurePropagationTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace utest;

    typedef TcpBlockServerDataChunkStorage                                  acceptor_t;
    typedef acceptor_t::async_wrapper_t                                     async_wrapper_t;
    typedef async_wrapper_t::backend_interface_t                            backend_interface_t;

    /*
     * First the self check of the verification itself: a block which follows the pattern
     * everywhere except in its very last byte must be rejected
     *
     * Note that this is the assertion which pins the full length check - with the old
     * 16 byte prefix check the very same block was accepted silently
     */

    {
        const auto block = BackendImplTestImpl::initDataBlock( DataBlock::createInstance() );

        UTF_REQUIRE( block -> size() > 16U );

        UTF_REQUIRE_NO_THROW( BackendImplTestImpl::verifyData( block ) );

        auto* const data = block -> begin();
        const auto lastPos = block -> size() - 1U;

        data[ lastPos ] = ( char )( ( ( std::size_t )( data[ lastPos ] ) + 1U ) % 128U );

        UTF_REQUIRE_THROW( BackendImplTestImpl::verifyData( block ), UnexpectedException );
    }

    test::MachineGlobalTestLock lock;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
    const auto dataBlocksPool = datablocks_pool_type::createInstance();
    const auto backendImpl = BackendImplTestImpl::createInstance();

    UTF_REQUIRE_EQUAL( backendImpl -> invalidChunkId(), uuids::nil() );
    UTF_REQUIRE_EQUAL( backendImpl -> injectedFailures(), 0U );

    const auto storage = om::lockDisposable(
        async_wrapper_t::createInstance< async_wrapper_t >(
            om::qi< backend_interface_t >( backendImpl )        /* writeBackend */,
            om::qi< backend_interface_t >( backendImpl )        /* readBackend */,
            test::UtfArgsParser::threadsCount(),
            om::qi< TaskControlToken >( controlToken ),
            0U                                                  /* maxConcurrentTasks */,
            dataBlocksPool
            )
        );

    const bl::uuid_t injectedChunkId = uuids::create();
    const bl::uuid_t healthyChunkId = uuids::create();

    const auto cbTest = [ & ]() -> void
    {
        tasks::scheduleAndExecuteInParallel(
            [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
            {
                const auto connect = [ & ]() -> om::ObjPtr< connection_t >
                {
                    const auto connector =
                        connector_t::createInstance( std::string( "localhost" ), 28100U );

                    const auto taskConnector = om::qi< tasks::Task >( connector.get() );
                    eq -> push_back( taskConnector );
                    eq -> waitForSuccess( taskConnector );

                    auto transfer = connection_t::createInstance(
                        connection_t::CommandId::NoCommand,
                        uuids::create()                                 /* peerId */,
                        dataBlocksPool
                        );

                    transfer -> attachStream( connector -> detachStream() );

                    return transfer;
                };

                /*
                 * A healthy chunk first, to establish that the transfer path works and that
                 * the full length data check accepts a block which travelled over the wire
                 */

                {
                    const auto transfer = connect();
                    const auto taskTransfer = om::qi< tasks::Task >( transfer );

                    transfer -> setChunkData( backendImpl -> getData() );
                    transfer -> setCommandId( connection_t::CommandId::SendChunk );
                    transfer -> setChunkId( healthyChunkId );

                    eq -> push_back( taskTransfer );
                    eq -> waitForSuccess( taskTransfer );

                    UTF_REQUIRE_EQUAL( backendImpl -> saveCalls(), 1U );
                    UTF_REQUIRE_EQUAL( backendImpl -> injectedFailures(), 0U );

                    transfer -> setCommandId( connection_t::CommandId::ReceiveChunk );
                    transfer -> setChunkId( healthyChunkId );
                    transfer -> detachChunkData();

                    eq -> push_back( taskTransfer );
                    eq -> waitForSuccess( taskTransfer );

                    UTF_REQUIRE_EQUAL( backendImpl -> loadCalls(), 1U );

                    BackendImplTestImpl::verifyData( transfer -> getChunkData() );

                    tasks::cancelAndWaitForSuccess( eq, taskTransfer );
                }

                /*
                 * Now arm the failure injection for one specific chunk and drive it
                 */

                backendImpl -> resetStats();
                backendImpl -> setInvalidChunkId( injectedChunkId );

                UTF_REQUIRE_EQUAL( backendImpl -> invalidChunkId(), injectedChunkId );
                UTF_REQUIRE_EQUAL( backendImpl -> injectedFailures(), 0U );

                {
                    const auto transfer = connect();
                    const auto taskTransfer = om::qi< tasks::Task >( transfer );

                    transfer -> setChunkData( backendImpl -> getData() );
                    transfer -> setCommandId( connection_t::CommandId::SendChunk );
                    transfer -> setChunkId( injectedChunkId );

                    eq -> push_back( taskTransfer );

                    /*
                     * The backend error must arrive at the client as a ServerErrorException
                     * carrying the very error code the storage layer failed with
                     */

                    UTF_REQUIRE_THROW_ERROR_CODE(
                        eq -> waitForSuccess( taskTransfer ),
                        ServerErrorException,
                        eh::errc::make_error_code( eh::errc::no_such_file_or_directory )
                        );

                    UTF_REQUIRE_EQUAL( backendImpl -> injectedFailures(), 1U );

                    /*
                     * The failure is injected ahead of any work, so no counter may advance
                     */

                    UTF_REQUIRE_EQUAL( 0U, backendImpl -> saveCalls() );
                    UTF_REQUIRE_EQUAL( 0U, backendImpl -> loadCalls() );
                    UTF_REQUIRE_EQUAL( 0U, backendImpl -> removeCalls() );

                    /*
                     * The failure was expected and has already been asserted, so the failed
                     * task must be discarded here - otherwise the queue would rethrow it when
                     * it is flushed at the end of the scope
                     */

                    eq -> forceFlushNoThrow();
                }

                /*
                 * The injection must be scoped to its own chunk id - every other chunk still
                 * has to be served normally
                 */

                {
                    const auto transfer = connect();
                    const auto taskTransfer = om::qi< tasks::Task >( transfer );

                    transfer -> setChunkData( backendImpl -> getData() );
                    transfer -> setCommandId( connection_t::CommandId::SendChunk );
                    transfer -> setChunkId( healthyChunkId );

                    eq -> push_back( taskTransfer );
                    eq -> waitForSuccess( taskTransfer );

                    UTF_REQUIRE_EQUAL( backendImpl -> saveCalls(), 1U );
                    UTF_REQUIRE_EQUAL( backendImpl -> injectedFailures(), 1U );

                    tasks::cancelAndWaitForSuccess( eq, taskTransfer );
                }

                backendImpl -> assertions().requireNone();
            }
            );
    };

    TestTaskUtils::createAcceptorAndExecute< acceptor_t >(
        controlToken,
        cbTest,
        dataBlocksPool,
        storage,
        std::string( "localhost" ),
        28100U
        );
}

namespace
{
    /**
     * @brief A message block completion queue mock which only counts the heartbeat requests
     */

    template
    <
        typename E = void
    >
    class HeartbeatCountingQueueT : public bl::messaging::MessageBlockCompletionQueue
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( HeartbeatCountingQueueT, bl::messaging::MessageBlockCompletionQueue )

    protected:

        std::atomic< std::size_t >                                                  m_heartbeatsRequested;

        HeartbeatCountingQueueT()
            :
            m_heartbeatsRequested( 0U )
        {
        }

    public:

        std::size_t heartbeatsRequested() const NOEXCEPT
        {
            return m_heartbeatsRequested;
        }

        virtual void requestHeartbeat() OVERRIDE
        {
            ++m_heartbeatsRequested;
        }

        virtual bool tryScheduleBlock(
            SAA_in                  const bl::uuid_t&                                   targetPeerId,
            SAA_in                  bl::om::ObjPtr< bl::data::DataBlock >&&             dataBlock,
            SAA_in                  CompletionCallback&&                                callback
            ) OVERRIDE
        {
            BL_UNUSED( targetPeerId );
            BL_UNUSED( dataBlock );
            BL_UNUSED( callback );

            return true;
        }
    };

    typedef bl::om::ObjectImpl< HeartbeatCountingQueueT<> > HeartbeatCountingQueue;
}

UTF_AUTO_TEST_CASE( IO_OutgoingBackendStateRegistrationTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    /*
     * Registration policy of the outbound (delivery) queues, see
     * TcpBlockServerOutgoingBackendState::registerQueue():
     *
     * - a registration for a peer id demotes the active queues registered from the same
     *   remote address to the unconfirmed list until they confirm by heartbeat (cooperative
     *   multi-connection from one host)
     *
     * - active queues registered from a different remote address stay active (they are
     *   still asked to heartbeat), so a registration from another host cannot take over
     *   delivery for the peer id
     *
     * - a registration with an unknown address behaves like the same host
     */

    typedef std::set< MessageBlockCompletionQueue* > queues_set_t;

    const auto backendState = TcpBlockServerOutgoingBackendState::createInstance();
    const auto peerId = uuids::create();

    const auto q1Impl = HeartbeatCountingQueue::createInstance();
    const auto q2Impl = HeartbeatCountingQueue::createInstance();
    const auto q3Impl = HeartbeatCountingQueue::createInstance();
    const auto q4Impl = HeartbeatCountingQueue::createInstance();

    const auto q1 = om::qi< MessageBlockCompletionQueue >( q1Impl );
    const auto q2 = om::qi< MessageBlockCompletionQueue >( q2Impl );
    const auto q3 = om::qi< MessageBlockCompletionQueue >( q3Impl );
    const auto q4 = om::qi< MessageBlockCompletionQueue >( q4Impl );

    const auto collectActiveQueues = [ & ]() -> queues_set_t
    {
        /*
         * tryGetQueue() rotates over the active queues only; enough calls visit all of them
         */

        queues_set_t result;

        for( std::size_t i = 0U; i < 16U; ++i )
        {
            const auto queue = backendState -> tryGetQueue( peerId );

            if( queue )
            {
                result.insert( queue.get() );
            }
        }

        return result;
    };

    const auto requireActive = [ & ]( SAA_in const queues_set_t& expected ) -> void
    {
        const auto active = collectActiveQueues();

        UTF_REQUIRE_EQUAL( active.size(), expected.size() );

        for( const auto& queue : expected )
        {
            UTF_REQUIRE( active.count( queue ) );
        }
    };

    /*
     * First registration from host A
     */

    backendState -> registerQueue( peerId, om::copy( q1 ), "10.0.0.1" );

    requireActive( queues_set_t( { q1.get() } ) );
    UTF_REQUIRE_EQUAL( q1Impl -> heartbeatsRequested(), 0U );

    /*
     * A registration for the same peer id from host B: q1 must stay active (probed by a
     * heartbeat) and q2 joins the rotation
     */

    backendState -> registerQueue( peerId, om::copy( q2 ), "10.0.0.2" );

    requireActive( queues_set_t( { q1.get(), q2.get() } ) );
    UTF_REQUIRE_EQUAL( q1Impl -> heartbeatsRequested(), 1U );
    UTF_REQUIRE_EQUAL( q2Impl -> heartbeatsRequested(), 0U );

    /*
     * A registration from host A again: q1 (same host) is demoted until it confirms,
     * q2 (other host) stays active; both are asked to heartbeat
     */

    backendState -> registerQueue( peerId, om::copy( q3 ), "10.0.0.1" );

    requireActive( queues_set_t( { q2.get(), q3.get() } ) );
    UTF_REQUIRE_EQUAL( q1Impl -> heartbeatsRequested(), 2U );
    UTF_REQUIRE_EQUAL( q2Impl -> heartbeatsRequested(), 1U );
    UTF_REQUIRE_EQUAL( q3Impl -> heartbeatsRequested(), 0U );

    /*
     * q1 confirms and rejoins the rotation
     */

    backendState -> confirmQueue( peerId, q1 );

    requireActive( queues_set_t( { q1.get(), q2.get(), q3.get() } ) );

    UTF_REQUIRE_THROW( backendState -> confirmQueue( peerId, q1 ), UnexpectedException );

    /*
     * A registration with an unknown address behaves like the legacy code: every active
     * queue is demoted until it confirms
     */

    backendState -> registerQueue( peerId, om::copy( q4 ), str::empty() );

    requireActive( queues_set_t( { q4.get() } ) );

    backendState -> confirmQueue( peerId, q1 );
    backendState -> confirmQueue( peerId, q2 );
    backendState -> confirmQueue( peerId, q3 );

    requireActive( queues_set_t( { q1.get(), q2.get(), q3.get(), q4.get() } ) );

    /*
     * Unregistering removes the queue from the rotation and from the accounting
     */

    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 4U );

    backendState -> unregisterQueue( peerId, q2 );

    requireActive( queues_set_t( { q1.get(), q3.get(), q4.get() } ) );
    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 3U );
    UTF_REQUIRE( backendState -> getAllActiveQueuesIds().count( peerId ) );

    UTF_REQUIRE_THROW( backendState -> unregisterQueue( peerId, q2 ), UnexpectedException );
    UTF_REQUIRE_THROW( backendState -> registerQueue( peerId, om::copy( q1 ), "10.0.0.1" ), UnexpectedException );

    /*
     * Draining the peer must erase its entry from m_peersInfo
     *
     * Without that erase every peer id ever seen stays in the map forever, and
     * getAllActiveQueuesIds() - which the proxy broker backend factory and the HTTP server
     * messaging bridge both consume - keeps advertising a peer whose last connection is gone
     */

    backendState -> unregisterQueue( peerId, q1 );
    backendState -> unregisterQueue( peerId, q3 );
    backendState -> unregisterQueue( peerId, q4 );

    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 0U );
    UTF_REQUIRE( backendState -> getAllActiveQueuesIds().empty() );
    UTF_REQUIRE( ! backendState -> tryGetQueue( peerId ) );

    /*
     * The peer id is reusable after its entry was erased
     */

    backendState -> registerQueue( peerId, om::copy( q1 ), "10.0.0.1" );

    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 1U );
    UTF_REQUIRE( backendState -> getAllActiveQueuesIds().count( peerId ) );
    UTF_REQUIRE( om::areEqual( backendState -> tryGetQueue( peerId ), q1 ) );

    /*
     * The 'unexpected peerId' guard is what prevents one peer's unregistration from evicting
     * another peer's queue, and it must reject the call before it mutates anything
     */

    const auto otherPeerId = uuids::create();

    UTF_REQUIRE( otherPeerId != peerId );

    UTF_REQUIRE_THROW( backendState -> confirmQueue( otherPeerId, q1 ), UnexpectedException );
    UTF_REQUIRE_THROW( backendState -> unregisterQueue( otherPeerId, q1 ), UnexpectedException );

    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 1U );
    UTF_REQUIRE( om::areEqual( backendState -> tryGetQueue( peerId ), q1 ) );

    /*
     * A nil remote peer id must be refused - otherwise every anonymous connection would
     * share one registry slot
     */

    UTF_REQUIRE_THROW(
        backendState -> registerQueue( uuids::nil(), om::copy( q2 ), "10.0.0.1" ),
        UnexpectedException
        );

    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 1U );
}

namespace
{
    /**
     * @brief A message block completion queue mock which calls back into the backend state
     * from within requestHeartbeat()
     */

    template
    <
        typename E = void
    >
    class ReentrantHeartbeatQueueT : public bl::messaging::MessageBlockCompletionQueue
    {
        BL_DECLARE_OBJECT_IMPL_ONEIFACE( ReentrantHeartbeatQueueT, bl::messaging::MessageBlockCompletionQueue )

    protected:

        bl::cpp::void_callback_t                                                    m_onHeartbeat;

        ReentrantHeartbeatQueueT()
        {
        }

    public:

        void onHeartbeat( SAA_in bl::cpp::void_callback_t&& onHeartbeat ) NOEXCEPT
        {
            onHeartbeat.swap( m_onHeartbeat );
        }

        virtual void requestHeartbeat() OVERRIDE
        {
            if( m_onHeartbeat )
            {
                m_onHeartbeat();
            }
        }

        virtual bool tryScheduleBlock(
            SAA_in                  const bl::uuid_t&                                   targetPeerId,
            SAA_in                  bl::om::ObjPtr< bl::data::DataBlock >&&             dataBlock,
            SAA_in                  CompletionCallback&&                                callback
            ) OVERRIDE
        {
            BL_UNUSED( targetPeerId );
            BL_UNUSED( dataBlock );
            BL_UNUSED( callback );

            return true;
        }
    };

    typedef bl::om::ObjectImpl< ReentrantHeartbeatQueueT<> > ReentrantHeartbeatQueue;
}

UTF_AUTO_TEST_CASE( IO_OutgoingBackendStateHeartbeatOutsideLockTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    /*
     * The heartbeat requests which registerQueue() issues must be made after its lock has
     * been released - requestHeartbeat() takes the lock of a connection task, and a task
     * which is terminating concurrently holds that very lock while it unregisters itself
     * from the backend state (an ABBA inversion which freezes the whole outbound path)
     *
     * The mock below closes the same cycle in a single thread: its heartbeat handler calls
     * back into the backend state, which self-deadlocks on the non-recursive lock unless the
     * heartbeats are issued outside of it
     */

    const auto backendState = TcpBlockServerOutgoingBackendState::createInstance();
    const auto peerId = uuids::create();

    const auto q1Impl = ReentrantHeartbeatQueue::createInstance();
    const auto q2Impl = HeartbeatCountingQueue::createInstance();

    const auto q1 = om::qi< MessageBlockCompletionQueue >( q1Impl );
    const auto q2 = om::qi< MessageBlockCompletionQueue >( q2Impl );

    backendState -> registerQueue( peerId, om::copy( q1 ), "10.0.0.1" );

    q1Impl -> onHeartbeat(
        [ & ]() -> void
        {
            backendState -> unregisterQueue( peerId, q1 );
        }
        );

    backendState -> registerQueue( peerId, om::copy( q2 ), "10.0.0.1" );

    /*
     * The re-entrant queue unregistered itself from within the heartbeat request, so only
     * the newly registered one is left
     */

    UTF_REQUIRE_EQUAL( backendState -> activeTasksCount(), 1U );

    const auto queue = backendState -> tryGetQueue( peerId );

    UTF_REQUIRE( queue );
    UTF_REQUIRE( om::areEqual( queue, q2 ) );
}

UTF_AUTO_TEST_CASE( IO_SimplePerfTests )
{
    using namespace test;

    simplePerfTest< bl::tasks::TcpBlockServerDataChunkStorage, connector_t >(
        std::string( UtfArgsParser::host() ),
        UtfArgsParser::port(),
        UtfArgsParser::connections(),
        UtfArgsParser::dataSizeInMB()
        );
}

UTF_AUTO_TEST_CASE( IO_SimplePerfMessageDispatcherTests )
{
    using namespace test;

    simplePerfTest< bl::tasks::TcpBlockServerMessageDispatcher, connector_t >(
        std::string( UtfArgsParser::host() ),
        UtfArgsParser::port(),
        UtfArgsParser::connections(),
        UtfArgsParser::dataSizeInMB()
        );
}

UTF_AUTO_TEST_CASE( IO_PerfStartServer )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual performance run)" );

    test::MachineGlobalTestLock lock;

    simplePerfStartServer< bl::tasks::TcpBlockServerDataChunkStorage >();
}

UTF_AUTO_TEST_CASE( IO_PerfStartMessageDispatcherServer )
{
    UTF_SKIP_UNLESS( test::UtfArgsParser::isServer(), "requires --is-server (manual performance run)" );

    test::MachineGlobalTestLock lock;

    simplePerfStartServer< bl::tasks::TcpBlockServerMessageDispatcher >();
}

UTF_AUTO_TEST_CASE( IO_PerfStartClient )
{
    using namespace test;

    UTF_SKIP_UNLESS( UtfArgsParser::isClient(), "requires --is-client (manual performance run)" );

    const auto dataBlocksPool = bl::data::datablocks_pool_type::createInstance();

    runClientPerfTest< connector_t >(
        dataBlocksPool,
        std::string( UtfArgsParser::host() ),
        UtfArgsParser::port(),
        UtfArgsParser::connections(),
        UtfArgsParser::dataSizeInMB()
        );
}

UTF_AUTO_TEST_CASE( IO_MaxConnectionsTest )
{
    using namespace test;
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace utest;

    UTF_SKIP_UNLESS( UtfArgsParser::isClient(), "requires --is-client (manual performance run)" );

    tasks::scheduleAndExecuteInParallel(
        []( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
        {
            const auto dataBlocksPool = datablocks_pool_type::createInstance();

            std::vector< om::ObjPtr< connector_t > > connections;

            const std::size_t connectionsCount = UtfArgsParser::connections();

            {
                const auto t1 = bl::time::microsec_clock::universal_time();

                BL_CHK(
                    false,
                    connectionsCount < ( std::size_t ) INT_MAX,
                    BL_MSG()
                        << "Invalid value for connections "
                        << connectionsCount
                    );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Establishing "
                        << connectionsCount
                        << " connections...."
                    );

                for( std::size_t i = 0; i < connectionsCount; ++i )
                {
                    auto connector = connector_t::createInstance(
                        cpp::copy( UtfArgsParser::host() ),
                        UtfArgsParser::port()
                        );

                    const auto taskConnector = om::qi< tasks::Task >( connector.get() );
                    eq -> push_back( taskConnector );

                    connections.push_back( std::move( connector ) );
                }

                eq -> flush();

                const auto duration = bl::time::microsec_clock::universal_time() - t1;
                const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Established "
                        << connectionsCount
                        << " connections took "
                        << durationInSeconds
                        << " seconds; waiting for 5 seconds"
                    );

                os::sleep( time::seconds( 5 ) );
            }

            {
                const auto newBlock = BackendImplTestImpl::initDataBlock();
                newBlock -> setSize( 1024 );

                std::vector< om::ObjPtr< connection_t > > transfers;

                const auto chunkId = uuids::create();

                const std::size_t maxTransfers = 100;
                std::size_t actualTransfers = 0U;

                const auto t1 = bl::time::microsec_clock::universal_time();

                for( std::size_t i = 0; i < maxTransfers; ++i )
                {
                    auto transfer =
                        connection_t::createInstance(
                            connection_t::CommandId::NoCommand,
                            uuids::nil(),
                            dataBlocksPool
                            );

                    const std::size_t rndIndex = std::rand() % ( int ) connectionsCount;

                    const auto pos = connections.begin() + rndIndex;

                    auto& connector = *pos;

                    if( ! connector )
                    {
                        /*
                         * Used already
                         */

                        continue;
                    }

                    ++actualTransfers;

                    transfer -> attachStream( ( *pos ) -> detachStream() );
                    connector.reset();

                    const auto taskTransfer = om::qi< tasks::Task >( transfer.get() );

                    if( 0U == i )
                    {
                        transfer -> setChunkData( newBlock );
                    }
                    else
                    {
                        transfer -> setChunkData( nullptr );
                    }

                    transfer -> setCommandId(
                        0U == i ?
                            connection_t::CommandId::SendChunk :
                            (
                                ( maxTransfers - 1 ) == i ?
                                    connection_t::CommandId::RemoveChunk :
                                    connection_t::CommandId::ReceiveChunk
                            )
                        );

                    transfer -> setChunkId( chunkId );

                    eq -> push_back( taskTransfer );
                    eq -> waitForSuccess( taskTransfer );

                    if( ( maxTransfers - 1 ) != i )
                    {
                        BackendImplTestImpl::verifyData( transfer -> getChunkData() );
                    }

                    transfers.push_back( std::move( transfer ) );
                }

                const auto duration = bl::time::microsec_clock::universal_time() - t1;
                const auto durationInSeconds = duration.total_milliseconds() / 1000.0;

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Performing "
                        << actualTransfers
                        << " transfers took "
                        << durationInSeconds
                        << " seconds; waiting for 5 seconds"
                    );

                os::sleep( time::seconds( 5 ) );
            }
        }
        );
}

UTF_AUTO_TEST_CASE( IO_BinaryProtocolInvariants )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::tasks::detail;

    CommandBlock command;

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Size of command block is "
            << sizeof( command )
        );

    BL_LOG(
        Logging::debug(),
        BL_MSG()
            << "Size of command.data block is "
            << sizeof( command.data )
        );

    /*
     * Ensure that something breaks if the sizes change for some reason
     *
     * These structures and fields sizes should change very deliberately
     * and if some of these invariants have to change legitimately then
     * these values need to be adjusted
     */

    UTF_REQUIRE_EQUAL( sizeof( command ), 72U );
    UTF_REQUIRE_EQUAL( sizeof( command.data ), 28U );

    UTF_REQUIRE_EQUAL( sizeof( command.data.reserved.reserved1 ), sizeof( std::uint32_t ) );
    UTF_REQUIRE_EQUAL( sizeof( command.data.reserved.reserved2 ), sizeof( std::uint32_t ) );
    UTF_REQUIRE_EQUAL( sizeof( command.data.reserved.reserved3 ), sizeof( std::uint16_t ) );
    UTF_REQUIRE_EQUAL( sizeof( command.data.reserved.reserved4 ), sizeof( std::uint16_t ) );

    UTF_REQUIRE_EQUAL(
        sizeof( command.data.version.value ),
        sizeof( command.data.reserved.reserved1 )
        );

    UTF_REQUIRE_EQUAL(
        sizeof( command.data.blockInfo.flags ) + sizeof( command.data.blockInfo.unused ),
        sizeof( command.data.reserved.reserved1 )
        );

    UTF_REQUIRE_EQUAL(
        sizeof( command.data.blockInfo.protocolDataOffset ),
        sizeof( command.data.reserved.reserved2 )
        );

    UTF_REQUIRE_EQUAL(
        sizeof( command.data.blockInfo.blockType ),
        sizeof( command.data.reserved.reserved3 )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader, version ),
        offsetof( CommandBlock::DataHeader, reserved )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader, blockInfo ),
        offsetof( CommandBlock::DataHeader, reserved )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader, raw ),
        offsetof( CommandBlock::DataHeader, reserved )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader::tagVersion, value ),
        offsetof( CommandBlock::DataHeader::tagReserved, reserved1 )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader::tagBlockInfo, flags ),
        offsetof( CommandBlock::DataHeader::tagReserved, reserved1 )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader::tagBlockInfo, protocolDataOffset ),
        offsetof( CommandBlock::DataHeader::tagReserved, reserved2 )
        );

    UTF_REQUIRE_EQUAL(
        offsetof( CommandBlock::DataHeader::tagBlockInfo, blockType ),
        offsetof( CommandBlock::DataHeader::tagReserved, reserved3 )
        );

     UTF_REQUIRE(
        offsetof( CommandBlock::DataHeader::tagBlockInfo, flags ) <
        offsetof( CommandBlock::DataHeader::tagBlockInfo, unused )
        );

     UTF_REQUIRE(
        offsetof( CommandBlock::DataHeader::tagBlockInfo, unused ) <
        offsetof( CommandBlock::DataHeader::tagBlockInfo, protocolDataOffset )
        );

     UTF_REQUIRE(
        offsetof( CommandBlock::DataHeader::tagBlockInfo, protocolDataOffset ) <
        offsetof( CommandBlock::DataHeader::tagBlockInfo, blockType )
        );

    const auto printDataBytes = [ & ]() -> void
    {
        cpp::SafeOutputStringStream os;

        os << "{";
        for( std::size_t i = 0; i < sizeof( command.data.raw.bytes ); ++i )
        {
            if( i )
            {
                os << ", ";
            }

            os
                << "["
                << i
                << " : "
                << static_cast< int >( command.data.raw.bytes[ i ] )
                << "]";
        }
        os << "}";

        BL_LOG(
            Logging::debug(),
            BL_MSG()
                << "Data block bytes: "
                << os.str()
            );
    };

    const auto ensureAllZeros = [ & ]( SAA_in const std::size_t startPos ) -> void
    {
        for( std::size_t i = startPos; i < sizeof( command.data.raw.bytes ); ++i )
        {
            UTF_REQUIRE( command.data.raw.bytes[ i ] == 0U );
        }
    };

    /*
     * These tests below assume little endian architecture which is currently the case
     * for all architectures we are actually supporting
     *
     * In the future when/if we add big endian architectures these tests must be
     * adjusted
     *
     * Note that the blob server code does support both big and little endian as it
     * always converts number on the wire to network byte order format (which is same
     * as big endian actually)
     */

    {
        /*
         * Test the command.data.version fields
         */

        command = CommandBlock();
        command.data.version.value = CommandBlock::BLOB_TRANSFER_PROTOCOL_SERVER_VERSION;

        printDataBytes();

        UTF_REQUIRE( command.data.raw.bytes[ 0 ] == CommandBlock::BLOB_TRANSFER_PROTOCOL_SERVER_VERSION );

        ensureAllZeros( 1U );
    }

    {
        /*
         * Test the command.data.blockInfo fields
         */

        command = CommandBlock();
        command.data.blockInfo.flags = CommandBlock::IgnoreIfNotFound;
        command.data.blockInfo.protocolDataOffset = 0xFFFFFFFF;
        command.data.blockInfo.blockType = BlockTransferDefs::BlockType::TransferOnly;

        printDataBytes();

        const auto blockTypeAsUshort =
            static_cast< std::uint16_t >( BlockTransferDefs::BlockType::TransferOnly );

        UTF_REQUIRE( command.data.raw.bytes[ 0 ] == CommandBlock::IgnoreIfNotFound );
        UTF_REQUIRE( command.data.raw.bytes[ 1 ] == 0U );
        UTF_REQUIRE( command.data.raw.bytes[ 2 ] == 0U );
        UTF_REQUIRE( command.data.raw.bytes[ 3 ] == 0U );

        UTF_REQUIRE( command.data.raw.bytes[ 4 ] == 0xFF );
        UTF_REQUIRE( command.data.raw.bytes[ 5 ] == 0xFF );
        UTF_REQUIRE( command.data.raw.bytes[ 6 ] == 0xFF );
        UTF_REQUIRE( command.data.raw.bytes[ 7 ] == 0xFF );

        UTF_REQUIRE( command.data.raw.bytes[ 8 ] == blockTypeAsUshort );

        ensureAllZeros( 9U );
    }

    /*
     * The three blocks below exercise host2Network() / network2Host() themselves
     *
     * The conversions are applied symmetrically on both ends of every connection and every
     * test runs both ends from the same build, so dropping a field from both methods at once
     * (or byte swapping a uuid) leaves the suite green while silently breaking interop with a
     * peer built from a different revision - and the failure mode is a desynchronized stream,
     * not a decode error
     *
     * Each block starts from a freshly default constructed CommandBlock because that is the
     * only state guaranteed to have data.blockInfo.unused == 0, which both conversions require
     */

    const auto byteAt = []( SAA_in const void* p, SAA_in const std::size_t i ) -> unsigned
    {
        return static_cast< const std::uint8_t* >( p )[ i ];
    };

    {
        /*
         * The wire image must be big endian for every numeric field, and must leave both
         * uuids and the reserved uuid inside DataHeader completely untouched
         *
         * Like the blocks above these expectations are the little endian host images
         */

        command = CommandBlock();

        command.cntrlCode = CommandBlock::CntrlCodePutDataBlock;
        command.flags = CommandBlock::AckBit;
        command.errorCode = 0x01020304U;
        command.chunkSize = 0x0A0B0C0DU;
        command.peerId = uuids::string2uuid( "8ba5f6c4-2fd0-4d0e-9e0e-1b3fd1c2a4d7" );
        command.chunkId = uuids::string2uuid( "1d7e3f52-4c9a-4a1b-8d63-9f0c7ea5b218" );
        command.data.blockInfo.flags = CommandBlock::IgnoreIfNotFound;
        command.data.blockInfo.protocolDataOffset = 0x11223344U;
        command.data.blockInfo.blockType = BlockTransferDefs::BlockType::TransferOnly;

        const CommandBlock original = command;

        command.host2Network();

        printDataBytes();

        /*
         * CntrlCodePutDataBlock is 5 and it is a std::uint16_t
         */

        UTF_REQUIRE_EQUAL( byteAt( &command.cntrlCode, 0U ), 0U );
        UTF_REQUIRE_EQUAL( byteAt( &command.cntrlCode, 1U ), 5U );

        UTF_REQUIRE_EQUAL( byteAt( &command.errorCode, 0U ), 0x01U );
        UTF_REQUIRE_EQUAL( byteAt( &command.errorCode, 1U ), 0x02U );
        UTF_REQUIRE_EQUAL( byteAt( &command.errorCode, 2U ), 0x03U );
        UTF_REQUIRE_EQUAL( byteAt( &command.errorCode, 3U ), 0x04U );

        UTF_REQUIRE_EQUAL( byteAt( &command.chunkSize, 0U ), 0x0AU );
        UTF_REQUIRE_EQUAL( byteAt( &command.chunkSize, 1U ), 0x0BU );
        UTF_REQUIRE_EQUAL( byteAt( &command.chunkSize, 2U ), 0x0CU );
        UTF_REQUIRE_EQUAL( byteAt( &command.chunkSize, 3U ), 0x0DU );

        /*
         * The big endian image of reserved1, whose host value is
         * flags | ( unused << 16 ) == 1
         */

        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 0 ], 0x00U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 1 ], 0x00U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 2 ], 0x00U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 3 ], 0x01U );

        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 4 ], 0x11U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 5 ], 0x22U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 6 ], 0x33U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 7 ], 0x44U );

        /*
         * BlockType::TransferOnly is 3
         */

        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 8 ], 0x00U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 9 ], 0x03U );

        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 10 ], 0x00U );
        UTF_REQUIRE_EQUAL( command.data.raw.bytes[ 11 ], 0x00U );

        /*
         * The uuids are byte sequences and must never be byte swapped
         */

        UTF_REQUIRE_EQUAL( command.peerId, original.peerId );
        UTF_REQUIRE_EQUAL( command.chunkId, original.chunkId );

        UTF_REQUIRE_EQUAL(
            0,
            std::memcmp( &command.data.raw.bytes[ 12 ], &original.data.raw.bytes[ 12 ], 16U )
            );

        /*
         * The round trip must restore the value bit for bit; CommandBlock is exactly
         * 72 bytes with no padding, so memcmp over the whole struct is well defined
         */

        command.network2Host();

        UTF_REQUIRE_EQUAL( 0, std::memcmp( &command, &original, sizeof( command ) ) );

        UTF_REQUIRE_EQUAL( command.cntrlCode, original.cntrlCode );
        UTF_REQUIRE_EQUAL( command.flags, original.flags );
        UTF_REQUIRE_EQUAL( command.errorCode, original.errorCode );
        UTF_REQUIRE_EQUAL( command.chunkSize, original.chunkSize );
        UTF_REQUIRE_EQUAL( command.data.blockInfo.flags, original.data.blockInfo.flags );

        UTF_REQUIRE_EQUAL(
            command.data.blockInfo.protocolDataOffset,
            original.data.blockInfo.protocolDataOffset
            );

        UTF_REQUIRE(
            command.data.blockInfo.blockType == original.data.blockInfo.blockType
            );
    }

    {
        /*
         * The 'unused' invariant is the only guard which keeps the aliasing between
         * data.reserved and data.blockInfo honest - one sub-case per direction
         */

        CommandBlock bad;
        bad.data.blockInfo.unused = 1U;

        UTF_REQUIRE_THROW_MESSAGE(
            bad.host2Network(),
            bl::UnexpectedException,
            "The 'data.blockInfo.unused' field is expected to be zero"
            );

        CommandBlock bad2;
        bad2.data.blockInfo.unused = 1U;
        bad2.data.reserved.reserved1 = os::host2NetworkLong( bad2.data.reserved.reserved1 );

        UTF_REQUIRE_THROW_MESSAGE(
            bad2.network2Host(),
            bl::UnexpectedException,
            "The 'data.blockInfo.unused' field is expected to be zero"
            );
    }
}

/************************************************************************
 * The blob server protocol version negotiation negatives
 *
 * None of these frames can be produced by TcpBlockTransferClientConnectionT - it always
 * interposes a SetProtocolVersion before any user command and its clientVersion() setter
 * rejects anything which is not V1 or V2 - so the whole forward / backward compatibility
 * contract of the protocol is only reachable over a raw socket
 */

UTF_AUTO_TEST_CASE( IO_BlockTransferServerProtocolNegotiationNegativeTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::tasks::detail;
    using namespace utest;

    typedef bl::tasks::TcpBlockServerDataChunkStorage                                       acceptor_t;
    typedef acceptor_t::async_wrapper_t                                                     async_wrapper_t;
    typedef async_wrapper_t::backend_interface_t                                            backend_interface_t;

    test::MachineGlobalTestLock lock;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
    const auto dataBlocksPool = datablocks_pool_type::createInstance();
    const auto backendImpl = BackendImplTestImpl::createInstance();

    const auto backend = om::lockDisposable(
        async_wrapper_t::createInstance<>(
            om::qi< backend_interface_t >( backendImpl )    /* writeBackend */,
            om::qi< backend_interface_t >( backendImpl )    /* readBackend */,
            test::UtfArgsParser::threadsCount(),
            om::qi< TaskControlToken >( controlToken ),
            0U                                              /* maxConcurrentTasks */,
            dataBlocksPool
            )
        );

    /*
     * Both peer ids must be non-nil and distinct so the peer id selection in
     * scheduleResponseCommand() can be told apart in the acks
     */

    const auto serverPeerId = uuids::create();
    const auto clientPeerId = uuids::create();

    UTF_REQUIRE( serverPeerId != clientPeerId );

    const auto acceptor = acceptor_t::createInstance< acceptor_t >(
        controlToken,
        dataBlocksPool,
        "localhost",
        28100,
        bl::str::empty()                                    /* privateKeyPem */,
        bl::str::empty()                                    /* certificatePem */,
        backend,
        serverPeerId
        );

    UTF_REQUIRE( acceptor );

    TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            tasks::scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
                {
                    const auto connector = connector_t::createInstance< connector_t >( "localhost", 28100 );
                    const auto taskConnector = om::qi< tasks::Task >( connector.get() );

                    eq -> push_back( taskConnector );
                    eq -> waitForSuccess( taskConnector );

                    const auto stream = connector -> detachStream();

                    UTF_REQUIRE( stream );

                    /*
                     * All the sub-scenarios below run on the same socket and in this order -
                     * after an ErrBit ack the server calls scheduleReadCommand( true ), so the
                     * connection survives every rejection
                     */

                    const auto protocolNotSupported = static_cast< std::uint32_t >(
                        eh::errc::make_error_code( eh::errc::protocol_not_supported ).value()
                        );

                    const auto makePutCommand = [ & ]() -> CommandBlock
                    {
                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodePutDataBlock;
                        command.chunkId = uuids::create();
                        command.chunkSize = 16U;
                        command.peerId = clientPeerId;
                        command.data.blockInfo.blockType = BlockTransferDefs::BlockType::Normal;

                        return command;
                    };

                    const auto chkPreNegotiationRejection = [ & ]() -> void
                    {
                        sendCommand( *stream, makePutCommand() );

                        const auto ack = recvCommand( *stream );

                        UTF_REQUIRE_EQUAL( ack.cntrlCode, CommandBlock::CntrlCodePutDataBlock );
                        UTF_REQUIRE( ack.flags & CommandBlock::AckBit );
                        UTF_REQUIRE( ack.flags & CommandBlock::ErrBit );
                        UTF_REQUIRE_EQUAL( ack.errorCode, protocolNotSupported );

                        UTF_REQUIRE_EQUAL( backendImpl -> saveCalls(), 0U );
                    };

                    /*
                     * (i) a data command sent before any negotiation must be rejected
                     */

                    chkPreNegotiationRejection();

                    chkTaskCompletedOkOrRunning( acceptor );

                    /*
                     * (ii) CntrlCodeGetProtocolVersion is answered with the server version and
                     * the ack carries the server's own peer id
                     */

                    {
                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodeGetProtocolVersion;
                        command.peerId = clientPeerId;

                        sendCommand( *stream, command );

                        const auto ack = recvCommand( *stream );

                        UTF_REQUIRE_EQUAL( ack.cntrlCode, CommandBlock::CntrlCodeGetProtocolVersion );
                        UTF_REQUIRE( ack.flags & CommandBlock::AckBit );
                        UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );
                        UTF_REQUIRE_EQUAL( ack.errorCode, 0U );

                        UTF_REQUIRE_EQUAL(
                            ack.data.version.value,
                            static_cast< std::uint32_t >( CommandBlock::BLOB_TRANSFER_PROTOCOL_SERVER_VERSION )
                            );

                        UTF_REQUIRE_EQUAL( ack.peerId, serverPeerId );
                    }

                    /*
                     * (iii) getting the version must not implicitly enable commands
                     */

                    chkPreNegotiationRejection();

                    /*
                     * (iv) a client version newer than the server's is rejected
                     */

                    {
                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodeSetProtocolVersion;
                        command.peerId = clientPeerId;
                        command.data.version.value = CommandBlock::BLOB_TRANSFER_PROTOCOL_SERVER_VERSION + 1;

                        sendCommand( *stream, command );

                        const auto ack = recvCommand( *stream );

                        UTF_REQUIRE( ack.flags & CommandBlock::ErrBit );
                        UTF_REQUIRE_EQUAL( ack.errorCode, protocolNotSupported );
                        UTF_REQUIRE_EQUAL( ack.peerId, serverPeerId );
                    }

                    /*
                     * (v) an older / equal client version is accepted - the changes are
                     * expected to be backward compatible
                     */

                    {
                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodeSetProtocolVersion;
                        command.peerId = clientPeerId;
                        command.data.version.value = CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1;

                        sendCommand( *stream, command );

                        const auto ack = recvCommand( *stream );

                        UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );
                        UTF_REQUIRE_EQUAL( ack.peerId, serverPeerId );
                    }

                    /*
                     * (vi) now that the version is negotiated a real command is accepted, and
                     * its ack carries m_remotePeerId - the peer id the server learned from the
                     * negotiation exchange - instead of its own
                     */

                    {
                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodeRemoveDataBlock;
                        command.chunkId = uuids::create();
                        command.peerId = clientPeerId;
                        command.data.blockInfo.blockType = BlockTransferDefs::BlockType::Normal;
                        command.data.blockInfo.flags = CommandBlock::IgnoreIfNotFound;

                        sendCommand( *stream, command );

                        const auto ack = recvCommand( *stream );

                        UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );
                        UTF_REQUIRE_EQUAL( backendImpl -> removeCalls(), 1U );
                        UTF_REQUIRE_EQUAL( ack.peerId, clientPeerId );
                    }

                    chkTaskCompletedOkOrRunning( acceptor );

                    backendImpl -> assertions().requireNone();
                }
                );
        },
        acceptor,
        "localhost",
        28100
        );
}

/************************************************************************
 * The blob server malformed command ladder and the blast radius of a bad frame
 *
 * Every one of these branches is unreachable through TcpBlockTransferClientConnectionT,
 * which validates block types, chunk ids and the protocol data offset before it puts
 * anything on the wire - so these are the server's only defences against a malformed or
 * hostile peer on a port which has no transport level authentication
 */

UTF_AUTO_TEST_CASE( IO_BlockTransferServerMalformedCommandTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::tasks::detail;
    using namespace utest;

    typedef bl::tasks::TcpBlockServerDataChunkStorage                                       acceptor_t;
    typedef acceptor_t::async_wrapper_t                                                     async_wrapper_t;
    typedef async_wrapper_t::backend_interface_t                                            backend_interface_t;

    test::MachineGlobalTestLock lock;

    /*
     * Every rejection below logs a warning naming the malformed field, and the unit test
     * harness turns a warning line into a test failure, so the level is lowered for the
     * duration of the case - these warnings are the expected output of a negative test
     */

    const Logging::LevelPusher pushLevel( Logging::LL_ERROR, true /* global */ );

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
    const auto dataBlocksPool = datablocks_pool_type::createInstance();
    const auto backendImpl = BackendImplTestImpl::createInstance();

    const auto backend = om::lockDisposable(
        async_wrapper_t::createInstance<>(
            om::qi< backend_interface_t >( backendImpl )    /* writeBackend */,
            om::qi< backend_interface_t >( backendImpl )    /* readBackend */,
            test::UtfArgsParser::threadsCount(),
            om::qi< TaskControlToken >( controlToken ),
            0U                                              /* maxConcurrentTasks */,
            dataBlocksPool
            )
        );

    const auto serverPeerId = uuids::create();
    const auto clientPeerId = uuids::create();

    const auto acceptor = acceptor_t::createInstance< acceptor_t >(
        controlToken,
        dataBlocksPool,
        "localhost",
        28100,
        bl::str::empty()                                    /* privateKeyPem */,
        bl::str::empty()                                    /* certificatePem */,
        backend,
        serverPeerId
        );

    UTF_REQUIRE( acceptor );

    TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            tasks::scheduleAndExecuteInParallel(
                [ & ]( SAA_in const om::ObjPtr< tasks::ExecutionQueue >& eq ) -> void
                {
                    const auto connectStream = [ & ]() -> connector_t::stream_ref
                    {
                        const auto connector =
                            connector_t::createInstance< connector_t >( "localhost", 28100 );

                        const auto taskConnector = om::qi< tasks::Task >( connector.get() );

                        eq -> push_back( taskConnector );
                        eq -> waitForSuccess( taskConnector );

                        return connector -> detachStream();
                    };

                    const auto negotiateV2 = [ & ]( SAA_inout connector_t::stream_t& stream ) -> void
                    {
                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodeSetProtocolVersion;
                        command.peerId = clientPeerId;
                        command.data.version.value = CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2;

                        sendCommand( stream, command );

                        const auto ack = recvCommand( stream );

                        UTF_REQUIRE( ack.flags & CommandBlock::AckBit );
                        UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );
                    };

                    const auto invalidArgument = static_cast< std::uint32_t >(
                        eh::errc::make_error_code( eh::errc::invalid_argument ).value()
                        );

                    const auto chkRejectedWithEinval = [ & ](
                        SAA_inout           connector_t::stream_t&                          stream,
                        SAA_in              const CommandBlock&                             command
                        )
                        -> void
                    {
                        sendCommand( stream, command );

                        const auto ack = recvCommand( stream );

                        UTF_REQUIRE_EQUAL( ack.cntrlCode, command.cntrlCode );
                        UTF_REQUIRE( ack.flags & CommandBlock::AckBit );
                        UTF_REQUIRE( ack.flags & CommandBlock::ErrBit );
                        UTF_REQUIRE_EQUAL( ack.errorCode, invalidArgument );

                        /*
                         * A rejected frame must never have reached the storage backend
                         */

                        UTF_REQUIRE_EQUAL( backendImpl -> saveCalls(), 0U );
                        UTF_REQUIRE_EQUAL( backendImpl -> loadCalls(), 0U );
                        UTF_REQUIRE_EQUAL( backendImpl -> removeCalls(), 0U );
                    };

                    {
                        /*
                         * The malformed command ladder - all of it on one socket, so that
                         * getting a reply to the next sub-scenario is itself the proof that
                         * the connection survived the previous rejection
                         */

                        const auto stream = connectStream();

                        UTF_REQUIRE( stream );

                        negotiateV2( *stream );

                        {
                            /*
                             * (1) an unknown control code once the version is set
                             */

                            CommandBlock command;

                            command.cntrlCode = CommandBlock::CntrlCodeNone;
                            command.peerId = clientPeerId;

                            chkRejectedWithEinval( *stream, command );
                        }

                        {
                            /*
                             * (2) a block type outside the enum
                             */

                            CommandBlock command;

                            command.cntrlCode = CommandBlock::CntrlCodePutDataBlock;
                            command.chunkId = uuids::create();
                            command.chunkSize = 16U;
                            command.peerId = clientPeerId;
                            command.data.blockInfo.blockType =
                                static_cast< BlockTransferDefs::BlockType >( 99 );

                            chkRejectedWithEinval( *stream, command );
                        }

                        {
                            /*
                             * (3) a Normal block with a nil chunk id
                             */

                            CommandBlock command;

                            command.cntrlCode = CommandBlock::CntrlCodePutDataBlock;
                            command.chunkId = uuids::nil();
                            command.chunkSize = 16U;
                            command.peerId = clientPeerId;
                            command.data.blockInfo.blockType = BlockTransferDefs::BlockType::Normal;

                            chkRejectedWithEinval( *stream, command );
                        }

                        {
                            /*
                             * (4) an Authentication block is only valid with a PUT
                             *
                             * The chunk id must be chunkIdDefault() - the server opens this
                             * branch with a BL_ASSERT on exactly that, which would abort a
                             * debug build otherwise
                             */

                            CommandBlock command;

                            command.cntrlCode = CommandBlock::CntrlCodeGetDataBlockSize;
                            command.chunkId = BlockTransferDefs::chunkIdDefault();
                            command.peerId = clientPeerId;
                            command.data.blockInfo.blockType = BlockTransferDefs::BlockType::Authentication;

                            chkRejectedWithEinval( *stream, command );
                        }

                        {
                            /*
                             * (5) a ServerState block is only valid with a GET size / GET
                             */

                            CommandBlock command;

                            command.cntrlCode = CommandBlock::CntrlCodePutDataBlock;
                            command.chunkId = BlockTransferDefs::chunkIdDefault();
                            command.chunkSize = 16U;
                            command.peerId = clientPeerId;
                            command.data.blockInfo.blockType = BlockTransferDefs::BlockType::ServerState;

                            chkRejectedWithEinval( *stream, command );
                        }

                        {
                            /*
                             * The connection is still fully usable after the last rejection
                             */

                            CommandBlock command;

                            command.cntrlCode = CommandBlock::CntrlCodeGetProtocolVersion;
                            command.peerId = clientPeerId;

                            sendCommand( *stream, command );

                            const auto ack = recvCommand( *stream );

                            UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );

                            UTF_REQUIRE_EQUAL(
                                ack.data.version.value,
                                static_cast< std::uint32_t >(
                                    CommandBlock::BLOB_TRANSFER_PROTOCOL_SERVER_VERSION
                                    )
                                );
                        }
                    }

                    {
                        /*
                         * (6) a PUT whose protocolDataOffset is past the announced chunkSize
                         *
                         * This one is caught only after the payload has been read, by
                         * setOffset1Checked(), which fails the connection task - so it gets a
                         * socket of its own. Without that check every downstream consumer
                         * computes size() - offset1(), which wraps for an offset past the end
                         */

                        const auto stream = connectStream();

                        UTF_REQUIRE( stream );

                        negotiateV2( *stream );

                        CommandBlock command;

                        command.cntrlCode = CommandBlock::CntrlCodePutDataBlock;
                        command.chunkId = uuids::create();
                        command.chunkSize = 16U;
                        command.peerId = clientPeerId;
                        command.data.blockInfo.blockType = BlockTransferDefs::BlockType::Normal;
                        command.data.blockInfo.protocolDataOffset = 1024U;

                        sendCommand( *stream, command );

                        const auto ack = recvCommand( *stream );

                        UTF_REQUIRE( ack.flags & CommandBlock::AckBit );
                        UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );

                        const std::string payload( 16U, 'X' );

                        bl::asio::write( *stream, bl::asio::buffer( payload.c_str(), payload.size() ) );

                        CommandBlock trailing;
                        eh::error_code ec;

                        ( void ) bl::asio::read(
                            *stream,
                            bl::asio::buffer( &trailing, sizeof( trailing ) ),
                            ec
                            );

                        UTF_REQUIRE( ec );

                        UTF_REQUIRE(
                            TcpSocketCommonBase::isExpectedSocketException( false /* isCancelExpected */, &ec ) ||
                            asio::error::eof == ec
                            );

                        UTF_REQUIRE_EQUAL( backendImpl -> saveCalls(), 0U );
                    }

                    {
                        /*
                         * (7) the malformed frame killed one connection, not the acceptor
                         */

                        chkTaskCompletedOkOrRunning( acceptor );

                        const auto stream = connectStream();

                        UTF_REQUIRE( stream );

                        negotiateV2( *stream );

                        chkTaskCompletedOkOrRunning( acceptor );
                    }

                    backendImpl -> assertions().requireNone();
                }
                );
        },
        acceptor,
        "localhost",
        28100
        );
}

/************************************************************************
 * The block transfer client command state, the payload truth table and
 * the errorCode-without-ErrBit protocol guard
 *
 * The chunk id normalization is the wire contract for non-Normal blocks - the server opens
 * the Authentication / ServerState / TransferOnly branches with
 * BL_ASSERT( m_cmdBuffer.chunkId == BlockTransferDefs::chunkIdDefault() ) - so a change here
 * turns into a debug build abort on the *server*, far away from its cause
 */

UTF_AUTO_TEST_CASE( IO_BlockTransferClientCommandStateTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::tasks::detail;

    typedef PayloadProbe::CommandId                                                         CommandId;
    typedef BlockTransferDefs::BlockType                                                    BlockType;

    /*
     * No acceptor and no socket - the connections below are never scheduled
     */

    const auto dataBlocksPool = datablocks_pool_type::createInstance();

    const auto realChunkId = uuids::create();

    UTF_REQUIRE( realChunkId != uuids::nil() );
    UTF_REQUIRE( realChunkId != BlockTransferDefs::chunkIdDefault() );

    const auto c1 = PayloadProbeImpl::createInstance(
        CommandId::NoCommand,
        uuids::create()                     /* peerId */,
        dataBlocksPool
        );

    {
        /*
         * (1) the default state of a Normal / V1 connection created with NoCommand
         */

        UTF_REQUIRE_EQUAL(
            c1 -> clientVersion(),
            static_cast< std::uint32_t >( CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V1 )
            );

        UTF_REQUIRE( CommandId::NoCommand == c1 -> getCommandId() );
        UTF_REQUIRE( BlockType::Normal == c1 -> getBlockType() );
        UTF_REQUIRE_EQUAL( c1 -> getChunkId(), uuids::nil() );
        UTF_REQUIRE( ! c1 -> isClientVersionNegotiated() );
        UTF_REQUIRE( ! c1 -> isAuthenticated() );
        UTF_REQUIRE( ! c1 -> protocolOperationsOnly() );
        UTF_REQUIRE( ! c1 -> getChunkData() );
        UTF_REQUIRE( nullptr == c1 -> getChunkDataPtr() );
        UTF_REQUIRE_EQUAL( c1 -> targetPeerId(), uuids::nil() );
    }

    {
        /*
         * (2) the chunk id normalization table for a Normal block
         */

        c1 -> setCommandInfo( CommandId::ReceiveChunk, BlockTransferDefs::chunkIdDefault() );
        UTF_REQUIRE_EQUAL( c1 -> getChunkId(), uuids::nil() );

        c1 -> setCommandInfo( CommandId::ReceiveChunk, realChunkId );
        UTF_REQUIRE_EQUAL( c1 -> getChunkId(), realChunkId );

        c1 -> setCommandInfo( CommandId::FlushPeerSessions, realChunkId );
        UTF_REQUIRE_EQUAL( c1 -> getChunkId(), uuids::nil() );

        c1 -> setChunkId( realChunkId );
        UTF_REQUIRE_EQUAL( c1 -> getChunkId(), uuids::nil() );

        /*
         * Restore a plain state for the sub-scenarios below
         */

        c1 -> setCommandInfo( CommandId::NoCommand );
        UTF_REQUIRE_EQUAL( c1 -> getChunkId(), uuids::nil() );
    }

    /*
     * (3) a V2 connection - the constructor derives V2 from the non-Normal block type, and any
     * chunk id is forced to chunkIdDefault()
     *
     * The command id must be a real one: NoCommand forces uuids::nil() regardless of the block
     * type and the constructor itself never runs the normalization
     */

    const auto c2 = PayloadProbeImpl::createInstance(
        CommandId::ReceiveChunk,
        uuids::create()                     /* peerId */,
        dataBlocksPool,
        BlockType::ServerState
        );

    {
        UTF_REQUIRE_EQUAL(
            c2 -> clientVersion(),
            static_cast< std::uint32_t >( CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2 )
            );

        c2 -> setChunkId( uuids::create() );
        UTF_REQUIRE_EQUAL( c2 -> getChunkId(), BlockTransferDefs::chunkIdDefault() );

        c2 -> setChunkId( BlockTransferDefs::chunkIdDefault() );
        UTF_REQUIRE_EQUAL( c2 -> getChunkId(), BlockTransferDefs::chunkIdDefault() );
    }

    {
        /*
         * (4) an out of range block type
         *
         * chkBlockTypeIsSupported checks the negotiated version *before* the >= Count check, so
         * the V1 connection reports the version message instead
         */

        UTF_REQUIRE_THROW_MESSAGE(
            c2 -> setBlockType( static_cast< BlockType >( 99 ) ),
            bl::ArgumentException,
            "Invalid block type was specified"
            );

        UTF_REQUIRE_THROW_MESSAGE(
            c1 -> setBlockType( static_cast< BlockType >( 99 ) ),
            bl::ArgumentException,
            "This block type requires V2 of the blob server protocol"
            );

        UTF_REQUIRE( BlockType::ServerState == c2 -> getBlockType() );
        UTF_REQUIRE( BlockType::Normal == c1 -> getBlockType() );
    }

    {
        /*
         * (5) the clientVersion() setter only accepts V1 and V2
         */

        const auto versionBefore = c1 -> clientVersion();

        UTF_REQUIRE_THROW( c1 -> clientVersion( 3U ), bl::ArgumentException );
        UTF_REQUIRE_EQUAL( c1 -> clientVersion(), versionBefore );

        const auto versionBefore2 = c2 -> clientVersion();

        UTF_REQUIRE_THROW( c2 -> clientVersion( 0U ), bl::ArgumentException );
        UTF_REQUIRE_EQUAL( c2 -> clientVersion(), versionBefore2 );
    }

    {
        /*
         * (6) setCommandInfo() installs the new block and resets the target peer id back to
         * the current remote peer id - which is what every caller relies on between two
         * rescheduled commands
         */

        const auto block = DataBlock::get( dataBlocksPool );

        block -> setSize( 1024U );
        block -> setOffset1( 512U );

        c1 -> targetPeerId( uuids::create() );

        c1 -> setCommandInfo( CommandId::SendChunk, realChunkId, om::copy( block ) );

        UTF_REQUIRE( c1 -> getChunkDataPtr() == block.get() );
        UTF_REQUIRE_EQUAL( c1 -> targetPeerId(), c1 -> remotePeerId() );
    }

    {
        /*
         * (7) the payload truth table
         *
         * Every row is driven through setCommandInfoRawPtr(), which installs the command id,
         * the (normalized) chunk id and the data pointer in one call
         */

        const auto probe = PayloadProbeImpl::createInstance(
            CommandId::NoCommand,
            uuids::create()                 /* peerId */,
            dataBlocksPool
            );

        const auto block = DataBlock::get( dataBlocksPool );

        block -> setSize( 1024U );

        const auto chkRow = [ & ](
            SAA_in              const CommandId                                  commandId,
            SAA_in              const bl::uuid_t&                                chunkId,
            SAA_in              data::DataBlock*                                 dataRawPtr,
            SAA_in              const bool                                       expected
            )
            -> void
        {
            probe -> setCommandInfoRawPtr( commandId, chunkId, dataRawPtr );

            UTF_REQUIRE_EQUAL( probe -> validatePayload(), expected );
        };

        /*
         * NoCommand - the chunk id is always normalized to nil, so only the data varies
         */

        chkRow( CommandId::NoCommand, uuids::nil(), nullptr, true );
        chkRow( CommandId::NoCommand, realChunkId, block.get(), false );

        /*
         * SendChunk requires both a chunk id and the data
         */

        chkRow( CommandId::SendChunk, uuids::nil(), nullptr, false );
        chkRow( CommandId::SendChunk, uuids::nil(), block.get(), false );
        chkRow( CommandId::SendChunk, realChunkId, nullptr, false );
        chkRow( CommandId::SendChunk, realChunkId, block.get(), true );

        /*
         * ReceiveChunk requires a chunk id and does not care about the data
         */

        chkRow( CommandId::ReceiveChunk, uuids::nil(), nullptr, false );
        chkRow( CommandId::ReceiveChunk, uuids::nil(), block.get(), false );
        chkRow( CommandId::ReceiveChunk, realChunkId, nullptr, true );
        chkRow( CommandId::ReceiveChunk, realChunkId, block.get(), true );

        /*
         * RemoveChunk requires a chunk id and no data
         */

        chkRow( CommandId::RemoveChunk, uuids::nil(), nullptr, false );
        chkRow( CommandId::RemoveChunk, uuids::nil(), block.get(), false );
        chkRow( CommandId::RemoveChunk, realChunkId, nullptr, true );
        chkRow( CommandId::RemoveChunk, realChunkId, block.get(), false );

        /*
         * FlushPeerSessions requires neither - the chunk id is always normalized to nil
         */

        chkRow( CommandId::FlushPeerSessions, realChunkId, nullptr, true );
        chkRow( CommandId::FlushPeerSessions, realChunkId, block.get(), false );

        /*
         * A command id outside the enum lands in the default arm
         */

        chkRow( static_cast< CommandId >( 99 ), realChunkId, nullptr, false );
        chkRow( static_cast< CommandId >( 99 ), realChunkId, block.get(), false );
    }

    {
        /*
         * (8) the errorCode-without-ErrBit protocol guard
         *
         * Unlike validatePayload() this one is a real BL_CHK and is present in release
         */

        const auto probe = PayloadProbeImpl::createInstance(
            CommandId::NoCommand,
            uuids::create()                 /* peerId */,
            dataBlocksPool
            );

        probe -> setAckState( CommandBlock::AckBit /* ErrBit is clear */, 42U /* errorCode */ );

        UTF_REQUIRE_THROW_MESSAGE(
            probe -> chk4ServerErrorsClient(),
            bl::UnexpectedException,
            "error flag is not set"
            );

        probe -> setAckState( CommandBlock::AckBit /* ErrBit is clear */, 0U /* errorCode */ );

        UTF_REQUIRE_NO_THROW( probe -> chk4ServerErrorsClient() );

        probe -> setAckState( CommandBlock::AckBit | CommandBlock::ErrBit, 42U /* errorCode */ );

        try
        {
            probe -> chk4ServerErrorsClient();

            UTF_FAIL( "chk4ServerErrorsClient() must throw when the error bit is set" );
        }
        catch( bl::ServerErrorException& e )
        {
            const auto* errorNo = eh::get_error_info< eh::errinfo_errno >( e );

            UTF_REQUIRE( errorNo );
            UTF_REQUIRE_EQUAL( *errorNo, 42 );
        }
    }
}

/************************************************************************
 * RemoveChunk + IgnoreIfNotFound - the ENOENT suppression and its without-flag contrast arm
 *
 * chk4ServerErrors() resets errorValue to zero for exactly one combination - the control
 * code is CntrlCodeRemoveDataBlock, the peer set IgnoreIfNotFound, and the errno is
 * no_such_file_or_directory - so a delete which finds nothing reports success. The shipped
 * client sets that flag unconditionally in scheduleRemoveData(), which is why the
 * complementary half needs a hand built command block on a raw socket
 */

UTF_AUTO_TEST_CASE( IO_BlockTransferRemoveIgnoreIfNotFoundTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::tasks::detail;
    using namespace utest;

    typedef bl::tasks::TcpBlockServerDataChunkStorage                                       acceptor_t;

    test::MachineGlobalTestLock lock;

    fs::TmpDir tempDir;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
    const auto dataBlocksPool = datablocks_pool_type::createInstance();

    /*
     * DataChunkStorageFilesystem::remove() is what actually raises ENOENT for a chunk which
     * is not there - utest::BackendImplTestImpl never does
     */

    const auto syncStorage = om::lockDisposable(
        DataChunkStorageFilesystemMultiFiles::createInstance< data::DataChunkStorage >(
            cpp::copy( tempDir.path() )
            )
        );

    const auto asyncStorage = om::lockDisposable(
        AsyncDataChunkStorage::createInstance(
            syncStorage                                     /* writeStorage */,
            syncStorage                                     /* readStorage */,
            test::UtfArgsParser::threadsCount(),
            om::qi< TaskControlToken >( controlToken ),
            0U                                              /* maxConcurrentTasks */,
            dataBlocksPool
            )
        );

    const auto acceptor = acceptor_t::createInstance< acceptor_t >(
        controlToken,
        dataBlocksPool,
        "localhost",
        28100,
        bl::str::empty()                                    /* privateKeyPem */,
        bl::str::empty()                                    /* certificatePem */,
        asyncStorage
        );

    UTF_REQUIRE( acceptor );

    TestTaskUtils::startAcceptorAndExecuteCallback(
        [ & ]() -> void
        {
            const auto eq = om::lockDisposable(
                ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
                );

            const auto connector = connector_t::createInstance< connector_t >( "localhost", 28100 );
            const auto taskConnector = om::qi< tasks::Task >( connector.get() );

            eq -> push_back( taskConnector );
            eq -> waitForSuccess( taskConnector );

            const auto transfer = connection_t::createInstance(
                connection_t::CommandId::RemoveChunk,
                uuids::create()                             /* peerId */,
                dataBlocksPool
                );

            transfer -> attachStream( connector -> detachStream() );

            const auto taskTransfer = om::qi< tasks::Task >( transfer );

            const auto neverSavedChunkId = uuids::create();

            {
                /*
                 * (a) removing a chunk which was never saved must succeed
                 *
                 * This pins the deliberate suppression in chk4ServerErrors() - the ENOENT the
                 * storage raised never reaches the client
                 */

                transfer -> setCommandInfo( connection_t::CommandId::RemoveChunk, neverSavedChunkId );

                eq -> push_back( taskTransfer );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( taskTransfer ) );

                /*
                 * A suppressed error must not have been treated as a fatal server error
                 */

                chkTaskCompletedOkOrRunning( acceptor );
                UTF_REQUIRE( ! acceptor -> activeEndpoints().empty() );
            }

            {
                /*
                 * (b) the positive control - save a chunk, remove it, then remove it again
                 *
                 * The connection is still fully usable after (a), and the delete is idempotent
                 * from the client's point of view
                 */

                const auto chunkId = uuids::create();

                const auto block = DataBlock::get( dataBlocksPool );

                block -> setSize( 128U );

                std::memset( block -> pv(), 'Z', block -> size() );

                transfer -> setCommandInfo(
                    connection_t::CommandId::SendChunk,
                    chunkId,
                    om::copy( block )
                    );

                eq -> push_back( taskTransfer );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( taskTransfer ) );

                transfer -> setCommandInfo( connection_t::CommandId::RemoveChunk, chunkId );

                eq -> push_back( taskTransfer );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( taskTransfer ) );

                transfer -> setCommandInfo( connection_t::CommandId::RemoveChunk, chunkId );

                eq -> push_back( taskTransfer );

                UTF_REQUIRE_NO_THROW( eq -> waitForSuccess( taskTransfer ) );
            }

            {
                /*
                 * (c) the suppression is keyed on the control code, not a blanket ENOENT
                 * swallow - the same missing chunk fails a load
                 */

                transfer -> setCommandInfo( connection_t::CommandId::ReceiveChunk, neverSavedChunkId );

                eq -> push_back( taskTransfer );

                try
                {
                    eq -> waitForSuccess( taskTransfer );

                    UTF_FAIL( "Loading a chunk which was never saved must fail" );
                }
                catch( bl::ServerErrorException& e )
                {
                    const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

                    UTF_REQUIRE( ec );

                    UTF_REQUIRE(
                        eh::errc::make_error_code( eh::errc::no_such_file_or_directory ) == *ec
                        );
                }
            }

            {
                /*
                 * (d) the without-flag contrast arm
                 *
                 * scheduleRemoveData() sets IgnoreIfNotFound unconditionally, so the only way
                 * to observe the other polarity is a hand built command block on a raw socket
                 */

                const auto connectorRaw = connector_t::createInstance< connector_t >( "localhost", 28100 );
                const auto taskConnectorRaw = om::qi< tasks::Task >( connectorRaw.get() );

                eq -> push_back( taskConnectorRaw );
                eq -> waitForSuccess( taskConnectorRaw );

                const auto stream = connectorRaw -> detachStream();

                UTF_REQUIRE( stream );

                const auto clientPeerId = uuids::create();

                {
                    CommandBlock command;

                    command.cntrlCode = CommandBlock::CntrlCodeSetProtocolVersion;
                    command.peerId = clientPeerId;
                    command.data.version.value = CommandBlock::BLOB_TRANSFER_PROTOCOL_CLIENT_VERSION_V2;

                    sendCommand( *stream, command );

                    const auto ack = recvCommand( *stream );

                    UTF_REQUIRE( 0U == ( ack.flags & CommandBlock::ErrBit ) );
                }

                CommandBlock command;

                command.cntrlCode = CommandBlock::CntrlCodeRemoveDataBlock;
                command.chunkId = uuids::create();
                command.peerId = clientPeerId;
                command.data.blockInfo.blockType = BlockTransferDefs::BlockType::Normal;
                command.data.blockInfo.flags = 0U;

                sendCommand( *stream, command );

                const auto ack = recvCommand( *stream );

                UTF_REQUIRE( ack.flags & CommandBlock::ErrBit );

                UTF_REQUIRE_EQUAL(
                    ack.errorCode,
                    static_cast< std::uint32_t >(
                        eh::errc::make_error_code( eh::errc::no_such_file_or_directory ).value()
                        )
                    );

                /*
                 * That ack is exactly what the shipped client turns into the client visible
                 * failure, so run it through the client side conversion to pin the whole pair
                 */

                const auto probe = PayloadProbeImpl::createInstance(
                    connection_t::CommandId::NoCommand,
                    uuids::create()                         /* peerId */,
                    dataBlocksPool
                    );

                probe -> setAckState( ack.flags, ack.errorCode );

                try
                {
                    probe -> chk4ServerErrorsClient();

                    UTF_FAIL( "A remove without IgnoreIfNotFound must fail on the client" );
                }
                catch( bl::ServerErrorException& e )
                {
                    const auto* errorNo = eh::get_error_info< eh::errinfo_errno >( e );

                    UTF_REQUIRE( errorNo );

                    UTF_REQUIRE_EQUAL(
                        *errorNo,
                        eh::errc::make_error_code( eh::errc::no_such_file_or_directory ).value()
                        );
                }

                chkTaskCompletedOkOrRunning( acceptor );
                UTF_REQUIRE( ! acceptor -> activeEndpoints().empty() );
            }

            chkTaskCompletedOkOrRunning( acceptor );
        },
        acceptor,
        "localhost",
        28100
        );
}

/************************************************************************
 * Dispatching backend task selection and failure isolation
 *
 * Only the success path of one of the four arms of
 * BrokerDispatchingBackendProcessing::createBackendProcessingTask() is exercised anywhere.
 * Nothing verifies that a *failed* backend processing task prevents the block from being
 * dispatched - i.e. that an unauthenticated or malformed message does not reach the target
 * peer just because the send task was already constructed
 *
 * A dispatching backend with no connected peers makes scheduleSendBlock() throw
 * TargetPeerNotFound, which is exactly the observable that tells "the send task ran" apart
 * from "it did not"
 */

UTF_AUTO_TEST_CASE( IO_BrokerDispatchingBackendTaskSelectionTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace bl::messaging;
    using namespace utest;

    typedef BrokerDispatchingBackendProcessingImpl                                          dispatching_backend_t;

    test::MachineGlobalTestLock lock;

    const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
    const auto dataBlocksPool = datablocks_pool_type::createInstance();

    const auto sessionId = uuids::create();
    const auto chunkId = uuids::create();
    const auto sourcePeerId = uuids::create();
    const auto targetPeerId = uuids::create();

    const auto data = DataBlock::get( dataBlocksPool );

    data -> setSize( 128U );

    const auto createDispatchingBackend = [ & ](
        SAA_in_opt          om::ObjPtr< BackendProcessing >&&                               processingBackend
        )
        -> om::ObjPtr< dispatching_backend_t >
    {
        return dispatching_backend_t::createInstance< dispatching_backend_t >(
            BL_PARAM_FWD( processingBackend ),
            controlToken,
            dataBlocksPool,
            "localhost",
            28100,
            test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
            test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
            uuids::create()                                     /* peerId */
            );
    };

    /*
     * ExecutionQueue::disposeQueue() resets m_eq, so the second dispose() must take the
     * 'if( ! m_eq ) return' early exit rather than blowing up or hanging
     */

    const auto chkDisposeIsIdempotent = [ & ](
        SAA_in              const om::ObjPtr< dispatching_backend_t >&                      dispatchingBackend
        )
        -> void
    {
        dispatchingBackend -> dispose();

        UTF_REQUIRE_NO_THROW( dispatchingBackend -> dispose() );
    };

    const auto runTask = [ & ]( SAA_in const om::ObjPtr< tasks::Task >& task ) -> void
    {
        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        eq -> push_back( task );
        eq -> flushNoThrowIfFailed();
    };

    {
        /*
         * (a) arm 1 - anything which is not a Put / None pair is not a dispatching concern
         *
         * LocalBackendProcessingImpl is the only implementation in the repository which
         * inherits the base's autoBlockDispatching() == true, so it is also what makes arm 3
         * reachable at all
         */

        const auto processingBackend = LocalBackendProcessingImpl::createInstance< BackendProcessing >(
            0U                                                  /* dataExpectedOffset */,
            std::string()                                       /* dataProcessed */,
            std::string()                                       /* dataUnprocessed */
            );

        const auto dispatchingBackend = om::lockDisposable(
            createDispatchingBackend( om::copy( processingBackend ) )
            );

        UTF_REQUIRE(
            ! dispatchingBackend -> createBackendProcessingTask(
                BackendProcessing::OperationId::Get,
                BackendProcessing::CommandId::None,
                sessionId,
                chunkId,
                sourcePeerId,
                targetPeerId,
                data
                )
            );

        UTF_REQUIRE(
            ! dispatchingBackend -> createBackendProcessingTask(
                BackendProcessing::OperationId::Put,
                BackendProcessing::CommandId::FlushPeerSessions,
                sessionId,
                chunkId,
                sourcePeerId,
                targetPeerId,
                data
                )
            );

        UTF_REQUIRE(
            ! dispatchingBackend -> createBackendProcessingTask(
                BackendProcessing::OperationId::Command,
                BackendProcessing::CommandId::Remove,
                sessionId,
                chunkId,
                sourcePeerId,
                targetPeerId,
                data
                )
            );

        chkDisposeIsIdempotent( dispatchingBackend );
    }

    {
        /*
         * (b) arm 3 - autoBlockDispatching() == true chains the send task behind the
         * processing task, and a failed processing task must terminate the tree before the
         * send task is ever reached
         */

        const auto processingBackendImpl = ThrowingBackendProcessingImpl::createInstance();

        processingBackendImpl -> setAutoBlockDispatching( true );

        const auto dispatchingBackend = om::lockDisposable(
            createDispatchingBackend( om::qi< BackendProcessing >( processingBackendImpl ) )
            );

        const auto task = dispatchingBackend -> createBackendProcessingTask(
            BackendProcessing::OperationId::Put,
            BackendProcessing::CommandId::None,
            sessionId,
            chunkId,
            sourcePeerId,
            targetPeerId,
            data
            );

        UTF_REQUIRE( task );

        runTask( task );

        UTF_REQUIRE( task -> isFailed() );
        UTF_REQUIRE( processingBackendImpl -> ran() );

        const auto eptr = task -> exception();

        UTF_REQUIRE( eptr );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( eptr ),
            ServerErrorException,
            "Broker backend operation has failed"
            );

        try
        {
            cpp::safeRethrowException( eptr );

            UTF_FAIL( "The dispatching task must have failed" );
        }
        catch( ServerErrorException& e )
        {
            /*
             * The failure is the *processing* failure - if scheduleSendBlock() had run it
             * would have reported TargetPeerNotFound instead, since no peer is connected
             */

            const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

            UTF_REQUIRE(
                ! ec || eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound ) != *ec
                );

            const auto* nested = eh::get_error_info< eh::errinfo_nested_exception_ptr >( e );

            UTF_REQUIRE( nested );

            UTF_REQUIRE_THROW_MESSAGE(
                cpp::safeRethrowException( *nested ),
                bl::ArgumentException,
                "backend processing rejected the message"
                );
        }

        chkDisposeIsIdempotent( dispatchingBackend );
    }

    {
        /*
         * (c) arm 4 - autoBlockDispatching() == false returns the bare processing task, so
         * chkToRemapToServerError() never runs on it and the raw exception survives
         *
         * chkToWrapInServerErrorAndThrowT only covers exceptions thrown while selecting or
         * constructing the task
         */

        const auto processingBackendImpl = ThrowingBackendProcessingImpl::createInstance();

        processingBackendImpl -> setAutoBlockDispatching( false );

        const auto dispatchingBackend = om::lockDisposable(
            createDispatchingBackend( om::qi< BackendProcessing >( processingBackendImpl ) )
            );

        const auto task = dispatchingBackend -> createBackendProcessingTask(
            BackendProcessing::OperationId::Put,
            BackendProcessing::CommandId::None,
            sessionId,
            chunkId,
            sourcePeerId,
            targetPeerId,
            data
            );

        UTF_REQUIRE( task );

        runTask( task );

        UTF_REQUIRE( task -> isFailed() );
        UTF_REQUIRE( processingBackendImpl -> ran() );

        UTF_REQUIRE_THROW_MESSAGE(
            cpp::safeRethrowException( task -> exception() ),
            bl::ArgumentException,
            "backend processing rejected the message"
            );

        chkDisposeIsIdempotent( dispatchingBackend );
    }

    {
        /*
         * (d) arm 2 - with no processing backend at all the block is dispatched as is, and
         * scheduleSendBlock()'s not-found guard is what reports the failure
         */

        const auto dispatchingBackend = om::lockDisposable(
            createDispatchingBackend( om::ObjPtr< BackendProcessing >() )
            );

        const auto task = dispatchingBackend -> createBackendProcessingTask(
            BackendProcessing::OperationId::Put,
            BackendProcessing::CommandId::None,
            sessionId,
            chunkId,
            sourcePeerId,
            targetPeerId,
            data
            );

        UTF_REQUIRE( task );

        runTask( task );

        UTF_REQUIRE( task -> isFailed() );

        try
        {
            cpp::safeRethrowException( task -> exception() );

            UTF_FAIL( "The dispatch task must have failed" );
        }
        catch( ServerErrorException& e )
        {
            const auto* ec = eh::get_error_info< eh::errinfo_error_code >( e );

            UTF_REQUIRE( ec );

            UTF_REQUIRE(
                eh::errc::make_error_code( BrokerErrorCodes::TargetPeerNotFound ) == *ec
                );

            const auto* isExpected = eh::get_error_info< eh::errinfo_is_expected >( e );

            UTF_REQUIRE( isExpected );
            UTF_REQUIRE( *isExpected );
        }

        chkDisposeIsIdempotent( dispatchingBackend );
    }
}

/************************************************************************
 * AcceptorNotify rejection must cancel the outbound connection
 *
 * This is the hook a proxy broker uses to reject an unauthorized peer. If the continuation
 * stopped cancelling - or the notify task stopped being scheduled at all - an unauthorized
 * peer would stay registered and keep receiving messages, and every existing test would
 * still pass
 *
 * Note the deliberate ordering in notifyCallback(): the peer is registered *before* it is
 * authorized, so only the eventual state is contractual - never how soon the cancel lands
 */

UTF_AUTO_TEST_CASE( IO_OutgoingAcceptorNotifyRejectionTests )
{
    using namespace bl;
    using namespace bl::data;
    using namespace bl::tasks;
    using namespace utest;

    typedef messaging::BrokerDispatchingBackendProcessingImpl                               dispatching_backend_t;
    typedef bl::tasks::TcpBlockServerMessageDispatcher::async_wrapper_t                     async_wrapper_t;
    typedef async_wrapper_t::backend_interface_t                                            backend_interface_t;
    typedef bl::tasks::detail::BlockTransferServerStateImpl< async_wrapper_t >               server_state_t;

    typedef bl::tasks::TcpBlockTransferServerConnectionImpl
    <
        dispatching_backend_t::acceptor_t::stream_t,
        async_wrapper_t
    >
    server_connection_t;

    typedef dispatching_backend_t::acceptor_t::connection_t                                 auto_push_connection_t;

    test::MachineGlobalTestLock lock;

    const long heartbeatIntervalInSeconds = 2L;

    const std::size_t maxRetries = 2U * 60U;

    const auto runFixture = [ & ]( SAA_in const bool rejectPeers ) -> void
    {
        const auto controlToken = SimpleTaskControlTokenImpl::createInstance< TaskControlTokenRW >();
        const auto dataBlocksPool = datablocks_pool_type::createInstance();
        const auto backendImpl = BackendImplTestImpl::createInstance();

        const auto backend = om::lockDisposable(
            async_wrapper_t::createInstance<>(
                om::qi< backend_interface_t >( backendImpl )    /* writeBackend */,
                om::qi< backend_interface_t >( backendImpl )    /* readBackend */,
                test::UtfArgsParser::threadsCount(),
                om::qi< TaskControlToken >( controlToken ),
                0U                                              /* maxConcurrentTasks */,
                dataBlocksPool
                )
            );

        const auto serverState = server_state_t::createInstance( dataBlocksPool, backend );

        const auto processingBackendImpl = LocalBackendProcessingImpl::createInstance(
            0U                                                  /* dataExpectedOffset */,
            std::string()                                       /* dataProcessed */,
            std::string()                                       /* dataUnprocessed */
            );

        processingBackendImpl -> rejectPeers( rejectPeers );

        const auto dispatchingBackendImpl = om::lockDisposable(
            dispatching_backend_t::createInstance< dispatching_backend_t >(
                om::qi< messaging::BackendProcessing >( processingBackendImpl ),
                controlToken,
                dataBlocksPool,
                "localhost",
                28100,
                test::UtfCrypto::getDefaultServerKey()          /* privateKeyPem */,
                test::UtfCrypto::getDefaultServerCertificate()  /* certificatePem */,
                uuids::create()                                 /* peerId */,
                time::seconds( heartbeatIntervalInSeconds )     /* heartbeatInterval */
                )
            );

        UTF_REQUIRE( dispatchingBackendImpl );

        const auto& acceptor = dispatchingBackendImpl -> acceptor();

        TestTaskUtils::waitForAcceptorReady( "localhost", 28100 );

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        /*
         * The peer side connection is expected to be dropped in the reject run, so the queue
         * is flushed without throwing rather than waited on
         */

        BL_SCOPE_EXIT(
            {
                eq -> forceFlushNoThrow();
            }
            );

        const auto connector = connector_t::createInstance< connector_t >( "localhost", 28100 );
        const auto taskConnector = om::qi< tasks::Task >( connector.get() );

        eq -> push_back( taskConnector );
        eq -> waitForSuccess( taskConnector );

        const auto peerId = uuids::create();
        const auto transfer = server_connection_t::createInstance( serverState, peerId );

        transfer -> attachStream( connector -> detachStream() );

        const auto taskTransfer = om::qi< tasks::Task >( transfer );

        eq -> push_back( taskTransfer );

        /*
         * The registration only happens after the first successful heartbeat
         */

        std::size_t retries = 0U;

        for( ;; )
        {
            os::sleep( time::seconds( 1 ) );

            chkTaskCompletedOkOrRunning( acceptor );

            if( processingBackendImpl -> peerConnectedCalls() >= 1U )
            {
                break;
            }

            if( retries > maxRetries )
            {
                UTF_FAIL( "The peer was never announced to the acceptor notify interface" );

                return;
            }

            ++retries;
        }

        UTF_REQUIRE( processingBackendImpl -> peerConnectedCalls() >= 1U );

        if( ! rejectPeers )
        {
            /*
             * The accept run - the peer is registered and stays connected
             */

            chkTaskCompletedOkOrRunning( transfer );

            UTF_REQUIRE( dispatchingBackendImpl -> getAllActiveQueuesIds().count( peerId ) );
            UTF_REQUIRE_EQUAL( acceptor -> activeEndpoints().size(), 1U );

            const auto serverTask =
                om::qi< auto_push_connection_t >( acceptor -> activeEndpoints().back() );

            os::sleep( time::seconds( 2L * heartbeatIntervalInSeconds ) );

            chkTaskCompletedOkOrRunning( acceptor );
            chkTaskCompletedOkOrRunning( transfer );
            chkTaskCompletedOkOrRunning( serverTask );

            UTF_REQUIRE( Task::Completed != om::qi< tasks::Task >( transfer ) -> getState() );
            UTF_REQUIRE( Task::Completed != om::qi< tasks::Task >( serverTask ) -> getState() );

            UTF_REQUIRE( dispatchingBackendImpl -> tryGetMessageBlockCompletionQueue( peerId ) );

            return;
        }

        /*
         * The reject run - the notify task failed, so the continuation must cancel the
         * connection, which unregisters the queue and tears the connection down
         */

        retries = 0U;

        for( ;; )
        {
            chkTaskCompletedOkOrRunning( acceptor );

            if(
                acceptor -> activeEndpoints().empty() &&
                dispatchingBackendImpl -> getAllActiveQueuesIds().empty()
                )
            {
                break;
            }

            if( retries > maxRetries )
            {
                UTF_FAIL( "The rejected peer was not disconnected" );

                return;
            }

            os::sleep( time::seconds( 1 ) );

            ++retries;
        }

        UTF_REQUIRE( acceptor -> activeEndpoints().empty() );
        UTF_REQUIRE( dispatchingBackendImpl -> getAllActiveQueuesIds().empty() );

        /*
         * scheduleSendBlock() would now raise TargetPeerNotFound rather than routing a
         * message to a rejected peer
         */

        UTF_REQUIRE( ! dispatchingBackendImpl -> tryGetMessageBlockCompletionQueue( peerId ) );

        /*
         * The peer observed the drop
         */

        retries = 0U;

        while( Task::Completed != taskTransfer -> getState() )
        {
            if( retries > maxRetries )
            {
                UTF_FAIL( "The peer side connection was not terminated" );

                return;
            }

            os::sleep( time::seconds( 1 ) );

            ++retries;
        }

        UTF_REQUIRE( Task::Completed == om::qi< tasks::Task >( transfer ) -> getState() );

        /*
         * The acceptor itself survived the rejection
         */

        chkTaskCompletedOkOrRunning( acceptor );
    };

    runFixture( false /* rejectPeers */ );
    runFixture( true /* rejectPeers */ );
}

/************************************************************************
 * Execute the trace tier at least once
 *
 * UtfMain.h sets the global logging level to LL_DEBUG and LL_TRACE > LL_DEBUG, so
 * Logging::trace().isEnabled() is false for the whole run and every one of the 42 guarded
 * trace expressions in the library is never evaluated. They are not inert text - they call
 * net::formatEndpointId(), eh::diagnostic_information( e ), the redacting stream operator
 * for a BrokerProtocol, pointer chains such as m_operationState -> data() -> size(), and
 * m_eqConnections -> size() inside a NOEXCEPT teardown handler
 *
 * Raising the log level is precisely what an operator does while diagnosing a live incident,
 * so without this case the first execution of that tier is guaranteed to be under production
 * pressure
 *
 * The output is captured into a stream and not the console: at LL_TRACE this exercise emits
 * a large volume, and UtfMain.h's line logger turns WARNING / ERROR lines into BOOST_ERROR
 */

UTF_AUTO_TEST_CASE( IO_TraceTierSmokeTests )
{
    using namespace bl;

    cpp::SafeOutputStringStream oss;

    {
        /*
         * The global level has to be pushed - the worker threads which execute most of the
         * trace sites have no TLS override of their own
         */

        Logging::LineLoggerPusher pushLogger( Logging::getDefaultLineLogger( oss ) );
        Logging::LevelPusher pushLevel( Logging::LL_TRACE, true /* global */ );

        simpleConnectAndTransmitDataTest< bl::tasks::TcpBlockServerDataChunkStorage, connector_t >(
            true /* startConnector */
            );

        /*
         * The SSL variant puts AsioSslStreamWrapper's trace sites on the path too
         */

        simpleConnectAndTransmitDataTest< bl::tasks::TcpSslBlockServerMessageDispatcher, ssl_connector_t >(
            true /* startConnector */
            );
    }

    /*
     * The case must not leak the level into the rest of the module
     */

    UTF_REQUIRE_EQUAL( ( int ) Logging::LL_DEBUG, ( int ) Logging::getLevel() );

    const auto text = oss.str();

    UTF_REQUIRE( ! text.empty() );

    /*
     * One marker per production header on the path, so the case cannot pass while the tier
     * stays switched off
     */

    UTF_REQUIRE( cpp::contains( text, "Endpoint resolved:" ) );
    UTF_REQUIRE( cpp::contains( text, "Blob server connection" ) );
    UTF_REQUIRE( cpp::contains( text, "was shut down" ) );
    UTF_REQUIRE( cpp::contains( text, "destroying simple pool" ) );
}

UTF_AUTO_TEST_CASE( IO_MessagingClientBlockDispatchLocalTests )
{
    using namespace bl;
    using namespace bl::messaging;

    const auto targetPeerIdExpected = uuids::create();

    const std::size_t sizeExpected = 42;
    const std::size_t offset1Expected = sizeExpected / 3;

    const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

    std::atomic< std::size_t > callsCount( 0U );

    const auto receiver = om::lockDisposable(
        MessagingClientBlockDispatchFromCallback::createInstance(
            [ & ](
                SAA_in              const bl::uuid_t&                               targetPeerId,
                SAA_in              const om::ObjPtr< data::DataBlock >&            dataBlock
                ) -> void
            {
                UTF_REQUIRE_EQUAL( targetPeerIdExpected, targetPeerId );

                UTF_REQUIRE( dataBlock );
                UTF_REQUIRE_EQUAL( sizeExpected, dataBlock -> size() );
                UTF_REQUIRE_EQUAL( offset1Expected, dataBlock -> offset1() );

                const char* psz = dataBlock -> begin();
                UTF_REQUIRE_EQUAL( *psz + *( psz + 1 ), *( psz + 2 ) );

                ++callsCount;
            }
            )
        );

    {
        const auto dispatcher = om::lockDisposable(
            MessagingClientBlockDispatchLocal::createInstance(
                om::qi< MessagingClientBlockDispatch >( receiver ),
                om::copy( dataBlocksPool )
                )
            );

        const std::size_t noOfBlocks = 1024U;

        for( std::size_t i = 0U; i < noOfBlocks; ++i )
        {
            const auto dataBlock = data::DataBlock::createInstance( 512U /* capacity */ );

            dataBlock -> setSize( sizeExpected );
            dataBlock -> setOffset1( offset1Expected );

            char* psz = dataBlock -> begin();

            *psz = static_cast< char >( random::getUniformRandomUnsignedValue< int >( 32 ) );
            *( psz + 1 ) = static_cast< char >( random::getUniformRandomUnsignedValue< int >( 32 ) );
            *( psz + 2 ) = *psz + *( psz + 1 );

            dispatcher -> pushBlock( targetPeerIdExpected, dataBlock );
        }

        dispatcher -> flush();

        UTF_REQUIRE_EQUAL( callsCount.load(), noOfBlocks );
    }

    /*
     * MessagingClientBlockDispatchFromCallbackT::isNoCopyDataBlocks() hard returns false, so
     * every producer which pushes into such a channel must copy the data block first - that
     * is the other half of the invariant IO_DataBlockCrossPoolCapacityTests pins
     *
     * Note that the matching setter must NOT be called from a test - it is a BL_RIP_MSG(...),
     * i.e. os::fastAbort(), so calling it would take the whole test module down instead of
     * failing a case
     */

    UTF_REQUIRE( ! receiver -> isNoCopyDataBlocks() );
}

UTF_AUTO_TEST_CASE( IO_MessagingClientTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace bl::messaging;

    const auto callbackTests = []() -> void
    {
        const auto target = om::lockDisposable(
            MessagingClientBlockDispatchFromCallback::createInstance< MessagingClientBlockDispatch >(
                [](
                    SAA_in              const bl::uuid_t&                               targetPeerId,
                    SAA_in              const om::ObjPtr< data::DataBlock >&            dataBlock
                    ) -> void
                {
                    BL_UNUSED( targetPeerId );
                    BL_UNUSED( dataBlock );

                    UTF_FAIL( "This should not be called from this test" );
                }
                )
            );

        const auto dataBlocksPool = data::datablocks_pool_type::createInstance();

        const auto eq = om::lockDisposable(
            ExecutionQueueImpl::createInstance< ExecutionQueue >( ExecutionQueue::OptionKeepAll )
            );

        const auto backend = om::lockDisposable(
            MessagingClientFactorySsl::createClientBackendProcessingFromBlockDispatch( om::copy( target ) )
            );

        const auto asyncWrapper = om::lockDisposable(
            MessagingClientFactorySsl::createAsyncWrapperFromBackend(
                om::copy( backend ),
                0U              /* threadsCount */,
                0U              /* maxConcurrentTasks */,
                om::copy( dataBlocksPool )
                )
            );

        const auto peerId = uuids::create();

        {
            const auto client = om::lockDisposable(
                MessagingClientFactorySsl::createWithSmartDefaults(
                    om::copy( eq ),
                    peerId,
                    om::copy( backend ),
                    om::copy( asyncWrapper ),
                    test::UtfArgsParser::host(),
                    test::UtfArgsParser::port()             /* inboundPort */
                    )
                );

            os::sleep( time::seconds( 2L ) );
        }

        {
            /*
             * Create some number of clients backed by the same async wrapper
             *
             * Note that these don't own the backend and the queue, so when they
             * get disposed they will not actually dispose the backend
             */

            std::vector< om::ObjPtrDisposable< MessagingClientBlockDispatch > > clients;

            for( std::size_t i = 0; i < 120; ++i )
            {
                clients.emplace_back(
                    om::lockDisposable(
                        MessagingClientFactorySsl::createWithSmartDefaults(
                            om::copy( eq ),
                            peerId,
                            om::copy( backend ),
                            om::copy( asyncWrapper ),
                            test::UtfArgsParser::host(),
                            test::UtfArgsParser::port()             /* inboundPort */
                            )
                        )
                    );
            }

            os::sleep( time::seconds( 2L ) );
        }

        {
            auto connections1 = MessagingClientFactorySsl::createEstablishedConnections(
                "localhost"                                         /* host */,
                test::UtfArgsParser::port()                         /* inboundPort */,
                test::UtfArgsParser::port() + 1                     /* outboundPort */
                );

            auto connections2 = MessagingClientFactorySsl::createEstablishedConnections(
                "localhost"                                         /* host */,
                test::UtfArgsParser::port()                         /* inboundPort */,
                test::UtfArgsParser::port() + 1                     /* outboundPort */
                );

            const auto client1 = om::lockDisposable(
                MessagingClientFactorySsl::createWithSmartDefaults(
                    nullptr                                 /* eq */,
                    peerId,
                    om::copy( backend ),
                    om::copy( asyncWrapper ),
                    test::UtfArgsParser::host(),
                    test::UtfArgsParser::port()             /* inboundPort */,
                    test::UtfArgsParser::port() + 1         /* outboundPort */,
                    std::move( connections1.first )         /* inboundConnection */,
                    std::move( connections1.second )        /* outboundConnection */
                    )
                );

            const auto client2 = om::lockDisposable(
                MessagingClientFactorySsl::createWithSmartDefaults(
                    peerId,
                    om::copy( target ),
                    test::UtfArgsParser::host(),
                    test::UtfArgsParser::port()             /* inboundPort */,
                    test::UtfArgsParser::port() + 1         /* outboundPort */,
                    std::move( connections2.first )         /* inboundConnection */,
                    std::move( connections2.second )        /* outboundConnection */
                    )
                );

            os::sleep( time::seconds( 2L ) );
        }
    };

    test::MachineGlobalTestLock lock;

    auto blockDispatch = om::lockDisposable(
        MessagingClientBlockDispatchFromCallback::createInstance< MessagingClientBlockDispatch >(
            [ & ](
                SAA_in              const bl::uuid_t&                               targetPeerId,
                SAA_in              const om::ObjPtr< data::DataBlock >&            dataBlock
                ) -> void
            {
                BL_UNUSED( targetPeerId );
                BL_UNUSED( dataBlock );
            }
            )
        );

    const auto processingBackend = bl::om::lockDisposable(
        MessagingClientBackendProcessing::createInstance< bl::messaging::BackendProcessing >(
            std::move( blockDispatch )
            )
        );

    bl::messaging::BrokerFacade::execute(
        processingBackend,
        test::UtfCrypto::getDefaultServerKey()              /* privateKeyPem */,
        test::UtfCrypto::getDefaultServerCertificate()      /* certificatePem */,
        test::UtfArgsParser::port()                         /* inboundPort */,
        test::UtfArgsParser::port() + 1                     /* outboundPort */,
        test::UtfArgsParser::threadsCount(),
        0U                                                  /* maxConcurrentTasks */,
        callbackTests
        );
}

namespace
{
    /**
     * @brief A test local error category which impersonates the modern ASIO SSL stream
     * category by name
     *
     * TcpSslSocketAsyncBase::isExpectedSslErrorCode() identifies that category by the string
     * "asio.ssl.stream" on purpose rather than by referencing asio::ssl::error::stream_truncated,
     * so that baselib builds against the whole supported ASIO / OpenSSL range - reproducing
     * the string here is what lets the test cover the modern accepted form without
     * reintroducing into the test exactly the dependency the production code avoids
     */

    class FakeSslStreamCategory : public bl::eh::error_category
    {
    public:

        virtual const char* name() const NOEXCEPT OVERRIDE
        {
            return "asio.ssl.stream";
        }

        virtual std::string message( int ) const OVERRIDE
        {
            return "fake";
        }
    };

    /*
     * Boost.System requires error categories to have static storage duration
     */

    const bl::eh::error_category& fakeSslStreamCategory()
    {
        static const FakeSslStreamCategory g_fakeSslStreamCategory;

        return g_fakeSslStreamCategory;
    }
}

UTF_AUTO_TEST_CASE( IO_MessagingBackendProcessingHelpers )
{
    using namespace bl;
    using namespace bl::messaging;
    using namespace bl::tasks;

    const std::string messagePrefix = "My server operation";

    const eh::errc::errc_t errorCondition = eh::errc::address_in_use;

    const eh::error_code errorCode = eh::errc::make_error_code( errorCondition );

    const auto throwNonDecoratedServerErrorException = []() -> void
    {
        BL_THROW(
            ServerErrorException(),
            BL_MSG()
                << "Test server raw exception"
            );
    };

    const auto throwDecoratedArgumentException = [ &errorCode ]() -> void
    {
        BL_THROW(
            ArgumentException()
                << eh::errinfo_errno( errorCode.value() )
                << eh::errinfo_error_code( errorCode )
                << eh::errinfo_error_code_message( errorCode.message() )
                << eh::errinfo_category_name( errorCode.category().name() )
                << eh::errinfo_is_expected( true ),
            BL_MSG()
                << "Test server wrapped exception"
            );
    };

    const auto throwPartiallyDecoratedArgumentException = [ &errorCode ]() -> void
    {
        BL_THROW(
            ArgumentException()
                << eh::errinfo_errno( errorCode.value() )
                << eh::errinfo_is_expected( true ),
            BL_MSG()
                << "Test server wrapped exception"
            );
    };

    const auto testNoWrappingCase = [ & ]( SAA_in const cpp::void_callback_t& callback ) -> void
    {
        /*
         * Verify that ServerErrorException exceptions are propagated as is and no
         * wrapping occurs
         */

        try
        {
            callback();

            UTF_FAIL( "BackendProcessingBase::chkToWrapInServerErrorAndThrow must throw" );
        }
        catch( ServerErrorException& exception )
        {
            const std::string msg = exception.what();
            UTF_REQUIRE_EQUAL( msg, std::string( "Test server raw exception" ) );

            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_errno >( exception ) );
            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_error_code >( exception ) );
            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_error_code_message >( exception ) );
            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_category_name >( exception ) );

            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_is_expected >( exception ) );
            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_nested_exception_ptr >( exception ) );
        }
    };

    const auto testWrappingCase = [ & ]( SAA_in const cpp::void_callback_t& callback ) -> void
    {
        /*
         * Verify that non-ServerErrorException exceptions are wrapped and the relevant
         * properties are copied accordingly
         */

        try
        {
            callback();

            UTF_FAIL( "BackendProcessingBase::chkToWrapInServerErrorAndThrow must throw" );
        }
        catch( ServerErrorException& exception )
        {
            const std::string msg = exception.what();
            UTF_REQUIRE_EQUAL( msg, messagePrefix + " has failed" );

            UTF_REQUIRE( eh::get_error_info< eh::errinfo_errno >( exception ) );
            UTF_REQUIRE( eh::get_error_info< eh::errinfo_error_code >( exception ) );
            UTF_REQUIRE( eh::get_error_info< eh::errinfo_error_code_message >( exception ) );
            UTF_REQUIRE( eh::get_error_info< eh::errinfo_category_name >( exception ) );

            UTF_REQUIRE_EQUAL(
                errorCode.value(),
                *eh::get_error_info< eh::errinfo_errno >( exception )
                );

            UTF_REQUIRE_EQUAL(
                errorCode,
                *eh::get_error_info< eh::errinfo_error_code >( exception )
                );

            UTF_REQUIRE_EQUAL(
                errorCode.message(),
                *eh::get_error_info< eh::errinfo_error_code_message >( exception )
                );

            UTF_REQUIRE_EQUAL(
                errorCode.category().name(),
                *eh::get_error_info< eh::errinfo_category_name >( exception )
                );

            const bool* isExpected = eh::get_error_info< eh::errinfo_is_expected >( exception );
            UTF_REQUIRE( isExpected && *isExpected );

            const auto* eeptr = eh::get_error_info< eh::errinfo_nested_exception_ptr >( exception );
            UTF_REQUIRE( eeptr );

            UTF_REQUIRE_THROW_MESSAGE(
                cpp::safeRethrowException( *eeptr ),
                ArgumentException,
                "Test server wrapped exception"
                );
        }
    };

    /*
     * Tests for BackendProcessingBase::chkToWrapInServerErrorAndThrow
     */

    testNoWrappingCase(
        cpp::bind(
            &BackendProcessingBase::chkToWrapInServerErrorAndThrow,
            throwNonDecoratedServerErrorException,
            messagePrefix,
            eh::errc::success
            )
        );

    testWrappingCase(
        cpp::bind(
            &BackendProcessingBase::chkToWrapInServerErrorAndThrow,
            throwDecoratedArgumentException,
            messagePrefix,
            eh::errc::success
            )
        );

    testWrappingCase(
        cpp::bind(
            &BackendProcessingBase::chkToWrapInServerErrorAndThrow,
            throwPartiallyDecoratedArgumentException,
            messagePrefix,
            errorCondition
            )
        );

    /*
     * Tests for BackendProcessingBase::chkToRemapToServerError
     */

    const auto convertToEptr = []( SAA_in const cpp::void_callback_t& callback ) -> std::exception_ptr
    {
        try
        {
            callback();

            UTF_FAIL( "callback must throw" );
        }
        catch( std::exception& )
        {
            return std::current_exception();
        }

        UTF_FAIL( "callback must throw" );

        return std::exception_ptr();
    };

    testNoWrappingCase(
        cpp::bind< void >(
            cpp::safeRethrowException,
            BackendProcessingBase::chkToRemapToServerError(
                convertToEptr( throwNonDecoratedServerErrorException ),
                messagePrefix,
                eh::errc::success
                )
            )
        );

    testWrappingCase(
        cpp::bind< void >(
            cpp::safeRethrowException,
            BackendProcessingBase::chkToRemapToServerError(
                convertToEptr( throwDecoratedArgumentException ),
                messagePrefix,
                eh::errc::success
                )
            )
        );

    testWrappingCase(
        cpp::bind< void >(
            cpp::safeRethrowException,
            BackendProcessingBase::chkToRemapToServerError(
                convertToEptr( throwPartiallyDecoratedArgumentException ),
                messagePrefix,
                errorCondition
                )
            )
        );


    /*
     * Four more arms of wrapInServerError(), none of which the three lambdas above can reach
     * because they all attach errinfo_errno *and* errinfo_is_expected( true ) and never set
     * the user friendly flag or a non-generic category
     */

    const auto throwUserFriendlyException = [ &errorCode ]() -> void
    {
        BL_THROW_USER_FRIENDLY(
            bl::SecurityException()
                << bl::eh::errinfo_errno( errorCode.value() )
                << bl::eh::errinfo_error_code( errorCode ),
            "the token has expired, please sign in again"
            );
    };

    const auto throwCryptoCategoryException = []() -> void
    {
        ( void ) ::ERR_clear_error();

        /*
         * Force one deterministic OpenSSL failure - an ASN.1 decode of something which is
         * plainly not a DER encoded certificate - so the error queue is non-empty and
         * crypto::getException() can build a SystemException in the OpenSSL error category
         */

        const std::string notACertificate = "not a certificate";

        const auto* derBytes = reinterpret_cast< const unsigned char* >( notACertificate.c_str() );

        const auto cert = bl::crypto::x509cert_ptr_t::attach(
            ::d2i_X509( nullptr, &derBytes, static_cast< long >( notACertificate.size() ) )
            );

        UTF_REQUIRE( ! cert );
        UTF_REQUIRE( ::ERR_peek_error() );

        BL_THROW( bl::crypto::getException( "crypto failure" ), "crypto failure" );
    };

    const auto throwBareArgumentException = []() -> void
    {
        BL_THROW(
            ArgumentException(),
            BL_MSG()
                << "Test server bare exception"
            );
    };

    const auto throwSocketErrorException = []() -> void
    {
        BL_THROW(
            ArgumentException()
                << eh::errinfo_error_code(
                    asio::error::make_error_code( asio::error::operation_aborted )
                    ),
            BL_MSG()
                << "Test server socket exception"
            );
    };

    {
        /*
         * (1) the user friendly flag is *not* on wrapInServerError()'s copy whitelist, so the
         * moment a user friendly exception crosses a messaging backend it stops being user
         * friendly and ServerErrorHelpers writes the generic message instead
         *
         * This pins the current behaviour deliberately: whether a user friendly message may
         * cross a trust boundary is a product decision, and the test's job is to make the
         * current answer explicit rather than to "fix" it here
         */

        try
        {
            throwUserFriendlyException();

            UTF_FAIL( "throwUserFriendlyException must throw" );
        }
        catch( bl::SecurityException& e )
        {
            /*
             * A positive control on the source, so the case cannot pass vacuously if the
             * user friendly macro stops working
             */

            UTF_REQUIRE( bl::eh::isUserFriendly( e ) );

            const auto serverError = bl::dm::ServerErrorHelpers::createServerErrorObject(
                std::current_exception()
                );

            UTF_REQUIRE_EQUAL(
                serverError -> result() -> message(),
                std::string( "the token has expired, please sign in again" )
                );

            UTF_REQUIRE( serverError -> result() -> exceptionProperties() -> isUserFriendly() );
        }

        const auto chkUserFriendlyIsDropped = [ & ]( SAA_in const cpp::void_callback_t& callback ) -> void
        {
            try
            {
                callback();

                UTF_FAIL( "The wrapping helper must throw" );
            }
            catch( ServerErrorException& wrapped )
            {
                UTF_REQUIRE( ! bl::eh::isUserFriendly( wrapped ) );

                UTF_REQUIRE(
                    nullptr == bl::eh::get_error_info< bl::eh::errinfo_is_user_friendly >( wrapped )
                    );

                /*
                 * The information is still present but unreachable by the consumer, which is
                 * the precise shape of the loss
                 */

                const auto* eeptr =
                    eh::get_error_info< eh::errinfo_nested_exception_ptr >( wrapped );

                UTF_REQUIRE( eeptr );

                try
                {
                    cpp::safeRethrowException( *eeptr );

                    UTF_FAIL( "The nested exception must rethrow" );
                }
                catch( std::exception& inner )
                {
                    UTF_REQUIRE( bl::eh::isUserFriendly( inner ) );

                    UTF_REQUIRE(
                        bl::cpp::contains( std::string( inner.what() ), "please sign in again" )
                        );
                }

                /*
                 * The two strings a human actually sees
                 */

                const auto serverError = bl::dm::ServerErrorHelpers::createServerErrorObject(
                    std::current_exception()
                    );

                UTF_REQUIRE_EQUAL(
                    serverError -> result() -> message(),
                    std::string( BL_GENERIC_FRIENDLY_UNEXPECTED_MSG )
                    );

                UTF_REQUIRE_EQUAL(
                    serverError -> result() -> exceptionMessage(),
                    messagePrefix + " has failed"
                    );
            }
        };

        chkUserFriendlyIsDropped(
            cpp::bind(
                &BackendProcessingBase::chkToWrapInServerErrorAndThrow,
                cpp::void_callback_t( throwUserFriendlyException ),
                messagePrefix,
                eh::errc::success
                )
            );

        chkUserFriendlyIsDropped(
            cpp::bind< void >(
                cpp::safeRethrowException,
                BackendProcessingBase::chkToRemapToServerError(
                    convertToEptr( throwUserFriendlyException ),
                    messagePrefix,
                    eh::errc::success
                    )
                )
            );
    }

    {
        /*
         * (2) the category is forwarded verbatim, so a non-generic, non-system category name
         * reaches the wire - where createExceptionFromObject() rejects it
         */

        const auto chkCryptoCategoryIsForwarded = [ & ](
            SAA_in          const eh::errc::errc_t                   defaultError,
            SAA_in          const bool                               expectDefaultErrno
            )
            -> void
        {
            try
            {
                BackendProcessingBase::chkToWrapInServerErrorAndThrow(
                    cpp::void_callback_t( throwCryptoCategoryException ),
                    messagePrefix,
                    defaultError
                    );

                UTF_FAIL( "chkToWrapInServerErrorAndThrow must throw" );
            }
            catch( ServerErrorException& wrapped )
            {
                const auto* categoryName =
                    eh::get_error_info< eh::errinfo_category_name >( wrapped );

                UTF_REQUIRE( categoryName );
                UTF_REQUIRE_EQUAL( *categoryName, std::string( "OpenSSL" ) );

                const auto* ec = eh::get_error_info< eh::errinfo_error_code >( wrapped );

                UTF_REQUIRE( ec );
                UTF_REQUIRE_EQUAL( std::string( ec -> category().name() ), std::string( "OpenSSL" ) );

                const auto* errorNo = eh::get_error_info< eh::errinfo_errno >( wrapped );

                if( expectDefaultErrno )
                {
                    /*
                     * The source carried no errno, so the default supplies one - and that
                     * combination (EACCES plus an OpenSSL error code) is what makes
                     * updateHttpStatusFromException answer 401 for a TLS handshake failure
                     */

                    UTF_REQUIRE( errorNo );

                    UTF_REQUIRE_EQUAL(
                        *errorNo,
                        static_cast< int >( BrokerErrorCodes::AuthorizationFailed )
                        );
                }
                else
                {
                    UTF_REQUIRE( ! errorNo );
                }

                UTF_REQUIRE( ! BrokerErrorCodes::isExpectedException( std::current_exception() ) );
            }
        };

        chkCryptoCategoryIsForwarded( eh::errc::success, false /* expectDefaultErrno */ );

        chkCryptoCategoryIsForwarded(
            BrokerErrorCodes::AuthorizationFailed,
            true /* expectDefaultErrno */
            );

        ( void ) ::ERR_clear_error();
    }

    {
        /*
         * (3) the errno / error code defaults, and no is-expected inference for an exception
         * which carries no error code at all
         */

        try
        {
            BackendProcessingBase::chkToWrapInServerErrorAndThrow(
                cpp::void_callback_t( throwBareArgumentException ),
                messagePrefix,
                errorCondition
                );

            UTF_FAIL( "chkToWrapInServerErrorAndThrow must throw" );
        }
        catch( ServerErrorException& wrapped )
        {
            const auto* errorNo = eh::get_error_info< eh::errinfo_errno >( wrapped );

            UTF_REQUIRE( errorNo );
            UTF_REQUIRE_EQUAL( *errorNo, static_cast< int >( errorCondition ) );

            const auto* ec = eh::get_error_info< eh::errinfo_error_code >( wrapped );

            UTF_REQUIRE( ec );
            UTF_REQUIRE_EQUAL( *ec, eh::errc::make_error_code( errorCondition ) );

            const auto* ecMessage = eh::get_error_info< eh::errinfo_error_code_message >( wrapped );

            UTF_REQUIRE( ecMessage );
            UTF_REQUIRE_EQUAL( *ecMessage, eh::errc::make_error_code( errorCondition ).message() );

            const auto* categoryName = eh::get_error_info< eh::errinfo_category_name >( wrapped );

            UTF_REQUIRE( categoryName );

            UTF_REQUIRE_EQUAL(
                *categoryName,
                std::string( eh::errc::make_error_code( errorCondition ).category().name() )
                );

            UTF_REQUIRE( ! eh::get_error_info< eh::errinfo_is_expected >( wrapped ) );
        }
    }

    {
        /*
         * (4) when the source carries no errinfo_is_expected,
         * isExpectedSocketException( true, errorCode ) alone decides - losing that inference
         * would mislabel a cancelled socket as a server fault
         */

        try
        {
            BackendProcessingBase::chkToWrapInServerErrorAndThrow(
                cpp::void_callback_t( throwSocketErrorException ),
                messagePrefix,
                errorCondition
                );

            UTF_FAIL( "chkToWrapInServerErrorAndThrow must throw" );
        }
        catch( ServerErrorException& wrapped )
        {
            const bool* isExpected = eh::get_error_info< eh::errinfo_is_expected >( wrapped );

            UTF_REQUIRE( isExpected );
            UTF_REQUIRE( *isExpected );
        }
    }

    /*
     * Test TcpSocketCommonBase::isExpectedSocketException logic
     */

    const auto ecOperationAborted = asio::error::make_error_code( asio::error::operation_aborted );

    UTF_REQUIRE(
        TcpSocketCommonBase::isExpectedSocketException(
            true /* isCancelExpected */,
            &ecOperationAborted
            )
        );

    UTF_REQUIRE(
        ! TcpSocketCommonBase::isExpectedSocketException(
            false /* isCancelExpected */,
            &ecOperationAborted
            )
        );

    const auto testExpectedSocketException = [ &errorCode ]( SAA_in const bool isCancelExpected ) -> void
    {
        UTF_REQUIRE(
            ! TcpSocketCommonBase::isExpectedSocketException(
                isCancelExpected,
                nullptr /* ec */
                )
            );

        UTF_REQUIRE(
            ! TcpSocketCommonBase::isExpectedSocketException(
                isCancelExpected,
                &errorCode
                )
            );

        const auto ecEof = asio::error::make_error_code( asio::error::eof );

        UTF_REQUIRE(
            TcpSocketCommonBase::isExpectedSocketException(
                isCancelExpected,
                &ecEof
                )
            );

        /*
         * On Windows the error codes for connection reset and aborted are different
         * and we need to handle these separately
         *
         * The values for WSAECONNRESET (10054), WSAECONNABORTED (10053) and
         * WSAETIMEDOUT (10060), etc are from here:
         * http://msdn.microsoft.com/en-us/library/windows/desktop/ms740668%28v=vs.85%29.aspx
         */

        const auto ecNotConnected =
            os::onUNIX() ?  eh::errc::not_connected : 10057 /* WSAENOTCONN */;
        const auto ecConnectionAborted =
            os::onUNIX() ?  eh::errc::connection_aborted : 10053 /* WSAECONNABORTED */;
        const auto ecConnectionReset =
            os::onUNIX() ?  eh::errc::connection_reset : 10054 /* WSAECONNRESET */;
        const auto ecConnectionInProgress =
            os::onUNIX() ?  eh::errc::connection_already_in_progress : 10037 /* WSAEALREADY */;
        const auto ecConnectionRefused =
            os::onUNIX() ?  eh::errc::connection_refused : 10061 /* WSAECONNREFUSED */;
        const auto ecBrokenPipe =
            os::onUNIX() ?  eh::errc::broken_pipe : 10053 /* WSAECONNABORTED */;
        const auto ecTimedOut =
            os::onUNIX() ?  eh::errc::timed_out : 10060 /* WSAETIMEDOUT */;
        const auto ecHostUnreachable =
            os::onUNIX() ?  eh::errc::host_unreachable : 10065 /* WSAEHOSTUNREACH */;

        {
            const auto ec = eh::error_code( ecNotConnected, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:107" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:57" ) : std::string( "system:10057" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecConnectionAborted, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:103" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:53" ) : std::string( "system:10053" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecConnectionReset, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:104" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:54" ) : std::string( "system:10054" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecConnectionInProgress, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:114" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:37" ) : std::string( "system:10037" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecConnectionRefused, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:111" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:61" ) : std::string( "system:10061" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecBrokenPipe, eh::system_category() );

            UTF_REQUIRE_EQUAL(
                eh::errorCodeToString( ec ),
                os::onUNIX() ? std::string( "system:32" ) : std::string( "system:10053" )
                );

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecTimedOut, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:110" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:60" ) : std::string( "system:10060" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }

        {
            const auto ec = eh::error_code( ecHostUnreachable, eh::system_category() );

            if( os::onLinux() )
            {
                UTF_REQUIRE_EQUAL( eh::errorCodeToString( ec ), std::string( "system:113" ) );                                    }
            else
            {
                UTF_REQUIRE_EQUAL(
                    eh::errorCodeToString( ec ),
                    os::onUNIX() ? std::string( "system:65" ) : std::string( "system:10065" )
                    );
            }

            UTF_REQUIRE( TcpSocketCommonBase::isExpectedSocketException( isCancelExpected, &ec ) );
        }
    };

    testExpectedSocketException( true /* isCancelExpected */ );
    testExpectedSocketException( false /* isCancelExpected */ );

    /*
     * Test TcpSslSocketAsyncBase::isExpectedSslErrorCode / ::isExpectedSslException /
     * ::isExpectedProtocolException
     *
     * Two forms are accepted on purpose - the modern one (a code whose category is named
     * "asio.ssl.stream" and whose value is 1, i.e. asio::ssl::error::stream_truncated) and
     * the legacy one (g_sslErrorShortRead). Losing either turns an abrupt TLS close into a
     * hard failure on one half of the supported toolchain matrix, and widening the
     * comparison to any SSL category code would swallow genuine SSL errors as 'expected'
     */

    {
        /*
         * (1) The modern form - the category is matched by name and the value must be
         * exactly 1
         */

        const eh::error_code ecStreamTruncated( 1, fakeSslStreamCategory() );
        const eh::error_code ecStreamOther( 2, fakeSslStreamCategory() );

        UTF_REQUIRE( TcpSslSocketAsyncBase::isExpectedSslErrorCode( ecStreamTruncated ) );
        UTF_REQUIRE( ! TcpSslSocketAsyncBase::isExpectedSslErrorCode( ecStreamOther ) );

        /*
         * (2) The legacy form - rebuilt here exactly the way the protected
         * g_sslErrorShortRead member is built in TcpSslBaseTasks.h; SSL_R_SHORT_READ is
         * guaranteed to be defined because that header #defines it when OpenSSL does not
         */

        const eh::error_code ecShortRead(
            static_cast< int >( ERR_PACK( ERR_LIB_SSL, 0, SSL_R_SHORT_READ ) ),
            asio::error::get_ssl_category()
            );

        UTF_REQUIRE( TcpSslSocketAsyncBase::isExpectedSslErrorCode( ecShortRead ) );

        /*
         * (3) The negatives, including an SSL category code which is not the short read one -
         * that row is what pins that the legacy form compares the whole error code and not
         * just the category
         */

        const eh::error_code ecSslOther(
            static_cast< int >( ERR_PACK( ERR_LIB_SSL, 0, SSL_R_SHORT_READ ) ) + 1,
            asio::error::get_ssl_category()
            );

        UTF_REQUIRE( ! TcpSslSocketAsyncBase::isExpectedSslErrorCode( eh::error_code() ) );

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedSslErrorCode(
                asio::error::make_error_code( asio::error::eof )
                )
            );

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedSslErrorCode(
                asio::error::make_error_code( asio::error::operation_aborted )
                )
            );

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedSslErrorCode(
                asio::error::make_error_code( asio::error::connection_reset )
                )
            );

        UTF_REQUIRE( ! TcpSslSocketAsyncBase::isExpectedSslErrorCode( ecSslOther ) );

        /*
         * (4) The two exception forms - the exception_ptr and the exception itself are both
         * BL_UNUSED in the implementation, so only the error code decides
         */

        const UnexpectedException sslException;
        const auto sslExceptionPtr = std::make_exception_ptr( sslException );

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedSslException( sslExceptionPtr, sslException, nullptr /* ec */ )
            );

        UTF_REQUIRE(
            TcpSslSocketAsyncBase::isExpectedSslException( sslExceptionPtr, sslException, &ecShortRead )
            );

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedSslException( sslExceptionPtr, sslException, &ecSslOther )
            );

        /*
         * ... and isExpectedProtocolException just forwards to isExpectedSslException
         */

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedProtocolException( sslExceptionPtr, sslException, nullptr /* ec */ )
            );

        UTF_REQUIRE(
            TcpSslSocketAsyncBase::isExpectedProtocolException( sslExceptionPtr, sslException, &ecShortRead )
            );

        UTF_REQUIRE(
            ! TcpSslSocketAsyncBase::isExpectedProtocolException( sslExceptionPtr, sslException, &ecSslOther )
            );
    }
}

UTF_AUTO_TEST_CASE( IO_ConnectorFailureEndpointErrorInfoTests )
{
    using namespace bl;
    using namespace bl::tasks;

    /*
     * TcpConnectionEstablisherBase::enhanceException attaches the host and the service names
     * from m_query and the endpoint address and port from m_endpoint, which onResolved()
     * assigns *before* the connect is attempted; data/eh/ServerErrorHelpers.h consumes all
     * four, so a regression which dropped any of them - or which moved the m_endpoint
     * assignment after the connect, leaving the address and the port empty on exactly the
     * failures which need them - silently degrades every connection failure diagnostic
     *
     * The machine global lock is taken so no other module is listening on the test port;
     * nothing is started here, so the port stays closed and the connect is refused
     * immediately on both Linux and Windows - no timeout tuning is needed
     */

    test::MachineGlobalTestLock lock;

    const std::string host( "127.0.0.1" );

    const auto port = test::UtfArgsParser::port();

    om::ObjPtr< Task > task;

    tasks::scheduleAndExecuteInParallel(
        [ & ]( SAA_in const om::ObjPtr< ExecutionQueue >& eq ) -> void
        {
            const auto connector = connector_t::createInstance( cpp::copy( host ), port );

            eq -> push_back( om::qi< Task >( connector ) );

            /*
             * pop() waits for the task and removes it from the queue, so the flush which
             * scheduleAndExecuteInParallel performs on the way out has nothing to rethrow
             */

            task = eq -> pop( true /* wait */ );
        }
        );

    UTF_REQUIRE( task );
    UTF_REQUIRE( task -> isFailed() );

    try
    {
        cpp::safeRethrowException( task -> exception() );

        UTF_FAIL( "Connecting to a closed port is expected to throw" );
    }
    catch( bl::eh::exception& e )
    {
        BL_LOG_MULTILINE(
            Logging::debug(),
            BL_MSG()
                << "Expected connection failure:\n"
                << eh::diagnostic_information( e )
            );

        const auto* hostName = eh::get_error_info< eh::errinfo_host_name >( e );

        UTF_REQUIRE( hostName );
        UTF_REQUIRE_EQUAL( *hostName, host );

        const auto* serviceName = eh::get_error_info< eh::errinfo_service_name >( e );

        UTF_REQUIRE( serviceName );
        UTF_REQUIRE_EQUAL( *serviceName, utils::lexical_cast< std::string >( port ) );

        /*
         * The address and the port come from m_endpoint, which the resolve step has already
         * populated by the time the connect can fail
         */

        const auto* endpointAddress = eh::get_error_info< eh::errinfo_endpoint_address >( e );

        UTF_REQUIRE( endpointAddress );
        UTF_REQUIRE_EQUAL( *endpointAddress, host );

        const auto* endpointPort = eh::get_error_info< eh::errinfo_endpoint_port >( e );

        UTF_REQUIRE( endpointPort );
        UTF_REQUIRE_EQUAL( *endpointPort, port );
    }

    UTF_REQUIRE( task -> isFailed() );
}

UTF_AUTO_TEST_CASE( IO_DataBlockCrossPoolCapacityTests )
{
    using namespace bl;

    /*
     * data::DataBlock::get( pool, capacity ) honours the requested capacity only on a pool
     * miss - on a pool hit the pooled block is handed back as is and the argument is
     * discarded. data::DataBlock::copy( block, pool ) forwards block -> capacity() as that
     * argument, so a copy into a pool of *smaller* blocks throws BufferTooSmallException out
     * of write()
     *
     * That is the mechanism behind the process wide invariant which
     * ForwardingBackendSharedState's constructor enforces by setting
     * isNoCopyDataBlocks( true ) on every client channel it owns, and which
     * AsyncExecutorWrapperBlocks.h only checks through a BL_ASSERT - i.e. not at all in a
     * release build, where the same mistake is either a dropped message at an arbitrary
     * later point or a pool which slowly accumulates wrong capacity blocks
     */

    const auto poolSmall = data::datablocks_pool_type::createInstance();

    poolSmall -> put( data::DataBlock::createInstance( 4096U /* capacity */ ) );

    const auto big = data::DataBlock::createInstance( 64U * 1024U /* capacity */ );

    big -> setSize( 8192U );

    UTF_REQUIRE( big -> size() > 4096U );

    /*
     * The copy asks for 64 KiB, the pool hands back the 4 KiB block it holds and the
     * subsequent write() no longer fits - this is the assertion which pins get()'s capacity
     * discarding behaviour, which is otherwise invisible
     */

    UTF_REQUIRE_THROW( data::DataBlock::copy( big, poolSmall ), bl::BufferTooSmallException );

    /*
     * The control - with an empty pool the requested capacity *is* honoured, which states
     * plainly that it is honoured only on a pool miss
     */

    const auto poolEmpty = data::datablocks_pool_type::createInstance();

    const auto copied = data::DataBlock::copy( big, poolEmpty );

    UTF_REQUIRE( copied );
    UTF_REQUIRE_EQUAL( copied -> capacity(), big -> capacity() );
    UTF_REQUIRE_EQUAL( copied -> size(), big -> size() );
}
