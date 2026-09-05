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

#ifndef __BL_JNI_JAVAVIRTUALMACHINE_H_
#define __BL_JNI_JAVAVIRTUALMACHINE_H_

#include <jni.h>

#include <baselib/jni/JavaVirtualMachineConfig.h>
#include <baselib/jni/JniEnvironment.h>

#include <baselib/core/BuildInfo.h>
#include <baselib/core/ObjModel.h>
#include <baselib/core/BaseIncludes.h>

#include <cstring>

namespace bl
{
    namespace jni
    {
        /**
         * @brief class JavaVirtualMachine - representation of Java Virtual Machine (JavaVM).
         * Only one JavaVM can be created in a process, and after JavaVM has been destroyed, another JavaVM can't be created.
         * Also the JVM library itself (jvm.dll, libjvm.so) can't be unloaded after JavaVM has been created and destroyed.
         */

        template
        <
            typename E = void
        >
        class JavaVirtualMachineT FINAL
        {
            BL_NO_COPY_OR_MOVE( JavaVirtualMachineT )

        private:

            typedef JavaVirtualMachineT< E >                            this_type;

            static this_type*                                           g_instance;

            static cpp::ScalarTypeIniter< bool >                        g_javaVMDestroyed;
            static cpp::ScalarTypeIniter< bool >                        g_javaVMCreateAttempted;
            static os::mutex                                            g_lock;
            static JavaVirtualMachineConfig                             g_jvmConfig;

            JavaVM*                                                     m_javaVM;

            JavaVirtualMachineT()
                :
                m_javaVM( nullptr )
            {
                const auto& jvmLibraryPath = ! g_jvmConfig.getLibraryPath().empty()
                    ? g_jvmConfig.getLibraryPath()
                    : getJvmPathFromJavaHome();

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Loading JVM library from: "
                        << jvmLibraryPath
                    );

                /*
                 * The library handle is released rather than kept: the JVM library can never be
                 * unloaded once a JVM has been created in the process (see the class comment),
                 * so it must not be closed by a throwing constructor or by the destructor
                 * either; plugin libraries are kept loaded the same way in Manifest.h
                 */

                auto jvmLibrary = os::loadLibrary( jvmLibraryPath );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "JVM library loaded successfully, getting JNI_CreateJavaVM address"
                    );

                const auto procAddress = os::getProcAddress( jvmLibrary, "JNI_CreateJavaVM" );

                ( void ) jvmLibrary.release();

                const auto jniCreateJavaVM =
                    reinterpret_cast< jint ( JNICALL* )( JavaVM**, void**, void *) >( procAddress );

                JavaVMInitArgs vmArgs;
                std::memset( &vmArgs, 0, sizeof( vmArgs ) );
                vmArgs.version = JNI_VERSION_1_8;
                vmArgs.ignoreUnrecognized = JNI_FALSE;

                const auto options = g_jvmConfig.getJavaVMOptions();

                const auto optionsSize = options.size();

                const auto javaVMOptions = cpp::SafeUniquePtr< JavaVMOption[] >::attach(
                    new ::JavaVMOption[ optionsSize ]
                    );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Creating JVM with "
                        << optionsSize
                        << " options: "
                    );

                for( std::size_t i = 0; i < optionsSize; ++i )
                {
                    javaVMOptions[i].optionString = const_cast< char* >( options[ i ].c_str() );

                    BL_LOG(
                        Logging::debug(),
                        javaVMOptions[i].optionString
                        );
                }

                vmArgs.nOptions = static_cast< jint >( optionsSize );
                vmArgs.options = javaVMOptions.get();

                JNIEnv* jniEnv;
                JavaVM* javaVM;

                /*
                 * HotSpot supports one JVM per process and permits a second JNI_CreateJavaVM only
                 * after a failure in its argument parsing; every other outcome, success or
                 * failure, makes a retry unsupported, so the attempt is latched here (before the
                 * call, so that a failure inside it counts) and instance() refuses a second one.
                 * The failures above this point (JAVA_HOME, the library path, the entry point)
                 * happened before anything was created and remain retryable
                 */

                g_javaVMCreateAttempted = true;

                const jint jniErrorCode = jniCreateJavaVM(
                    &javaVM,
                    ( void** )&jniEnv,
                    &vmArgs
                    );

                BL_CHK_T(
                    false,
                    jniErrorCode == JNI_OK,
                    JavaException(),
                    BL_MSG()
                        << "Failed to create JVM. ErrorCode "
                        << jniErrorCode
                        << " ["
                        << jniErrorMessage( jniErrorCode )
                        << "]"
                    );

                m_javaVM = javaVM;

