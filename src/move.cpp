#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <codecvt>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/dcmdata/dctk.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include <Poco/Data/Session.h>
using namespace Poco::Data::Keywords;

#include "model.h"
#include "config.h"
#include "move.h"

using namespace boost::gregorian;
using namespace boost::posix_time;

void MoveHandler::MoveCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers)
{
	MoveHandler *handler = (MoveHandler *) callbackData;
	handler->MoveCallback(cancelled, request, requestIdentifiers, responseCount, response, statusDetail, responseIdentifiers);
}

MoveHandler::MoveHandler(std::string aetitle, std::string peeraetitle, boost::uuids::uuid uuid, CloudClient &cloudclient, DBPool &dbpool) :
aetitle(aetitle), peeraetitle(peeraetitle), uuid(uuid), cloudclient(cloudclient), dbpool(dbpool)
{	
	nCompleted = nFailed = nWarning = 0;
}

// copy of Sender::Initialize
void MoveHandler::Initialize(Destination &destination)
{
	this->destination = destination;
}

// copy of Sender::SetFileList
void MoveHandler::SetFileList(const naturalpathmap &instances)
{
	this->instances = instances;
	totalfiles = instances.size();
}

void MoveHandler::SetStatus(std::string msg)
{
	Poco::Data::Session dbconnection(dbpool.get());
	Poco::DateTime rightnow;

	std::string uuidstring = boost::uuids::to_string(uuid);

	std::vector<OutgoingSession> out_sessions;
	Poco::Data::Statement outsessionsselect(dbconnection);
	outsessionsselect << "SELECT id,"
		"uuid,"
		"queued,"
		"StudyInstanceUID,"		
		"PatientName,"
		"PatientID,"
		"StudyDate,"
		"ModalitiesInStudy,"
		"destination_id,"
		"status,"
		"createdAt,updatedAt"
		" FROM outgoing_sessions WHERE uuid = ?",
		into(out_sessions), use(uuidstring);

	outsessionsselect.execute();

	if (out_sessions.size() == 1)
	{
		out_sessions[0].status = msg;
		out_sessions[0].updated_at = rightnow;

		Poco::Data::Statement update(dbconnection);
		update << "UPDATE outgoing_sessions SET status = ?, updatedAt = ? WHERE uuid = ?", use(msg), use(rightnow), use(out_sessions[0].uuid);
		update.execute();

		cloudclient.send_updateoutsessionitem(out_sessions[0], destination.name);
	}
}

void MoveHandler::CreateOutgoingSession(std::string StudyInstanceUID, std::string PatientID, std::string PatientName)
{
	Poco::Data::Session dbconnection(dbpool.get());
	
	OutgoingSession out_session;	
	out_session.uuid = boost::lexical_cast<std::string>(uuid);
	out_session.queued = 0;
	out_session.StudyInstanceUID = StudyInstanceUID;
	out_session.PatientID = PatientID;
	out_session.PatientName = PatientName;
	out_session.destination_id = boost::lexical_cast<int>(destination.id);	
	out_session.status = "Starting...";
	out_session.created_at = Poco::DateTime();
	out_session.updated_at = Poco::DateTime();

	Poco::Data::Statement insert(dbconnection);
	insert << "INSERT INTO outgoing_sessions (id, uuid, queued, StudyInstanceUID, PatientName, PatientID, StudyDate, ModalitiesInStudy, destination_id, status, createdAt, updatedAt) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
		use(out_session);
	insert.execute();

	dbconnection << "SELECT LAST_INSERT_ID()", into(out_session.id), now;
}

