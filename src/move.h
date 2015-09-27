#ifndef __MOVE_H__
#define __MOVE_H__

#include <boost/filesystem.hpp>
#include <vector>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/dimse.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "model.h"

#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"

using namespace soci;

class MoveHandler
{
public:
	MoveHandler(std::string aetitle);	
	static void MoveCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers);
protected:
	void MoveCallback(OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers);
	
	std::string aetitle;
};

#endif