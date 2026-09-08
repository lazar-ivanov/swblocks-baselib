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

#include <baselib/cmdline/CmdLineBase.h>

#include <baselib/core/Utils.h>

#include <utests/baselib/Utf.h>
#include <utests/baselib/UtfDirectoryFixture.h>

#include <string>
#include <cctype>
#include <cstdio>

namespace
{
    /**
     * Convert a vector of values to a string
     */

    template
    <
        typename T
    >
    static std::string vector2string(
        SAA_in      const std::vector< T >&         v,
        SAA_in      const char*                     prefix = "",
        SAA_in      const char*                     suffix = ""
        )
    {
        if( v.empty() )
        {
            return "[]";
        }

        bl::cpp::SafeOutputStringStream oss;

        oss << "[ ";
        bool delimiter = false;

        for( const auto& value : v )
        {
            if( delimiter )
            {
                oss << ", ";
            }

            oss << prefix << value << suffix;

            delimiter = true;
        }
        oss << " ]";

        return oss.str();
    }

    /**
     * Convert cpp::any value to a human-readable string
     *
     * Only some common scalar types are supported
     */

    std::string any2string( SAA_in const bl::cpp::any& value )
    {
        const auto& type = value.type();

        if( typeid( std::string ) == type )
        {
            return std::string( "\"" ) + bl::cpp::any_cast< std::string >( value ) + std::string( "\"" );
        }

        if( typeid( char * ) == type || typeid( signed char * ) == type || typeid( unsigned char * ) == type )
        {
            return std::string("\"") + bl::cpp::any_cast< const char * >( value ) + std::string("\"");
        }

        if( typeid( char ) == type || typeid( signed char ) == type || typeid( unsigned char ) == type )
        {
            unsigned char ch = bl::cpp::any_cast< char >( value );
            std::ostringstream oss;
            oss << std::hex << std::uppercase << std::setfill( '0' ) << std::setw( 2 );
            oss << static_cast< unsigned >( ch );
            return oss.str();
        }

        if( typeid( bool ) == type )
        {
            return bl::cpp::any_cast< bool >( value ) ? "Y" : "N";
        }

        if( typeid( short ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< short >( value ) );
        }

        if( typeid( unsigned short ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< unsigned short >( value ) );
        }

        if( typeid( int ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< int >( value ) );
        }

        if( typeid( unsigned int ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< unsigned int >( value ) );
        }

        if( typeid( long ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< long >( value ) );
        }

        if( typeid( unsigned long ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< unsigned long >( value ) );
        }

        if( typeid( long long ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< long long >( value ) );
        }

        if( typeid( unsigned long long ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< unsigned long long >( value ) );
        }

        if( typeid( float ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< float >( value ) );
        }

        if( typeid( double ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< double >( value ) );
        }

        if( typeid( long double ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< long double >( value ) );
        }

        if( typeid( std::complex< float > ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< std::complex< float > >( value ) );
        }

        if( typeid( std::complex< double > ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< std::complex< double > >( value ) );
        }

        if( typeid( std::complex< long double > ) == type )
        {
            return bl::utils::lexical_cast< std::string >( bl::cpp::any_cast< std::complex< long double > >( value ) );
        }

        if( typeid( void ) == type )
        {
            return "(void)";
        }

        if( typeid( std::vector< std::string > ) == type )
        {
            return vector2string( bl::cpp::any_cast< std::vector< std::string > >( value ), "\"", "\"" );
        }

        if( typeid( std::vector< int > ) == type )
        {
            return vector2string( bl::cpp::any_cast< std::vector< int > >( value ) );
        }

        return std::string( "unknown type: " ) + type.name();
    }

    void dumpVariablesMap( const bl::po::variables_map& vm )
    {
        for( auto iter = vm.cbegin(); iter != vm.cend(); ++iter )
        {
            UTF_MESSAGE( "  '" + iter -> first + "' = " + any2string( iter -> second.value() ) );
        }

        UTF_MESSAGE( "" );
    }

    class CmdLineTest : public bl::cmdline::CmdLineBase
    {
    public:

        typedef bl::cmdline::Option< std::complex< double > > ComplexOption;

        bl::cmdline::StringOption       m_command;
        bl::cmdline::MultiStringOption  m_filenames;
        bl::cmdline::HelpSwitch         m_help;
        bl::cmdline::VerboseSwitch      m_verbose;
        bl::cmdline::StringOption       m_opt0;
        bl::cmdline::BoolOption         m_opt1;
        bl::cmdline::StringOption       m_opt2;
        bl::cmdline::StringOption       m_opt3;
        bl::cmdline::IntOption          m_number1;
        bl::cmdline::LongOption         m_number2;
        bl::cmdline::ShortOption        m_number3;
        bl::cmdline::MultiIntOption     m_numbers;
        bl::cmdline::DoubleOption       m_factor1;
        bl::cmdline::FloatOption        m_factor2;
        bl::cmdline::UShortOption       m_port;
        bl::cmdline::CharOption         m_esc;
        ComplexOption                   m_complex;
        bl::cmdline::StringOption       m_hidden;
        bl::cmdline::BoolSwitch         m_missing;

