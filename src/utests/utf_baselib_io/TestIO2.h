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

/*
 * Continuation of TestIO.h, split so that no single test translation unit exhausts a 32-bit
 * compiler host - see notes/reviews/major/update_2026/test-module-split-plan.md
 *
 * These are the messaging client and backend processing cases. They were chosen because the whole
 * range references exactly one name from the anonymous namespace at the top of TestIO.h - the
 * connector_t typedef - which is the smallest coupling any contiguous island in that file has.
 *
 * This file must be included after TestIO.h - it is a continuation, not a standalone header.
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
