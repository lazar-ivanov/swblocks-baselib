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

#ifndef __UTEST_TESTTLSHANDSHAKERETRYCLASSIFIER_H_
#define __UTEST_TESTTLSHANDSHAKERETRYCLASSIFIER_H_

#include <baselib/tasks/TcpBaseTasks.h>
#include <baselib/tasks/TcpSslBaseTasks.h>

#include <baselib/core/AsioSSL.h>
#include <baselib/core/CPP.h>
#include <baselib/core/ErrorHandling.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <exception>
#include <string>

#include <utests/baselib/Utf.h>

/*
 * Which handshake failures the TLS stream policy calls retryable, and which it does not
 *
 * TcpConnectionEstablisherConnector::scheduleTaskFinishContinuation restarts the whole resolve /
 * connect / handshake transaction when the stream policy says the failure is retryable, so this
 * predicate is what decides whether a transient peer-side close costs one attempt or several. The
 * end-to-end consequence is pinned in TestTcpPreHandshakeStageTls.h against a real peer; what is
 * pinned here is the classification itself, exactly as the establisher resolves it, for the codes
 * a truncated or refused handshake can actually produce
 *
 * These cases open no socket, take no lock and resolve no name: the probe is constructed and never
 * scheduled, which is enough to reach the predicate
 */

namespace utest
{
    namespace tlsretry
    {
        /**
         * @brief A test local error category which impersonates the modern ASIO SSL stream
         * category by name
         *
         * TcpSslSocketAsyncBase::isExpectedSslErrorCode() identifies that category by the string
         * "asio.ssl.stream" rather than by referencing asio::ssl::error::stream_truncated, so that
         * baselib builds against the whole supported ASIO / OpenSSL range - reproducing the string
         * here is what lets the test cover the accepted modern form without reintroducing into the
         * test exactly the dependency the production code avoids
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

        inline const bl::eh::error_category& fakeSslStreamCategory()
        {
            static const FakeSslStreamCategory g_fakeSslStreamCategory;

            return g_fakeSslStreamCategory;
        }

        /**
         * @brief The TLS connection establisher with its handshake retry predicate exposed
         *
         * The predicate is a protected member of the STREAM policy which the establisher resolves
         * by template composition, so reaching it through the establisher - rather than through the
         * policy directly - is what makes this the same resolution scheduleTaskFinishContinuation
         * performs at TcpBaseTasks.h:1445
         */

        class RetryClassifierProbe :
            public bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSslSocketAsyncBase >
        {
            BL_DECLARE_OBJECT_IMPL( RetryClassifierProbe )

        public:

            typedef bl::tasks::TcpConnectionEstablisherConnector< bl::tasks::TcpSslSocketAsyncBase >
                base_type;

            bool isRetryable( SAA_in const std::exception_ptr& eptr )
            {
                return base_type::isProtocolHandshakeRetryableError( eptr );
            }

        protected:

            RetryClassifierProbe()
                :
                base_type( std::string( "localhost" ), 0U /* port */, false /* logExceptions */ )
            {
            }
        };

        typedef bl::om::ObjectImpl< RetryClassifierProbe >                   RetryClassifierProbeImpl;

        /**
         * @brief The exception a failed asynchronous operation carries, built the way baselib
         * builds it - BL_CHK_EC and the task handler macros both end in BL_THROW_EC
         */

        inline auto handshakeFailure( SAA_in const bl::eh::error_code& ec ) -> std::exception_ptr
        {
            try
            {
                BL_THROW_EC(
                    ec,
                    BL_MSG()
                        << "The TLS handshake has failed"
                    );
            }
            catch( std::exception& )
            {
                return std::current_exception();
            }

            return std::exception_ptr();
        }

        /*
         * The two forms of truncation the library knows about, built exactly the way
         * isExpectedSslErrorCode() matches them, plus the codes which must never be retried
         */

        inline auto truncationModern() -> bl::eh::error_code
        {
            return bl::eh::error_code( 1, fakeSslStreamCategory() );
        }

        inline auto truncationLegacy() -> bl::eh::error_code
        {
            return bl::eh::error_code(
                static_cast< int >( ERR_PACK( ERR_LIB_SSL, 0, SSL_R_SHORT_READ ) ),
                bl::asio::error::get_ssl_category()
                );
        }

    } // tlsretry

} // utest