        CmdLineTest()
            :
            bl::cmdline::CmdLineBase ( "CmdLineTest command line options" ),
            m_command   ( "command",         "Command: run, stop", bl::cmdline::RequiredPositional ),
            m_filenames ( "filename",        "Input file name(s)", bl::cmdline::PositionalAll ),
            m_opt0      ( "opt0",            "Option 0" ),
            m_opt1      ( "opt1",            "Option 1 (On/Off)" ),                          // bool with a value
            m_opt2      ( "opt2",            "Option 2" ),
            m_opt3      ( "opt3",            "Option 3",     "default" ),
            m_number1   ( "number1,n",       "Integer number 1", 42, bl::cmdline::Required ),
            m_number2   ( "number2,L",       "Integer number 2 (long)" ),                    // long
            m_number3   ( "number3",         "Integer number 3 (short)" ),                   // short
            m_numbers   ( "numbers",         "Integer numbers", bl::cmdline::MultiValue ),
            m_factor1   ( "factor1",         "Float factor 1", 1.0, bl::cmdline::Required ), // double
            m_factor2   ( "factor2",         "Float factor 2", 2.0f ),                       // float
            m_port      ( "port,p",          "Port number", 32000 ),                         // unsigned short
            m_esc       ( "esc",             "Escape character", '\x1B' ),                   // char
            m_complex   ( "complex",         "Complex number (Re,Im)" ),                     // (Re,Im)
            m_hidden    ( "hidden",          "You should never see this option", bl::cmdline::Hidden ),
            m_missing   ( "missing",         "Missing option" )
        {
            UTF_REQUIRE( findOption( "opt0" ) == nullptr );

            addOption( m_command, m_filenames );
            addOption( m_help, m_verbose );
            addOption( m_opt0, m_opt1, m_opt2, m_opt3 );
            addOption( m_number1, m_number2, m_number3, m_numbers );
            addOption( m_factor1, m_factor2 );
            addOption( m_port, m_esc, m_complex, m_hidden );

            UTF_REQUIRE( findOption( "opt0" ) == &m_opt0 );

            removeOption( m_opt0 );

            UTF_REQUIRE( findOption( "opt0" ) == nullptr );

            /*
             * NOTE: m_opt0 and m_missing are intentionally not added for testing purposes
             */
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( CmdLine_Parse_Class )
{
    UTF_MESSAGE( "***************** CmdLine_Parse_Class tests *****************\n" );

    UTF_MESSAGE( "* creating command line options class" );
    CmdLineTest cmdln;

    UTF_MESSAGE( "* help message:" );
    UTF_MESSAGE( cmdln.helpMessage() );

    const char * args[] = { "unittest", "run", "filename1", "--opt1=off", "--opt2=ABC", "-v",
                            "--number1", "1234", "-L123456789", "--numbers", "1", "2", "3",
                            "--factor1", "-3.14", "filename2", "-p", "40000",
                            "--comp" /* abbreviation */, "(1.2,3.4)", "--hidden", "secret" };

    UTF_MESSAGE( "* parsing command line" );
    cmdln.parseCommandLine( BL_ARRAY_SIZE( args ), args );

    UTF_MESSAGE( "* variable values:" );
    dumpVariablesMap( cmdln.getParsedArgs() );

    UTF_MESSAGE( "--command as string" );
    UTF_CHECK( cmdln.m_command.hasValue() );
    UTF_CHECK( ! cmdln.m_command.hasDefaultValue() );
    UTF_CHECK( cmdln.m_command.isReadable() );
    UTF_CHECK( cmdln.m_command.isRequired() );
    UTF_CHECK( ! cmdln.m_command.isMultiValue() );
    UTF_CHECK( ! cmdln.m_command.isHidden() );
    UTF_CHECK( cmdln.m_command.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_command.getPositionalCount(), 1 );
    UTF_CHECK_EQUAL( cmdln.m_command.getValue(), "run" );
    UTF_CHECK_EQUAL( cmdln.m_command.getValue( "default" ), "run" );
    UTF_CHECK_EQUAL( cmdln.m_command.getValue( std::string("string") ), "run" );

    UTF_MESSAGE( "--filename as vector<string>" );
    std::vector< std::string > filenamesExpected;
    filenamesExpected.push_back( "filename1" );
    filenamesExpected.push_back( "filename2" );
    UTF_CHECK( cmdln.m_filenames.hasValue() );
    UTF_CHECK( ! cmdln.m_filenames.hasDefaultValue() );
    UTF_CHECK( cmdln.m_filenames.isReadable() );
    UTF_CHECK( ! cmdln.m_filenames.isRequired() );
    UTF_CHECK( cmdln.m_filenames.isMultiValue() );
    UTF_CHECK( ! cmdln.m_filenames.isHidden() );
    UTF_CHECK( cmdln.m_filenames.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_filenames.getPositionalCount(), -1 );
    UTF_CHECK_EQUAL( cmdln.m_filenames.getValue(), filenamesExpected );

    UTF_MESSAGE( "--help / -?" );
    UTF_CHECK_EQUAL( cmdln.m_help.getName(), "help,?" );
    UTF_CHECK( ! cmdln.m_help.hasValue() );
    UTF_CHECK( cmdln.m_help.hasDefaultValue() );
    UTF_CHECK( cmdln.m_help.isReadable() );
    UTF_CHECK( ! cmdln.m_help.isRequired() );
    UTF_CHECK( ! cmdln.m_help.isMultiValue() );
    UTF_CHECK( ! cmdln.m_help.isHidden() );
    UTF_CHECK( ! cmdln.m_help.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_help.getPositionalCount(), 0 );
    UTF_CHECK_EQUAL( cmdln.m_help.getDefaultValue(), false );
    UTF_CHECK_EQUAL( cmdln.m_help.getValue(), false );

    UTF_MESSAGE( "--verbose / -v" );
    UTF_CHECK_EQUAL( cmdln.m_verbose.getName(), "verbose,v" );
    UTF_CHECK( cmdln.m_verbose.hasValue() );
    UTF_CHECK( cmdln.m_verbose.hasDefaultValue() );
    UTF_CHECK( cmdln.m_verbose.isReadable() );
    UTF_CHECK( ! cmdln.m_verbose.isRequired() );
    UTF_CHECK( ! cmdln.m_verbose.isMultiValue() );
    UTF_CHECK( ! cmdln.m_verbose.isHidden() );
    UTF_CHECK( ! cmdln.m_verbose.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_verbose.getPositionalCount(), 0 );
    UTF_CHECK_EQUAL( cmdln.m_verbose.getDefaultValue(), false );
    UTF_CHECK_EQUAL( cmdln.m_verbose.getValue(), true );

    UTF_MESSAGE( "--opt0 as string" );
    UTF_CHECK_EQUAL( cmdln.m_opt0.getName(), "opt0" );
    UTF_CHECK( ! cmdln.m_opt0.hasValue() );      // option m_opt0 has been removed so it should never have a value
    UTF_CHECK( ! cmdln.m_opt0.hasDefaultValue() );
    UTF_CHECK( ! cmdln.m_opt0.isReadable() );
    UTF_CHECK( ! cmdln.m_opt0.isRequired() );
    UTF_CHECK( ! cmdln.m_opt0.isMultiValue() );
    UTF_CHECK( ! cmdln.m_opt0.isHidden() );
    UTF_CHECK( ! cmdln.m_opt0.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_opt0.getValue(), "" );
    UTF_CHECK_EQUAL( cmdln.m_opt0.getValue( "test" ), "test" );
    UTF_CHECK_EQUAL( cmdln.m_opt0.getValue( std::string("string") ), "string" );

    UTF_MESSAGE( "--opt1 as bool" );
    UTF_CHECK( cmdln.m_opt1.hasValue() );
    UTF_CHECK( cmdln.m_opt1.hasDefaultValue() );
    UTF_CHECK( cmdln.m_opt1.isReadable() );
    UTF_CHECK( ! cmdln.m_opt1.isRequired() );
    UTF_CHECK( ! cmdln.m_opt1.isMultiValue() );
    UTF_CHECK( ! cmdln.m_opt1.isHidden() );
    UTF_CHECK( ! cmdln.m_opt1.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_opt1.getDefaultValue(), false );
    UTF_CHECK_EQUAL( cmdln.m_opt1.getValue(), false );
    UTF_CHECK_EQUAL( cmdln.m_opt1.getValue( true ), false );

    UTF_MESSAGE( "--opt2 as string" );
    UTF_CHECK( cmdln.m_opt2.hasValue() );
    UTF_CHECK( ! cmdln.m_opt2.hasDefaultValue() );
    UTF_CHECK( cmdln.m_opt2.isReadable() );
    UTF_CHECK( ! cmdln.m_opt2.isRequired() );
    UTF_CHECK( ! cmdln.m_opt2.isMultiValue() );
    UTF_CHECK( ! cmdln.m_opt2.isHidden() );
    UTF_CHECK( ! cmdln.m_opt2.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_opt2.getValue(), "ABC" );
    UTF_CHECK_EQUAL( cmdln.m_opt2.getValue( "test" ), "ABC" );
    UTF_CHECK_EQUAL( cmdln.m_opt2.getValue( std::string("string") ), "ABC" );

    UTF_MESSAGE( "--opt3 as string" );
    UTF_CHECK( ! cmdln.m_opt3.hasValue() );
    UTF_CHECK( cmdln.m_opt3.hasDefaultValue() );
    UTF_CHECK( cmdln.m_opt3.isReadable() );
    UTF_CHECK( ! cmdln.m_opt3.isRequired() );
    UTF_CHECK( ! cmdln.m_opt3.isMultiValue() );
    UTF_CHECK( ! cmdln.m_opt3.isHidden() );
    UTF_CHECK( ! cmdln.m_opt3.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_opt3.getDefaultValue(), "default" );
    UTF_CHECK_EQUAL( cmdln.m_opt3.getValue(), "default" );
    UTF_CHECK_EQUAL( cmdln.m_opt3.getValue( "test" ), "test" );
    UTF_CHECK_EQUAL( cmdln.m_opt3.getValue( std::string("string") ), "string" );

    UTF_MESSAGE( "--number1 as int" );
    UTF_CHECK( cmdln.m_number1.hasValue() );
    UTF_CHECK( cmdln.m_number1.hasDefaultValue() );
    UTF_CHECK( cmdln.m_number1.isReadable() );
    UTF_CHECK( cmdln.m_number1.isRequired() );
    UTF_CHECK( ! cmdln.m_number1.isMultiValue() );
    UTF_CHECK( ! cmdln.m_number1.isHidden() );
    UTF_CHECK( ! cmdln.m_number1.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_number1.getDefaultValue(), 42 );
    UTF_CHECK_EQUAL( cmdln.m_number1.getValue(), 1234 );
    UTF_CHECK_EQUAL( cmdln.m_number1.getValue( 555 ), 1234 );

    UTF_MESSAGE( "--number2 as long" );
    UTF_CHECK( cmdln.m_number2.hasValue() );
    UTF_CHECK( ! cmdln.m_number2.hasDefaultValue() );
    UTF_CHECK( cmdln.m_number2.isReadable() );
    UTF_CHECK( ! cmdln.m_number2.isRequired() );
    UTF_CHECK( ! cmdln.m_number2.isMultiValue() );
    UTF_CHECK( ! cmdln.m_number2.isHidden() );
    UTF_CHECK( ! cmdln.m_number2.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_number2.getValue(), 123456789 );
    UTF_CHECK_EQUAL( cmdln.m_number2.getValue( LONG_MIN ), 123456789 );

    UTF_MESSAGE( "--number3 as short" );
    UTF_CHECK( ! cmdln.m_number3.hasValue() );
    UTF_CHECK( ! cmdln.m_number3.hasDefaultValue() );
    UTF_CHECK( ! cmdln.m_number3.isReadable() );
    UTF_CHECK( ! cmdln.m_number3.isRequired() );
    UTF_CHECK( ! cmdln.m_number3.isMultiValue() );
    UTF_CHECK( ! cmdln.m_number3.isHidden() );
    UTF_CHECK( ! cmdln.m_number3.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_number3.getValue(), 0 );
    UTF_CHECK_EQUAL( cmdln.m_number3.getValue( SHRT_MIN ), SHRT_MIN );

    UTF_MESSAGE( "--numbers as vector<int>" );
    std::vector< int > numbers_expected;
    numbers_expected.push_back( 1 );
    numbers_expected.push_back( 2 );
    numbers_expected.push_back( 3 );
    UTF_CHECK( cmdln.m_numbers.hasValue() );
    UTF_CHECK( ! cmdln.m_numbers.hasDefaultValue() );
    UTF_CHECK( cmdln.m_numbers.isReadable() );
    UTF_CHECK( ! cmdln.m_numbers.isRequired() );
    UTF_CHECK( cmdln.m_numbers.isMultiValue() );
    UTF_CHECK( ! cmdln.m_numbers.isHidden() );
    UTF_CHECK( ! cmdln.m_numbers.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_numbers.getValue(), numbers_expected );

    UTF_MESSAGE( "--factor1 as double" );
    UTF_CHECK( cmdln.m_factor1.hasValue() );
    UTF_CHECK( cmdln.m_factor1.hasDefaultValue() );
    UTF_CHECK( cmdln.m_factor1.isReadable() );
    UTF_CHECK( cmdln.m_factor1.isRequired() );
    UTF_CHECK( ! cmdln.m_factor1.isMultiValue() );
    UTF_CHECK( ! cmdln.m_factor1.isHidden() );
    UTF_CHECK( ! cmdln.m_factor1.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_factor1.getDefaultValue(), 1.0 );
    UTF_CHECK_EQUAL( cmdln.m_factor1.getValue(), -3.14 );
    UTF_CHECK_EQUAL( cmdln.m_factor1.getValue( 0 ), -3.14 ); // NOTE: the default value is automatically converted to the right type (if possible)

    UTF_MESSAGE( "--factor2 as float" );
    UTF_CHECK( ! cmdln.m_factor2.hasValue() );
    UTF_CHECK( cmdln.m_factor2.hasDefaultValue() );
    UTF_CHECK( cmdln.m_factor2.isReadable() );
    UTF_CHECK( ! cmdln.m_factor2.isRequired() );
    UTF_CHECK( ! cmdln.m_factor2.isMultiValue() );
    UTF_CHECK( ! cmdln.m_factor2.isHidden() );
    UTF_CHECK( ! cmdln.m_factor2.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_factor2.getDefaultValue(), 2.0 );
    UTF_CHECK_EQUAL( cmdln.m_factor2.getValue(), 2.0 );
    UTF_CHECK_EQUAL( cmdln.m_factor2.getValue( 0 ), 0.0 );

    UTF_MESSAGE( "--port as unsigned short" );
    UTF_CHECK( cmdln.m_port.hasValue() );
    UTF_CHECK( cmdln.m_port.hasDefaultValue() );
    UTF_CHECK( cmdln.m_port.isReadable() );
    UTF_CHECK( ! cmdln.m_port.isRequired() );
    UTF_CHECK( ! cmdln.m_port.isMultiValue() );
    UTF_CHECK( ! cmdln.m_port.isHidden() );
    UTF_CHECK( ! cmdln.m_port.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_port.getDefaultValue(), 32000 );
    UTF_CHECK_EQUAL( cmdln.m_port.getValue(), 40000 );
    UTF_CHECK_EQUAL( cmdln.m_port.getValue( 1024 ), 40000 );

    UTF_MESSAGE( "--esc as char" );
    UTF_CHECK( ! cmdln.m_esc.hasValue() );
    UTF_CHECK( cmdln.m_esc.hasDefaultValue() );
    UTF_CHECK( cmdln.m_esc.isReadable() );
    UTF_CHECK( ! cmdln.m_esc.isRequired() );
    UTF_CHECK( ! cmdln.m_esc.isMultiValue() );
    UTF_CHECK( ! cmdln.m_esc.isHidden() );
    UTF_CHECK( ! cmdln.m_esc.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_esc.getDefaultValue(), '\x1B' );
    UTF_CHECK_EQUAL( cmdln.m_esc.getValue(), '\x1B' );
    UTF_CHECK_EQUAL( cmdln.m_esc.getValue( '!' ), '!' );

    UTF_MESSAGE( "--complex as complex<double>" );
    UTF_CHECK( cmdln.m_complex.hasValue() );
    UTF_CHECK( ! cmdln.m_complex.hasDefaultValue() );
    UTF_CHECK( cmdln.m_complex.isReadable() );
    UTF_CHECK( ! cmdln.m_complex.isRequired() );
    UTF_CHECK( ! cmdln.m_complex.isMultiValue() );
    UTF_CHECK( ! cmdln.m_complex.isHidden() );
    UTF_CHECK( ! cmdln.m_complex.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_complex.getValue(), std::complex< double >( 1.2, 3.4 ) );
    UTF_CHECK_EQUAL( cmdln.m_complex.getValue( std::complex< double >( 0, -1 ) ), std::complex< double >( 1.2, 3.4 ) );

    UTF_MESSAGE( "--hidden as string" );
    UTF_CHECK( cmdln.m_hidden.hasValue() );
    UTF_CHECK( ! cmdln.m_hidden.hasDefaultValue() );
    UTF_CHECK( cmdln.m_hidden.isReadable() );
    UTF_CHECK( ! cmdln.m_hidden.isRequired() );
    UTF_CHECK( ! cmdln.m_hidden.isMultiValue() );
    UTF_CHECK( cmdln.m_hidden.isHidden() );
    UTF_CHECK( ! cmdln.m_hidden.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_hidden.getValue(), "secret" );
    UTF_CHECK_EQUAL( cmdln.m_hidden.getValue( "test" ), "secret" );
    UTF_CHECK_EQUAL( cmdln.m_hidden.getValue( std::string("string") ), "secret" );

    UTF_MESSAGE( "--missing" );
    UTF_CHECK( ! cmdln.m_missing.hasValue() );   // option m_missing has not been added so it should never have a value
    UTF_CHECK( cmdln.m_missing.hasDefaultValue() );
    UTF_CHECK( cmdln.m_missing.isReadable() );
    UTF_CHECK( ! cmdln.m_missing.isRequired() );
    UTF_CHECK( ! cmdln.m_missing.isMultiValue() );
    UTF_CHECK( ! cmdln.m_missing.isHidden() );
    UTF_CHECK( ! cmdln.m_missing.isPositional() );
    UTF_CHECK_EQUAL( cmdln.m_missing.getDefaultValue(), false );
    UTF_CHECK_EQUAL( cmdln.m_missing.getValue(), false );

    UTF_MESSAGE( "***************** end CmdLine_Parse_Class tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_ParserInstanceIsSingleUse )
{
    UTF_MESSAGE( "***************** CmdLine_ParserInstanceIsSingleUse tests *****************\n" );

    /*
     * NOTE: this test case intentionally pins a *restriction*, it does not endorse it.
     *
     * A CmdLineBase instance is single-use. OptionBase::m_hasValue is set by the notifier
     * installed in Option::getSemantic() and is never cleared, and Option::m_value is never
     * reset either, so a second parseCommandLine() call on the same instance observes the state
     * left behind by the first one - the stale hasValue() bits satisfy checkRequiredOptions()
     * and getValue() keeps returning the previous parse's value instead of the default.
     *
     * A parser instance must therefore be constructed fresh for every command line which is to
     * be parsed, which is what all the negative expectations in this module rely on.
     *
     * If parseCommandLine() is ever made re-entrant (i.e. resetting m_hasValue and m_value at
     * the top) then this is the case where that decision is recorded and it must be rewritten.
     */

    CmdLineTest cmdln;

    UTF_MESSAGE( "* parsing the first command line, supplying all the required options" );
    UTF_REQUIRE_NO_THROW( cmdln.parseCommandLine( "run --number1 7 --factor1 1.5 --opt2 first" ) );

    UTF_REQUIRE( cmdln.m_command.hasValue() );
    UTF_REQUIRE_EQUAL( cmdln.m_command.getValue(), "run" );
    UTF_REQUIRE( cmdln.m_number1.hasValue() );
    UTF_REQUIRE_EQUAL( cmdln.m_number1.getValue(), 7 );
    UTF_REQUIRE( cmdln.m_factor1.hasValue() );
    UTF_REQUIRE_EQUAL( cmdln.m_factor1.getValue(), 1.5 );
    UTF_REQUIRE( cmdln.m_opt2.hasValue() );
    UTF_REQUIRE_EQUAL( cmdln.m_opt2.getValue(), "first" );

    /*
     * The key expectation: parsing an empty command line on the *same* instance does not throw,
     * even though all three required options are absent from it. Contrast this with
     * CmdLine_MissingRequiredOptionsMessage below, where the very same empty command line does
     * throw "required but missing" on a freshly constructed instance.
     */

    UTF_MESSAGE( "* re-parsing an empty command line on the same instance" );
    UTF_REQUIRE_NO_THROW( cmdln.parseCommandLine( "" ) );

    UTF_REQUIRE( cmdln.m_command.hasValue() );
    UTF_REQUIRE( cmdln.m_number1.hasValue() );
    UTF_REQUIRE( cmdln.m_factor1.hasValue() );

    /*
     * The values from the first parse survive - they are not reset to the default values
     */

    UTF_REQUIRE_EQUAL( cmdln.m_number1.getValue(), 7 );
    UTF_REQUIRE_EQUAL( cmdln.m_factor1.getValue(), 1.5 );
    UTF_REQUIRE_EQUAL( cmdln.m_opt2.getValue(), "first" );

    UTF_MESSAGE( "***************** end CmdLine_ParserInstanceIsSingleUse tests *****************\n" );
}

namespace
{
    class GlobalOptions
    {
    public:

        bl::cmdline::HelpSwitch     m_help;
        bl::cmdline::VerboseSwitch  m_verbose;
        bl::cmdline::DryRunSwitch   m_dryRun;
        BL_CMDLINE_OPTION     ( m_global,   StringOption,  "global",   "Global option" )

    protected:

        GlobalOptions( SAA_inout bl::cmdline::CommandBase* parent )
        {
            parent -> addOption( m_help, m_verbose, m_dryRun, m_global );
        }
    };

    class CommandRun : public bl::cmdline::CommandBase
    {
    public:

        const GlobalOptions&    m_globals;

        BL_CMDLINE_OPTION     ( m_how,      StringOption,       "how",          "Running style", bl::cmdline::RequiredPositional )
        BL_CMDLINE_OPTION     ( m_from,     StringOption,       "from",         "Where to run from", "here" )
        BL_CMDLINE_OPTION     ( m_to,       StringOption,       "to",           "Where to run to", bl::cmdline::Required )
        BL_CMDLINE_OPTION     ( m_speed,    DoubleOption,       "speed",        "Velocity in m/s" )
        BL_CMDLINE_OPTION     ( m_props,    MultiStringOption,  "property",     "Property <name> <value>", bl::cmdline::TwoValues )

        CommandRun(
            SAA_inout   bl::cmdline::CommandBase*   parent,
            SAA_in      const GlobalOptions&        globals
            )
            :
            bl::cmdline::CommandBase( parent, "run", "CmdLineModeTest @FULLNAME@ <how> [options]" ),
            m_globals( globals )
        {
            addOption( m_how, m_from, m_to, m_speed, m_props );
        }

        virtual bl::cmdline::Result execute() OVERRIDE
        {
            UTF_MESSAGE( "CommandRun::execute" );

            if( m_globals.m_verbose.getValue() )
            {
                UTF_MESSAGE( BL_MSG() <<
                    "Running \"" << m_how.getValue() <<
                    "\" from \"" << m_from.getValue() <<
                    "\" to \"" << m_to.getValue() <<
                    "\" at " << m_speed.getValue( 1.0 ) << " m/s\n"
                );
            }

            return 0;
        }
    };

    class CommandMail : public bl::cmdline::CommandBase
    {
    public:

        const GlobalOptions&    m_globals;

        BL_CMDLINE_OPTION     ( m_subject,  StringOption,       "subject",      "E-mail message subject", "No subject" )
        BL_CMDLINE_OPTION     ( m_to,       MultiStringOption,  "to",           "List of e-mail recipients", bl::cmdline::RequiredMultiValue )
        BL_CMDLINE_OPTION     ( m_body,     StringOption,       "body",         "Message body file name (or stdin)", "-" )
        BL_CMDLINE_OPTION     ( m_urgent,   BoolSwitch,         "urgent,U",     "Mark the e-mail as urgent" )

        CommandMail(
            SAA_inout   bl::cmdline::CommandBase*   parent,
            SAA_in      const GlobalOptions&        globals
            )
            :
            bl::cmdline::CommandBase( parent, "mail", "CmdLineModeTest @FULLNAME@ [options]" ),
            m_globals( globals )
        {
            addOption( m_subject, m_to, m_body, m_urgent );
        }

        virtual bl::cmdline::Result execute() OVERRIDE
        {
            UTF_MESSAGE( "CommandMail::execute" );

            if( m_globals.m_verbose.getValue() )
            {
                UTF_MESSAGE( BL_MSG() <<
                    "Sending " << (m_urgent.getValue() ? "an urgent" : "a normal") <<
                    " e-mail with subject \"" << m_subject.getValue() <<
                    "\" to " << vector2string( m_to.getValue(), "\"", "\"" ) <<
                    " with body in \"" << m_body.getValue() << "\"\n"
                );
            }

            return 0;
        }
    };

    class CommandCreateObject : public bl::cmdline::CommandBase
    {
    public:

        const GlobalOptions&    m_globals;

        BL_CMDLINE_OPTION     ( m_name,     StringOption,       "name",         "Object name", bl::cmdline::RequiredPositional )
        BL_CMDLINE_OPTION     ( m_force,    BoolSwitch,         "force,f",      "Force overwrite if the object already exists" )

        CommandCreateObject(
            SAA_inout   bl::cmdline::CommandBase*   parent,
            SAA_in      const GlobalOptions&        globals
            )
            :
            bl::cmdline::CommandBase( parent, "object", "CmdLineModeTest @FULLNAME@ <name> [options]" ),
            m_globals( globals )
        {
            addOption( m_name, m_force );
        }

        virtual bl::cmdline::Result execute() OVERRIDE
        {
            UTF_MESSAGE( "CommandCreateObject::execute" );

            if( m_globals.m_verbose.getValue() )
            {
                UTF_MESSAGE( BL_MSG() <<
                    "Creating object \"" << m_name.getValue() <<
                    "\"" << ( m_force.getValue() ? " (forcing)" : "" ) << "\n"
                );
            }

            return 0;
        }
    };

    class CommandCreateUser : public bl::cmdline::CommandBase
    {
    public:

        const GlobalOptions&    m_globals;

        BL_CMDLINE_OPTION     ( m_name,     MultiStringOption,  "name",         "User name", bl::cmdline::RequiredPositional | bl::cmdline::TwoValues )
        BL_CMDLINE_OPTION     ( m_password, StringOption,       "password,p",   "User password")

        CommandCreateUser(
            SAA_inout   bl::cmdline::CommandBase*   parent,
            SAA_in      const GlobalOptions&        globals
            )
            :
            bl::cmdline::CommandBase( parent, "user", "CmdLineModeTest @FULLNAME@ <first name> <last name> [options]" ),
            m_globals( globals )
        {
            addOption( m_name, m_password );
        }

        virtual bl::cmdline::Result execute() OVERRIDE
        {
            UTF_MESSAGE( "CommandCreateUser::execute" );

            if( m_globals.m_verbose.getValue() )
            {
                UTF_MESSAGE( BL_MSG() <<
                    "Creating user \"" << bl::str::join( m_name.getValue(), "," ) <<
                    "\" with password \"" << m_password.getValue() << "\"\n"
                );
            }

            return 0;
        }
    };

    class CommandCreate : public bl::cmdline::CommandBase
    {
    public:

        const GlobalOptions&    m_globals;
        CommandCreateObject     m_commandCreateObject;
        CommandCreateUser       m_commandCreateUser;

        CommandCreate(
            SAA_inout   bl::cmdline::CommandBase*   parent,
            SAA_in      const GlobalOptions&        globals
            )
            :
            bl::cmdline::CommandBase( parent, "create" /* no caption & no options -> empty help */ ),
            m_globals( globals ),
            m_commandCreateObject( this, globals ),
            m_commandCreateUser  ( this, globals )
        {
        }

        virtual bl::cmdline::Result execute() OVERRIDE
        {
            UTF_MESSAGE( "CommandCreate::execute" );

            UTF_MESSAGE( "Sub command is not specified. See the help:\n" );
            UTF_MESSAGE( helpMessage() );

            return -1;
        }
    };

    class CmdLineModeTest : public bl::cmdline::CmdLineBase, public GlobalOptions
    {
    public:

        CommandRun              m_commandRun;
        CommandMail             m_commandMail;
        CommandCreate           m_commandCreate;

        CmdLineModeTest()
            :
            bl::cmdline::CmdLineBase( "CmdLineModeTest <command> [options]" ),
            GlobalOptions  ( this ),
            m_commandRun   ( this, *this ),
            m_commandMail  ( this, *this ),
            m_commandCreate( this, *this )
        {
        }

        virtual bl::cmdline::Result execute() OVERRIDE
        {
            /*
             * If we are executing here, no command has been specified. Just display the help message.
             */

            UTF_MESSAGE( helpMessage() );

            return 1;
        }

        virtual bl::cmdline::Result executeCommand( bl::cmdline::CommandBase* command ) OVERRIDE
        {
            if( m_help.getValue() )
            {
                return execute().getReturnCode();
            }
            else
            {
                return command -> execute();
            }
        }
    };

} // __unnamed

UTF_AUTO_TEST_CASE( CmdLine_Parse_CommandMode )
{
    UTF_MESSAGE( "***************** CmdLine_Parse_CommandMode tests *****************\n" );

    UTF_MESSAGE( "* creating command line options class" );
    CmdLineModeTest cmdln;

    UTF_MESSAGE( "* help message:" );
    UTF_MESSAGE( cmdln.helpMessage() );

    /*
     * NOTE: every negative expectation below gets its own freshly constructed parser instance
     * because a CmdLineBase instance is single-use - see CmdLine_ParserInstanceIsSingleUse
     */

    UTF_MESSAGE( "* checking invalid command lines" );

    {
        CmdLineModeTest c;

        const char * args[] = { "unittest", "--from", "start", "--to", "finish", "--speed", "8.5", "-v" };

        UTF_REQUIRE_THROW(
            c.parseCommandLine( BL_ARRAY_SIZE( args ), args ),
            bl::po::unknown_option
            );
    }

    {
        CmdLineModeTest c;

        const char * args[] = { "unittest", "--global=test", "-?", "extra" };

        UTF_REQUIRE_THROW(
            c.parseCommandLine( BL_ARRAY_SIZE( args ), args ),
            bl::po::too_many_positional_options_error
            );
    }

    {
        CmdLineModeTest c;

        const char * args[] = { "unittest", "invalid" };

        UTF_REQUIRE_THROW(
            c.parseCommandLine( BL_ARRAY_SIZE( args ), args ),
            bl::UserMessageException
            );
    }

    UTF_MESSAGE( "* parsing empty command line" );
    auto command = cmdln.parseCommandLine( "" );
    UTF_REQUIRE( command != nullptr );

    UTF_MESSAGE( "* variable values:" );
    dumpVariablesMap( cmdln.getParsedArgs() );

    UTF_MESSAGE( "* command: <empty>" );
    UTF_CHECK_EQUAL( command -> getFullName(), "" );
    UTF_CHECK( ! cmdln.m_help.getValue() );
    UTF_CHECK( ! cmdln.m_verbose.getValue() );
    UTF_CHECK( ! cmdln.m_dryRun.getValue() );
    UTF_CHECK_EQUAL( cmdln.m_global.getValue(), "" );
    UTF_CHECK_EQUAL( cmdln.m_global.getValue( "default" ), "default" );

    UTF_MESSAGE( "***************** end CmdLine_Parse_CommandMode tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_Parse_CommandMode_Run )
{
    UTF_MESSAGE( "***************** CmdLine_Parse_CommandMode_Run tests *****************\n" );

    CmdLineModeTest cmdln;

    const char * args[] = { "unittest", "run", "quickly", "--from", "start", "--to", "finish", "--global=test",
                            "--speed", "8.5", "--property", "color", "green", "--property", "number", "42", "-v" };

    UTF_MESSAGE( "* parsing command line" );
    auto command = cmdln.parseCommandLine( BL_ARRAY_SIZE( args ), args );
    UTF_REQUIRE( command != nullptr );

    UTF_MESSAGE( "* variable values:" );
    dumpVariablesMap( cmdln.getParsedArgs() );

    UTF_MESSAGE( "* command: run" );
    UTF_REQUIRE_EQUAL( command -> getFullName(), "run" );
    UTF_CHECK_EQUAL( command -> getHelpCaption(), "CmdLineModeTest run <how> [options]" );

    const auto& run = *( CommandRun* )( command );
    UTF_CHECK( ! run.m_globals.m_help.getValue() );
    UTF_CHECK( run.m_globals.m_verbose.getValue() );
    UTF_CHECK( ! run.m_globals.m_dryRun.getValue() );
    UTF_CHECK_EQUAL( run.m_globals.m_global.getValue(), "test" );
    UTF_CHECK_EQUAL( run.m_globals.m_global.getValue( "default" ), "test" );
    UTF_CHECK_EQUAL( run.m_how.getValue(), "quickly" );
    UTF_CHECK_EQUAL( run.m_from.getValue(), "start" );
    UTF_CHECK_EQUAL( run.m_to.getValue(), "finish" );
    UTF_CHECK_EQUAL( run.m_speed.getValue(), 8.5 );
    std::vector< std::string > propsExpected;
    propsExpected.push_back( "color" );
    propsExpected.push_back( "green" );
    propsExpected.push_back( "number" );
    propsExpected.push_back( "42" );
    UTF_CHECK_EQUAL( run.m_props.getValue(), propsExpected );

    UTF_MESSAGE( "* executing command" );
    cmdln.executeCommand( command );

    UTF_MESSAGE( "* checking invalid command lines" );

    /*
     * NOTE: every negative expectation below gets its own freshly constructed parser instance
     * because a CmdLineBase instance is single-use - see CmdLine_ParserInstanceIsSingleUse
     */

    /*
     * Command names are case sensitive
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "RUN" ), bl::UserMessageException ); }

    /*
     * "run" has required options "<how>" and "--to"
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run" ), bl::UserMessageException ); }
    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run quickly" ), bl::UserMessageException ); }
    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run quickly --to the hills" ), bl::po::too_many_positional_options_error ); }

    /*
     * Invalid floating option "--speed" value
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run forever --to infinity --speed=light" ), bl::po::invalid_option_value ); }

    /*
     * Option "--property" requires exactly 2 arguments
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run test --property" ), bl::po::invalid_command_line_syntax ); }
    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run test --property color" ), bl::po::invalid_command_line_syntax ); }
    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "run test --property color light blue" ), bl::po::too_many_positional_options_error ); }

    UTF_MESSAGE( "***************** end CmdLine_Parse_CommandMode_Run tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_Parse_CommandMode_Mail )
{
    UTF_MESSAGE( "***************** CmdLine_Parse_CommandMode_Mail tests *****************\n" );

    CmdLineModeTest cmdln;

    const char * args[] = { "unittest", "mail", "-Uvn", "--to", "users@world.com", "admin@heaven.org",
                            "--subject", "The universe is rebooting",
                            "--global=universe", "--body", "mail.txt" };

    UTF_MESSAGE( "* parsing command line" );
    auto command = cmdln.parseCommandLine( BL_ARRAY_SIZE( args ), args );
    UTF_REQUIRE( command != nullptr );

    UTF_MESSAGE( "* variable values:" );
    dumpVariablesMap( cmdln.getParsedArgs() );

    UTF_MESSAGE( "* command: mail" );
    UTF_REQUIRE_EQUAL( command -> getFullName(), "mail" );
    UTF_CHECK_EQUAL( command -> getHelpCaption(), "CmdLineModeTest mail [options]" );

    const auto& mail = *( CommandMail* )( command );
    UTF_CHECK( ! mail.m_globals.m_help.getValue() );
    UTF_CHECK( mail.m_globals.m_verbose.getValue() );
    UTF_CHECK( mail.m_globals.m_dryRun.getValue() );
    UTF_CHECK_EQUAL( mail.m_globals.m_global.getValue(), "universe" );
    UTF_CHECK_EQUAL( mail.m_globals.m_global.getValue( "default" ), "universe" );
    UTF_CHECK_EQUAL( mail.m_subject.getValue(), "The universe is rebooting" );
    std::vector< std::string > toExpected;
    toExpected.push_back( "users@world.com" );
    toExpected.push_back( "admin@heaven.org" );
    UTF_CHECK_EQUAL( mail.m_to.getValue(), toExpected );
    UTF_CHECK_EQUAL( mail.m_body.getValue(), "mail.txt" );
    UTF_CHECK_EQUAL( mail.m_urgent.getValue(), true );

    UTF_MESSAGE( "* executing command" );
    cmdln.executeCommand( command );

    UTF_MESSAGE( "***************** end CmdLine_Parse_CommandMode_Mail tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_Parse_CommandMode_Create )
{
    UTF_MESSAGE( "***************** CmdLine_Parse_CommandMode_Create tests *****************\n" );

    {
        const std::string cmdLine = "create -n --global object";
        CmdLineModeTest cmdln;

        UTF_MESSAGE( "* parsing command line: " + cmdLine );
        auto command = cmdln.parseCommandLine( cmdLine );
        UTF_REQUIRE( command != nullptr );

        UTF_MESSAGE( "* variable values:" );
        dumpVariablesMap( cmdln.getParsedArgs() );

        UTF_MESSAGE( "* command: create" );
        UTF_REQUIRE_EQUAL( command -> getFullName(), "create" );
        UTF_CHECK_EQUAL( command -> getHelpCaption(), "" );

        const auto& create = *( CommandCreate* )( command );
        UTF_CHECK( ! create.m_globals.m_help.getValue() );
        UTF_CHECK( ! create.m_globals.m_verbose.getValue() );
        UTF_CHECK( create.m_globals.m_dryRun.getValue() );
        UTF_CHECK_EQUAL( create.m_globals.m_global.getValue(), "object" );
        UTF_CHECK_EQUAL( create.m_globals.m_global.getValue( "default" ), "object" );

        UTF_MESSAGE( "* executing command" );
        cmdln.executeCommand( command );
    }

    {
        const std::string cmdLine = "create object -f F00-BAR --verbose";
        CmdLineModeTest cmdln;

        UTF_MESSAGE( "* parsing command line: " + cmdLine );
        auto command = cmdln.parseCommandLine( cmdLine );
        UTF_REQUIRE( command != nullptr );

        UTF_MESSAGE( "* variable values:" );
        dumpVariablesMap( cmdln.getParsedArgs() );

        UTF_MESSAGE( "* command: create object" );
        UTF_REQUIRE_EQUAL( command -> getFullName(), "create object" );
        UTF_CHECK_EQUAL( command -> getHelpCaption(), "CmdLineModeTest create object <name> [options]" );

        const auto& create = *( CommandCreateObject* )( command );
        UTF_CHECK( ! create.m_globals.m_help.getValue() );
        UTF_CHECK( create.m_globals.m_verbose.getValue() );
        UTF_CHECK( ! create.m_globals.m_dryRun.getValue() );
        UTF_CHECK_EQUAL( create.m_name.getValue(), "F00-BAR" );
        UTF_CHECK_EQUAL( create.m_force.getValue(), true );

        UTF_MESSAGE( "* executing command" );
        cmdln.executeCommand( command );
    }

    {
        const std::string cmdLine = "create user -v John Smith -p password --dryrun";
        CmdLineModeTest cmdln;

        UTF_MESSAGE( "* parsing command line: " + cmdLine );
        auto command = cmdln.parseCommandLine( cmdLine );
        UTF_REQUIRE( command != nullptr );

        UTF_MESSAGE( "* variable values:" );
        dumpVariablesMap( cmdln.getParsedArgs() );

        UTF_MESSAGE( "* command: create user" );
        UTF_REQUIRE_EQUAL( command -> getFullName(), "create user" );
        UTF_CHECK_EQUAL( command -> getHelpCaption(), "CmdLineModeTest create user <first name> <last name> [options]" );

        const auto& create = *( CommandCreateUser* )( command );
        UTF_CHECK( ! create.m_globals.m_help.getValue() );
        UTF_CHECK( create.m_globals.m_verbose.getValue() );
        UTF_CHECK( create.m_globals.m_dryRun.getValue() );
        std::vector< std::string > namesExpected;
        namesExpected.push_back( "John" );
        namesExpected.push_back( "Smith" );
        UTF_CHECK_EQUAL( create.m_name.getValue(), namesExpected );
        UTF_CHECK_EQUAL( create.m_password.getValue(), "password" );

        UTF_MESSAGE( "* executing command" );
        cmdln.executeCommand( command );
    }

    UTF_MESSAGE( "* checking invalid command lines" );

    /*
     * NOTE: every negative expectation below gets its own freshly constructed parser instance
     * because a CmdLineBase instance is single-use - see CmdLine_ParserInstanceIsSingleUse
     */

    /*
     * Command names are case sensitive
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "CREATE" ), bl::UserMessageException ); }
    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "create USER username" ), bl::UserMessageException ); }

    /*
     * Positional options do not enforce the minimal number of arguments, hence no exception is thrown:
     *
     * UTF_REQUIRE_THROW( cmdln.parseCommandLine( "create user John" ), bl::po::required_option );
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "create user John von Neumann" ), bl::po::too_many_positional_options_error ); }

    /*
     * Invalid sub-command name
     */

    { CmdLineModeTest c; UTF_REQUIRE_THROW( c.parseCommandLine( "create something --else" ), bl::UserMessageException ); }

    /*
     * "create user" has a required positional option "name" but "--help" overrides it
     */

    {
        CmdLineModeTest c;

        UTF_REQUIRE_NO_THROW( c.parseCommandLine( "create user --help" ) );
        UTF_CHECK( c.m_help.getValue() );

        /*
         * The required positional option really was missing - the parse succeeded only
         * because of the Override flag on the help switch
         */

        UTF_CHECK( ! c.m_commandCreate.m_commandCreateUser.m_name.hasValue() );
    }

    UTF_MESSAGE( "***************** end CmdLine_Parse_CommandMode_Create tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_MissingRequiredOptionsMessage )
{
    UTF_MESSAGE( "***************** CmdLine_MissingRequiredOptionsMessage tests *****************\n" );

    /*
     * NOTE: every sub-block below gets its own freshly constructed parser instance
     * because a CmdLineBase instance is single-use - see CmdLine_ParserInstanceIsSingleUse
     */

    UTF_MESSAGE( "* three missing required options (the plural form and all getDisplayName() branches)" );

    {
        CmdLineTest cmdln;

        UTF_REQUIRE_THROW_MESSAGE(
            cmdln.parseCommandLine( "" ),
            bl::UserMessageException,
            "Invalid command line: the options '<command>', '--number1' and '--factor1' are required but missing"
            );
    }

    UTF_MESSAGE( "* two missing required options (only the last separator is used)" );

    {
        CmdLineModeTest cmdln;

        UTF_REQUIRE_THROW_MESSAGE(
            cmdln.parseCommandLine( "run" ),
            bl::UserMessageException,
            "the options '<how>' and '--to' are required but missing"
            );
    }

    UTF_MESSAGE( "* one missing required option (the singular form)" );

    {
        CmdLineModeTest cmdln;

        UTF_REQUIRE_THROW_MESSAGE(
            cmdln.parseCommandLine( "run quickly" ),
            bl::UserMessageException,
            "the option '--to' is required but missing"
            );
    }

    UTF_MESSAGE( "* an Override option wins over the missing required options" );

    {
        CmdLineTest cmdln;

        UTF_REQUIRE_NO_THROW( cmdln.parseCommandLine( "--help" ) );
        UTF_CHECK( cmdln.m_help.getValue() );
        UTF_CHECK( ! cmdln.m_command.hasValue() );
    }

    UTF_MESSAGE( "* OptionBase::getDisplayName()" );

    {
        CmdLineTest cmdln;

        UTF_CHECK_EQUAL( cmdln.m_command.getDisplayName(), "<command>" );
        UTF_CHECK_EQUAL( cmdln.m_number1.getDisplayName(), "--number1" );
        UTF_CHECK_EQUAL( cmdln.m_factor1.getDisplayName(), "--factor1" );
        UTF_CHECK_EQUAL( cmdln.m_filenames.getDisplayName(), "<filename>" );
        UTF_CHECK_EQUAL( cmdln.m_help.getDisplayName(), "--help" );
    }

    UTF_MESSAGE( "***************** end CmdLine_MissingRequiredOptionsMessage tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_HelpMessageComposition )
{
    UTF_MESSAGE( "***************** CmdLine_HelpMessageComposition tests *****************\n" );

    /*
     * NOTE: every sub-block below gets its own freshly constructed instance because
     * setHelpMessage() mutates the command it is invoked on
     */

    UTF_MESSAGE( "* @CMDNAME@ / @FULLNAME@ expansion, including repeated occurrences of a token" );

    {
        CmdLineModeTest cmdln;

        cmdln.m_commandCreate.m_commandCreateObject.setHelpMessage( "[@FULLNAME@]-[@CMDNAME@]-[@FULLNAME@]" );

        UTF_REQUIRE_EQUAL(
            cmdln.m_commandCreate.m_commandCreateObject.helpMessage( false /* includeSubCommands */ ),
            "[create object]-[object]-[create object]"
            );
    }

    UTF_MESSAGE( "* the caption substituted for @CAPTION@ is itself token expanded" );

    {
        CmdLineModeTest cmdln;

        cmdln.m_commandCreate.m_commandCreateObject.setHelpMessage( "@CAPTION@" );

        UTF_REQUIRE_EQUAL(
            cmdln.m_commandCreate.m_commandCreateObject.helpMessage( false /* includeSubCommands */ ),
            "CmdLineModeTest create object <name> [options]"
            );
    }

    UTF_MESSAGE( "* @COMMANDS@ skips commands with an empty caption but still recurses into them" );

    {
        CmdLineModeTest cmdln;

        cmdln.setHelpMessage( "@COMMANDS@" );

        /*
         * "create" is omitted because its caption is empty, but its children are still listed,
         * and the whole list is in registration order
         */

        UTF_REQUIRE_EQUAL(
            cmdln.helpMessage( false /* includeSubCommands */ ),
            "  run\n  mail\n  create object\n  create user\n"
            );
    }

    UTF_MESSAGE( "* @OPTIONS@ renders the positional options by hand and filters out the hidden ones" );

    {
        CmdLineTest cmdln;

        /*
         * NOTE: the non-positional option block is formatted by Boost and its column layout
         * depends on getScreenColumns(), hence the substring checks rather than equality
         */

        const auto help = cmdln.helpMessage( false /* includeSubCommands */ );

        UTF_CHECK( bl::cpp::contains( help, "<command>" ) );
        UTF_CHECK( bl::cpp::contains( help, "Command: run, stop" ) );
        UTF_CHECK( bl::cpp::contains( help, "--opt1" ) );
        UTF_CHECK( ! bl::cpp::contains( help, "You should never see this option" ) );
        UTF_CHECK( ! bl::cpp::contains( help, "--hidden" ) );
    }

    UTF_MESSAGE( "* a leaf command appends the root options; a command with nothing to say renders nothing" );

    {
        CmdLineModeTest cmdln;

        const auto runHelp = cmdln.m_commandRun.helpMessage( false /* includeSubCommands */ );

        UTF_CHECK( bl::cpp::contains( runHelp, "--global" ) );
        UTF_CHECK( bl::cpp::contains( runHelp, "--dryrun" ) );

        /*
         * "create" has no caption, no help message and no options of its own, and it is not a
         * leaf either, so it renders nothing at all and reports it - this is the branch which
         * suppresses the separating blank line in the parent's helpMessage() loop
         */

        bl::cpp::SafeOutputStringStream oss;

        const auto notEmpty = cmdln.m_commandCreate.helpMessage( oss, false /* includeSubCommands */ );

        UTF_CHECK( ! notEmpty );
        UTF_CHECK( oss.str().empty() );
    }

    UTF_MESSAGE( "***************** end CmdLine_HelpMessageComposition tests *****************\n" );
}

namespace
{
    /**
     * Write a list file with the exact content provided and return its path
     *
     * The file is opened in binary mode, so the content is written verbatim
     */

    std::string writeListFile(
        SAA_in      const utest::TestDirectory&     dir,
        SAA_in      const std::string&              name,
        SAA_in      const std::string&              content
        )
    {
        const auto path = dir.testFile( name );

        {
            bl::fs::SafeOutputFileStreamWrapper file( path );

            file.stream() << content;
            file.flushAndCheck();
        }

        return path.string();
    }

} // __unnamed

UTF_AUTO_TEST_CASE( CmdLine_ExpandListAndLoadListFile )
{
    UTF_MESSAGE( "***************** CmdLine_ExpandListAndLoadListFile tests *****************\n" );

    utest::TestDirectory dir;

    const auto list1 = writeListFile( dir, "list1.txt", "  alpha  \n\nbeta\n   \ngamma\n" );
    const auto list2 = writeListFile( dir, "list2.txt", "delta\r\nepsilon\r\n" );
    const auto list3 = writeListFile( dir, "list3.txt", "@list1.txt\nzeta" );

    UTF_MESSAGE( "* expandList() rejects empty values" );

    {
        std::vector< std::string > values;
        values.push_back( std::string() );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::cmdline::CommandBase::expandList( values ),
            bl::UserMessageException,
            "List may not contain empty values"
            );
    }

    UTF_MESSAGE( "* loadListFile() requires a file name to follow the '@'" );

    {
        std::vector< std::string > values;
        values.push_back( "@" );

        UTF_REQUIRE_THROW_MESSAGE(
            bl::cmdline::CommandBase::expandList( values ),
            bl::UserMessageException,
            "List @file name must be specified"
            );
    }

    UTF_MESSAGE( "* loadListFile() fails if the list file does not exist" );

    {
        std::vector< std::string > result;

        UTF_REQUIRE_THROW_MESSAGE(
            bl::cmdline::CommandBase::loadListFile( result, dir.testFile( "nope.txt" ).string() ),
            bl::SystemException,
            "Cannot open or create file"
            );
    }

    UTF_MESSAGE( "* '@file' entries are expanded in place; lines are trimmed and blank ones skipped" );

    {
        std::vector< std::string > values;
        values.push_back( "x" );
        values.push_back( "@" + list1 );
        values.push_back( "y" );

        std::vector< std::string > expected;
        expected.push_back( "x" );
        expected.push_back( "alpha" );
        expected.push_back( "beta" );
        expected.push_back( "gamma" );
        expected.push_back( "y" );

        UTF_REQUIRE_EQUAL( bl::cmdline::CommandBase::expandList( values ), expected );
    }

    UTF_MESSAGE( "* the list file is opened in binary mode, so the stray CR is removed by the trim" );

    {
        std::vector< std::string > values;
        values.push_back( "@" + list2 );

        std::vector< std::string > expected;
        expected.push_back( "delta" );
        expected.push_back( "epsilon" );

        UTF_REQUIRE_EQUAL( bl::cmdline::CommandBase::expandList( values ), expected );
    }

    UTF_MESSAGE( "* expansion is not recursive and a missing trailing newline is not an error" );

    {
        std::vector< std::string > values;
        values.push_back( "@" + list3 );

        std::vector< std::string > expected;
        expected.push_back( "@list1.txt" );
        expected.push_back( "zeta" );

        UTF_REQUIRE_EQUAL( bl::cmdline::CommandBase::expandList( values ), expected );
    }

    UTF_MESSAGE( "* a list with no '@file' entries is copied through without touching the file system" );

    {
        const std::vector< std::string > empty;

        UTF_REQUIRE( bl::cmdline::CommandBase::expandList( empty ).empty() );

        std::vector< std::string > values;
        values.push_back( "a" );
        values.push_back( "b" );

        UTF_REQUIRE_EQUAL( bl::cmdline::CommandBase::expandList( values ), values );
    }

    UTF_MESSAGE( "***************** end CmdLine_ExpandListAndLoadListFile tests *****************\n" );
}

UTF_AUTO_TEST_CASE( CmdLine_CommandTreeRegistration )
{
    UTF_MESSAGE( "***************** CmdLine_CommandTreeRegistration tests *****************\n" );

    UTF_MESSAGE( "* adding the very same option object twice is rejected" );

    {
        CmdLineModeTest cmdln;

        const auto sizeBefore = cmdln.getAllOptions().size();

        UTF_REQUIRE_THROW_MESSAGE(
            cmdln.addOption( cmdln.m_verbose ),
            bl::UnexpectedException,
            "Duplicate command line option: 'verbose,v'"
            );

        /*
         * The lookup map insert runs first, so the options vector must not have been touched
         */

        UTF_REQUIRE_EQUAL( cmdln.getAllOptions().size(), sizeBefore );
    }

    UTF_MESSAGE( "* adding a different option object with an already registered name is rejected" );

    {
        CmdLineModeTest cmdln;

        const auto sizeBefore = cmdln.getAllOptions().size();

        bl::cmdline::VerboseSwitch duplicate;

        UTF_REQUIRE_THROW_MESSAGE(
            cmdln.addOption( duplicate ),
            bl::UnexpectedException,
            "Duplicate command line option: 'verbose,v'"
            );

        UTF_REQUIRE_EQUAL( cmdln.getAllOptions().size(), sizeBefore );

        /*
         * The option which registered the name first still wins the lookup
         */

        UTF_REQUIRE( cmdln.findOption( "verbose,v" ) == &cmdln.m_verbose );
    }

    UTF_MESSAGE( "* registering a second command with an already registered name is rejected" );

    {
        CmdLineModeTest cmdln;

        /*
         * The nested command constructor is protected, hence the derived type
         */

        struct DuplicateCommand : public bl::cmdline::CommandBase
        {
            DuplicateCommand( SAA_inout bl::cmdline::CommandBase* parent )
                :
                bl::cmdline::CommandBase( parent, "run" )
            {
            }
        };

        /*
         * addCommand() throws from inside the child's constructor, so the child is never
         * constructed and its destructor (which would unregister it) never runs
         */

        UTF_REQUIRE_THROW_MESSAGE(
            DuplicateCommand duplicate( &cmdln ),
            bl::UnexpectedException,
            "Duplicate command: 'run'"
            );

        UTF_REQUIRE( cmdln.findCommand( "run" ) == &cmdln.m_commandRun );
    }

    UTF_MESSAGE( "* findOption() parent lookup and getRootCommand()" );

    {
        CmdLineModeTest cmdln;

        UTF_REQUIRE( cmdln.m_commandRun.findOption( "global" ) == nullptr );
        UTF_REQUIRE( cmdln.m_commandRun.findOption( "global", true /* lookupParent */ ) == &cmdln.m_global );

        UTF_REQUIRE( cmdln.m_commandRun.getRootCommand() == &cmdln );
        UTF_REQUIRE( cmdln.getRootCommand() == &cmdln );
    }

    UTF_MESSAGE( "* a nested command unregisters itself from its parent when it is destroyed" );

    {
        CmdLineModeTest cmdln;

        struct TempCommand : public bl::cmdline::CommandBase
        {
            TempCommand( SAA_inout bl::cmdline::CommandBase* parent )
                :
                bl::cmdline::CommandBase( parent, "temp" )
            {
            }
        };

        {
            TempCommand temp( &cmdln.m_commandCreate );

            UTF_REQUIRE( cmdln.m_commandCreate.findCommand( "temp" ) == &temp );
        }

        UTF_REQUIRE( cmdln.m_commandCreate.findCommand( "temp" ) == nullptr );

        /*
         * Both containers must stay consistent - "object" and "user" survive the removal
         */

        UTF_REQUIRE( cmdln.m_commandCreate.hasCommands() );
        UTF_REQUIRE( cmdln.m_commandCreate.findCommand( "object" ) == &cmdln.m_commandCreate.m_commandCreateObject );
        UTF_REQUIRE( cmdln.m_commandCreate.findCommand( "user" ) == &cmdln.m_commandCreate.m_commandCreateUser );
    }

    UTF_MESSAGE( "* removeOption() with the very object which registered the name" );

    {
        CmdLineTest cmdln;

        const auto sizeBefore = cmdln.getAllOptions().size();

        UTF_REQUIRE( cmdln.findOption( "opt2" ) == &cmdln.m_opt2 );

        cmdln.removeOption( cmdln.m_opt2 );

        UTF_REQUIRE_EQUAL( cmdln.getAllOptions().size(), sizeBefore - 1 );
        UTF_REQUIRE( cmdln.findOption( "opt2" ) == nullptr );

        /*
         * Removing an option whose name was never registered is a no-op
         */

        UTF_REQUIRE_NO_THROW( cmdln.removeOption( cmdln.m_missing ) );
        UTF_REQUIRE_EQUAL( cmdln.getAllOptions().size(), sizeBefore - 1 );
    }

    UTF_MESSAGE( "***************** end CmdLine_CommandTreeRegistration tests *****************\n" );
}

