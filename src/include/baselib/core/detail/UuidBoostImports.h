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

#ifndef __BL_UUIDBOOSTIMPORTS_H_
#define __BL_UUIDBOOSTIMPORTS_H_

#include <baselib/core/detail/BoostIncludeGuardPush.h>
#define BOOST_UUID_RANDOM_GENERATOR_COMPAT
/*
 * boost/version.hpp must be included before the first BOOST_VERSION guard below;
 * otherwise BOOST_VERSION expands to 0 for any translation unit which hasn't already
 * pulled in a Boost header, which silently selects the wrong compatibility branch
 */
#include <boost/version.hpp>
/*
 * Boost 1.89+ added 8-byte alignment to boost::uuids::uuid which breaks
 * binary protocol compatibility. Disable alignment to maintain compatibility.
 */
#if BOOST_VERSION >= 108900
#define BOOST_UUID_DISABLE_ALIGNMENT
#endif
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/functional/hash.hpp>
/*
 * Boost 1.81+ moved std::hash<uuid> to a separate header (uuid_hash.hpp)
 * Include it for versions 1.81 through 1.88
 */
#if BOOST_VERSION >= 108100 && BOOST_VERSION < 108900
#include <boost/uuid/uuid_hash.hpp>
#endif
/* Include mt19937 for Boost 1.89+ random_generator compatibility */
#if BOOST_VERSION >= 108900
#include <boost/random/mersenne_twister.hpp>
#endif
#include <baselib/core/detail/BoostIncludeGuardPop.h>

#if defined(_WIN32)

/*
 * uuid_t macro pulled from rpcdce.h shouldn't be defined at this point to avoid conflict with bl::uuid_t
 * Report an error in case we didn't #undef it
 */

#if defined( uuid_t )
#error "uuid_t shouldn't be defined as macro when compiling UuidBoostImports.h file"
#endif // defined( uuid_t )

#endif // defined(_WIN32)

namespace bl
{
    namespace uuids
    {
        using boost::uuids::uuid;

        /*
         * Boost 1.89+ changed random_generator API
         * In older versions, random_generator( urng ) accepted a custom URNG
         * In 1.89+, random_generator uses ChaCha20 and doesn't accept URNG
         * For 1.89+, we need to use basic_random_generator with boost::mt19937
         * (not std::mt19937) since the codebase uses boost::mt19937
         */
#if BOOST_VERSION >= 108900
        /* Use boost::mt19937 (not std::mt19937) to match existing code */
        using random_generator = boost::uuids::basic_random_generator<boost::mt19937>;
#else
        using boost::uuids::random_generator;
#endif

        using boost::uuids::nil_uuid;

    } // uuids

    typedef uuids::uuid uuid_t;

} // bl

/*
 * Boost 1.81+ provides std::hash<boost::uuids::uuid> natively
 * Only define our own for older versions
 */
#if BOOST_VERSION < 108100

namespace std
{
    /*
     * Provide specialization of hash function in the std namespace
     * for uuid_t. This will allow us to use uuid_t as key in the
     * new C++11 std containers (e.g. std::unordered_map)
     */

    template
    <
    >
    struct hash< bl::uuid_t >
    {
        std::size_t operator()( const bl::uuid_t& uuid ) const
        {
            boost::hash< bl::uuid_t > hasher;
            return hasher( uuid );
        }
    };

} // std

#endif // BOOST_VERSION < 108100

#endif /* __BL_UUIDBOOSTIMPORTS_H_ */
