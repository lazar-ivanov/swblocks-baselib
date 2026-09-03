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

#ifndef __BL_DATA_DATAMODELOBJECTDEFS_H_
#define __BL_DATA_DATAMODELOBJECTDEFS_H_

#include <baselib/data/DataModelObject.h>

#include <baselib/core/EnumUtils.h>
#include <baselib/core/BaseIncludes.h>

/*
 * JSON accessor macros (BL_JSON_ITER_VALUE, BL_JSON_PAIR_KEY, BL_JSON_PAIR_VALUE)
 * are defined in the implementation headers (BoostJsonImpl.h / JsonSpiritImpl.h)
 * and are included via DataModelObject.h -> JsonUtils.h
 */

#ifndef BL_DM_SERIALIZATION_CONTEXT_IMPL

#define BL_DM_SERIALIZATION_CONTEXT_IMPL \
    bl::dm::SerializationContextBase \

#undef BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_SERIALIZE
#define BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_SERIALIZE( context ) \
    BL_DM_SERIALIZATION_CONTEXT_IMPL context \

#undef BL_DM_SERIALIZATION_CONTEXT_IMPL_INVOKE_SERIALIZE
#define BL_DM_SERIALIZATION_CONTEXT_IMPL_INVOKE_SERIALIZE( context, canonicalize ) \
    serializeProperties( context, canonicalize ) \

#undef BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_DESERIALIZE
#define BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_DESERIALIZE( context, obj ) \
    BL_DM_SERIALIZATION_CONTEXT_IMPL context( obj ) \

#undef BL_DM_SERIALIZATION_CONTEXT_IMPL_DESERIALIZE_SETNAME
#define BL_DM_SERIALIZATION_CONTEXT_IMPL_DESERIALIZE_SETNAME( obj, value ) \
    do { } while( false )

#endif // BL_DM_SERIALIZATION_CONTEXT_IMPL

