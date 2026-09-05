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

#ifndef __BL_COMPILER_H_
#define __BL_COMPILER_H_

#include <baselib/core/Annotations.h>
#include <baselib/core/BaseDefs.h>

#include <utility>
#include <type_traits>
#include <cstring>

/*
 * GCC 15+ warns about overloaded virtual functions being hidden.
 * BL_VARIADIC_CREATE_INSTANCE creates a static template factory method
 * createInstance<Args...>() which intentionally has the same name as
 * virtual createInstance methods in some interfaces (e.g., Factory).
 * These serve different purposes (static factory vs interface method)
 * and the name collision is intentional design, not an error.
 */

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#endif

/**
 * @brief Main function linkage declaration
 *
 * GCC 15+ and Clang 20+ with -Wpedantic warn about 'extern "C"' on main function.
 * While the C++11 standard specifies main should not have explicit linkage,
 * some platforms historically required it. This macro provides backward
 * compatibility while avoiding the warning on modern compilers.
 */

#if (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15) || (defined(__clang__) && __clang_major__ >= 20)
#define BL_MAIN_LINKAGE_DECL
#else
#define BL_MAIN_LINKAGE_DECL extern "C"
#endif

#define BL_VARIADIC_CTOR( className, base_type, visibility ) \
    visibility: \
    template \
    < \
        typename... Args \
    > \
    className( SAA_in Args&&... args ) \
        : \
        base_type( std::forward< Args >( args )... ) \
    { \
    } \
    private: \

#define BL_VARIADIC_CREATE_INSTANCE( T, this_type, visibility ) \
    visibility: \
    template \
    < \
        typename T = this_type, \
        typename... Args \
    > \
    static bl::om::ObjPtr< T > createInstance( SAA_in Args&&... args ) \
    { \
        return bl::om::ObjPtr< T >::attach( \
            new this_type( std::forward< Args >( args )... ) \
            ); \
    } \
    private: \

/**
 * @brief Implements and encapsulates createInstance method using variadics
 */

#define BL_ENCAPSULATE_CREATE_INSTANCE( T, className, base_type, this_type ) \
    BL_VARIADIC_CTOR( className, base_type, protected ) \
    BL_VARIADIC_CREATE_INSTANCE( T, this_type, public ) \

/**
 * @brief Safe memset for non-trivial types (suppresses GCC 15+ warnings)
 *
 * GCC 15+ warns about using memset on non-trivial types. This provides
 * a surgical way to suppress this warning for specific uses where memset
 * is intentional and safe (e.g., zero-initializing uuid_t)
 */

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15

namespace bl
{
    namespace detail
    {
        /**
         * @brief Helper function to perform memset with warning suppression
         */

        inline void safeMemsetImpl( void* ptr, int value, std::size_t size )
        {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
            std::memset( ptr, value, size );
#pragma GCC diagnostic pop
        }
    }
}

#define BL_SAFE_MEMSET( ptr, value, size ) \
    bl::detail::safeMemsetImpl( ptr, value, size )

#else

#define BL_SAFE_MEMSET( ptr, value, size ) \
    std::memset( ptr, value, size )

#endif

#endif /* __BL_COMPILER_H_ */