UTF_AUTO_TEST_CASE( TlsHandshakeRetryClassifier_RetryableErrorSetTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tlsretry;

    /*
     * The whole set, one row per code the predicate can be handed. Only an orderly end of the
     * stream during the handshake is retried; a reset, a refusal and a cancellation are not, and
     * neither is a neighbouring code in either of the two categories the truncation forms live in
     */

    const auto probe = RetryClassifierProbeImpl::createInstance();

    /*
     * (1) An end of stream reported by ASIO itself, which is what the predicate has always
     * accepted and what makes the retry reachable against a peer on an older ASIO / OpenSSL
     */

    UTF_REQUIRE(
        probe -> isRetryable( handshakeFailure( asio::error::make_error_code( asio::error::eof ) ) )
        );

    /*
     * (2) The legacy truncation form, the OpenSSL packed SSL_R_SHORT_READ
     */

    UTF_REQUIRE( probe -> isRetryable( handshakeFailure( truncationLegacy() ) ) );

    /*
     * (3) The modern truncation form - asio.ssl.stream:1, which is what boost 1.90 with
     * OpenSSL 3.5 really reports when a peer closes during the handshake. Accepting it is what
     * makes the retry reachable against a real peer on that stack at all; while it was not
     * accepted the retry was dead code - see
     * notes/plans/issues/tls-handshake-retry-unreachable-record.md
     */

    UTF_REQUIRE( probe -> isRetryable( handshakeFailure( truncationModern() ) ) );

    /*
     * (4) The negatives. The two category rows pin that neither category is accepted wholesale -
     * losing that would turn a genuine SSL error into an attempt storm
     */

    UTF_REQUIRE(
        ! probe -> isRetryable( handshakeFailure( eh::error_code( 2, fakeSslStreamCategory() ) ) )
        );

    UTF_REQUIRE(
        ! probe -> isRetryable(
            handshakeFailure(
                eh::error_code(
                    static_cast< int >( ERR_PACK( ERR_LIB_SSL, 0, SSL_R_SHORT_READ ) ) + 1,
                    asio::error::get_ssl_category()
                    )
                )
            )
        );

    /*
     * A peer which resets rather than closes is a different condition and must not be retried,
     * and neither must a refusal or the operation_aborted of a cancelled task - retrying a
     * cancellation would defeat the cancellation
     *
     * connection_reset is asserted BOTH ways, because what it means is a property of the platform
     * and not of this predicate. Where the TCP stack sends FIN for a peer's orderly close, a reset
     * is a genuinely distinct condition and stays non-retryable, which is the row this case has
     * always pinned. Where the stack sends RST instead whenever data is still unread - Windows -
     * the orderly close IS this code, the two conditions are indistinguishable by the time any
     * library sees them, and refusing it would leave the retry unreachable exactly as the missing
     * truncation form once did. See os::peerCloseWithUnreadDataIsReportedAsReset() and the
     * measurement recorded in notes/plans/issues/tls-handshake-retry-unreachable-record.md
     *
     * Asserting both arms rather than skipping one keeps the case meaningful on every platform:
     * neither arm can silently become vacuous
     */

    if( os::peerCloseWithUnreadDataIsReportedAsReset() )
    {
        UTF_REQUIRE(
            probe -> isRetryable(
                handshakeFailure( asio::error::make_error_code( asio::error::connection_reset ) )
                )
            );
    }
    else
    {
        UTF_REQUIRE(
            ! probe -> isRetryable(
                handshakeFailure( asio::error::make_error_code( asio::error::connection_reset ) )
                )
            );
    }

    UTF_REQUIRE(
        ! probe -> isRetryable(
            handshakeFailure( asio::error::make_error_code( asio::error::operation_aborted ) )
            )
        );

    UTF_REQUIRE(
        ! probe -> isRetryable(
            handshakeFailure( asio::error::make_error_code( asio::error::connection_refused ) )
            )
        );

    UTF_REQUIRE( ! probe -> isRetryable( handshakeFailure( eh::error_code() ) ) );

    /*
     * (5) An exception which carries no error code at all falls through the predicate's second
     * catch clause
     */

    UTF_REQUIRE(
        ! probe -> isRetryable(
            BL_MAKE_EXCEPTION_PTR(
                UnexpectedException(),
                BL_MSG()
                    << "Not a system error"
                )
            )
        );
}

UTF_AUTO_TEST_CASE( TlsHandshakeRetryClassifier_TruncationAgreementTests )
{
    using namespace bl;
    using namespace bl::tasks;
    using namespace utest::tlsretry;

    /*
     * TcpSslBaseTasks.h carries two classifiers over the same codes: isExpectedSslErrorCode(),
     * which decides whether a truncation is an expected end of a TLS stream, and the handshake
     * retry predicate. What they say about the same code is the subject of this case
     */

    const auto probe = RetryClassifierProbeImpl::createInstance();

    /*
     * (1) The modern truncation form, where the two agree because the retry predicate asks
     * isStreamTruncationError(), which forwards to the first classifier. They once did not,
     * and a condition which one called expected and the other did not recognize is exactly
     * what made the retry unreachable against a real peer
     */

    UTF_REQUIRE( TcpSslSocketAsyncBase::isExpectedSslErrorCode( truncationModern() ) );
    UTF_REQUIRE( probe -> isRetryable( handshakeFailure( truncationModern() ) ) );

    /*
     * (2) The legacy truncation form, where the two have always agreed
     */

    UTF_REQUIRE( TcpSslSocketAsyncBase::isExpectedSslErrorCode( truncationLegacy() ) );
    UTF_REQUIRE( probe -> isRetryable( handshakeFailure( truncationLegacy() ) ) );

    /*
     * (3) And the direction which is not symmetrical and must stay so: an end of stream is
     * retryable although it is not an expected SSL error code, so the retryable set is never a
     * subset of the expected one
     */

    const auto eof = asio::error::make_error_code( asio::error::eof );

    UTF_REQUIRE( ! TcpSslSocketAsyncBase::isExpectedSslErrorCode( eof ) );
    UTF_REQUIRE( probe -> isRetryable( handshakeFailure( eof ) ) );
}

#endif /* __UTEST_TESTTLSHANDSHAKERETRYCLASSIFIER_H_ */
