#include <winsock2.h>	// include winsock2 before network includes

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

#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"


#include "model.h"
#include "config.h"
#include "move.h"
#include "dicomsender.h"

using namespace boost::gregorian;
using namespace boost::posix_time;

void MoveHandler::MoveCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_MoveRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_MoveRSP *response, DcmDataset **statusDetail, DcmDataset **responseIdentifiers)
{
	MoveHandler *handler = (MoveHandler *) callbackData;
	handler->MoveCallback(cancelled, request, requestIdentifiers, responseCount, response, statusDetail, responseIdentifiers);
}

MoveHandler::MoveHandler(std::string aetitle, std::string peeraetitle)
{
	this->aetitle = aetitle;
	this->peeraetitle = peeraetitle;
	net = NULL;
	assoc = NULL;
	nCompleted = nFailed = nWarning = 0;
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
		log << "-------";
		DCMNET_INFO(log.str());

		OFString studyuid;
		requestIdentifiers->findAndGetOFString(DCM_StudyInstanceUID, studyuid);		
		if(studyuid.size() > 0)
		{			
			if(GetFilesToSend(studyuid.c_str(), filestosend))
			{								
				DCMNET_INFO("Number of files to send: " << filestosend.size());				

				if(filestosend.size() > 0)					
				{
					// scan the files for sop class
					scanFiles();

					dimseStatus = STATUS_Pending;
				}
				else
					dimseStatus = STATUS_Success;									
			}
			else
				dimseStatus = STATUS_MOVE_Failed_UnableToProcess;
		}
		else		
			dimseStatus = STATUS_MOVE_Failed_IdentifierDoesNotMatchSOPClass;

		if(dimseStatus == STATUS_Pending)
		{
			// get the destination
			Destination destination;
			destination.destinationAE = request->MoveDestination;
			if(mapMoveDestination(destination.destinationAE, destination))
			{							
				DCMNET_INFO("Destination: " << destination.destinationhost << ":" << destination.destinationport
				 << " Destination AE: " << destination.destinationAE << " Source AE: " << destination.sourceAE);
				
				// start the sending association
				if(buildSubAssociation(request, destination).good())
				{
					dimseStatus = STATUS_Pending;
				}
				else
				{
					// failed to build association, must fail move
					// dimseStatus = failAllSubOperations();
            
					dimseStatus = STATUS_MOVE_Refused_OutOfResourcesSubOperations;				
				}
			}
			else
				dimseStatus = STATUS_MOVE_Failed_MoveDestinationUnknown;			
		}
	}	//  responseCount == 1

	if (cancelled && dimseStatus == STATUS_Pending)
    {
        // dimseStatus = dbHandle.cancelMoveRequest();
    }
    
	// no errors, let's send one image
	if(dimseStatus == STATUS_Pending)
	{
		dimseStatus = moveNextImage();
	}

	// no more images?
    if (dimseStatus != STATUS_Pending)
    {        
        closeSubAssociation();
     
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
    response->NumberOfRemainingSubOperations = filestosend.size();
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

bool MoveHandler::GetFilesToSend(std::string studyinstanceuid, naturalset &result)
{
	try
	{				
		// open the db
		soci::session dbconnection(Config::getConnectionString());
		PatientStudy study;
		dbconnection << "SELECT * FROM patient_studies WHERE StudyInstanceUID = :StudyInstanceUID LIMIT 1", soci::into(study),
			soci::use(studyinstanceuid);
		if(!dbconnection.got_data())
		{			
			throw std::exception();
		}

		soci::statement seriesselect(dbconnection);
		Series series;
		seriesselect.exchange(soci::into(series));
		
		seriesselect.exchange(soci::use(studyinstanceuid));
		seriesselect.alloc();
		seriesselect.prepare("SELECT * FROM series WHERE StudyInstanceUID = :StudyInstanceUID");
		seriesselect.define_and_bind();
		seriesselect.execute();

		std::vector<Series> series_list;		
		std::copy(soci::rowset_iterator<Series >(seriesselect, series), soci::rowset_iterator<Series >(), std::back_inserter(series_list));
				
		for(std::vector<Series>::iterator itr = series_list.begin(); itr != series_list.end(); itr++)
		{
			soci::statement instanceselect(dbconnection);
			Instance instance;
			instanceselect.exchange(soci::into(instance));
			std::string seriesinstanceuid = (*itr).SeriesInstanceUID;
			instanceselect.exchange(soci::use(seriesinstanceuid));
			instanceselect.alloc();
			instanceselect.prepare("SELECT * FROM instances WHERE SeriesInstanceUID = :SeriesInstanceUID");
			instanceselect.define_and_bind();
			instanceselect.execute();
					
			boost::filesystem::path serieslocation;
			serieslocation = Config::getStoragePath();
			serieslocation /= studyinstanceuid;
			serieslocation /= seriesinstanceuid;

			soci::rowset_iterator<Instance > itr2(instanceselect, instance);
			soci::rowset_iterator<Instance > end2;
			while(itr2 != end2)
			{
				boost::filesystem::path filename = serieslocation;
				filename /= (*itr2).SOPInstanceUID + ".dcm";
				result.insert(filename);
				itr2++;
			}
		}
	}
	catch (std::exception& e)
	{
		return false;
	}
	
	return true;
}

bool MoveHandler::mapMoveDestination(std::string destinationAE, Destination &destination)
{
	try
	{
		soci::session dbconnection(Config::getConnectionString());
		
		soci::session &destinationsselect = dbconnection;
		destinationsselect << "SELECT id,"
			"name, destinationhost, destinationport, destinationAE, sourceAE,"
			"created_at,updated_at"
			" FROM destinations WHERE destinationAE = :destinationAE LIMIT 1",
			soci::into(destination),
			soci::use(destinationAE);

		if(destinationsselect.got_data())
		{
			return true;
		}
	}
	catch(...)
	{

	}
	return false;
}

OFCondition MoveHandler::buildSubAssociation(T_DIMSE_C_MoveRQ *request, Destination &destination)
{
	OFCondition cond;

	try
	{
		T_ASC_Parameters *params = NULL;

		int acse_timeout = 20;
		cond = ASC_initializeNetwork(NET_REQUESTOR, 0, acse_timeout, &net);
		if (cond.bad())
		{		
			OFString tempStr;
			DCMNET_ERROR(DimseCondition::dump(tempStr, cond));
			throw std::runtime_error("ASC_initializeNetwork");
		}

		cond = ASC_createAssociationParameters(&params, ASC_DEFAULTMAXPDU);
		if (cond.bad())
		{		
			OFString tempStr;
			DCMNET_ERROR(DimseCondition::dump(tempStr, cond));
			throw std::runtime_error("ASC_createAssociationParameters");
		}

		ASC_setAPTitles(params, destination.sourceAE.c_str(), destination.destinationAE.c_str(), NULL);	

		DIC_NODENAME localHost;
		gethostname(localHost, DIC_NODENAME_LEN);
		std::stringstream peerHost;
		peerHost << destination.destinationhost << ":" << destination.destinationport;
		ASC_setPresentationAddresses(params, localHost, peerHost.str().c_str());

		// add presentation contexts				
		cond = addStoragePresentationContexts(params, sopClassUIDList);
		if (cond.bad())
		{
			OFString tempStr;
			DCMNET_ERROR(DimseCondition::dump(tempStr, cond));
			throw std::runtime_error("addStoragePresentationContexts");
		}

		DCMNET_INFO("Requesting Association.");

		cond = ASC_requestAssociation(net, params, &assoc);
		if (cond.bad())
		{
			if (cond == DUL_ASSOCIATIONREJECTED)
			{				
				T_ASC_RejectParameters rej;
				ASC_getRejectParameters(params, &rej);
				std::stringstream msg;
				msg << "Association Rejected:\n";
				ASC_printRejectParameters(msg, &rej);
				DCMNET_ERROR(msg.str());
				throw std::runtime_error("ASC_requestAssociation");
			}
			else
			{
				DCMNET_ERROR("Association Request Failed:\n");			
				OFString tempStr;
				DCMNET_ERROR(DimseCondition::dump(tempStr, cond));
				throw std::runtime_error("ASC_requestAssociation");
			}
		}

		// display the presentation contexts which have been accepted/refused	
		std::stringstream msg;
		msg << "Association Parameters Negotiated:\n";
		ASC_dumpParameters(params, msg);
		DCMNET_INFO(msg.str());

		/* count the presentation contexts which have been accepted by the SCP */
		/* If there are none, finish the execution */
		if (ASC_countAcceptedPresentationContexts(params) == 0)
		{
			DCMNET_ERROR("No Acceptable Presentation Contexts\n");
			throw new std::runtime_error("ASC_countAcceptedPresentationContexts");
		}
	}
	catch(...)
	{

	}

	return cond;
}

OFCondition MoveHandler::closeSubAssociation()
{
    OFCondition cond = EC_Normal;    
	
    if (assoc != NULL)
    {        
        cond = ASC_releaseAssociation(assoc);
        if (cond.bad())
        {
			DCMNET_ERROR("Sub-Association Release Failed:");			
            OFString tempStr;
			DCMNET_ERROR(DimseCondition::dump(tempStr, cond));			

        }
        cond = ASC_dropAssociation(assoc);
        if (cond.bad())
        {
            DCMNET_ERROR("Sub-Association Drop Failed:");
            OFString tempStr;
			DCMNET_ERROR(DimseCondition::dump(tempStr, cond));			
        }
        cond = ASC_destroyAssociation(&assoc);
        if (cond.bad())
        {
            DCMNET_ERROR("Sub-Association Destroy Failed:");
            OFString tempStr;
			DCMNET_ERROR(DimseCondition::dump(tempStr, cond));			
        }
        
		assoc = NULL;
    }

	if(net != NULL)
		ASC_dropNetwork(&net);

	net = NULL;

    return cond;
}

DIC_US MoveHandler::moveNextImage()
{
    OFCondition cond = EC_Normal;    

    std::stringstream log;
    DIC_US dbStatus = STATUS_Pending;
    
    if (filestosend.size() <= 0)
		return STATUS_Success;
    
	boost::filesystem::path filename = *filestosend.begin();
	filestosend.erase(filestosend.begin());
	
	std::stringstream msg;
#ifdef _WIN32
	// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
	msg << "Sending file: " << filename.string(std::codecvt_utf8<boost::filesystem::path::value_type>());
#else
	msg << "Sending file: " << filename.string();
#endif
	DCMNET_INFO(msg.str());

	DcmFileFormat dfile;
	cond = dfile.loadFile(filename.c_str());
	if(cond.bad())
	{
		nFailed++;

		OFString tempStr;
		DCMNET_ERROR(DimseCondition::dump(tempStr, cond));		

		return STATUS_Pending;
	}

	OFString studyuid, seriesuid, sopuid, sopclassuid;
	dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, studyuid);
	dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, seriesuid);
	dfile.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, sopuid);
	dfile.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid);	

	DcmXfer filexfer(dfile.getDataset()->getOriginalXfer());
    T_ASC_PresentationContextID presId = ASC_findAcceptedPresentationContextID(assoc, sopclassuid.c_str(), filexfer.getXferID());
	if (presId == 0)
	{
		addFailedUIDInstance(sopuid.c_str());
		return STATUS_Pending;
    }

	// convert to what they want
	T_ASC_PresentationContext pc;
	ASC_findAcceptedPresentationContext(assoc->params, presId, &pc);
	DcmXfer netTransfer(pc.acceptedTransferSyntax);

	dfile.getDataset()->chooseRepresentation(netTransfer.getXfer(), NULL);

	T_DIMSE_C_StoreRQ req;
    T_DIMSE_C_StoreRSP rsp;

	req.MessageID = assoc->nextMsgID++;;
    strcpy(req.AffectedSOPClassUID, sopclassuid.c_str());
    strcpy(req.AffectedSOPInstanceUID, sopuid.c_str());
    req.DataSetType = DIMSE_DATASET_PRESENT;
    req.Priority = DIMSE_PRIORITY_MEDIUM;
    req.opts = (O_STORE_MOVEORIGINATORAETITLE | O_STORE_MOVEORIGINATORID);
    strcpy(req.MoveOriginatorApplicationEntityTitle, peeraetitle.c_str());
    req.MoveOriginatorID = 0;	
	
	DcmDataset *stDetail = NULL;
	cond = DIMSE_storeUser(assoc, presId, &req,
                           NULL, dfile.getDataset(), moveSubOpProgressCallback, this,
                           DIMSE_NONBLOCKING, 60,
                           &rsp, &stDetail);

	if (cond.good())
    {
        DCMNET_INFO("Move SCP: Received Store SCU RSP [Status=" << DU_cstoreStatusString(rsp.DimseStatus) << "]");
        
        if (rsp.DimseStatus == STATUS_Success)
        {            
            nCompleted++;
        }
        else if ((rsp.DimseStatus & 0xf000) == 0xb000)
        {            
            nWarning++;
            DCMNET_INFO("Move SCP: Store Waring: Response Status: " << DU_cstoreStatusString(rsp.DimseStatus));
        }
        else
        {
            nFailed++;
            addFailedUIDInstance(sopuid.c_str());
            
            DCMNET_ERROR("Move SCP: Store Failed: Response Status: " << DU_cstoreStatusString(rsp.DimseStatus));            
        }
    }
    else
    {
        nFailed++;
        addFailedUIDInstance(sopuid.c_str());
        DCMNET_ERROR("Move SCP: storeSCU: Store Request Failed:");
        OFString tempStr;
		DCMNET_ERROR(DimseCondition::dump(tempStr, cond));
    }

    if (stDetail != NULL)
    {
        log << "  Status Detail:\n";
        stDetail->print(log);
        DCMNET_INFO(log);
        delete stDetail;
    }
    
    return STATUS_Pending;
}