#define BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET( name, operation ) \
    this_type::throwRequiredPropertyNotSet( #name, operation ); \

#define BL_DM_DEFINE_CHECK_READ_ONLY() \
    do \
    { \
        if( bl::dm::DataModelObject::readOnly() ) \
        { \
            this_type::readOnlyPropertyUpdateViolation(); \
        } \
    } \
    while( false );

#define BL_DM_DEFINE_CLASS_BEGIN_IMPL_INTERNAL( className, baseClass, isClassPartial ) \
template \
    < \
        typename E = void \
    > \
    class className ## T : public baseClass \
    { \
        BL_NO_POLYMORPHIC_BASE( className ## T ) \
        BL_NO_CREATE( className ## T ) \
        public: \
        typedef className ## T< E > this_type; \
        \
        static bool isPartial() NOEXCEPT \
        { \
            return isClassPartial; \
        } \
        private: \

#define BL_DM_DEFINE_CLASS_BEGIN_IMPL( className, baseClass ) \
    BL_DM_DEFINE_CLASS_BEGIN_IMPL_INTERNAL( className, baseClass, false /* isClassPartial */ )

#define BL_DM_DEFINE_CLASS_BEGIN_IMPL_PARTIAL( className, baseClass ) \
    BL_DM_DEFINE_CLASS_BEGIN_IMPL_INTERNAL( className, baseClass, true /* isClassPartial */ )

#define BL_DM_DEFINE_CLASS_BEGIN_BASE( className ) \
    BL_DM_DEFINE_CLASS_BEGIN_IMPL( className, bl::dm::DataModelObject )

#define BL_DM_DEFINE_CLASS_BEGIN( className ) \
    BL_DM_DEFINE_CLASS_BEGIN_BASE( className )

#define BL_DM_DEFINE_CLASS_END( className ) \
    }; \
    typedef bl::om::ObjectImpl< className ## T<> > className; \

/*
 * BL_DM_DECLARE_* and BL_DM_DEFINE_PROPERTY macros (for the serialization code)
 */

#define BL_DM_DEFINE_PROPERTY( className, propertyName ) \
    BL_DEFINE_STATIC_CONST_STRING_REF_INIT( className ## T, propertyName ## String ) \

#define BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \
    static const std::string& g_ ## name ## String; \
    static const std::string& name ## StringInit() \
    { \
        static const std::string g_## name ## StringImpl( #name ); \
        return g_## name ## StringImpl; \
    } \
    public: \
    static const std::string& name ## ToString() NOEXCEPT \
    { \
        return g_ ## name ## String;\
    } \
    private: \

#define BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, type ) \
    private: \
    bl::cpp::ScalarTypeIniter< type > m_ ## name; \
    bl::cpp::ScalarTypeIniter< bool > m_ ## name ## IsSet; \
    public: \
    type name() const NOEXCEPT { return m_ ## name; } \
    void name( SAA_in const type value ) \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        m_ ## name = value; \
        m_ ## name ## IsSet = true; \
    } \
    bool name ## IsSet() const NOEXCEPT { return m_ ## name ## IsSet; } \
    void name ## Reset() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        m_ ## name = type(); \
        m_ ## name ## IsSet = false; \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

#define BL_DM_DECLARE_PROPERTY_STRING_RO_IMPL( name ) \
    private: \
    std::string m_ ## name; \
    public: \
    const std::string& name() const NOEXCEPT { return m_ ## name; } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

#define BL_DM_DECLARE_PROPERTY_STRING_IMPL( name ) \
    BL_DM_DECLARE_PROPERTY_STRING_RO_IMPL( name ) \
    public: \
    std::string& name ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## name; \
    } \
    void name( SAA_in const std::string& value ) \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        m_ ## name = value; \
    } \
    \
    void name( SAA_in std::string&& value ) \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        value.swap( m_ ## name ); \
    } \
    private: \

/*
 * The 'canonicalize' parameter carried by every *Serialize below has TWO effects at this layer,
 * and the second one is not obvious from its name:
 *
 * 1) properties are emitted even when they were never set, so the output shape does not depend on
 *    which setters a caller happened to call
 *
 * 2) the required-property check is SUPPRESSED. Note that the emit branch is tested first, so when
 *    canonicalize is true the 'else if( isRequired && ! IsSet )' branch below is unreachable and
 *    BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET never fires
 *
 * Effect 2 means canonical serialization accepts an object which packed serialization would
 * reject, and therefore that a canonical hash can be computed over an object which is not valid.
 * That is a real consequence and it is why this is written down rather than left to be rediscovered
 *
 * This is the DECIDED behavior and it is deliberately not being changed - see the note on
 * getJsonString() in baselib/data/DataModelObject.h for the reasoning and for why splitting the
 * parameter was considered and rejected. Do not re-file it
 */

#define BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, scalar_type, isRequired ) \
    private: \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( canonicalize || m_ ## name ## IsSet ) \
        { \
            object.emplace( jsonProp, name() ); \
        } \
        else if( isRequired && ! m_ ## name ## IsSet ) \
        { \
            BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET( name, "saving" ) \
        } \
    } \
    void name ## Deserialize( \
        SAA_in          const bl::json::object&                         map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        const auto pos = map.find( jsonProp ); \
        \
        if( pos != map.end() && ! BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            const auto& jsonIterValue = BL_JSON_ITER_VALUE( pos ); \
            BL_UNUSED( jsonIterValue ); \
            m_ ## name ## IsSet = true; \
            m_ ## name = bl::json::value_to< scalar_type >( jsonIterValue ); \
            \
            context.addProcessedProperty( jsonProp ); \
        } \
        else \
        { \
            if( isRequired ) \
            { \
                BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET( name, "loading" ) \
            } \
            \
            m_ ## name ## IsSet = false; \
        } \
    } \

/*
 * BL_DM_DECLARE_BOOL_* macros
 */

#define BL_DM_DECLARE_BOOL_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, bool ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, bool, false /* isRequired */ ) \

#define BL_DM_DECLARE_BOOL_REQUIRED_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, bool ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, bool, true /* isRequired */ ) \

#define BL_DM_DECLARE_BOOL_ALTERNATE_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, bool ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, bool, false /* isRequired */ ) \

#define BL_DM_DECLARE_BOOL_ALTERNATE_REQUIRED_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, bool ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, bool, true /* isRequired */ ) \

/*
 * BL_DM_DECLARE_INT_* macros
 */

#define BL_DM_DECLARE_INT_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, int ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, int, false /* isRequired */ ) \

#define BL_DM_DECLARE_INT_REQUIRED_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, int ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, int, true /* isRequired */ ) \

#define BL_DM_DECLARE_INT_ALTERNATE_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, int ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, int, false /* isRequired */ ) \

#define BL_DM_DECLARE_INT_ALTERNATE_REQUIRED_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, int ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, int, true /* isRequired */ ) \

/*
 * BL_DM_DECLARE_UINT64_* macros
 */

#define BL_DM_DECLARE_UINT64_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::uint64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, std::uint64_t, false /* isRequired */ ) \

#define BL_DM_DECLARE_UINT64_REQUIRED_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::uint64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, std::uint64_t, true /* isRequired */ ) \

#define BL_DM_DECLARE_UINT64_ALTERNATE_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::uint64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, std::uint64_t, false /* isRequired */ ) \

#define BL_DM_DECLARE_UINT64_ALTERNATE_REQUIRED_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::uint64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, std::uint64_t, true /* isRequired */ ) \

/*
 * BL_DM_DECLARE_INT64_* macros
 */

#define BL_DM_DECLARE_INT64_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::int64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, std::int64_t, false /* isRequired */ ) \

#define BL_DM_DECLARE_INT64_REQUIRED_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::int64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, std::int64_t, true /* isRequired */ ) \

#define BL_DM_DECLARE_INT64_ALTERNATE_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::int64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, std::int64_t, false /* isRequired */ ) \

#define BL_DM_DECLARE_INT64_ALTERNATE_REQUIRED_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, std::int64_t ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, std::int64_t, true /* isRequired */ ) \

/*
 * BL_DM_DECLARE_DOUBLE_* macros
 */

#define BL_DM_DECLARE_DOUBLE_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, double ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, double, false /* isRequired */ ) \

#define BL_DM_DECLARE_DOUBLE_REQUIRED_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, double ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, #name, double, true /* isRequired */ ) \

#define BL_DM_DECLARE_DOUBLE_ALTERNATE_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, double ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, double, false /* isRequired */ ) \

#define BL_DM_DECLARE_DOUBLE_ALTERNATE_REQUIRED_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_PROPERTY_SCALAR_IMPL( name, double ) \
    BL_DM_DECLARE_SCALAR_SERIALIZATION( name, jsonProp, double, true /* isRequired */ ) \

/*
 * BL_DM_DECLARE_STRING_* macros
 */

#define BL_DM_DECLARE_STRING_PROPERTY_SERIALIZE( name, jsonProp, isRequired ) \
    private: \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( canonicalize || ( ! name().empty() ) ) \
        { \
            object.emplace( jsonProp, name() ); \
        } \
        else if( isRequired ) \
        { \
            BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET( name, "saving" ) \
        } \
        \
    } \

#define BL_DM_DECLARE_STRING_PROPERTY_DESERIALIZE( name, jsonProp, isRequired ) \
    private: \
    void name ## Deserialize( \
        SAA_in          const bl::json::object&                         map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        const auto pos = map.find( jsonProp ); \
        \
        if( pos != map.end() && ! BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            m_ ## name = bl::json::value_to< std::string >( BL_JSON_ITER_VALUE( pos ) ); \
            \
            context.addProcessedProperty( jsonProp ); \
        } \
        \
        if( isRequired && name().empty() ) \
        { \
            BL_DM_THROW_REQUIRED_PROPERTY_NOT_SET( name, "loading" ) \
        } \
    } \

#define BL_DM_DECLARE_STRING_PROPERTY_RO( name ) \
    BL_DM_DECLARE_PROPERTY_STRING_RO_IMPL( name ) \
    BL_DM_DECLARE_STRING_PROPERTY_DESERIALIZE( name, #name, false /* isRequired */ )

#define BL_DM_DECLARE_STRING_REQUIRED_PROPERTY_RO( name ) \
    BL_DM_DECLARE_PROPERTY_STRING_RO_IMPL( name ) \
    BL_DM_DECLARE_STRING_PROPERTY_DESERIALIZE( name, #name, true /* isRequired */ )

#define BL_DM_DECLARE_STRING_REQUIRED_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_STRING_IMPL( name ) \
    BL_DM_DECLARE_STRING_PROPERTY_SERIALIZE( name, #name, true /* isRequired */ ) \
    BL_DM_DECLARE_STRING_PROPERTY_DESERIALIZE( name, #name, true /* isRequired */ ) \

#define BL_DM_DECLARE_STRING_PROPERTY( name ) \
    BL_DM_DECLARE_PROPERTY_STRING_IMPL( name ) \
    BL_DM_DECLARE_STRING_PROPERTY_SERIALIZE( name, #name, false /* isRequired */ ) \
    BL_DM_DECLARE_STRING_PROPERTY_DESERIALIZE( name, #name, false /* isRequired */ ) \

#define BL_DM_DECLARE_STRING_ALTERNATE_PROPERTY_IMPL( name, jsonProp, isRequired ) \
    BL_DM_DECLARE_PROPERTY_STRING_IMPL( name ) \
    BL_DM_DECLARE_STRING_PROPERTY_SERIALIZE( name, jsonProp, isRequired ) \
    BL_DM_DECLARE_STRING_PROPERTY_DESERIALIZE( name, jsonProp, isRequired ) \

#define BL_DM_DECLARE_STRING_ALTERNATE_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_STRING_ALTERNATE_PROPERTY_IMPL( name, jsonProp, false /* isRequired */ ) \

#define BL_DM_DECLARE_STRING_ALTERNATE_REQUIRED_PROPERTY( name, jsonProp ) \
    BL_DM_DECLARE_STRING_ALTERNATE_PROPERTY_IMPL( name, jsonProp, true /* isRequired */ ) \


#define BL_DM_DECLARE_SIMPLE_CONTAINER_PROPERTY( name, jsonProp, item_type, jsonGetter, containerType, inserter ) \
    private: \
    containerType < item_type > m_ ## name; \
    \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( false == canonicalize && m_ ## name.size() == 0 ) \
        { \
            return; \
        } \
        \
        bl::json::array items; \
        \
        for( const auto& item : m_ ## name ) \
        { \
            items.push_back( bl::json::value( item ) ); \
        } \
        \
        object.emplace( #jsonProp, std::move( items ) ); \
    } \
    void name ## Deserialize( \
        SAA_in          const bl::json::object&                         map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        const auto pos = map.find( #jsonProp ); \
        \
        if( pos == map.end() || BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            return; \
        } \
        \
        const auto& arr = BL_JSON_ITER_VALUE( pos ).as_array(); \
        \
        containerType < item_type > temp; \
        \
        for( const auto& item : arr ) \
        { \
            if( ! std::is_same< item_type, std::string >::value && \
                item.is_string() ) \
            { \
                auto itemValue = bl::json::value_to< std::string >( item ); \
                temp.inserter( bl::utils::lexical_cast< item_type >( itemValue ) ); \
            } \
            else \
            { \
                temp.inserter( bl::json::value_to< item_type >( item ) ); \
            } \
        } \
        \
        m_ ##name .swap( temp ); \
        \
        context.addProcessedProperty( #jsonProp ); \
    } \
    \
    public: \
    const containerType < item_type >& name() const NOEXCEPT \
    { \
        return m_ ## name; \
    } \
    containerType < item_type >& name ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## name; \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

#define BL_DM_DECLARE_SIMPLE_VECTOR_PROPERTY( name, type, jsonGetter ) \
    BL_DM_DECLARE_SIMPLE_CONTAINER_PROPERTY( name, name, type, jsonGetter, std::vector, push_back ) \

#define BL_DM_DECLARE_SIMPLE_VECTOR_ALTERNATE_PROPERTY( name, jsonProp, type, jsonGetter ) \
    BL_DM_DECLARE_SIMPLE_CONTAINER_PROPERTY( name, jsonProp, type, jsonGetter, std::vector, push_back  ) \

/*
 * Note: std::set must be used to ensure the property values are always serialized
 * in canonical / stable order
 *
 * This is needed for the object hashing to work
 */

#define BL_DM_DECLARE_SIMPLE_SET_PROPERTY( name, type, jsonGetter ) \
    BL_DM_DECLARE_SIMPLE_CONTAINER_PROPERTY( name, name, type, jsonGetter, std::set, insert ) \

#define BL_DM_DECLARE_CUSTOM_PROPERTY( name ) \
    private: \
    bl::json::value m_ ## name; \
    \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( canonicalize || ! m_ ## name.is_null() ) \
        { \
            object.emplace( #name, m_ ## name ); \
        } \
    } \
    void name ## Deserialize( \
        SAA_in          bl::json::object&                               map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        auto pos = map.find( #name ); \
        \
        if( pos == map.end() || BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            return; \
        } \
        \
        m_ ## name = std::move( BL_JSON_ITER_VALUE( pos ) ); \
        \
        context.addProcessedProperty( #name ); \
    } \
    \
    public: \
    const bl::json::value& name() const NOEXCEPT \
    { \
        return m_ ## name; \
    } \
    bl::json::value& name ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## name; \
    } \
    void name( SAA_inout bl::json::value&& value ) \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        m_ ## name = BL_PARAM_FWD( value ); \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

#define BL_DM_DECLARE_COMPLEX_PROPERTY( name, type ) \
    BL_DM_DECLARE_COMPLEX_ALTERNATE_PROPERTY( name, name, type ) \

#define BL_DM_DECLARE_COMPLEX_ALTERNATE_PROPERTY( name, jsonProp, type ) \
    BL_DM_DECLARE_COMPLEX_ALTERNATE_PROPERTY_IMPL( \
        name, \
        jsonProp, \
        type, \
        BL_DM_SERIALIZATION_CONTEXT_IMPL_INVOKE_SERIALIZE( tempContext, canonicalize ) \
        ) \

#define BL_DM_DECLARE_COMPLEX_ALTERNATE_PROPERTY_IMPL( name, jsonProp, type, invokeSerialize ) \
    private: \
    bl::om::ObjPtr< type > m_ ## name; \
    \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        \
        if( canonicalize || m_ ## name ) \
        { \
            BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_SERIALIZE( tempContext ); \
            if( m_ ## name ) \
            { \
                m_ ## name -> invokeSerialize; \
            } \
            \
            object.emplace( #jsonProp, std::move( tempContext.serializationDoc() ) ); \
        } \
    } \
    void name ## Deserialize( \
        SAA_in          bl::json::object&                               map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        auto pos = map.find( #jsonProp ); \
        \
        if( pos == map.end() || BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            return; \
        } \
        \
        auto ptr = type::createInstance(); \
        \
        BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_DESERIALIZE( tempContext, std::move( BL_JSON_ITER_VALUE( pos ).as_object() ) ); \
        \
        tempContext.detectUnknownProperties( context.detectUnknownProperties() ); \
        \
        ptr -> serializeProperties( tempContext ); \
        \
        m_ ##name .swap( ptr ); \
        \
        context.addProcessedProperty( #jsonProp ); \
    } \
    \
    public: \
    const bl::om::ObjPtr< type >& name() const NOEXCEPT \
    { \
        return m_ ## name; \
    } \
    void name( SAA_in const bl::om::ObjPtr< type >& value ) \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        m_ ## name = bl::om::copy( value ); \
    } \
    void name( SAA_in bl::om::ObjPtr< type >&& value ) \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        m_ ## name = BL_PARAM_FWD( value ); \
    } \
    bl::om::ObjPtr< type >& name ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## name; \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

#define BL_DM_DECLARE_COMPLEX_VECTOR_PROPERTY( name, type ) \
    BL_DM_DECLARE_COMPLEX_VECTOR_ALTERNATE_PROPERTY( name, name, type ) \

#define BL_DM_DECLARE_COMPLEX_VECTOR_ALTERNATE_PROPERTY( name, jsonProp, type ) \
    private: \
    std::vector< bl::om::ObjPtr< type > > m_ ## name; \
    \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( false == canonicalize && m_ ## name.size() == 0 ) \
        { \
            return; \
        } \
        \
        bl::json::array items; \
        \
        for( const auto& item : m_ ## name ) \
        { \
            BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_SERIALIZE( tempContext ); \
            item -> serializeProperties( tempContext, canonicalize ); \
            items.push_back( bl::json::value( std::move( tempContext.serializationDoc() ) ) ); \
        } \
        \
        object.emplace( #jsonProp, std::move( items ) ); \
    } \
    void name ## Deserialize( \
        SAA_in          bl::json::object&                               map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        const auto pos = map.find( #jsonProp ); \
        \
        if( pos == map.end() || BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            return; \
        } \
        \
        auto& items = BL_JSON_ITER_VALUE( pos ).as_array(); \
        \
        std::vector< bl::om::ObjPtr< type > > temp; \
        \
        for( auto& item : items ) \
        { \
            BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_DESERIALIZE( tempContext, std::move( item.as_object() ) ); \
            \
            tempContext.detectUnknownProperties( context.detectUnknownProperties() ); \
            \
            auto obj = type::createInstance(); \
            obj -> serializeProperties( tempContext ); \
            temp.push_back( std::move( obj ) ); \
        } \
        \
        m_ ## name.swap( temp ); \
        \
        context.addProcessedProperty( #jsonProp ); \
    } \
    \
    public: \
    const std::vector< bl::om::ObjPtr< type > >& name() const NOEXCEPT \
    { \
        return m_ ## name; \
    } \
    std::vector< bl::om::ObjPtr< type > >& name ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## name; \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

/*
 * `nameArg` to avoid collision with `name` setter inside
 */
#define BL_DM_DECLARE_COMPLEX_MAP_PROPERTY( nameArg, type ) \
    private: \
    std::map< std::string, bl::om::ObjPtr< type > > m_ ## nameArg; \
    \
    void nameArg ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( false == canonicalize && m_ ## nameArg .size() == 0 ) \
        { \
            return; \
        } \
        \
        bl::json::object items; \
        \
        for( const auto& pair : m_ ## nameArg ) \
        { \
            BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_SERIALIZE( tempContext ); \
            pair.second -> serializeProperties( tempContext, canonicalize ); \
            items.emplace( pair.first, std::move( tempContext.serializationDoc() ) ); \
        } \
        \
        object.emplace( #nameArg, std::move( items ) ); \
    } \
    void nameArg ## Deserialize( \
        SAA_in          bl::json::object&                               map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        const auto pos = map.find( #nameArg ); \
        \
        if( pos == map.end() ) \
        { \
            return; \
        } \
        \
        auto& items = BL_JSON_ITER_VALUE( pos ).as_object(); \
        \
        std::map< std::string, bl::om::ObjPtr< type > > temp; \
        \
        for( auto& pair : items ) \
        { \
            BL_DM_SERIALIZATION_CONTEXT_IMPL_DECL_DESERIALIZE( tempContext, std::move( BL_JSON_PAIR_VALUE( pair ).as_object() ) ); \
            \
            tempContext.detectUnknownProperties( context.detectUnknownProperties() ); \
            \
            auto obj = type::createInstance(); \
            obj -> serializeProperties( tempContext ); \
            BL_DM_SERIALIZATION_CONTEXT_IMPL_DESERIALIZE_SETNAME( obj, std::string( BL_JSON_PAIR_KEY( pair ) ) ); \
            temp.emplace( std::string( BL_JSON_PAIR_KEY( pair ) ), std::move( obj ) ); \
        } \
        \
        m_ ##nameArg .swap( temp ); \
        \
        context.addProcessedProperty( #nameArg ); \
    } \
    \
    public: \
    const std::map< std::string, bl::om::ObjPtr< type > >& nameArg() const NOEXCEPT \
    { \
        return m_ ## nameArg; \
    } \
    std::map< std::string, bl::om::ObjPtr< type > >& nameArg ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## nameArg; \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( nameArg ) \
    private: \

#define BL_DM_DECLARE_MAP_PROPERTY( name, type ) \
    private: \
    std::map< std::string, type > m_ ## name; \
    \
    void name ## Serialize( \
        SAA_out         bl::json::object&                               object, \
        SAA_in          const bool                                      canonicalize \
        ) \
    { \
        if( false == canonicalize && m_ ## name .size() == 0 ) \
        { \
            return; \
        } \
        \
        bl::json::object items; \
        \
        for( const auto& pair : m_ ## name ) \
        { \
            items.emplace( pair.first, pair.second ); \
        } \
        \
        object.emplace( #name, std::move( items ) ); \
    } \
    void name ## Deserialize( \
        SAA_in          const bl::json::object&                         map, \
        SAA_inout       BL_DM_SERIALIZATION_CONTEXT_IMPL&               context \
        ) \
    { \
        const auto pos = map.find( #name ); \
        \
        if( pos == map.end() || BL_JSON_ITER_VALUE( pos ).is_null() ) \
        { \
            return; \
        } \
        \
        const auto& obj = BL_JSON_ITER_VALUE( pos ).as_object(); \
        \
        std::map< std::string, type > temp; \
        \
        for( const auto& pair : obj ) \
        { \
            temp[ std::string( BL_JSON_PAIR_KEY( pair ) ) ] = bl::json::value_to< type >( BL_JSON_PAIR_VALUE( pair ) ); \
        } \
        \
        m_ ## name .swap( temp ); \
        \
        context.addProcessedProperty( #name ); \
    }\
    \
    public: \
    const std::map< std::string, type >& name() const NOEXCEPT \
    { \
        return m_ ## name; \
    } \
    std::map< std::string, type >& name ## Lvalue() \
    { \
        BL_DM_DEFINE_CHECK_READ_ONLY(); \
        return m_ ## name; \
    } \
    BL_DM_DECLARE_PROPERTY_TO_STRING( name ) \
    private: \

/*
 * BL_DM_PROPERTIES_IMPL_* macros (for serializeProperties method)
 */

#define BL_DM_PROPERTIES_IMPL_BEGIN() \
    public: \
    virtual void serializeProperties( \
        SAA_inout   BL_DM_SERIALIZATION_CONTEXT_IMPL&           context, \
        SAA_in_opt  const bool                                  canonicalize = false \
        ) \
    { \
        BL_UNUSED( canonicalize ); \

#define BL_DM_IMPL_PROPERTY( name ) \
    if( context.isSerialization() ) \
    { \
        name ## Serialize( context.serializationDoc(), canonicalize ); \
    } \
    else \
    { \
        try \
        { \
            name ## Deserialize( context.deserializationDoc(), context ); \
        } \
        catch( std::runtime_error& e ) \
        { \
            bl::json::remapIncorrectValueTypeException( \
                e, \
                std::current_exception(), \
                ( BL_MSG() \
                    <<"property '" \
                    << #name \
                    << "'" \
                ).text() \
                ); \
        } \
    } \

#define BL_DM_PROPERTIES_IMPL_HANDLE_UNMAPPED() \
        if( ! this_type::isPartial() ) \
        { \
            if( context.isSerialization() ) \
            { \
                auto& doc = context.serializationDoc(); \
                \
                for( const auto& pair : m_unmapped ) \
                { \
                    if( bl::cpp::contains( doc, std::string( BL_JSON_PAIR_KEY( pair ) ) ) ) \
                    { \
                        BL_LOG( \
                            bl::Logging::debug(), \
                            BL_MSG() \
                                << "Unmapped property '" \
                                << std::string( BL_JSON_PAIR_KEY( pair ) ) \
                                << "' also in document" \
                            ); \
                        \
                        BL_ASSERT( true ); \
                    } \
                    \
                    doc.emplace( std::string( BL_JSON_PAIR_KEY( pair ) ), BL_JSON_PAIR_VALUE( pair ) ); \
                } \
            } \
            \
            if( ! context.isSerialization() ) \
            { \
                m_unmapped.clear(); \
                \
                for( const auto& pair : context.deserializationDoc() ) \
                { \
                    if( ! context.containsProcessedProperty( std::string( BL_JSON_PAIR_KEY( pair ) ) ) ) \
                    { \
                        m_unmapped.emplace( std::string( BL_JSON_PAIR_KEY( pair ) ), BL_JSON_PAIR_VALUE( pair ) ); \
                    } \
                } \
            } \
        } \

#define BL_DM_PROPERTIES_IMPL_END() \
        BL_DM_PROPERTIES_IMPL_HANDLE_UNMAPPED() \
    } \
    \
private: \


#define BL_DM_GET_AS_PRETTY_JSON_STRING( dataObject ) \
    bl::dm::DataModelUtils::getDocAsPrettyJsonString( dataObject )

#define BL_DM_LOAD_FROM_JSON_STRING( T, jsonText ) \
    bl::dm::DataModelUtils::loadFromJsonText< T >( jsonText )

/***********************************************************************************************
 * Some data model objects types that can be used in generic context
 */

namespace bl
{
    namespace dm
    {
        /**
         * @brief A placeholder object that can be used to represent a polymorphic data model
         * base object later to be converted to another concrete type (usually via
         * DataModelUtils::castTo< ... >( ... )
         */

        BL_DM_DEFINE_CLASS_BEGIN( Payload )
            BL_DM_PROPERTIES_IMPL_BEGIN()
            BL_DM_PROPERTIES_IMPL_END()
        BL_DM_DEFINE_CLASS_END( Payload )

    } // dm

} // bl

#endif /* __BL_DATA_DATAMODELOBJECTDEFS_H_ */
