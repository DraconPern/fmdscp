#ifndef __STORE_H__
#define __STORE_H__

#include <boost/filesystem.hpp>
#include <winsock2.h>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "myscp.h"       /* for base class DcmSCP */
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/oflog/ndc.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

class StoreHandler
{

public:
	OFCondition handleSTORERequest(boost::filesystem::path filename);
	bool AddDICOMFileInfoToDatabase(boost::filesystem::path filename);
};

#endif