void MoveHandler::scanFiles()
{	
	for(naturalset::iterator itr = filestosend.begin(); itr != filestosend.end(); itr++)
		scanFile(*itr);	
}

bool MoveHandler::scanFile(boost::filesystem::path currentFilename)
{	
	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(currentFilename.c_str());
	if (cond.bad())
	{		
		DCMNET_ERROR("cannot access file, ignoring: " << currentFilename);
		return true;
	}

	char sopClassUID[128];
	char sopInstanceUID[128];

	if (!DU_findSOPClassAndInstanceInDataSet(dfile.getDataset(), sopClassUID, sopInstanceUID))
	{					
		DCMNET_ERROR("missing SOP class (or instance) in file, ignoring: " << currentFilename);
		return false;
	}

	if (!dcmIsaStorageSOPClassUID(sopClassUID))
	{				
		DCMNET_ERROR("unknown storage sop class in file, ignoring: " << currentFilename << " : " << sopClassUID);
		return false;
	}

	sopClassUIDList.push_back(sopClassUID);	

	return true;
}

static bool
	isaListMember(OFList<OFString>& lst, OFString& s)
{	
	bool found = false;

	for(OFListIterator(OFString) itr = lst.begin(); itr != lst.end() && !found; itr++)
	{
		found = (s == *itr);	
	}

	return found;
}

