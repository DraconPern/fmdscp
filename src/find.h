#ifndef __FIND_H__
#define __FIND_H__

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
#include "dbpool.h"

typedef enum { patientstudyroot, seriesroot, instanceroot } QueryLevel;

class FindHandler
{
public:
	FindHandler(std::string aetitle, DBPool &dbpool);
	static void FindCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail);
protected:
	void FindCallback(OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail);
	bool QueryPatientStudyLevel(DcmDataset *requestIdentifiers);
	bool QuerySeriesLevel(DcmDataset *requestIdentifiers);
	bool QueryInstanceLevel(DcmDataset *requestIdentifiers);

	DIC_US FindStudyLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers);
	DIC_US FindSeriesLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers);
	DIC_US FindInstanceLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers);
		
	std::string aetitle;
	QueryLevel querylevel;
	std::vector<PatientStudy > patientstudies;
	std::vector<PatientStudy >::iterator patientstudies_itr;
	std::vector<Series > series;
	std::vector<Series >::iterator series_itr;
	std::vector<Instance > instances;	
	std::vector<Instance >::iterator instances_itr;

	DBPool &dbpool;
};

#endif