void MoveHandler::MoveCallback(OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers)
{
	DIC_US dimseStatus = STATUS_Pending;

	// If this is the first time this callback function is called, we need to get the list of files to send.
	if ( responseCount == 1 )
	{	
		std::stringstream log;
		log << "Move SCP Request Identifiers:";
		requestIdentifiers->print(log);		
		log << "-------\n";
		log << "Move Destination: " << request->MoveDestination;
		DCMNET_INFO(log.str());

		try
		{
			OFString studyuid;
			requestIdentifiers->findAndGetOFString(DCM_StudyInstanceUID, studyuid);
			if (studyuid.size() <= 0)
				throw STATUS_MOVE_Failed_IdentifierDoesNotMatchSOPClass;

			std::string PatientID, PatientName;
			if (!GetFilesToSend(studyuid.c_str(), instances, PatientID, PatientName))
				throw STATUS_MOVE_Failed_UnableToProcess;

			totalfiles = instances.size();
			DCMNET_INFO("Number of files to send: " << instances.size());

			if (instances.size() <= 0)
				throw STATUS_Success;

			// scan the files for sop class and syntax
			scanFiles();

			// get the destination			
			destination.destinationAE = request->MoveDestination;
			if (!findDestination(destination.destinationAE, destination))
				throw STATUS_MOVE_Failed_MoveDestinationUnknown;
			
			CreateOutgoingSession(studyuid.c_str(), PatientID, PatientName);

			DCMNET_INFO("Destination: " << destination.destinationhost << ":" << destination.destinationport
				<< " Destination AE: " << destination.destinationAE << " Source AE: " << destination.sourceAE);

			// start the sending association				
			if (buildSubAssociation(request, destination).bad())
			{
				// dimseStatus = failAllSubOperations();
				throw STATUS_MOVE_Refused_OutOfResourcesSubOperations;
			}
						
			dimseStatus = STATUS_Pending;
		}
		catch (int thrown_dimseStatus)
		{
			dimseStatus = thrown_dimseStatus;
		}
		
	}	//  responseCount == 1

	if (cancelled && dimseStatus == STATUS_Pending)
    {
        // dimseStatus = dbHandle.cancelMoveRequest();
    }
    
	// no errors, let's send one image
	if(dimseStatus == STATUS_Pending)
	{
		dimseStatus = moveNextImage(request->MessageID);
	}

	// no more images?
    if (dimseStatus != STATUS_Pending)
    {   
		scu.releaseAssociation();        
     
        if (nFailed > 0 || nWarning > 0)
        {
            dimseStatus = STATUS_MOVE_Warning_SubOperationsCompleteOneOrMoreFailures;
        }

        // if all the sub-operations failed then we need to generate a failed or refused status.        
        if ((nFailed > 0) && ((nCompleted + nWarning) == 0))
        {
            dimseStatus = STATUS_MOVE_Refused_OutOfResourcesSubOperations;
        }
    }

    if (dimseStatus != STATUS_Success && dimseStatus != STATUS_Pending)
    {
		if (failedUIDs.length() != 0)
		{
			*responseIdentifiers = new DcmDataset();
			DU_putStringDOElement(*responseIdentifiers, DCM_FailedSOPInstanceUIDList, failedUIDs.c_str());
		}
    }
       
	response->DimseStatus = dimseStatus;
	response->NumberOfRemainingSubOperations = instances.size();
    response->NumberOfCompletedSubOperations = nCompleted;
    response->NumberOfFailedSubOperations = nFailed;
    response->NumberOfWarningSubOperations = nWarning;
	
}

void MoveHandler::addFailedUIDInstance(const char *sopInstance)
{
    if (failedUIDs.length() == 0)
    {
        failedUIDs = sopInstance;
    }
    else
    {
        /* tag sopInstance onto end of old with '\' between */
        failedUIDs += "\\";
        failedUIDs += sopInstance;
    }
}