static OFCondition
	addPresentationContext(T_ASC_Parameters *params,
	int presentationContextId, const OFString& abstractSyntax,
	const OFString& transferSyntax,
	T_ASC_SC_ROLE proposedRole = ASC_SC_ROLE_DEFAULT)
{
	const char* c_p = transferSyntax.c_str();
	OFCondition cond = ASC_addPresentationContext(params, presentationContextId,
		abstractSyntax.c_str(), &c_p, 1, proposedRole);
	return cond;
}

static OFCondition
	addPresentationContext(T_ASC_Parameters *params,
	int presentationContextId, const OFString& abstractSyntax,
	const OFList<OFString>& transferSyntaxList,
	T_ASC_SC_ROLE proposedRole = ASC_SC_ROLE_DEFAULT)
{
	// create an array of supported/possible transfer syntaxes
	const char** transferSyntaxes = new const char*[transferSyntaxList.size()];
	int transferSyntaxCount = 0;
	OFListConstIterator(OFString) s_cur = transferSyntaxList.begin();
	OFListConstIterator(OFString) s_end = transferSyntaxList.end();
	while (s_cur != s_end)
	{
		transferSyntaxes[transferSyntaxCount++] = (*s_cur).c_str();
		++s_cur;
	}

	OFCondition cond = ASC_addPresentationContext(params, presentationContextId,
		abstractSyntax.c_str(), transferSyntaxes, transferSyntaxCount, proposedRole);

	delete[] transferSyntaxes;
	return cond;
}

