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

typedef enum { patientstudyroot, seriesroot, instanceroot } QueryLevel;

class FindHandler
{
public:
	FindHandler(std::string aetitle);
	static void FindCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail);
protected:
	void FindCallback(OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail);
	bool QueryPatientStudyLevel(DcmDataset *requestIdentifiers);
	bool QuerySeriesLevel(DcmDataset *requestIdentifiers);
	bool QueryInstanceLevel(DcmDataset *requestIdentifiers);

	DIC_US FindStudyLevel(rowset_iterator<row> &itr, DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers);
	DIC_US FindSeriesLevel(rowset_iterator<row> &itr, DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers);
	DIC_US FindInstanceLevel(rowset_iterator<row> &itr, DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers);
		
	std::string aetitle;
	QueryLevel querylevel;

	session dbconnection;
	row row_;
	soci::rowset_iterator<soci::row> itr;
};