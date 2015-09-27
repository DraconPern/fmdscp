#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/dcmdata/dcdeftag.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif


#include "model.h"
#include "config.h"
#include "move.h"
#include "util.h"

using namespace soci;
using namespace boost::gregorian;
using namespace boost::posix_time;

void MoveHandler::MoveCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers)
{
	MoveHandler *handler = (MoveHandler *) callbackData;
	handler->MoveCallback(cancelled, request, requestIdentifiers, responseCount, response, statusDetail, responseIdentifiers);
}

MoveHandler::MoveHandler(std::string aetitle)
{	
	this->aetitle = aetitle;
}


void MoveHandler::MoveCallback(OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers)
{
	
}
