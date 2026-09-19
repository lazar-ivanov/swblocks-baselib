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

#ifndef __UTEST_RAWFRAMESCRIPTPEER_H_
#define __UTEST_RAWFRAMESCRIPTPEER_H_

#include <utests/baselib/Http2TestServer.h>

#include <baselib/core/OS.h>
#include <baselib/core/TimeUtils.h>
#include <baselib/core/BaseIncludes.h>

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

/*
 * The byte-exact half of the test peer of notes/plans/http2-design.md 8.2 (D8), slice S4.4
 *
 * Http2TestServer is a CONFORMING peer built on the protocol core, which is what makes it useful
 * and also what makes it unable to produce the inputs a client's defensive guards exist for: it
 * cannot send a WINDOW_UPDATE of zero, a CONTINUATION on the wrong stream, a frame longer than the
 * SETTINGS_MAX_FRAME_SIZE it advertised, or a header block which claims a stream identifier of
 * zero. This is RawHttpResponder (utf_baselib_http/TestClientHttpTasks.h:31) at frame level - a
 * socket which sends exactly the octets the case names, in the order it names them
 *
 * It is deliberately NOT built on TcpServerBase: there is no session here, nothing is full duplex,
 * and a script of synchronous calls on a worker thread is what makes a byte script read as one. A
 * blocking read cannot hang the suite, for the same two reasons it cannot in FakeProxy - the only
 * way the client stops talking is by ending the stream, which arrives as EOF, and the one call
 * which could block forever is an accept nobody connects to, which the destructor unblocks with a
 * throwaway connection before joining
 *
 * NOTHING IS ASSERTED ON THE WORKER THREAD. The Boost.Test macros are not safe to call from two
 * threads, so the worker records what it did and the case asserts on the records - after
 * waitForRecordsOf( ), which is the rendezvous that makes them visible. The recorder is the same
 * one Http2TestServer uses, deliberately: a second implementation of the rendezvous is a second
 * chance to get it wrong
 *
 * THIS HEADER LIVES UNDER src/utests/include AND MUST NEVER BE INCLUDED FROM src/include
 */

namespace utest
{
    namespace h2peer
    {
        /**
         * @brief A byte string written the way the RFC writes it
         */

        inline auto rawOctets( SAA_in const std::initializer_list< int >& values ) -> std::string
        {
            std::string result;

            for( const int value : values )
            {
                result.push_back( static_cast< char >( static_cast< unsigned char >( value ) ) );
            }

            return result;
        }

        inline auto uint32Octets( SAA_in const std::uint32_t value ) -> std::string
        {
            std::string result;

            result.push_back( static_cast< char >( ( value >> 24 ) & 0xFFU ) );
            result.push_back( static_cast< char >( ( value >> 16 ) & 0xFFU ) );
            result.push_back( static_cast< char >( ( value >> 8 ) & 0xFFU ) );
            result.push_back( static_cast< char >( value & 0xFFU ) );

            return result;
        }

        /**
         * @brief A nine octet frame header plus a payload, built by hand
         *
         * Deliberately not through FrameCodec: a serializer refuses to build most of what this
         * peer exists to send, and a script built with the serializer the client parses with
         * proves only that the two agree. The length is taken from the payload, so a case which
         * needs a LYING length writes the header itself with rawOctets( )
         */

        inline auto frameOctets(
            SAA_in              const std::uint8_t                              type,
            SAA_in              const std::uint8_t                              flags,
            SAA_in              const std::uint32_t                             streamId,
            SAA_in_opt          const std::string&                              payload = std::string()
            )
            -> std::string
        {
            std::string result;

            result.push_back( static_cast< char >( ( payload.size() >> 16 ) & 0xFFU ) );
            result.push_back( static_cast< char >( ( payload.size() >> 8 ) & 0xFFU ) );
            result.push_back( static_cast< char >( payload.size() & 0xFFU ) );
            result.push_back( static_cast< char >( type ) );
            result.push_back( static_cast< char >( flags ) );
            result.push_back( static_cast< char >( ( streamId >> 24 ) & 0xFFU ) );
            result.push_back( static_cast< char >( ( streamId >> 16 ) & 0xFFU ) );
            result.push_back( static_cast< char >( ( streamId >> 8 ) & 0xFFU ) );
            result.push_back( static_cast< char >( streamId & 0xFFU ) );

            result.append( payload );

            return result;
        }

        /**
         * @brief What one step of a raw script does
         */

        enum class RawStepKind : std::uint8_t
        {
            ExpectPreface,      /* read the 24 octet client connection preface and check it */
            ReadFrame,          /* read one frame - nine octets of header and its payload */
            ReadOctets,         /* read exactly this many octets, whatever they are */
            Send,               /* write these octets, verbatim */
            Delay,              /* wait, without reading */
            WaitForClose,       /* block until the client ends the stream */
            Close,              /* close the socket */
        };

        struct RawStep
        {
            bl::cpp::ScalarTypeIniter< RawStepKind >                            kind;
            bl::cpp::ScalarTypeIniter< std::size_t >                            count;
            bl::cpp::ScalarTypeIniter< long >                                   delayInMilliseconds;

