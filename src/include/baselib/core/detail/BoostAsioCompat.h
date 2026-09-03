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

#include <type_traits>
#include <utility>

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
 * Resolver query compatibility wrapper
 *
 * Boost 1.89+ removed the query-based resolve() / async_resolve() overloads, so a query
 * object now has to be forwarded to the string-based API by hand.
 *
 * The legacy boost::asio::ip::basic_resolver_query cannot be used for that because it does
 * not expose the protocol and the resolve flags it was constructed with (it only provides
 * host_name(), service_name() and hints()). This class stores the protocol, the host, the
 * service and the flags explicitly and mirrors the constructors of the legacy query class,
 * including their default flags.
 */

template <typename Protocol>
class basic_resolver_query_compat : public resolver_base
{
public:

    typedef Protocol protocol_type;

    basic_resolver_query_compat(
        const std::string& service,
        flags resolve_flags = passive | address_configured)
        : m_protocol(protocol_type::v4())
        , m_hasProtocol(false)
        , m_serviceName(service)
        , m_flags(resolve_flags)
    {
    }

    basic_resolver_query_compat(
        const protocol_type& protocol,
        const std::string& service,
        flags resolve_flags = passive | address_configured)
        : m_protocol(protocol)
        , m_hasProtocol(true)
        , m_serviceName(service)
        , m_flags(resolve_flags)
    {
    }

    basic_resolver_query_compat(
        const std::string& host,
        const std::string& service,
        flags resolve_flags = address_configured)
        : m_protocol(protocol_type::v4())
        , m_hasProtocol(false)
        , m_hostName(host)
        , m_serviceName(service)
        , m_flags(resolve_flags)
    {
    }

    basic_resolver_query_compat(
        const protocol_type& protocol,
        const std::string& host,
        const std::string& service,
        flags resolve_flags = address_configured)
        : m_protocol(protocol)
        , m_hasProtocol(true)
        , m_hostName(host)
        , m_serviceName(service)
        , m_flags(resolve_flags)
    {
    }

    bool has_protocol() const
    {
        return m_hasProtocol;
    }

    const protocol_type& protocol() const
    {
        return m_protocol;
    }

    std::string host_name() const
    {
        return m_hostName;
    }

    std::string service_name() const
    {
        return m_serviceName;
    }

    /*
     * Note: this accessor cannot be called flags() because that would hide the
     * resolver_base::flags type name inherited by this class
     */

    flags flags_value() const
    {
        return m_flags;
    }

private:

    /*
     * When no protocol was specified m_protocol holds an unused placeholder value
     * and m_hasProtocol is false
     */

    protocol_type m_protocol;
    bool m_hasProtocol;
    std::string m_hostName;
    std::string m_serviceName;
    flags m_flags;
};

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
    typedef basic_resolver_query_compat<Protocol> query;
    typedef basic_resolver_results<Protocol> results_type;
    typedef typename results_type::iterator iterator;
    typedef typename Protocol::endpoint endpoint_type;

    /* Inherit all constructors */
    using base_type::base_type;

    /*
     * Keep the modern string-based overloads visible - the compatibility overloads
     * below would otherwise hide all of them
     */
    using base_type::resolve;
    using base_type::async_resolve;

    /*
     * Compatibility resolve() methods that accept query objects
     *
     * The protocol (if one was specified), the host, the service and the resolve flags
     * are all forwarded to the corresponding string-based resolve() overload
     */
    results_type resolve(const query& q)
    {
        if( q.has_protocol() )
        {
            return base_type::resolve(
                q.protocol(), q.host_name(), q.service_name(), q.flags_value());
        }

        return base_type::resolve(q.host_name(), q.service_name(), q.flags_value());
    }

    results_type resolve(const query& q, boost::system::error_code& ec)
    {
        if( q.has_protocol() )
        {
            return base_type::resolve(
                q.protocol(), q.host_name(), q.service_name(), q.flags_value(), ec);
        }

        return base_type::resolve(q.host_name(), q.service_name(), q.flags_value(), ec);
    }

private:
    /*
     * Helper function to convert results to iterator - C++11 compatible
     * This is needed because async_resolve in Boost 1.89+ passes results,
     * but our callbacks expect iterator
     *
     * IMPORTANT - this shim is INTERNAL to baselib and is not a general purpose Asio
     * compatibility layer; it is deliberately narrower than the operation it replaces and it
     * must not be treated as public API:
     *
     * -- it accepts a plain callable only, not an Asio completion token, and async_resolve()
     *    returns void, so use_future, use_awaitable and the deferred token do not work with it
     *
     * -- it does not propagate the handler's associated executor, allocator or cancellation
     *    slot, so an operation started through it does not participate in cancellation and does
     *    not run on the handler's intended executor
     *
     * Every call site inside this repository passes a bound callable and needs none of the
     * above. Closing the gap means writing a token conforming adapter, which is a substantially
     * larger piece of work than the shim itself - see
     * notes/plans/issues/residual-cxx-findings-deferral.md, item 1
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
     *
     * The protocol (if one was specified), the host, the service and the resolve flags
     * are all forwarded to the corresponding string-based async_resolve() overload
     *
     * Note: In Boost 1.89+, async_resolve passes basic_resolver_results<Protocol>
     * but our code expects basic_resolver_iterator<Protocol>. We wrap the handler
     * to convert results to iterator by calling .begin()
     */
    template <typename ResolveHandler>
    void async_resolve(const query& q, ResolveHandler&& handler)
    {
        /*
         * Note that the handler type must be decayed before it is used as the wrapper's
         * template argument
         *
         * ResolveHandler is deduced from a forwarding reference, so for an lvalue argument it
         * deduces to T& and the wrapper's member would become a reference bound to the caller's
         * object; the wrapper outlives the call because it is handed to the asynchronous
         * operation, so that reference would dangle as soon as the caller's frame goes away
         */

        resolve_handler_wrapper< typename std::decay< ResolveHandler >::type > wrapper
        {
            std::forward<ResolveHandler>(handler)
        };

        if( q.has_protocol() )
        {
            base_type::async_resolve(
                q.protocol(),
                q.host_name(),
                q.service_name(),
                q.flags_value(),
                std::move(wrapper)
            );

            return;
        }

        base_type::async_resolve(
            q.host_name(),
            q.service_name(),
            q.flags_value(),
            std::move(wrapper)
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
