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

#ifndef __BL_REST_RESTUTILS_H_
#define __BL_REST_RESTUTILS_H_

#include <baselib/messaging/BackendProcessingBase.h>
#include <baselib/messaging/BrokerErrorCodes.h>
#include <baselib/messaging/GraphQLErrorHelpers.h>

#include <baselib/httpserver/ServerBackendProcessing.h>
#include <baselib/httpserver/Response.h>

#include <baselib/tasks/Task.h>
#include <baselib/tasks/TaskBase.h>
#include <baselib/tasks/TasksUtils.h>

#include <baselib/data/eh/ServerErrorHelpers.h>
#include <baselib/data/models/Http.h>
#include <baselib/data/DataBlock.h>

#include <baselib/core/Uuid.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace rest
    {
        template
        <
            typename E = void
        >
        class RestUtilsT
        {
            BL_DECLARE_STATIC( RestUtilsT )

        public:

            typedef http::Parameters::HttpStatusCode                HttpStatusCode;

            static void updateHttpStatusFromException(
                SAA_inout           std::exception&                 exception,
                SAA_inout           HttpStatusCode&                 httpStatusCode
                )
            {
                using namespace bl::messaging;

                /*
                 * Try to map the generic error codes to meaningful HTTP statuses
                 */

                int errorCodeValue = 0;

                const auto* ec = eh::get_error_info< eh::errinfo_error_code >( exception );

                if( ec && ec -> category() == eh::generic_category() )
                {
                    errorCodeValue = ec -> value();
                }
                else
                {
                    const auto* errNo = eh::get_error_info< eh::errinfo_errno >( exception );

                    if( errNo )
                    {
                        errorCodeValue = *errNo;
                    }
                }

                if( errorCodeValue )
                {
                    switch( static_cast< eh::errc::errc_t >( errorCodeValue ) )
                    {
                        default:
                            break;

                        case BrokerErrorCodes::AuthorizationFailed:
                            httpStatusCode = http::Parameters::HTTP_CLIENT_ERROR_UNAUTHORIZED;
                            break;

                        case BrokerErrorCodes::TargetPeerNotFound:
                            httpStatusCode = http::Parameters::HTTP_SERVER_ERROR_SERVICE_UNAVAILABLE;
                            break;

                        case BrokerErrorCodes::TargetPeerQueueFull:
                        case BrokerErrorCodes::ProtocolValidationFailed:
                            httpStatusCode = http::Parameters::HTTP_SERVER_ERROR_INTERNAL;
                            break;

                        case eh::errc::no_such_file_or_directory:
                            httpStatusCode = http::Parameters::HTTP_CLIENT_ERROR_NOT_FOUND;
                            break;

                        case eh::errc::operation_not_supported:
                            httpStatusCode = http::Parameters::HTTP_SERVER_ERROR_NOT_IMPLEMENTED;
                            break;

                        case eh::errc::operation_not_permitted:
                            httpStatusCode = http::Parameters::HTTP_CLIENT_ERROR_FORBIDDEN;
                            break;

                    }
                }
            }

            /**
             * @brief Formats an exception as a simple JSON server error response body
             *
             * The response of an HTTP gateway goes out to a client which may be untrusted, so
             * the first entry point below - the one every caller uses by default - returns the
             * redacted document; the unredacted variant is the explicit opt-out for an internal
             * deployment which scrapes the full diagnostics over HTTP
             */

            static auto formatEhResponseSimpleJson(
                SAA_in              const HttpStatusCode            httpStatusCode,
                SAA_in              const std::exception_ptr&       eptr
                )
                -> om::ObjPtr< httpserver::Response >
            {
                return formatEhResponseSimpleJsonImpl(
                    httpStatusCode,
                    eptr,
                    true                                                    /* redactErrorResponses */
                    );
            }

            static auto formatEhResponseSimpleJsonUnredacted(
                SAA_in              const HttpStatusCode            httpStatusCode,
                SAA_in              const std::exception_ptr&       eptr
                )
                -> om::ObjPtr< httpserver::Response >
            {
                return formatEhResponseSimpleJsonImpl(
                    httpStatusCode,
                    eptr,
                    false                                                   /* redactErrorResponses */
                    );
            }

            /**
             * @brief The JSON server error document a REST server places in a response body
             *
             * @param redactErrorResponses when 'true' (the default of every caller) the
             * properties which disclose the internals of the server are removed
             */

            static auto getServerErrorAsJson(
                SAA_in              const std::exception_ptr&                   eptr,
                SAA_in              const bool                                  redactErrorResponses,
                SAA_in_opt          const eh::void_exception_callback_t&        exceptionCallback =
                    dm::ServerErrorHelpers::defaultEhCallback()
                )
                -> std::string
            {
                return redactErrorResponses ?
                    dm::ServerErrorHelpers::getRedactedServerErrorAsJson( eptr, exceptionCallback ) :
                    dm::ServerErrorHelpers::getServerErrorAsJson( eptr, exceptionCallback );
            }

            static auto formatEhResponseGraphQL(
                SAA_in              const HttpStatusCode            httpStatusCode,
                SAA_in              const std::exception_ptr&       eptr
                )
                -> om::ObjPtr< httpserver::Response >
            {
                using namespace bl::messaging;

                auto httpStatusCodeActual = httpStatusCode;

                auto contentJson = GraphQLErrorHelpers::getServerErrorAsGraphQL(
                    eptr,
                    cpp::bind(
                        &updateHttpStatusFromException,
                        _1,
                        cpp::ref( httpStatusCodeActual )
                        )
                    );

                return httpserver::Response::createInstance(
                    httpStatusCodeActual                                    /* httpStatusCode */,
                    std::move( contentJson )                                /* content */,
                    cpp::copy( http::HttpHeader::g_contentTypeJsonUtf8 )    /* contentType */
                    );
            }

        private:

            static auto formatEhResponseSimpleJsonImpl(
                SAA_in              const HttpStatusCode            httpStatusCode,
                SAA_in              const std::exception_ptr&       eptr,
                SAA_in              const bool                      redactErrorResponses
                )
                -> om::ObjPtr< httpserver::Response >
            {
                using namespace bl::messaging;

                auto httpStatusCodeActual = httpStatusCode;

                auto contentJson = getServerErrorAsJson(
                    eptr,
                    redactErrorResponses,
                    cpp::bind(
                        &updateHttpStatusFromException,
                        _1,
                        cpp::ref( httpStatusCodeActual )
                        )
                    );

                return httpserver::Response::createInstance(
                    httpStatusCodeActual                                    /* httpStatusCode */,
                    std::move( contentJson )                                /* content */,
                    cpp::copy( http::HttpHeader::g_contentTypeJsonUtf8 )    /* contentType */
                    );
            }
        };

        typedef RestUtilsT<> RestUtils;

    } // rest

} // bl

#endif /* __BL_REST_RESTUTILS_H_ */
