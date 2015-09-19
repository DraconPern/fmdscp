#include <boost/algorithm/string.hpp>

#include "find.h"

#include "poco/File.h"
#include "Poco/Data/Session.h"
#include "poco/Tuple.h"

using namespace Poco;
using namespace Poco::Data;
using namespace Poco::Data::Keywords;

#include "model.h"
#include "config.h"
#include "util.h"

void FindHandler::FindCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail)
{
	FindHandler *handler = (FindHandler *) callbackData;
	handler->FindCallback(cancelled, request, requestIdentifiers, responseCount, response, responseIdentifiers, statusDetail);
}

FindHandler::FindHandler() :
	session_(Config::getDBPool().get())
{


}

void FindHandler::FindCallback(OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail)
{    
    // Determine the data source's current status.
    DIC_US dbstatus = STATUS_Pending;

    // If this is the first time this callback function is called, we need to do open the recordset
    if ( responseCount == 1 )
    {
        // Dump some information if required
        /*std::stringstream log;
		log << "Find SCP Request Identifiers:\n";
        requestIdentifiers->print(log);
        DCMNET_INFO(CA2W(text2bin(log.str()).c_str()));
        log.str("");
		*/
        // support only study level model
        if (strcmp(request->AffectedSOPClassUID, UID_FINDStudyRootQueryRetrieveInformationModel) != 0)
        {
            response->DimseStatus = STATUS_FIND_Refused_SOPClassNotSupported;
            return;
        }
    }

    // If we encountered a C-CANCEL-RQ and if we have pending
    // responses, the search shall be cancelled
    if (cancelled && DICOM_PENDING_STATUS(dbstatus))
    {
		// normally we close the db
        //dataSource->cancelFindRequest(&dbStatus);
    }

    // If the dbstatus is "pending" try to select another matching record.
    if (DICOM_PENDING_STATUS(dbstatus))
    {
		
		

        // which model are we doing?
        OFString retrievelevel;
        requestIdentifiers->findAndGetOFString(DCM_QueryRetrieveLevel, retrievelevel);
		if(retrievelevel == "IMAGE")
		{
			// dbstatus = FindImageLevel(requestIdentifiers, responseIdentifiers);
		}
        else if (retrievelevel == "SERIES")
        {
            // dbstatus = FindSeriesLevel(requestIdentifiers, responseIdentifiers);
        }        
        else if (retrievelevel == "STUDY")
        {
            // dbstatus = FindStudyLevel(requestIdentifiers, responseIdentifiers);
        }
		else
			dbstatus = STATUS_FIND_Failed_UnableToProcess;
    }
  
	// DEBUGLOG(sessionguid, DB_INFO, L"Worklist Find SCP Response %d [status: %s]\r\n", responseCount, CA2W(DU_cfindStatusString( (Uint16)dbstatus )));
       	
    if( *responseIdentifiers != NULL && (*responseIdentifiers)->card() > 0 )
    {
		/*
		std::stringstream log;
		log << "Response Identifiers #" << responseCount;         
        (*responseIdentifiers)->print(log);
        log << "-------" << endl;
		DEBUGLOG(sessionguid, DB_INFO, CA2W(text2bin(log.str()).c_str()));
		log.str("");*/
    }
     
    // Set response status
    response->DimseStatus = dbstatus;

    // Delete status detail information if there is some
    if ( *statusDetail != NULL )
    {
        delete *statusDetail;
        *statusDetail = NULL;
    }
}