OFCondition MoveHandler::addStoragePresentationContexts(T_ASC_Parameters *params, OFList<OFString>& sopClasses)
{		
	OFList<OFString> preferredTransferSyntaxes;
	preferredTransferSyntaxes.push_back(UID_JPEGLSLosslessTransferSyntax);	
	preferredTransferSyntaxes.push_back(UID_JPEG2000LosslessOnlyTransferSyntax);
	preferredTransferSyntaxes.push_back(UID_JPEGProcess14SV1TransferSyntax);
		
	OFList<OFString> fallbackSyntaxes;	
	fallbackSyntaxes.push_back(UID_LittleEndianExplicitTransferSyntax);	
	fallbackSyntaxes.push_back(UID_LittleEndianImplicitTransferSyntax);		
	
	OFListIterator(OFString) s_cur, s_end;

	// thin out the sop classes to remove any duplicates.
	OFList<OFString> sops;
	s_cur = sopClasses.begin();
	s_end = sopClasses.end();
	while (s_cur != s_end)
	{
		if (!isaListMember(sops, *s_cur))
		{
			sops.push_back(*s_cur);
		}
		++s_cur;
	}
		
	// add a presentations context for each sop class x transfer syntax pair
	OFCondition cond = EC_Normal;
	int pid = 1; // presentation context id
	s_cur = sops.begin();
	s_end = sops.end();
	while (s_cur != s_end && cond.good())
	{
		if (pid > 255)
		{
			DCMNET_ERROR("Too many presentation contexts");
			return ASC_BADPRESENTATIONCONTEXTID;
		}

		// created a list of transfer syntaxes combined from the preferred and fallback syntaxes
		OFList<OFString> combinedSyntaxes(fallbackSyntaxes);
		OFListIterator(OFString) s_cur2 = preferredTransferSyntaxes.begin();
		OFListIterator(OFString) s_end2 = preferredTransferSyntaxes.end();		
		while (s_cur2 != s_end2)
		{
			combinedSyntaxes.push_front(*s_cur2);			
			++s_cur2;
		}

		cond = addPresentationContext(params, pid, *s_cur, combinedSyntaxes);
		pid += 2;   /* only odd presentation context id's */
		
		++s_cur;
	}

	return cond;
}


void MoveHandler::moveSubOpProgressCallback(void *callbackData, T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req)
{
	MoveHandler *handler = (MoveHandler *) callbackData;
	handler->moveSubOpProgressCallback(progress, req);
}

void MoveHandler::moveSubOpProgressCallback(T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req)
{

}