            std::string                                                         data;
        };

        /**
         * @brief class RawFrameScriptT - the ordered list of octets a raw peer exchanges
         */

        template
        <
            typename E = void
        >
        class RawFrameScriptT
        {
        public:

            typedef RawFrameScriptT< E >                                        this_type;

            this_type& expectPreface()
            {
                RawStep step;

                step.kind = RawStepKind::ExpectPreface;

                m_steps.push_back( step );

                return *this;
            }

            /**
             * @brief Reads 'count' whole frames, recording the header of each one
             */

            this_type& readFrames( SAA_in_opt const std::size_t count = 1U )
            {
                for( std::size_t i = 0U; i < count; ++i )
                {
                    RawStep step;

                    step.kind = RawStepKind::ReadFrame;

                    m_steps.push_back( step );
                }

                return *this;
            }

            this_type& readOctets( SAA_in const std::size_t count )
            {
                RawStep step;

                step.kind = RawStepKind::ReadOctets;
                step.count = count;

                m_steps.push_back( step );

                return *this;
            }

            this_type& send( SAA_in const std::string& data )
            {
                RawStep step;

                step.kind = RawStepKind::Send;
                step.data = data;

                m_steps.push_back( step );

                return *this;
            }

            this_type& delay( SAA_in const long delayInMilliseconds )
            {
                RawStep step;

                step.kind = RawStepKind::Delay;
                step.delayInMilliseconds = delayInMilliseconds;

                m_steps.push_back( step );

                return *this;
            }

            this_type& waitForClose()
            {
                RawStep step;

                step.kind = RawStepKind::WaitForClose;

                m_steps.push_back( step );

                return *this;
            }

            this_type& closeConnection()
            {
                RawStep step;

                step.kind = RawStepKind::Close;

                m_steps.push_back( step );

                return *this;
            }

            const std::vector< RawStep >& steps() const NOEXCEPT
            {
                return m_steps;
            }

        private:

            std::vector< RawStep >                                              m_steps;
        };

        typedef RawFrameScriptT<> RawFrameScript;

        /**
         * @brief class RawFrameScriptPeerT - an in-process peer which sends exactly what it is told
         *
         * The port is ephemeral, so these cases need no fixed port and do not take the machine
         * global test lock
         */

        template
        <
            typename E = void
        >
        class RawFrameScriptPeerT
        {
            BL_NO_COPY_OR_MOVE( RawFrameScriptPeerT )

        public:

            enum : std::uint32_t
            {
                /*
                 * The key the octets read from the client are accumulated under, in the same
                 * store a session peer keeps request bodies in. Zero is the connection identifier
                 * of RFC 9113 and never a stream, so the two can never collide
                 */

                RECEIVED_OCTETS_KEY = 0U,
            };

            RawFrameScriptPeerT(
                SAA_in              const RawFrameScript&                       script,
                SAA_in_opt          const std::size_t                           connections = 1U
                )
                :
                m_recorder( std::make_shared< Http2TestRecorder >() ),
                m_acceptor(
                    m_ioService,
                    bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), 0 /* ephemeral */ )
                    ),
                m_port( m_acceptor.local_endpoint().port() ),
                m_script( script ),
                m_connections( connections ),
                m_stopRequested( false )
            {
                m_thread.reset( new bl::os::thread( bl::cpp::bind( &RawFrameScriptPeerT::run, this ) ) );
            }

            ~RawFrameScriptPeerT() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                m_stopRequested = true;

                {
                    /*
                     * Closing the acceptor does not reliably wake a worker already blocked in
                     * accept( ), so one throwaway connection does it while the acceptor is still
                     * open; the flag above makes the worker drop it rather than run the script
                     */

                    bl::eh::error_code ec;

                    bl::asio::io_service ioService;
                    bl::asio::ip::tcp::socket socket( ioService );

                    socket.connect(
                        bl::asio::ip::tcp::endpoint( bl::asio::ip::address_v4::loopback(), m_port ),
                        ec
                        );

                    socket.close( ec );
                }

                bl::os::safeThreadJoin( *m_thread );

