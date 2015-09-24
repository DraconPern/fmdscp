#include "config.h"

#include "Poco/Util/WinRegistryConfiguration.h"
#include "poco/Path.h"

using namespace Poco;
using namespace Poco::Util;

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/ofstd/ofstd.h"
#include "dcmtk/oflog/oflog.h"
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmimgle/dcmimage.h"
#include "dcmtk/dcmimage/diregist.h" // register support for color

#include "dcmtk/dcmdata/dcrledrg.h"	// rle decoder
#include "dcmtk/dcmjpeg/djdecode.h"	// jpeg decoder
#include "dcmtk/dcmjpeg/djencode.h" 
#include "dcmtk/dcmjpls/djdecode.h"	// jpeg-ls decoder
#include "dcmtk/dcmjpls/djencode.h" 
#include "dcmtk/dcmdata/dcrledrg.h" 
#include "dcmtk/dcmdata/dcrleerg.h" 
#include "fmjpeg2k/djencode.h"
#include "fmjpeg2k/djdecode.h"

#include "dcmtk/dcmjpls/djrparam.h"   /* for class DJLSRepresentationParameter */

// check DCMTK functionality
#if !defined(WIDE_CHAR_FILE_IO_FUNCTIONS) && defined(_WIN32)
#error "DCMTK and this program must be compiled with DCMTK_WIDE_CHAR_FILE_IO_FUNCTIONS"
#endif

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

boost::filesystem::path Config::getTempPath()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	std::string p = pConf->getString("TempPath", boost::filesystem::temp_directory_path().string());
		
	return Path::expand(p);
}

boost::filesystem::path Config::getStoragePath()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	std::string p = pConf->getString("StoragePath", "C:\\PACS\\Storage");	
	return Path::expand(p);
}

void Config::registerCodecs()
{
	DJDecoderRegistration::registerCodecs();
	DJEncoderRegistration::registerCodecs();    
	DJLSEncoderRegistration::registerCodecs();    
	DJLSEncoderRegistration::registerCodecs();    
	DcmRLEEncoderRegistration::registerCodecs();    
	DcmRLEDecoderRegistration::registerCodecs();			
	FMJP2KEncoderRegistration::registerCodecs();
	FMJP2KDecoderRegistration::registerCodecs();	
}


void Config::deregisterCodecs()
{
	DJDecoderRegistration::cleanup();
	DJEncoderRegistration::cleanup();    
	DJLSDecoderRegistration::cleanup();
	DJLSEncoderRegistration::cleanup();   
	DcmRLEEncoderRegistration::cleanup();    
	DcmRLEDecoderRegistration::cleanup();		
	FMJP2KEncoderRegistration::cleanup();
	FMJP2KDecoderRegistration::cleanup();
}

std::string Config::getConnectionString()
{
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));
	
	return pConf->getString("ConnectionString", "Driver={SQL Server};Server=localhost;Database=PACSDB;Trusted_Connection=yes;");
}

void Config::createDBPool()
{
	//_pool = new SessionPool("MySQL", getConnectionString());
}

bool Config::test(std::string &errormsg)
{
	bool result = true;	

	try
	{		
		// test database connection	
		// Session session("MySQL", Config::getConnectionString());
	}	
	/*catch(Poco::DataException &e)
	{
		errormsg = e.displayText();
		result = false;
	}*/
	catch(std::exception &e)
	{
		errormsg = e.what();
		result = false;
	}

	if(!result)
		return false;

	try
	{
		// test to see if we can write to the temp dir
		boost::filesystem::path tempTest = Config::getTempPath();
		tempTest /= "_test";

		if(!boost::filesystem::create_directories(tempTest))			
			return false;

		boost::filesystem::remove(tempTest);

		// test to see if we can write to the storage dir
		boost::filesystem::path storageTest = Config::getTempPath();
		storageTest /= "_test";

		if(!boost::filesystem::create_directories(storageTest))			
			return false;

		boost::filesystem::remove(storageTest);
	}
	catch(std::exception &e)
	{
		errormsg = e.what();
		result = false;
	}

	return result;
}

