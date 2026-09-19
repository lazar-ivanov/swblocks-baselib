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

#ifndef __BL_DATA_MODELS_HTTPCLIENTPROFILES_H_
#define __BL_DATA_MODELS_HTTPCLIENTPROFILES_H_

#include <baselib/data/DataModelObjectDefs.h>

namespace bl
{
    namespace dm
    {
        namespace httpclient
        {
            /*
             * The JSON form of a browser profile (notes/plans/http2-design.md 6.2)
             *
             * Built-in profiles are JSON literals parsed at first use and supplied profiles are
             * JSON files, so both take one code path and a refresh of the version strings needs no
             * library release. These models are that path's input side; the loader which turns them
             * into crypto::TlsClientProfile, http2::Http2Profile and httpclient::HeaderProfile,
             * validating as it goes, is a separate slice and lives elsewhere
             *
             * Every enumerated value is a string here and an enum in the corresponding struct. That
             * is deliberate: a loaded profile is untrusted input, so an unknown value has to be a
             * rejection the loader reports rather than an out-of-range enum nobody checked
             *
             * This file carries no profile content - no browser, no version, no suite name
             */

            /**
             * @brief Class TlsGroup - crypto::TlsGroup
             */

            BL_DM_DEFINE_CLASS_BEGIN( TlsGroup )

                BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( name )
                BL_DM_DECLARE_BOOL_PROPERTY( keyShare )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( name )
                    BL_DM_IMPL_PROPERTY( keyShare )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( TlsGroup )

            BL_DM_DEFINE_PROPERTY( TlsGroup, name )
            BL_DM_DEFINE_PROPERTY( TlsGroup, keyShare )

            /**
             * @brief Class TlsClientProfile - crypto::TlsClientProfile
             */

            BL_DM_DEFINE_CLASS_BEGIN( TlsClientProfile )

                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( cipherSuitesTls12, std::string, get_string )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( cipherSuitesTls13, std::string, get_string )
                BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY( groups, bl::dm::httpclient::TlsGroup )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( signatureAlgorithms, std::string, get_string )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( alpnProtocols, std::string, get_string )
                BL_DM_DECLARE_BOOL_PROPERTY( sessionTicket )
                BL_DM_DECLARE_BOOL_PROPERTY( statusRequest )
                BL_DM_DECLARE_BOOL_PROPERTY( signedCertificateTimestamp )
                BL_DM_DECLARE_BOOL_PROPERTY( padding )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( cipherSuitesTls12 )
                    BL_DM_IMPL_PROPERTY( cipherSuitesTls13 )
                    BL_DM_IMPL_PROPERTY( groups )
                    BL_DM_IMPL_PROPERTY( signatureAlgorithms )
                    BL_DM_IMPL_PROPERTY( alpnProtocols )
                    BL_DM_IMPL_PROPERTY( sessionTicket )
                    BL_DM_IMPL_PROPERTY( statusRequest )
                    BL_DM_IMPL_PROPERTY( signedCertificateTimestamp )
                    BL_DM_IMPL_PROPERTY( padding )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( TlsClientProfile )