                BL_NOEXCEPT_END()
            }

            unsigned short port() const NOEXCEPT
            {
                return m_port;
            }

            auto recorder() const NOEXCEPT -> const Http2TestRecorder&
            {
                return *m_recorder;
            }

            /**
             * @brief Every octet the peer read from the client, in order
             *
             * READ IT AFTER waitForRecordsOf, which is what orders the worker's writes against
             * this read - the same reason failure( ) is read there
             */

            auto receivedOctets() const -> std::string
            {
                return m_recorder -> bodyOf( RECEIVED_OCTETS_KEY );
            }

        private:

            static auto readExactly(
                SAA_inout           bl::asio::ip::tcp::socket&                  socket,
                SAA_in              const std::size_t                           count
                )
                -> std::string
            {
                std::string buffer( count, '\0' );

                bl::eh::error_code ec;

                const auto transferred =
                    bl::asio::read( socket, bl::asio::buffer( &buffer[ 0 ], count ), ec );

                buffer.resize( transferred );

                return buffer;
            }

            void readAndRecord(
                SAA_inout           bl::asio::ip::tcp::socket&                  socket,
                SAA_in              const std::size_t                           count,
                SAA_out             std::string&                                data
                )
            {
                data = readExactly( socket, count );

                m_recorder -> appendBody( RECEIVED_OCTETS_KEY, data );

                BL_CHK(
                    false,
                    data.size() == count,
                    BL_MSG()
                        << "A raw frame script expected "
                        << count
                        << " octets from the client and read "
                        << data.size()
                    );
            }

            void runStep(
                SAA_inout           bl::asio::ip::tcp::socket&                  socket,
                SAA_in              const RawStep&                              step
                )
            {
                using namespace bl;

                switch( step.kind.value() )
                {
                    case RawStepKind::ExpectPreface:
                        {
                            std::string data;

                            readAndRecord( socket, http2::Globals::g_connectionPreface.size(), data );

                            m_recorder -> record(
                                data == http2::Globals::g_connectionPreface ?
                                    std::string( "the client sent the connection preface" ) :
                                    std::string( "the client sent something other than the preface" )
                                );
                        }

                        break;

                    case RawStepKind::ReadFrame:
                        {
                            std::string header;

                            readAndRecord( socket, 9U /* the frame header */, header );

                            const auto length =
                                ( static_cast< std::size_t >(
                                    static_cast< unsigned char >( header[ 0 ] ) ) << 16 ) |
                                ( static_cast< std::size_t >(
                                    static_cast< unsigned char >( header[ 1 ] ) ) << 8 ) |
                                static_cast< std::size_t >(
                                    static_cast< unsigned char >( header[ 2 ] ) );

                            if( 0U != length )
                            {
                                std::string payload;

                                readAndRecord( socket, length, payload );
                            }

                            m_recorder -> record(
                                "the client sent frame type " +
                                utils::lexical_cast< std::string >(
                                    static_cast< unsigned >(
                                        static_cast< unsigned char >( header[ 3 ] )
                                        )
                                    ) +
                                " of " +
                                utils::lexical_cast< std::string >( length ) +
                                " octets"
                                );
                        }

                        break;

                    case RawStepKind::ReadOctets:
                        {
                            std::string data;

                            readAndRecord( socket, step.count, data );

                            m_recorder -> record(
                                "the client sent " +
                                utils::lexical_cast< std::string >( data.size() ) +
                                " octets"
                                );
                        }

                        break;

                    case RawStepKind::Send:
                        {
                            eh::error_code ec;

                            ( void ) asio::write( socket, asio::buffer( step.data ), ec );

                            m_recorder -> record(
                                "sent " +
                                utils::lexical_cast< std::string >( step.data.size() ) +
                                " octets"
                                );
                        }

                        break;

                    case RawStepKind::Delay:

                        os::sleep( time::milliseconds( step.delayInMilliseconds.value() ) );

                        break;

                    case RawStepKind::WaitForClose:
                        {
                            char buffer[ 64 ];

                            eh::error_code ec;

                            const auto transferred =
                                socket.read_some( asio::buffer( buffer, sizeof( buffer ) ), ec );

                            m_recorder -> record(
                                0U == transferred && ec ?
                                    std::string( "the client closed the connection" ) :
                                    std::string( "the client sent more instead of closing" )
                                );
                        }

                        break;

                    case RawStepKind::Close:
                        {
                            eh::error_code ec;

                            socket.close( ec );

                            m_recorder -> record( "closed the connection" );
                        }

                        break;

                    default:

                        BL_RIP_MSG( "A raw frame script carries an unknown step" );

                        break;
                }
            }

            void run()
            {
                BL_NOEXCEPT_BEGIN()

                for( std::size_t i = 0U; i < m_connections; ++i )
                {
                    bl::eh::error_code ec;

                    bl::asio::ip::tcp::socket socket( m_ioService );

                    m_acceptor.accept( socket, ec );

                    if( ec || m_stopRequested )
                    {
                        break;
                    }

                    try
                    {
                        for( std::size_t step = 0U; step < m_script.steps().size(); ++step )
                        {
                            runStep( socket, m_script.steps()[ step ] );
                        }
                    }
                    catch( std::exception& e )
                    {
                        m_recorder -> recordFailure( std::string( e.what() ) );
                    }

                    socket.close( ec );
                }

                BL_NOEXCEPT_END()
            }

            const std::shared_ptr< Http2TestRecorder >                          m_recorder;

            bl::asio::io_service                                                m_ioService;
            bl::asio::ip::tcp::acceptor                                         m_acceptor;
            const unsigned short                                                m_port;
            const RawFrameScript                                                m_script;
            const std::size_t                                                   m_connections;
            std::atomic< bool >                                                 m_stopRequested;
            bl::cpp::SafeUniquePtr< bl::os::thread >                            m_thread;
        };

        typedef RawFrameScriptPeerT<> RawFrameScriptPeer;

    } // h2peer

} // utest

#endif /* __UTEST_RAWFRAMESCRIPTPEER_H_ */
