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

#ifndef __BL_DATA_DATAMODELOBJECT_H_
#define __BL_DATA_DATAMODELOBJECT_H_

#include <baselib/crypto/HashCalculator.h>

#include <baselib/core/JsonUtils.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/ObjModelDefs.h>
#include <baselib/core/FileEncoding.h>
#include <baselib/core/BaseIncludes.h>

namespace bl
{
    namespace dm
    {
        /**
         * @brief The base class for all serialization context objects
         */

        template
        <
            typename E = void
        >
        class SerializationContextBaseT
        {
            BL_NO_COPY_OR_MOVE( SerializationContextBaseT )

        protected:

            const bool                                          m_isSerialization;
            json::object                                        m_doc;
            cpp::ScalarTypeIniter< bool >                       m_detectUnknownProperties;
            std::unordered_set< std::string >                   m_processedProperties;

        public:

            SerializationContextBaseT( SAA_in_opt const bool isSerialization = true ) NOEXCEPT
                :
                m_isSerialization( isSerialization )
            {
            }

            SerializationContextBaseT( SAA_in const std::string& json )
                :
                m_isSerialization( false )
            {
                auto rootValue = json::readFromString( json );

                m_doc = std::move( rootValue.as_object() );
            }

            SerializationContextBaseT( SAA_inout json::object&& object ) NOEXCEPT
                :
                m_isSerialization( false ),
                m_doc( std::move( object ) )
            {
            }

            bool detectUnknownProperties() const NOEXCEPT
            {
                return m_detectUnknownProperties;
            }

            void detectUnknownProperties( SAA_in const bool detectUnknownProperties ) NOEXCEPT
            {
                m_detectUnknownProperties = detectUnknownProperties;
            }

            bool isSerialization() const NOEXCEPT
            {
                return m_isSerialization;
            }

            json::object& serializationDoc() NOEXCEPT
            {
                BL_ASSERT( isSerialization() );

                return m_doc;
            }

            json::object& deserializationDoc() NOEXCEPT
            {
                BL_ASSERT( ! isSerialization() );

                return m_doc;
            }

            void addProcessedProperty( SAA_in std::string&& name )
            {
                m_processedProperties.emplace( BL_PARAM_FWD( name ) );
            }

            bool containsProcessedProperty( SAA_in const std::string& name ) const
            {
                auto contains = cpp::contains( m_processedProperties, name );

                BL_CHK_USER(
                    true,
                    ! contains && detectUnknownProperties(),
                    BL_MSG()
                        << "Unrecognized property '"
                        << name
                        << "' found while parsing JSON document. Check if the property is typed correctly."
                    );

                return contains;
            }
        };

        typedef SerializationContextBaseT<> SerializationContextBase;

        /**
         * @brief The base class for all serialize-able data model objects
         */

        template
        <
            typename E = void
        >
        class DataModelObjectT : public om::Object
        {
            BL_DECLARE_OBJECT_IMPL_DEFAULT( DataModelObjectT )
            BL_NO_CREATE( DataModelObjectT )

        protected:

            cpp::ScalarTypeIniter< bool >                                                       m_readOnly;
            json::object                                                                        m_unmapped;

            void readOnlyPropertyUpdateViolation()
            {
                BL_THROW(
                    UnexpectedException(),
                    BL_MSG()
                        << "Trying to modify a read only object"
                    );
            }

            void throwRequiredPropertyNotSet(
                SAA_in              const char*                                                 name,
                SAA_in              const char*                                                 operation
                )
            {
                BL_THROW(
                    UserMessageException(),
                    BL_MSG()
                        << "Required property '"
                        << name
                        << "' is not provided when "
                        << operation
                    );
            }

        public:

            static bool isPartial() NOEXCEPT
            {
                return true;
            }

            bool readOnly() const NOEXCEPT
            {
                return m_readOnly;
            }

            void readOnly( SAA_in const bool readOnly ) NOEXCEPT
            {
                m_readOnly = readOnly;
            }

            auto unmapped() const NOEXCEPT -> const json::object&
            {
                return m_unmapped;
            }

            auto unmappedLvalue() NOEXCEPT -> json::object&
            {
                return m_unmapped;
            }
        };

        typedef DataModelObjectT<> DataModelObject;

