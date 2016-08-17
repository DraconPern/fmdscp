#ifndef __STORE_H__
#define __STORE_H__

#include <winsock2.h>	// include winsock2 before network includes
#include <boost/filesystem.hpp>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/ofstd/ofstd.h"
#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

class StoreHandler
{

public:	
	OFCondition handleSTORERequest(boost::filesystem::path filename);
	bool AddDICOMFileInfoToDatabase(boost::filesystem::path filename);
	OFCondition UploadToS3(boost::filesystem::path filename, std::string sopuid, std::string seriesuid, std::string studyuid);
};

#endif