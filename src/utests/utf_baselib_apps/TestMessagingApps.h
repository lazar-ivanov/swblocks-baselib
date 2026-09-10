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

#include <apps/bl-messaging-http-gateway/MessagingHttpGatewayApp.h>

namespace
{
    /**
     * @brief Re-exports the gateway application's nested implementation type
     *
     * MessagingHttpGatewayAppT declares MessagingHttpGatewayAppImplT and the
     * MessagingHttpGatewayAppImpl typedef under 'protected:', so a derived class may name
     * them. The accessor itself is never instantiated - BL_DECLARE_STATIC makes the base's
     * default constructor private - it exists only to make the typedef reachable.
     *
     * appMain() is called directly rather than through main(), which would construct a
     * second AppInitDoneDefault on top of the one DefaultUtfConfig already owns.
     */

    struct GatewayAppAccessor : public bl::rest::MessagingHttpGatewayApp
    {
        typedef bl::rest::MessagingHttpGatewayApp::MessagingHttpGatewayAppImpl impl_t;
    };

    /*
     * --target-peer-id is the command line's only Required option, so every sub-block which
     * is expected to reach the TLS guards has to supply a parseable UUID for it
     */

    const char* const g_targetPeerId = "8e213524-8c75-4622-8273-6a5eeaa26250";

} // __unnamed

UTF_AUTO_TEST_CASE( MessagingApps_HttpGatewayTlsValidationTests )
{
    /*
     * The gateway's two BL_CHK_USER guards are the only thing standing between an operator
     * who supplied TLS material and a plaintext HTTP listener - inverting or removing either
     * one silently downgrades a security boundary, or refuses to start a correctly
     * configured gateway.
     *
     * Both guards run before any socket, backend or thread is created, which is what makes
     * them reachable from a test at all. Nothing past them is asserted here: the port default
     * and the serverAuthenticationRequired expression run on into
     * ForwardingBackendProcessingFactoryDefaultSsl::create(), which needs a live broker.
     */

    {
        /*
         * (1) --no-tls together with a private key
         */

        const char* argv[] =
        {
            "gw", "--target-peer-id", g_targetPeerId, "--no-tls", "--private-key-file", "k.pem"
        };

        GatewayAppAccessor::impl_t app;

        UTF_REQUIRE_THROW_MESSAGE(
            app.appMain( BL_ARRAY_SIZE( argv ), argv ),
            bl::UserMessageException,
            "should not be provided"
            );
    }

    {
        /*
         * (2) --no-tls together with a certificate
         */

        const char* argv[] =
        {
            "gw", "--target-peer-id", g_targetPeerId, "--no-tls", "--certificate-file", "c.pem"
        };

        GatewayAppAccessor::impl_t app;

        UTF_REQUIRE_THROW_MESSAGE(
            app.appMain( BL_ARRAY_SIZE( argv ), argv ),
            bl::UserMessageException,
            "should not be provided"
            );
    }

    {
        /*
         * (3) TLS requested, but only half of the material was provided
         */

        const char* argv[] =
        {
            "gw", "--target-peer-id", g_targetPeerId, "--private-key-file", "k.pem"
        };

        GatewayAppAccessor::impl_t app;

        UTF_REQUIRE_THROW_MESSAGE(
            app.appMain( BL_ARRAY_SIZE( argv ), argv ),
            bl::UserMessageException,
            "are required unless"
            );
    }

    {
        /*
         * (4) TLS requested and nothing was provided - the plain misconfiguration
         */

        const char* argv[] = { "gw", "--target-peer-id", g_targetPeerId };

        GatewayAppAccessor::impl_t app;

        UTF_REQUIRE_THROW_MESSAGE(
            app.appMain( BL_ARRAY_SIZE( argv ), argv ),
            bl::UserMessageException,
            "are required unless"
            );
    }

    {
        /*
         * (5) --help returns normally even though --target-peer-id is Required, proving the
         * HelpSwitch override reaches this app; this is the only sub-block which returns and
         * it returns before any resource is acquired
         */

        const char* argv[] = { "gw", "--help" };

        GatewayAppAccessor::impl_t app;

        UTF_REQUIRE_NO_THROW( app.appMain( BL_ARRAY_SIZE( argv ), argv ) );
    }

    {
        /*
         * (6) A malformed --source-peer-id is a hard failure out of uuids::string2uuid rather
         * than a silent fallback to uuids::create()
         */

        const char* argv[] =
        {
            "gw", "--source-peer-id", "not-a-uuid", "--target-peer-id", g_targetPeerId, "--no-tls"
        };

        GatewayAppAccessor::impl_t app;

        UTF_REQUIRE_THROW( app.appMain( BL_ARRAY_SIZE( argv ), argv ), std::exception );
    }
}