        /**
         * @brief Data model utility code
         */

        template
        <
            typename E = void
        >
        class DataModelUtilsT
        {
            BL_DECLARE_STATIC( DataModelUtilsT )

        public:

            /*************************************************************************************************
             * Serialize helpers
             */

            template
            <
                typename T
            >
            static auto getJsonObject(
                SAA_in              const om::ObjPtr< T >&                          dataObject,
                SAA_in_opt          const bool                                      canonicalize = false
                )
                -> json::object
            {
                SerializationContextBase context;

                dataObject -> serializeProperties( context, canonicalize );

                return std::move( context.serializationDoc() );
            }

            /**
             * @brief Serializes a data model object into a JSON string
             *
             * Note that the rawUTF8 parameter of this function and of the getDocAs*JsonString
             * wrappers below is retained for source compatibility and has no effect - string
             * content is always emitted as raw UTF-8 on both backends, with the control
             * characters below 0x20 always escaped; see the note on rawUtf8 in
             * baselib/core/JsonUtils.h
             *
             *
             * ON THE 'canonicalize' PARAMETER - a decision, not an oversight
             *
             * This single flag is forwarded to two different layers, where it means two different
             * things:
             *
             * -- to the data model, via getJsonObject() -> serializeProperties(): emit properties
             *    even when unset, and - less obviously - suppress the required-property check; see
             *    the note above BL_DM_DECLARE_SCALAR_SERIALIZATION in
             *    baselib/data/DataModelObjectDefs.h
             *
             * -- to the serializer, via json::saveToString(): sort object keys, and refuse to also
             *    pretty print
             *
             * Reviews have repeatedly flagged this as a conflated parameter which should be split
             * into something like emitUnsetProperties + sortKeys. The DECISION IS TO KEEP IT AS IT
             * IS, for three reasons:
             *
             * 1) no caller wants the two meanings separated. getObjectHashCanonical() wants both
             *    on; getObjectHash( canonicalize = false ), getDocAsPrettyJsonString() and
             *    getDocAsPackedJsonString() want both off. Splitting would add a knob which nothing
             *    turns
             *
             * 2) "canonical form" - sorted keys AND every property emitted - is a single coherent
             *    concept, so one flag expressing it is reasonable even though the two behaviours are
             *    implemented in different layers
             *
             * 3) no compatible migration exists. A split parameter would occupy the same positional
             *    slot as the current one, so getJsonString( obj, true, true ) would compile under
             *    both spellings and mean different things - producing different bytes and a
             *    different hash. That is exactly the failure mode which the deleted integral
             *    saveToStream() overload in baselib/core/JsonUtils.h was introduced to prevent, and
             *    reintroducing it here to stage a rename would be a regression
             *
             * Performance note for anyone reconsidering the default: canonical serialization costs
             * roughly 5.5x non-canonical on Boost.JSON and roughly 1.0x on json-spirit, because
             * canonicalizeValue() rebuilds the whole value tree while std::map is already ordered -
             * see notes/performance/json-library-performance-comparison.md
             *
             * See also the note on getObjectHash() below, which records the separate and already
             * accepted limitation that these hashes are process local
             */

            template
            <
                typename T
            >
            static auto getJsonString(
                SAA_in              const om::ObjPtr< T >&                          dataObject,
                SAA_in_opt          const bool                                      prettyPrint = false,
                SAA_in_opt          const bool                                      canonicalize = false,
                SAA_in_opt          const bool                                      rawUTF8 = false
                )
                -> std::string
            {
                auto jsonObject = getJsonObject( dataObject, canonicalize );

                return json::saveToString( json::value( std::move( jsonObject ) ), prettyPrint, rawUTF8, canonicalize );
            }

            template
            <
                typename T
            >
            static auto getDocAsPrettyJsonString(
                SAA_in              const om::ObjPtr< T >&                          dataObject,
                SAA_in_opt          const bool                                      rawUTF8 = false
                )
                -> std::string
            {
                return getJsonString( dataObject, true /* prettyPrint */, false /* canonicalize */, rawUTF8 );
            }

