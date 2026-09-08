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

#include <baselib/core/StringTemplateResolver.h>

#include <utests/baselib/Utf.h>

UTF_AUTO_TEST_CASE( StringTemplateTests )
{
    using namespace bl;

    typedef str::StringTemplateResolver                                     resolver_t;
    typedef std::unordered_map< std::string, std::string >                  variables_list_t;

    const auto testResolve = [](
        SAA_in          const variables_list_t&         variables,
        SAA_in          std::string&&                   templateText,
        SAA_in          const std::string&              expectedResolvedText
        )
        -> void
    {
        const auto resolver =
            resolver_t::createInstance( BL_PARAM_FWD( templateText ), true /* skipUndefined */ );

        UTF_REQUIRE_EQUAL( resolver -> resolve( variables ), expectedResolvedText );

        UTF_REQUIRE_EQUAL( resolver -> resolve( variables ), expectedResolvedText );
    };

    const auto testNegativeResolve = [](
        SAA_in          const variables_list_t&         variables,
        SAA_in          std::string&&                   templateText,
        SAA_in          const std::string&              expectedResolvedText,
        SAA_in          const bool                      skipUndefined
        )
        -> void
    {
        const auto resolver =
            resolver_t::createInstance( BL_PARAM_FWD( templateText ), skipUndefined );

        UTF_REQUIRE_EQUAL( resolver -> resolve( variables ), expectedResolvedText );

        UTF_FAIL( "This call is expected to throw" );
    };

    variables_list_t vars;

    testResolve( vars, "" /* templateText */, "" /* expectedResolvedText */ );
    testResolve( vars, "test" /* templateText */, "test" /* expectedResolvedText */ );

    testResolve( vars, "\n" /* templateText */, "\n" /* expectedResolvedText */ );
    testResolve( vars, "\n\n" /* templateText */, "\n\n" /* expectedResolvedText */ );
    testResolve( vars, "\n\n\n" /* templateText */, "\n\n\n" /* expectedResolvedText */ );

    testResolve( vars, "{{}}" /* templateText */, "" /* expectedResolvedText */ );
    testResolve( vars, "{{}}{{}}" /* templateText */, "" /* expectedResolvedText */ );
    testResolve( vars, "{{}}\n{{}}" /* templateText */, "\n" /* expectedResolvedText */ );
    testResolve( vars, "a{{}}\n{{}}b" /* templateText */, "a\nb" /* expectedResolvedText */ );

    testResolve( vars, "test\n\n\n" /* templateText */, "test\n\n\n" /* expectedResolvedText */ );
    testResolve( vars, "\n\n\ntest" /* templateText */, "\n\n\ntest" /* expectedResolvedText */ );
    testResolve( vars, "test\n\n\ntest" /* templateText */, "test\n\n\ntest" /* expectedResolvedText */ );
    testResolve( vars, "test\n\nab\ntest" /* templateText */, "test\n\nab\ntest" /* expectedResolvedText */ );

    vars[ "var1" ] = "value1";
    vars[ "var2" ] = "value2";
    vars[ "var3" ] = "value3";

    /*
     * Test some simple replacement cases first
     */

    testResolve( vars, "{{var1}}" /* templateText */, "value1" /* expectedResolvedText */ );

    testResolve(
        vars,
        "prefix_{{var1}}_suffix"                                        /* templateText */,
        "prefix_value1_suffix"                                          /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{var2}}prefix_{{var1}}_suffix"                                /* templateText */,
        "value2prefix_value1_suffix"                                    /* expectedResolvedText */
        );

    testResolve(
        vars,
        "prefix_{{var1}}_suffix{{var2}}"                                /* templateText */,
        "prefix_value1_suffixvalue2"                                    /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{var2}}prefix_{{var1}}_suffix{{var3}}"                        /* templateText */,
        "value2prefix_value1_suffixvalue3"                              /* expectedResolvedText */
        );

    /*
     * Test more complex cases - e.g. implicit and explicit blocks and undefined variables
     */

    testResolve(
        vars,
        "{{var2}}pr\nefix_{{var1}}_suffix{{var3}}\n"                    /* templateText */,
        "value2pr\nefix_value1_suffixvalue3\n"                          /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{var2}}pr{{}}efix_{{var1}}_suffix{{var3}}\n"                  /* templateText */,
        "value2prefix_value1_suffixvalue3\n"                            /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{undef}}pr{{}}efix_{{var1}}_suffix{{var3}}\n"                 /* templateText */,
        "efix_value1_suffixvalue3\n"                                    /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{var2}}pr{{}}efix_{{undef}}_suffix{{var3}}\n"                 /* templateText */,
        "value2pr"                                                      /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{var2}}pr{{}}efix_{{undef}}_suffix{{var3}}test\n"             /* templateText */,
        "value2pr"                                                      /* expectedResolvedText */
        );

    testResolve(
        vars,
        "{{var2}}pr{{}}efix_{{undef}}_suffix{{var3}}\ntest"             /* templateText */,
        "value2prtest"                                                  /* expectedResolvedText */
        );

    /*
     * Test some negative examples
     */

    UTF_REQUIRE_THROW_MESSAGE(
        testNegativeResolve(
            vars,
            "{{ foo bar"                                                /* templateText */,
            "n/a"                                                       /* expectedResolvedText */,
            true                                                        /* skipUndefined */
            ),
        InvalidDataFormatException,
        "Variable end marker '}}' cannot be found while parsing string template"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        testNegativeResolve(
            vars,
            "{{var1\n}}"                                                /* templateText */,
            "n/a"                                                       /* expectedResolvedText */,
            true                                                        /* skipUndefined */
            ),
        InvalidDataFormatException,
        "Variable end marker '}}' cannot be found while parsing string template"
        );

    UTF_REQUIRE_THROW_MESSAGE(
        testNegativeResolve(
            vars,
            "{{undefinedVar}}"                                          /* templateText */,
            "n/a"                                                       /* expectedResolvedText */,
            false                                                       /* skipUndefined */
            ),
        NotFoundException,
        "Variable 'undefinedVar' is undefined when resolving a string template"
        );
}

