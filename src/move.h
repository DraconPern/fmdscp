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
#include "dcmtk/dcmnet/scu.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "model.h"
#include "sender.h"

class MoveHandler
{
public:
	MoveHandler(std::string aetitle, std::string peeraetitle, boost::uuids::uuid uuid, CloudClient &cloudclient);
	static void MoveCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers);
protected:
	void Initialize(Destination &destination);
	void SetFileList(const naturalpathmap &instances);
	void SetStatus(std::string msg);
	void CreateOutgoingSession(std::string StudyInstanceUID, std::string PatientID, std::string PatientName);
	void MoveCallback(OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers);
	bool GetFilesToSend(std::string studyinstanceuid, naturalpathmap &result, std::string &PatientName, std::string &PatientID);
	bool findDestination(std::string destinationAE, Destination &destination);
	void addFailedUIDInstance(const char *sopInstance);
	OFCondition buildSubAssociation(T_DIMSE_C_MoveRQ *request, Destination &destination);
	OFCondition closeSubAssociation();
	DIC_US moveNextImage(Uint16 moveOriginatorMsgID);
	void scanFiles();
	bool scanFile(boost::filesystem::path currentFilename);
	OFCondition addStoragePresentationContexts(T_ASC_Parameters *params, OFList<OFString>& sopClasses);
	static void moveSubOpProgressCallback(void *callbackData, T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req);
	void moveSubOpProgressCallback(T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req);


	std::string aetitle, peeraetitle;

	// list of dicom images to send, all images should be in the same patient
	naturalpathmap instances;
	int totalfiles;

	Destination destination;

	typedef std::map<std::string, std::set<std::string> > mapset;
	mapset sopclassuidtransfersyntax;

	// info of the subassociation
	DcmSCU scu;

	std::string failedUIDs;
	int nCompleted, nFailed, nWarning;

	boost::uuids::uuid uuid;
	CloudClient &cloudclient;
};

#endif