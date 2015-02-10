#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstdint>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/thread/future.hpp>
#include <boost/chrono.hpp>

#include <stdexcept>
#include <csignal>
#include <future>
#include <chrono>

//Boost namespaces
namespace po = boost::program_options;
using boost::filesystem::path;
using boost::format;

//Preparation for signal handling - not used yet
namespace
{
    volatile std::sig_atomic_t gSignalStatus;
}

void signal_handler(int signal)
{
    gSignalStatus = signal;
}


//Data stored in 32 bit  (4 char) words
typedef std::uint32_t word;
constexpr std::size_t wordlength = sizeof( word );

//Interesting words at known positions
word event_length(  const std::vector<word>&  buf ){ return buf[1]; }
word burst_id( const std::vector<word>&  buf ){ return buf[2]; }
word trigger_word( const std::vector<word>&  buf ){ return buf[4]; }
word trig_key( const std::vector<word>& buf ){ return trigger_word( buf ) % 256; }

//Print in hex form: cout << hex % my_number ;
boost::format hex("0x%08x");
boost::format hex2("0x%02x");

//Event pretty printer
void print_event( const std::vector<word> &buf, std::ostream& log )
{
    log << "Event length: " <<  event_length(buf) << std::endl;
    log << "         Top: " <<  hex % buf.front() << std::endl;
    log << "       Burst: " <<  burst_id(buf ) << std::endl;
    log << "Trigger word: " <<  hex % trigger_word(buf) << std::endl;
    log << "        Tail: " <<  hex % buf.back() << std::endl;
}

//Load event starting from current file position
void load_event( std::ifstream& ifs, std::vector<word>& buffer, std::ostream& log )
{
    //event length is second word so seek forward 1 word
    ifs.seekg( wordlength, std::ios_base::cur);
    word event_length;
    ifs.read( reinterpret_cast<char*>(&event_length),  wordlength );

    //Buffer should hold the entire event
    buffer.resize(event_length );
    ifs.seekg( -2*wordlength, std::ios_base::cur);
    ifs.read( reinterpret_cast<char*>( buffer.data() ) , event_length * wordlength );
}

//Move file position to start of last event
void find_last_event( std::ifstream& ifs, std::vector<word>& buffer, std::ostream& log )
{
    //Read last word
    ifs.seekg( -wordlength, std::ios_base::end );
    word last_word;
    ifs.read( reinterpret_cast<char*>(&last_word),  wordlength );

    //Start of event is last word with 62 prepended
    word key_word = last_word + 0x62000000;

    //Hunt for key_word one buffer at a time working backwards
    std::size_t bufsize = buffer.size();
    int nchunk = 1;
    while( true )
    {
        //rewind to start of buffer, nchunks from the end
        ifs.seekg( -nchunk * bufsize * wordlength, std::ios_base::end );
        ifs.read(  reinterpret_cast<char*>(buffer.data()),  bufsize*wordlength );

        //look for key_word in this buffer
        std::vector<word>::iterator found = std::find( begin( buffer ), end( buffer), key_word );
        if ( found != end( buffer ) )
        {
            std::size_t bufpos = std::distance( begin( buffer ), found );
            ifs.seekg( bufpos * wordlength  - nchunk * bufsize *  wordlength , std::ios_base::end );
            break;
        }
        else
        {
            ++nchunk;
        }

        if ( nchunk * bufsize > 200000)
        {
            throw std::runtime_error( "No key word match in last 200000 words" );
        }
    }

    log << "\nSearched " << nchunk << " chunks of " << bufsize << " words.\n";
}