bool MoveHandler::GetFilesToSend(std::string studyinstanceuid, naturalpathmap &result, std::string &PatientName, std::string &PatientID)
{
	try
	{
		// open the db
		Poco::Data::Session dbconnection(dbpool.get());

		std::vector<PatientStudy> patient_studies_list;

		Poco::Data::Statement patientstudiesselect(dbconnection);
		patientstudiesselect << "SELECT id,"
			"StudyInstanceUID,"
			"StudyID,"
			"AccessionNumber,"
			"PatientName,"
			"PatientID,"
			"StudyDate,"
			"ModalitiesInStudy,"
			"StudyDescription,"
			"PatientSex,"
			"PatientBirthDate,"
			"ReferringPhysicianName,"
			"NumberOfStudyRelatedInstances,"
			"createdAt,updatedAt"
			" FROM patient_studies WHERE StudyInstanceUID = ?",
			into(patient_studies_list),
			use(studyinstanceuid);

		patientstudiesselect.execute();

		if (patient_studies_list.size() <= 0)
		{
			throw std::exception();
		}

		PatientID = patient_studies_list[0].PatientID;
		PatientName = patient_studies_list[0].PatientName;

		std::vector<Series> series_list;
		Poco::Data::Statement seriesselect(dbconnection);
		seriesselect << "SELECT id,"
			"SeriesInstanceUID,"
			"Modality,"
			"SeriesDescription,"
			"SeriesNumber,"
			"SeriesDate,"
			"patient_study_id,"
			"createdAt,updatedAt"
			" FROM series WHERE patient_study_id = ?",
			into(series_list),
			use(patient_studies_list[0].id);

		seriesselect.execute();
		for (std::vector<Series>::iterator itr = series_list.begin(); itr != series_list.end(); itr++)
		{
			std::vector<Instance> instance_list;
			Poco::Data::Statement instanceselect(dbconnection);
			instanceselect << "SELECT id,"
				"SOPInstanceUID,"
				"InstanceNumber,"
				"series_id,"
				"createdAt,updatedAt"
				" FROM instances WHERE series_id = ?",
				into(instance_list),
				use(itr->id);

			instanceselect.execute();

			boost::filesystem::path serieslocation;
			serieslocation = config::getStoragePath();
			serieslocation /= studyinstanceuid;
			serieslocation /= itr->SeriesInstanceUID;

			for (std::vector<Instance>::iterator itr2 = instance_list.begin(); itr2 != instance_list.end(); itr2++)
			{
				boost::filesystem::path filename = serieslocation;
				filename /= (*itr2).SOPInstanceUID + ".dcm";
				result.insert(std::pair<std::string, boost::filesystem::path>((*itr2).SOPInstanceUID, filename));
			}
		}
	}
	catch (std::exception& e)
	{
		return false;
	}

	return true;
}

bool MoveHandler::findDestination(std::string destinationAE, Destination &destination)
{
	try
	{
		Poco::Data::Session dbconnection(dbpool.get());

		std::vector<Destination> dests;
		dbconnection << "SELECT id,"
			"name, destinationhost, destinationport, destinationAE, sourceAE,"
			"createdAt,updatedAt"
			" FROM destinations WHERE destinationAE = ? LIMIT 1",
			into(dests),
			use(destinationAE), now;

		if (dests.size() > 0)
		{
			destination = dests[0];
			return true;
		}
	}
	catch (...)
	{

	}
	return false;
}

OFCondition MoveHandler::buildSubAssociation(T_DIMSE_C_MoveRQ *request, Destination &destination)
{
	OFCondition cond;

	scu.setVerbosePCMode(true);
	scu.setAETitle(destination.sourceAE.c_str());
	scu.setPeerHostName(destination.destinationhost.c_str());
	scu.setPeerPort(destination.destinationport);
	scu.setPeerAETitle(destination.destinationAE.c_str());
	scu.setACSETimeout(30);
	scu.setDIMSETimeout(60);
	scu.setDatasetConversionMode(true);

	OFList<OFString> defaulttransfersyntax;
	defaulttransfersyntax.push_back(UID_LittleEndianExplicitTransferSyntax);

	// for every class..
	for (mapset::iterator it = sopclassuidtransfersyntax.begin(); it != sopclassuidtransfersyntax.end(); it++)
	{
		// make list of what's in the file, and propose it first.  default proposed as a seperate context
		OFList<OFString> transfersyntax;
		for (std::set<std::string>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			if (*it2 != UID_LittleEndianExplicitTransferSyntax)
				transfersyntax.push_back(it2->c_str());
		}

		if (transfersyntax.size() > 0)
			scu.addPresentationContext(it->first.c_str(), transfersyntax);

		// propose the default UID_LittleEndianExplicitTransferSyntax
		scu.addPresentationContext(it->first.c_str(), defaulttransfersyntax);
	}
	
	cond = scu.initNetwork();
	if (cond.bad())
		return cond;

	cond = scu.negotiateAssociation();
	if (cond.bad())
		return cond;
	
	return cond;
}