            BL_DM_DEFINE_PROPERTY( TlsClientProfile, cipherSuitesTls12 )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, cipherSuitesTls13 )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, groups )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, signatureAlgorithms )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, alpnProtocols )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, sessionTicket )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, statusRequest )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, signedCertificateTimestamp )
            BL_DM_DEFINE_PROPERTY( TlsClientProfile, padding )

            /**
             * @brief Class Http2Setting - http2::Http2Setting
             *
             * The value is carried as a 64-bit unsigned because a SETTINGS value is a full uint32
             * on the wire and would not fit the signed 32-bit int property
             */

            BL_DM_DEFINE_CLASS_BEGIN( Http2Setting )

                BL_DM_DECLARE_INT_REQUIRED_PROPERTY( id )
                BL_DM_DECLARE_UINT64_REQUIRED_PROPERTY( value )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( id )
                    BL_DM_IMPL_PROPERTY( value )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( Http2Setting )

            BL_DM_DEFINE_PROPERTY( Http2Setting, id )
            BL_DM_DEFINE_PROPERTY( Http2Setting, value )

            /**
             * @brief Class Http2PriorityFrame - http2::Http2PriorityFrame
             */

            BL_DM_DEFINE_CLASS_BEGIN( Http2PriorityFrame )

                BL_DM_DECLARE_UINT64_REQUIRED_PROPERTY( streamId )
                BL_DM_DECLARE_UINT64_PROPERTY( streamDependency )
                BL_DM_DECLARE_INT_PROPERTY( weight )
                BL_DM_DECLARE_BOOL_PROPERTY( exclusive )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( streamId )
                    BL_DM_IMPL_PROPERTY( streamDependency )
                    BL_DM_IMPL_PROPERTY( weight )
                    BL_DM_IMPL_PROPERTY( exclusive )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( Http2PriorityFrame )

            BL_DM_DEFINE_PROPERTY( Http2PriorityFrame, streamId )
            BL_DM_DEFINE_PROPERTY( Http2PriorityFrame, streamDependency )
            BL_DM_DEFINE_PROPERTY( Http2PriorityFrame, weight )
            BL_DM_DEFINE_PROPERTY( Http2PriorityFrame, exclusive )

            /**
             * @brief Class Http2HeadersPriority - http2::Http2HeadersPriority
             */

            BL_DM_DEFINE_CLASS_BEGIN( Http2HeadersPriority )

                BL_DM_DECLARE_BOOL_PROPERTY( isSet )
                BL_DM_DECLARE_UINT64_PROPERTY( streamDependency )
                BL_DM_DECLARE_INT_PROPERTY( weight )
                BL_DM_DECLARE_BOOL_PROPERTY( exclusive )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( isSet )
                    BL_DM_IMPL_PROPERTY( streamDependency )
                    BL_DM_IMPL_PROPERTY( weight )
                    BL_DM_IMPL_PROPERTY( exclusive )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( Http2HeadersPriority )

            BL_DM_DEFINE_PROPERTY( Http2HeadersPriority, isSet )
            BL_DM_DEFINE_PROPERTY( Http2HeadersPriority, streamDependency )
            BL_DM_DEFINE_PROPERTY( Http2HeadersPriority, weight )
            BL_DM_DEFINE_PROPERTY( Http2HeadersPriority, exclusive )

            /**
             * @brief Class Http2Profile - http2::Http2Profile
             */

            BL_DM_DEFINE_CLASS_BEGIN( Http2Profile )

                BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY( settings, bl::dm::httpclient::Http2Setting )
                BL_DM_DECLARE_UINT64_PROPERTY( connectionWindowUpdateIncrement )
                BL_DM_DECLARE_UINT64_PROPERTY( windowUpdateThreshold )
                BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY( idleStreamPriorities, bl::dm::httpclient::Http2PriorityFrame )
                BL_DM_DECLARE_COMPLEX_PROPERTY( headersPriority, bl::dm::httpclient::Http2HeadersPriority )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( pseudoHeaderOrder, std::string, get_string )
                BL_DM_DECLARE_UINT64_PROPERTY( hpackEncoderTableSize )
                BL_DM_DECLARE_STRING_PROPERTY( hpackIndexingPolicy )
                BL_DM_DECLARE_BOOL_PROPERTY( cookieCrumbling )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( settings )
                    BL_DM_IMPL_PROPERTY( connectionWindowUpdateIncrement )
                    BL_DM_IMPL_PROPERTY( windowUpdateThreshold )
                    BL_DM_IMPL_PROPERTY( idleStreamPriorities )
                    BL_DM_IMPL_PROPERTY( headersPriority )
                    BL_DM_IMPL_PROPERTY( pseudoHeaderOrder )
                    BL_DM_IMPL_PROPERTY( hpackEncoderTableSize )
                    BL_DM_IMPL_PROPERTY( hpackIndexingPolicy )
                    BL_DM_IMPL_PROPERTY( cookieCrumbling )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( Http2Profile )

            BL_DM_DEFINE_PROPERTY( Http2Profile, settings )
            BL_DM_DEFINE_PROPERTY( Http2Profile, connectionWindowUpdateIncrement )
            BL_DM_DEFINE_PROPERTY( Http2Profile, windowUpdateThreshold )
            BL_DM_DEFINE_PROPERTY( Http2Profile, idleStreamPriorities )
            BL_DM_DEFINE_PROPERTY( Http2Profile, headersPriority )
            BL_DM_DEFINE_PROPERTY( Http2Profile, pseudoHeaderOrder )
            BL_DM_DEFINE_PROPERTY( Http2Profile, hpackEncoderTableSize )
            BL_DM_DEFINE_PROPERTY( Http2Profile, hpackIndexingPolicy )
            BL_DM_DEFINE_PROPERTY( Http2Profile, cookieCrumbling )

            /**
             * @brief Class ProfileHeader - httpclient::ProfileHeader
             */

            BL_DM_DEFINE_CLASS_BEGIN( ProfileHeader )

                BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( name )
                BL_DM_DECLARE_STRING_PROPERTY( value )
                BL_DM_DECLARE_BOOL_PROPERTY( isComputed )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( name )
                    BL_DM_IMPL_PROPERTY( value )
                    BL_DM_IMPL_PROPERTY( isComputed )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( ProfileHeader )

            BL_DM_DEFINE_PROPERTY( ProfileHeader, name )
            BL_DM_DEFINE_PROPERTY( ProfileHeader, value )
            BL_DM_DEFINE_PROPERTY( ProfileHeader, isComputed )

            /**
             * @brief Class HeaderProfileForKind - httpclient::HeaderProfileForKind
             */

            BL_DM_DEFINE_CLASS_BEGIN( HeaderProfileForKind )

                BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY( defaultHeaders, bl::dm::httpclient::ProfileHeader )
                BL_DM_DECLARE_STRING_PROPERTY( callerHeaderPlacement )
                BL_DM_DECLARE_STRING_PROPERTY( callerHeaderAnchor )
                BL_DM_DECLARE_MAP_PROPERTY( http1CaseMap, std::string )
                BL_DM_DECLARE_STRING_PROPERTY( priorityHeaderValue )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( defaultHeaders )
                    BL_DM_IMPL_PROPERTY( callerHeaderPlacement )
                    BL_DM_IMPL_PROPERTY( callerHeaderAnchor )
                    BL_DM_IMPL_PROPERTY( http1CaseMap )
                    BL_DM_IMPL_PROPERTY( priorityHeaderValue )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( HeaderProfileForKind )

            BL_DM_DEFINE_PROPERTY( HeaderProfileForKind, defaultHeaders )
            BL_DM_DEFINE_PROPERTY( HeaderProfileForKind, callerHeaderPlacement )
            BL_DM_DEFINE_PROPERTY( HeaderProfileForKind, callerHeaderAnchor )
            BL_DM_DEFINE_PROPERTY( HeaderProfileForKind, http1CaseMap )
            BL_DM_DEFINE_PROPERTY( HeaderProfileForKind, priorityHeaderValue )

            /**
             * @brief Class HeaderProfile - httpclient::HeaderProfile
             *
             * The per-kind tables are three named properties rather than one map, because the set
             * of request kinds is fixed by the enum and a JSON object keyed by an enumerated name
             * would be one more place for an unknown key to pass unnoticed
             */

            BL_DM_DEFINE_CLASS_BEGIN( HeaderProfile )

                BL_DM_DECLARE_COMPLEX_PROPERTY( navigation, bl::dm::httpclient::HeaderProfileForKind )
                BL_DM_DECLARE_COMPLEX_PROPERTY( fetch, bl::dm::httpclient::HeaderProfileForKind )
                BL_DM_DECLARE_COMPLEX_PROPERTY( subresource, bl::dm::httpclient::HeaderProfileForKind )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( acceptEncoding, std::string, get_string )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( acceptLanguageQValues, std::string, get_string )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( navigation )
                    BL_DM_IMPL_PROPERTY( fetch )
                    BL_DM_IMPL_PROPERTY( subresource )
                    BL_DM_IMPL_PROPERTY( acceptEncoding )
                    BL_DM_IMPL_PROPERTY( acceptLanguageQValues )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( HeaderProfile )

            BL_DM_DEFINE_PROPERTY( HeaderProfile, navigation )
            BL_DM_DEFINE_PROPERTY( HeaderProfile, fetch )
            BL_DM_DEFINE_PROPERTY( HeaderProfile, subresource )
            BL_DM_DEFINE_PROPERTY( HeaderProfile, acceptEncoding )
            BL_DM_DEFINE_PROPERTY( HeaderProfile, acceptLanguageQValues )

            /**
             * @brief Class SecChUaBrand - one entry of the sec-ch-ua brand list
             */

            BL_DM_DEFINE_CLASS_BEGIN( SecChUaBrand )

                BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( brand )
                BL_DM_DECLARE_STRING_PROPERTY( version )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( brand )
                    BL_DM_IMPL_PROPERTY( version )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( SecChUaBrand )

            BL_DM_DEFINE_PROPERTY( SecChUaBrand, brand )
            BL_DM_DEFINE_PROPERTY( SecChUaBrand, version )

            /**
             * @brief Class BrowserProfile - identity, shape and version strings (6.2)
             *
             * Shape changes a few times a year; the version strings change every four weeks. They
             * are separate properties for exactly that reason - refreshing a user-agent must not
             * mean editing the shape
             */

            BL_DM_DEFINE_CLASS_BEGIN( BrowserProfile )

                BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( id )
                BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( family )
                BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( grade )
                BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( deviations, std::string, get_string )

                BL_DM_DECLARE_COMPLEX_PROPERTY( tls, bl::dm::httpclient::TlsClientProfile )
                BL_DM_DECLARE_COMPLEX_PROPERTY( http2, bl::dm::httpclient::Http2Profile )
                BL_DM_DECLARE_COMPLEX_PROPERTY( headers, bl::dm::httpclient::HeaderProfile )

                BL_DM_DECLARE_STRING_PROPERTY( userAgent )
                BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY( secChUaBrands, bl::dm::httpclient::SecChUaBrand )
                BL_DM_DECLARE_STRING_PROPERTY( platform )

                BL_DM_PROPERTIES_IMPL_BEGIN()
                    BL_DM_IMPL_PROPERTY( id )
                    BL_DM_IMPL_PROPERTY( family )
                    BL_DM_IMPL_PROPERTY( grade )
                    BL_DM_IMPL_PROPERTY( deviations )
                    BL_DM_IMPL_PROPERTY( tls )
                    BL_DM_IMPL_PROPERTY( http2 )
                    BL_DM_IMPL_PROPERTY( headers )
                    BL_DM_IMPL_PROPERTY( userAgent )
                    BL_DM_IMPL_PROPERTY( secChUaBrands )
                    BL_DM_IMPL_PROPERTY( platform )
                BL_DM_PROPERTIES_IMPL_END()

            BL_DM_DEFINE_CLASS_END( BrowserProfile )

            BL_DM_DEFINE_PROPERTY( BrowserProfile, id )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, family )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, grade )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, deviations )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, tls )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, http2 )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, headers )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, userAgent )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, secChUaBrands )
            BL_DM_DEFINE_PROPERTY( BrowserProfile, platform )

        } // httpclient

    } // dm

} // bl

#endif /* __BL_DATA_MODELS_HTTPCLIENTPROFILES_H_ */
