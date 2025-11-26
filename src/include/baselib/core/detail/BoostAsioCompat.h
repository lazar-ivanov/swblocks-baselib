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

#ifndef __BL_BOOSTASIOCOMPAT_H_
#define __BL_BOOSTASIOCOMPAT_H_

#include <boost/version.hpp>

/*
 * Boost.Asio Compatibility Layer for Boost 1.89+
 *
 * This file provides compatibility wrappers for Boost.Asio API changes
 * introduced in Boost 1.89 and later versions. The goal is to maintain
 * backward compatibility with existing code while using newer Boost versions.
 *
 * Changes addressed:
 * - Boost 1.89+ removed nested typedefs from basic_resolver<Protocol>
 * - Boost 1.89+ changed resolver API to use string-based resolve() methods
 * - Boost 1.89+ removed io_service::work class
 * - Boost 1.89+ removed ssl::rfc2818_verification
 * - Boost 1.89+ changed io_context::post() API
 * - Future compatibility fixes should be added here
 */

#if BOOST_VERSION >= 108900

namespace boost {
namespace asio {

/*
 * io_service::work compatibility wrapper
 *
 * Boost 1.89+ removed io_service::work and replaced it with executor_work_guard.
 * This wrapper provides the old work interface using the new executor_work_guard.
 */
class io_service_work_compat
{
public:
    explicit io_service_work_compat(io_context& io_ctx)
        : m_work_guard(io_ctx.get_executor())
    {
    }

    io_service_work_compat(const io_service_work_compat&) = delete;
    io_service_work_compat& operator=(const io_service_work_compat&) = delete;

    ~io_service_work_compat()
    {
        /* Work guard is automatically released on destruction */
    }

private:
    executor_work_guard<io_context::executor_type> m_work_guard;
};

/*
 * io_context compatibility extensions
 *
 * Boost 1.89+ removed io_context::post() member function and io_context::work.
 * We extend io_context to add them back for compatibility.
 *
 * In Boost 1.72+, io_service is just a typedef to io_context, so by adding
 * the work typedef to io_context, it becomes available via io_service::work.
 */
class io_context_compat : public io_context
{
public:
    using io_context::io_context;

    /* Nested work typedef for compatibility - makes io_service::work available */
    typedef io_service_work_compat work;

    /* Restore post() member function */
    template <typename CompletionHandler>
    void post(CompletionHandler&& handler)
    {
        boost::asio::post(*this, std::forward<CompletionHandler>(handler));
    }
};

/*
 * Make io_service point to our compatibility-enhanced io_context
 * This is needed because the original io_service typedef doesn't have nested work
 */
typedef io_context_compat io_service;


namespace ip {

/*
 * TCP/UDP/ICMP resolver compatibility wrapper
 *
 * This template class wraps basic_resolver<Protocol> and restores:
 * 1. Nested typedefs (query, iterator, results_type, endpoint_type)
 * 2. Query-based resolve() and async_resolve() methods
 *
 * The wrapper maps the old API to the new string-based API internally.
 */
template <typename Protocol>
class basic_resolver_compat : public basic_resolver<Protocol>
{
public:
    typedef basic_resolver<Protocol> base_type;

    /* Nested typedefs removed in Boost 1.89 */
    typedef basic_resolver_query<Protocol> query;
    typedef basic_resolver_results<Protocol> results_type;
    typedef typename results_type::iterator iterator;
    typedef typename Protocol::endpoint endpoint_type;

    /* Inherit all constructors */
    using base_type::base_type;

    /*
     * Compatibility resolve() methods that accept query objects
     * Maps to string-based resolve(host, service) API
     */
    results_type resolve(const query& q)
    {
        return base_type::resolve(q.host_name(), q.service_name());
    }

    results_type resolve(const query& q, boost::system::error_code& ec)
    {
        return base_type::resolve(q.host_name(), q.service_name(), ec);
    }

private:
    /* 
     * Helper function to convert results to iterator - C++11 compatible
     * This is needed because async_resolve in Boost 1.89+ passes results,
     * but our callbacks expect iterator
     */
    template <typename Handler>
    struct resolve_handler_wrapper
    {
        Handler handler;
        
        void operator()(const boost::system::error_code& ec, const results_type& results)
        {
            handler(ec, results.begin());
        }
    };

public:
    /*
     * Async resolve with query object
     * Maps to string-based async_resolve(host, service, handler) API
     *
     * Note: In Boost 1.89+, async_resolve passes basic_resolver_results<Protocol>
     * but our code expects basic_resolver_iterator<Protocol>. We wrap the handler
     * to convert results to iterator by calling .begin()
     */
    template <typename ResolveHandler>
    void async_resolve(const query& q, ResolveHandler&& handler)
    {
        base_type::async_resolve(
            q.host_name(),
            q.service_name(),
            resolve_handler_wrapper<ResolveHandler>{ std::forward<ResolveHandler>(handler) }
        );
    }
};

/*
 * Provide convenient typedefs in boost::asio::ip namespace
 * These make it easier to use the compat types without version guards
 */

/* TCP resolver compatibility type */
typedef basic_resolver_compat<tcp> tcp_resolver;

/* UDP resolver compatibility type */
typedef basic_resolver_compat<udp> udp_resolver;

/* ICMP resolver compatibility type */
typedef basic_resolver_compat<icmp> icmp_resolver;

} // namespace ip

/*
 * async_connect compatibility wrapper
 *
 * Boost 1.89+ removed the single-iterator overload of async_connect.
 * This wrapper allows calling async_connect with a single iterator
 * (representing the start of a range) by creating a range from iterator to end.
 */
template <typename Protocol, typename Executor, typename Iterator, typename ConnectToken>
inline auto async_connect(
    basic_socket<Protocol, Executor>& s,
    Iterator begin,
    ConnectToken&& token)
    -> decltype(boost::asio::async_connect(s, begin, begin, std::forward<ConnectToken>(token)))
{
    /*
     * Create an endpoint sequence from iterator to end
     * In Boost 1.89+, we need to pass the range (begin to end)
     */
    Iterator end; /* Default-constructed iterator represents end */

    return boost::asio::async_connect(
        s,
        begin,
        end,
        std::forward<ConnectToken>(token)
    );
}

} // namespace asio
} // namespace boost

#else // BOOST_VERSION < 108900

/*
 * For older Boost versions, use the standard resolver types
 */
namespace boost {
namespace asio {
namespace ip {

/* Use standard tcp::resolver for older Boost versions */
typedef tcp::resolver tcp_resolver;

/* Use standard udp::resolver for older Boost versions */
typedef udp::resolver udp_resolver;

/* Use standard icmp::resolver for older Boost versions */
typedef icmp::resolver icmp_resolver;

} // namespace ip
} // namespace asio
} // namespace boost

#endif // BOOST_VERSION >= 108900

#endif /* __BL_BOOSTASIOCOMPAT_H_ */