//true means EOB was found, false means it wasn't
bool process_file( std::string input, std::string output, std::ostream& log )
{
    //Static buffers which can be reused each time
    static std::vector<word> first_buffer(10000);
    static std::vector<word> last_buffer(10000);

    log << "input: " << input << "\n";

    //Mangle the filename if it's on castor or eos
    if ( input.find( "castor" ) != std::string::npos 
            &&  input.find( "castorpublic" ) == std::string::npos )
    {
        input = "root://castorpublic.cern.ch//" + input +"?svcClass=na62";
        log << "input: " << input << "\n";
    }
    if ( input.find( "eos" ) != std::string::npos 
            &&  input.find( "eosna62" ) == std::string::npos )
    {
        input = "root://eosna62.cern.ch//" + input;
        log << "input: " << input << "\n";
    }

    log << "output: " << output << "\n";

    //open the file - it's done in a separate thread so we can time it
    //and print a message if it seems to have hung. There's no safe way 
    //to kill the thread though, but at lease we get a message and 
    //we can hit C^C

    std::ifstream ifs;
    ifs.exceptions(std::ios::failbit | std::ios::badbit);
    std::cout << "Opening input file..." << std::endl;

    boost::packaged_task<bool> packaged_file( 
            [&ifs, input](){ 
            std::cout << "...lambda open..." << std::endl;
            ifs.open( input); return ifs.is_open(); });

    boost::unique_future<bool> future_file = packaged_file.get_future();
    boost::thread file_open_thread( boost::move(packaged_file) );
    boost::chrono::milliseconds wait_time(5000);
    boost::future_status file_status = future_file.wait_for( wait_time );

    if ( file_status == boost::future_status::timeout )
    {
        std::cout << "Timeout on open!" << std::endl;
        return false;
    }

    std::cout << "done!" << std::endl;

    //load first event
    load_event( ifs, first_buffer, log );
    log << "\nFirst event: \n";
    print_event( first_buffer, log );

    //find last event
    try
    {
        find_last_event( ifs, last_buffer, log );
    }
    catch( std::runtime_error& e)
    {
        log << e.what() << std::endl;
        log << "Skipping this file ..." << std::endl;
        return false;
    }

    //load last event
    load_event( ifs, last_buffer, log );
    log << "\nLast event: \n";
    print_event( last_buffer, log );

    //Check trigger
    word trig_bits = trig_key( last_buffer );
    log << "    SOB pair: " << hex2 % trig_bits << std::endl;

    //Write result
    if  ( trig_bits == 0x23 )
    {
        log << "\nEOB found!\n"
            "Writing 1 event and EOB to " << output << "\n";

        std::ofstream ofs;
        ofs.exceptions(std::ios::failbit | std::ios::badbit);
        ofs.open( output, std::ios::binary );
        ofs.write( reinterpret_cast<char*>( first_buffer.data() ), first_buffer.size() * wordlength );
        ofs.write( reinterpret_cast<char*>( last_buffer.data() ), last_buffer.size() * wordlength );
        return true;
    }
    else
    {
        log << "\nNo EOB found!\n"
            "Not writing anything\n" ;
        return false;
    }
}

int main(int argc, char * argv[])
{
    //Set up program options
    po::options_description general("general");
    general.add_options()
        ( "help,h", "Display this help message")
        ( "input,i", po::value<std::string>(), "Input file" )
        ( "list,l", po::value<std::string>(), "Input filelist" )
        ( "output,o", po::value<std::string>(), "Output file" )
        ( "prefix,p", po::value<std::string>(), "Output prefix" )
        ( "bursts,b", po::value<int>(), "Number of bursts to process" )
        ;

    po::variables_map vm;
    po::store( po::command_line_parser(argc, argv)
            .options( general).run() , vm);

    if ( vm.count("help" ) )
    {
        std::cout << general << std::endl;
        exit(0);
    }

    //COLLECT INPUT FILES
    std::vector<path> input_files;

    //Invidually specified
    if ( vm.count( "input" ) > 0  )
    {
        input_files.push_back( vm["input"].as<std::string>() );
    }

    //From runlists
    if ( vm.count( "list" ) > 0 ) 
    {
        std::ifstream file_list( vm["list"].as<std::string>() );
        std::copy( std::istream_iterator<std::string>( file_list ), 
                std::istream_iterator<std::string>(),
                std::back_inserter( input_files ) );
    }

    //We need at least 1 file
    if ( input_files.empty() )
    {
        std::cout << "No input files specified!" << std::endl;
        exit(0);
    }

    //Special case: for 1 file we can specify output filename directly
    if ( vm.count( "output") ==1 )
    {
        if ( input_files.size() == 1 )
        {
            std::string output_filename = vm["output"].as<std::string>();
            process_file( input_files[0].string(), output_filename, std::cout );
            return(0);
        }
        else
        {
            std::cout << "Output filename is only "
                "compatible with a single file!" << std::endl;
        }
    }

    //Remove extra input files if are processes a fixed number of bursts
    if ( vm.count( "bursts" ) )
    {
        int nbursts = vm["bursts"].as<int>();
        input_files.erase( begin(input_files ) + nbursts, end( input_files ) );
    }

    std::string fail_filename = "miss_eob.list" ;
    if ( vm.count( "prefix" ) == 1 )
    {
        fail_filename =  vm["prefix"].as<std::string>() + fail_filename;
    }

    std::ofstream  of_fail( fail_filename );

    std::cout << "Processing " << input_files.size() << 
        ( input_files.size() == 1 ? " file" : " files" ) << std::endl;

    //Otherwise loop over all files and add prefix
    for ( auto& input_filename : input_files )
    {
        std::string output_filename = "";

        if ( vm.count( "prefix" ) == 1 )
        {
            output_filename =  vm["prefix"].as<std::string>();
        }

        path inter = input_filename.filename();
        inter.replace_extension( ".eob" );

        output_filename += inter.string();
        output_filename += ".dat";
        if ( boost::filesystem::exists( output_filename) )
        {
            std::cout << "Ouput file already exists. Skipping ... " << std::endl;
            continue;
        }
        bool success = process_file( input_filename.string(), output_filename, std::cout );
        std::cout << "\n\n";
        if ( !success )
        {
            of_fail << input_filename.string() << std::endl;
        }
    }
}