            template
            <
                typename T
            >
            static auto getDocAsPackedJsonString(
                SAA_in const        om::ObjPtr< T >&                                dataObject,
                SAA_in_opt          const bool                                      rawUTF8 = false
                )
                -> std::string
            {
                return getJsonString( dataObject, false /* prettyPrint */, false /* canonicalize */, rawUTF8 );
            }

            /**
             * @brief Computes a hash over the serialized form of a data model object
             *
             * Note that canonicalize defaults to false, in which case the hash is taken over the
             * serialization in property declaration and insertion order rather than over a
             * normalized form; getObjectHashCanonical() below is the form which should be
             * preferred and it is the only one used inside this library
             *
             * The canonical form is a project specific stable ordering and is NOT RFC 8785 /
             * JCS - see the comment on canonicalizeValue() in
             * baselib/core/detail/BoostJsonImpl.h
             *
             * Neither form is stable across a change of JSON backend for a document which
             * contains non-ASCII text or numbers whose shortest representation differs between
             * the two serializers, so a hash produced here must not be persisted, used as a
             * cache key across processes built differently, or fed into a signature which
             * another build has to reproduce, unless the backend is pinned. This is a known and
             * accepted limitation - see notes/plans/issues/medium-severity-findings-f11-f17-plan.md
             * (F-11) - and it is not re-litigated by review findings against this file
             */

            template
            <
                typename T
            >
            static auto getObjectHash(
                SAA_in              const om::ObjPtr< T >&                          dataObject,
                SAA_in_opt          const std::string&                              salt = str::empty(),
                SAA_in_opt          const bool                                      canonicalize = false
                )
                -> std::string
            {
                const auto serializedProperties =
                    getJsonString< T >( dataObject, false /* prettyPrint */, canonicalize );

                /*
                 * The salt can be a security domain id or some other tag / global identification
                 * which we want to be included in the hash (if such is provided)
                 */

                hash::HashCalculatorDefault hasher;

                if( ! salt.empty() )
                {
                    hasher.update( salt.c_str(), salt.size() );
                }

                hasher.update( serializedProperties.c_str(), serializedProperties.size() );

                hasher.finalize();

                return hasher.digestStr();
            }

            template
            <
                typename T
            >
            static auto getObjectHashCanonical(
                SAA_in              const om::ObjPtr< T >&                          dataObject,
                SAA_in_opt          const std::string&                              salt = str::empty()
                )
                -> std::string
            {
                return getObjectHash< T >( dataObject, salt, true /* canonicalize */ );
            }

            /*************************************************************************************************
             * Deserialize helpers
             */

            template
            <
                typename T
            >
            static auto loadFromJsonObject( SAA_in json::object&& jsonObject ) -> om::ObjPtr< T >
            {
                SerializationContextBase context( std::move( jsonObject ) );

                auto dataObject = T::template createInstance<>();

                dataObject -> serializeProperties( context );

                return dataObject;
            }

            template
            <
                typename T
            >
            static auto loadFromJsonObject( SAA_in const json::object& jsonObject ) -> om::ObjPtr< T >
            {
                return loadFromJsonObject< T >( cpp::copy( jsonObject ) );
            }

            template
            <
                typename T
            >
            static auto loadFromJsonValue( SAA_in const json::value& jsonValue ) -> om::ObjPtr< T >
            {
                return loadFromJsonObject< T >( jsonValue.as_object() );
            }

            template
            <
                typename T
            >
            static auto loadFromJsonText( SAA_in const std::string& jsonText ) -> om::ObjPtr< T >
            {
                auto rootValue = json::readFromString( jsonText );

                return loadFromJsonObject< T >( std::move( rootValue.as_object() ) );
            }

            template
            <
                typename T
            >
            static auto loadFromFile( SAA_in const fs::path& fileName ) -> om::ObjPtr< T >
            {
                return loadFromJsonText< T >( encoding::readTextFile( fileName ) );
            }

            template
            <
                typename T,
                typename U
            >
            static auto castTo( SAA_in const om::ObjPtr< U >& from ) -> om::ObjPtr< T >
            {
                return loadFromJsonObject< T >( getJsonObject< U >( from ) );
            }
        };

        typedef DataModelUtilsT<> DataModelUtils;

    } // dm

} // bl

#endif /* __BL_DATA_DATAMODELOBJECT_H_ */