                /*
                 * Detach the thread that created JavaVM so that JavaVM can be destroyed from any thread.
                 */

                const jint detachErrorCode = m_javaVM -> DetachCurrentThread();

                if( JNI_OK != detachErrorCode )
                {
                    /*
                     * The JVM exists at this point and the destructor never runs for a throwing
                     * constructor, so it is destroyed here - the creating thread is still
                     * attached, which is what DestroyJavaVM() requires - and the process is
                     * marked as having had its JVM destroyed, which is the truth
                     */

                    ( void ) m_javaVM -> DestroyJavaVM();
                    m_javaVM = nullptr;

                    g_javaVMDestroyed = true;
                }

                BL_CHK_T(
                    false,
                    JNI_OK == detachErrorCode,
                    JavaException(),
                    BL_MSG()
                        << "Failed to detach jni thread from JVM. ErrorCode "
                        << detachErrorCode
                        << " ["
                        << jniErrorMessage( detachErrorCode )
                        << "]"
                    );
            }

            static std::string getJvmPathFromJavaHome()
            {
                const auto javaHome = os::tryGetEnvironmentVariable( "JAVA_HOME" );

                BL_CHK_T_USER_FRIENDLY(
                    true,
                    ! javaHome.get(),
                    JavaException(),
                    "Environment variable JAVA_HOME is not defined"
                    );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "JAVA_HOME = '"
                        << *javaHome
                        << "'"
                    );

                /*
                 * JDK 9+ uses lib/server structure
                 * JDK 8 and earlier use jre/lib/<arch>/server structure
                 * On macOS, JDK 9+ uses .dylib extension instead of .so
                 */
                std::vector< fs::path > jvmPathCandidates;

                /*
                 * On Windows, normalize the JAVA_HOME path to ensure consistent separators.
                 * This is critical because JAVA_HOME may come from environment with forward slashes,
                 * but boost::filesystem uses backslashes on Windows, causing mixed separators
                 * which breaks fs::exists() checks.
                 */
                const fs::path javaHomeBase = os::onWindows()
                    ? fs::normalize( fs::path( *javaHome ) )
                    : fs::path( *javaHome );

                if( os::onWindows() )
                {
                    jvmPathCandidates.push_back( javaHomeBase / "bin" / "server" / "jvm.dll" );
                    jvmPathCandidates.push_back( javaHomeBase / "jre" / "bin" / "server" / "jvm.dll" );
                }
                else if( os::onDarwin() )
                {
                    /* macOS JDK 9+ structure */
                    jvmPathCandidates.push_back( javaHomeBase / "lib" / "server" / "libjvm.dylib" );
                    /* macOS JDK 8 structure */
                    if( os::on32BitPlatform() )
                    {
                        jvmPathCandidates.push_back( javaHomeBase / "jre" / "lib" / "i386" / "server" / "libjvm.dylib" );
                    }
                    else
                    {
                        const auto archDir = BuildInfo::arch == "a64" ? "aarch64" : "amd64";
                        jvmPathCandidates.push_back( javaHomeBase / "jre" / "lib" / archDir / "server" / "libjvm.dylib" );
                    }
                }
                else
                {
                    /* Linux JDK 25+ structure - no arch subdirectory */
                    jvmPathCandidates.push_back( javaHomeBase / "lib" / "server" / "libjvm.so" );

                    /* Linux JDK 9-24 structure - with arch subdirectory */
                    if( os::on32BitPlatform() )
                    {
                        jvmPathCandidates.push_back( javaHomeBase / "lib" / "i386" / "server" / "libjvm.so" );
                    }
                    else
                    {
                        const auto archDir = BuildInfo::arch == "a64" ? "aarch64" : "amd64";
                        jvmPathCandidates.push_back( javaHomeBase / "lib" / archDir / "server" / "libjvm.so" );
                    }
                    /* Linux JDK 8 structure */
                    if( os::on32BitPlatform() )
                    {
                        jvmPathCandidates.push_back( javaHomeBase / "jre" / "lib" / "i386" / "server" / "libjvm.so" );
                    }
                    else
                    {
                        const auto archDir = BuildInfo::arch == "a64" ? "aarch64" : "amd64";
                        jvmPathCandidates.push_back( javaHomeBase / "jre" / "lib" / archDir / "server" / "libjvm.so" );
                    }
                }

                fs::path jvmPath;
                for( const auto& candidate : jvmPathCandidates )
                {
                    BL_LOG(
                        Logging::debug(),
                        BL_MSG()
                            << "Checking JVM path candidate: "
                            << fs::normalizePathParameterForPrint( candidate )
                            << " (exists: "
                            << ( fs::exists( candidate ) ? "yes" : "no" )
                            << ")"
                        );

                    if( fs::exists( candidate ) )
                    {
                        jvmPath = candidate;
                        break;
                    }
                }

                BL_CHK_T_USER_FRIENDLY(
                    false,
                    ! jvmPath.empty(),
                    JavaException(),
                    BL_MSG()
                        << "Could not find JVM library in JAVA_HOME: "
                        << *javaHome
                    );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Found JVM at: "
                        << fs::normalizePathParameterForPrint( jvmPath )
                    );

                jvmPath = fs::normalize( jvmPath );

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Normalized JVM path: "
                        << fs::normalizePathParameterForPrint( jvmPath )
                    );

                if( os::onWindows() )
                {
                    /*
                     * Remove "\\?\" prefix from JavaVM path on Windows.
                     * Otherwise after the JVM library is loaded the call to JNI_CreateJavaVM will fail with:
                     *     Error occurred during initialization of VM
                     *     java/lang/NoClassDefFoundError: java/lang/Object
                     */

                    return fs::detail::WinLfnUtils::chk2RemovePrefix( std::move( jvmPath ) ).string();
                }
                else
                {
                    return jvmPath.string();
                }
            }

            ~JavaVirtualMachineT() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                BL_LOG(
                    Logging::debug(),
                    BL_MSG()
                        << "Destroying JVM"
                    );

                const jint jniErrorCode = m_javaVM -> DestroyJavaVM();

                BL_CHK_T(
                    false,
                    jniErrorCode == JNI_OK,
                    JavaException(),
                    BL_MSG()
                        << "Failed to destroy JVM. ErrorCode "
                        << jniErrorCode
                        << " ["
                        << jniErrorMessage( jniErrorCode )
                        << "]"
                    );

                BL_NOEXCEPT_END()
            }

        public:

            static void setConfig( SAA_in JavaVirtualMachineConfig&& jvmConfig )
            {
                g_jvmConfig = BL_PARAM_FWD( jvmConfig );
            }

            static auto getConfig() NOEXCEPT -> const JavaVirtualMachineConfig&
            {
                return g_jvmConfig;
            }

            static auto instance() -> this_type&
            {
                BL_MUTEX_GUARD( g_lock );

                if( ! g_instance )
                {
                    BL_CHK_T(
                        true,
                        g_javaVMDestroyed,
                        JavaException(),
                        "JavaVM has already been destroyed"
                        );

                    BL_CHK_T(
                        true,
                        g_javaVMCreateAttempted,
                        JavaException(),
                        "JavaVM creation has already been attempted in this process and it cannot be retried"
                        );

                    g_instance = new JavaVirtualMachineT();

                    JniEnvironment::setJavaVM( g_instance -> getJavaVM() );
                }

                BL_ASSERT( g_instance );

                return *g_instance;
            }

            static void destroy() NOEXCEPT
            {
                BL_NOEXCEPT_BEGIN()

                JniEnvironment::detach();

                const auto jniThreadCount = JniEnvironment::getJniThreadCount();

                BL_CHK_T(
                    false,
                    jniThreadCount == 0,
                    JavaException(),
                    BL_MSG()
                        << "Can't destroy JavaVM because not all jni threads were detached from it. Number of attached threads: "
                        << jniThreadCount
                    );

                BL_MUTEX_GUARD( g_lock );

                BL_ASSERT( g_instance );

                delete g_instance;
                g_instance = nullptr;

                g_javaVMDestroyed = true;

                BL_NOEXCEPT_END()
            }

            auto getJavaVM() const NOEXCEPT -> JavaVM*
            {
                BL_ASSERT( g_instance );

                return m_javaVM;
            }
        };

        typedef JavaVirtualMachineT<> JavaVirtualMachine;

        BL_DEFINE_STATIC_MEMBER( JavaVirtualMachineT, cpp::ScalarTypeIniter< bool >,    g_javaVMDestroyed );
        BL_DEFINE_STATIC_MEMBER( JavaVirtualMachineT, cpp::ScalarTypeIniter< bool >,    g_javaVMCreateAttempted );
        BL_DEFINE_STATIC_MEMBER( JavaVirtualMachineT, os::mutex,                        g_lock );
        BL_DEFINE_STATIC_MEMBER( JavaVirtualMachineT, JavaVirtualMachineConfig,         g_jvmConfig );

        BL_DEFINE_STATIC_MEMBER( JavaVirtualMachineT,
            typename JavaVirtualMachineT< TCLASS >::this_type*,                         g_instance );

    } // jni

} // bl

#endif /* __BL_JNI_JAVAVIRTUALMACHINE_H_ */