DIC_US MoveHandler::moveNextImage(Uint16 moveOriginatorMsgID)
{
    OFCondition cond = EC_Normal;    

    std::stringstream log;
    DIC_US dbStatus = STATUS_Pending;
    
    if (instances.size() <= 0)
		return STATUS_Success;
    
	boost::filesystem::path filename = instances.begin()->second;
	instances.erase(instances.begin());

	Uint16 rspStatusCode;

	{
		std::stringstream msg;
		msg << "Sending file: " << filename;
		DCMNET_INFO(msg.str());	
	}

	// load file
	DcmFileFormat dcmff;
	cond = dcmff.loadFile(filename.c_str());
	if (cond.bad())
	{
		nFailed++;
		return STATUS_Pending;
	}

	// do some precheck of the transfer syntax
	DcmXfer fileTransfer(dcmff.getDataset()->getOriginalXfer());
	OFString sopclassuid;
	dcmff.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid);

	{
		std::stringstream msg;
		msg << "File encoding: " << fileTransfer.getXferName();
		DCMNET_INFO(msg.str());
	}
	
	// out found.. change to 
	T_ASC_PresentationContextID pid = scu.findAnyPresentationContextID(sopclassuid, fileTransfer.getXferID());

	cond = scu.sendSTORERequest(pid, "", dcmff.getDataset(), rspStatusCode, peeraetitle.c_str(), moveOriginatorMsgID);
		
	if (cond.good())
    {               				                
        nCompleted++;
        
		if ((rspStatusCode & 0xf000) == 0xb000)        
            nWarning++;        
    }
    else
    {
        nFailed++;
		OFString sopuid;
		dcmff.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, sopuid);
        addFailedUIDInstance(sopuid.c_str());
        DCMNET_ERROR("Move SCP: Store Request Failed:");
        OFString tempStr;
		DCMNET_ERROR(DimseCondition::dump(tempStr, cond));
    }
	
	SetStatus(boost::lexical_cast<std::string>(totalfiles - instances.size()) + " of " + boost::lexical_cast<std::string>(totalfiles)+" sent");

	DCMNET_INFO("\n");

    return STATUS_Pending;
}


void MoveHandler::scanFiles()
{	
	for (naturalpathmap::iterator itr = instances.begin(); itr != instances.end(); itr++)
		scanFile(itr->second);	
}

// copy of Sender::scanFile
bool MoveHandler::scanFile(boost::filesystem::path currentFilename)
{

	DcmFileFormat dfile;
	if (dfile.loadFile(currentFilename.c_str()).bad())
	{
		std::stringstream msg;
		msg << "cannot access file, ignoring: " << currentFilename << std::endl;
		DCMNET_INFO(msg.str());
		return true;
	}

	DcmXfer filexfer(dfile.getDataset()->getOriginalXfer());
	OFString transfersyntax = filexfer.getXferID();

	OFString sopclassuid;
	if (dfile.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid).bad())
	{
		std::stringstream msg;
		msg << "missing SOP class in file, ignoring: " << currentFilename << std::endl;
		DCMNET_INFO(msg.str());
		return false;
	}

	sopclassuidtransfersyntax[sopclassuid.c_str()].insert(transfersyntax.c_str());

	return true;
}



void MoveHandler::moveSubOpProgressCallback(void *callbackData, T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req)
{
	MoveHandler *handler = (MoveHandler *) callbackData;
	handler->moveSubOpProgressCallback(progress, req);
}

void MoveHandler::moveSubOpProgressCallback(T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req)
{

}