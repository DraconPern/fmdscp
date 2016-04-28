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
#include "sender.h"

class MoveHandler
{
public:
	MoveHandler(std::string aetitle, std::string peeraetitle);	
	static void MoveCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers);
protected:
	void MoveCallback(OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers);
	bool GetFilesToSend(std::string studyinstanceuid, naturalpathmap &result);
	bool mapMoveDestination(std::string destinationAE, Destination &destination);
	void addFailedUIDInstance(const char *sopInstance);
	OFCondition buildSubAssociation(T_DIMSE_C_MoveRQ *request, Destination &destination);
	OFCondition closeSubAssociation();
	DIC_US moveNextImage();
	void scanFiles();
	bool scanFile(boost::filesystem::path currentFilename);
	OFCondition addStoragePresentationContexts(T_ASC_Parameters *params, OFList<OFString>& sopClasses);
	static void moveSubOpProgressCallback(void *callbackData, T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req);
	void moveSubOpProgressCallback(T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req);


	std::string aetitle, peeraetitle;

	naturalpathmap instances;
	OFList<OFString> sopClassUIDList;    // the list of sop classes

	// info of the subassociation
	T_ASC_Network *net;	
	T_ASC_Association *assoc;

	std::string failedUIDs;
	int nCompleted, nFailed, nWarning;
};

#endif