/************************************************************************
 * Self-tests for the exception assertion macros in Utf.h
 *
 * The macros are harness code with no coverage of their own, and getting one of them
 * wrong is silent - a predicate which always returns true turns every site which uses
 * it into an assertion which cannot fail
 */

UTF_AUTO_TEST_CASE( UtfExceptionMacrosTests )
{
    using namespace bl;

    const auto ioError = eh::errc::make_error_code( eh::errc::io_error );
    const auto permissionDenied = eh::errc::make_error_code( eh::errc::permission_denied );

    const auto throwServerError = []( SAA_in const eh::error_code& ec ) -> void
    {
        BL_THROW(
            ServerErrorException()
                << eh::errinfo_error_code( ec ),
            BL_MSG()
                << "Simulated server error for the harness self-test"
            );
    };

    const auto throwWithoutErrorCode = []() -> void
    {
        BL_THROW(
            ServerErrorException(),
            BL_MSG()
                << "Simulated server error which carries no error code"
            );
    };

    /*
     * The positive direction - the error code, the error code together with a message
     * fragment, and the errno all match
     */

    UTF_REQUIRE_THROW_ERROR_CODE( throwServerError( ioError ), ServerErrorException, ioError );

    UTF_CHECK_THROW_ERROR_CODE( throwServerError( ioError ), ServerErrorException, ioError );

    UTF_REQUIRE_THROW_ERROR_CODE_AND_MESSAGE(
        throwServerError( ioError ),
        ServerErrorException,
        ioError,
        "Simulated server error for the harness self-test"
        );

    /*
     * BL_THROW_EC attaches the errno alongside the error code for a generic category code
     */

    UTF_REQUIRE_THROW_ERRNO(
        BL_THROW_EC( ioError, BL_MSG() << "Simulated system error for the harness self-test" ),
        SystemException,
        ioError.value()
        );

    /*
     * The negative directions are asserted against the predicates directly rather than
     * through the macros, so that the run does not record a real Boost.Test failure
     */

    try
    {
        throwServerError( permissionDenied );

        UTF_FAIL( "The exception was expected to be thrown" );
    }
    catch( ServerErrorException& e )
    {
        UTF_REQUIRE( test::UtfExceptionTools::matchErrorCode( e, permissionDenied ) );

        /*
         * An exception carrying a different error code must not satisfy the predicate
         */

        UTF_REQUIRE( ! test::UtfExceptionTools::matchErrorCode( e, ioError ) );

        /*
         * ... and neither must a message fragment which is not part of the message
         */

        UTF_REQUIRE( ! test::UtfExceptionTools::matchMessage( e, "no such text in the message" ) );
    }

    try
    {
        throwWithoutErrorCode();

        UTF_FAIL( "The exception was expected to be thrown" );
    }
    catch( ServerErrorException& e )
    {
        /*
         * The 'no error code attached' branch must be rejected rather than dereferenced,
         * and reported distinctly from 'a different error code was attached'
         */

        UTF_REQUIRE( ! test::UtfExceptionTools::matchErrorCode( e, ioError ) );

        UTF_REQUIRE( ! test::UtfExceptionTools::matchErrNo( e, ioError.value() ) );
    }
}
