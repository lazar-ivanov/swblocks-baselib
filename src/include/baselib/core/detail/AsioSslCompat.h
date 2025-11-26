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

#ifndef __BL_ASIOSSLCOMPAT_H_
#define __BL_ASIOSSLCOMPAT_H_

#include <boost/version.hpp>

/*
 * Boost.Asio SSL Compatibility Layer for Boost 1.89+
 *
 * This file provides compatibility for Boost.Asio SSL API changes.
 * This must be included AFTER boost/asio/ssl.hpp has been included.
 */

#if BOOST_VERSION >= 108900

namespace boost {
namespace asio {
namespace ssl {

/*
 * rfc2818_verification compatibility typedef
 *
 * Boost 1.89+ removed rfc2818_verification and replaced it with host_name_verification.
 * This typedef provides backward compatibility.
 */
typedef host_name_verification rfc2818_verification;

} // namespace ssl
} // namespace asio
} // namespace boost

#endif // BOOST_VERSION >= 108900

#endif /* __BL_ASIOSSLCOMPAT_H_ */
