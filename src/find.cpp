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
#include "find.h"
#include "util.h"

using namespace soci;
using namespace boost::gregorian;
using namespace boost::posix_time;

void FindHandler::FindCallback(void *callbackData, OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail)
{
	FindHandler *handler = (FindHandler *) callbackData;
	handler->FindCallback(cancelled, request, requestIdentifiers, responseCount, response, responseIdentifiers, statusDetail);
}

FindHandler::FindHandler(std::string aetitle)
{	
	this->aetitle = aetitle;
}


// page 299 (sample only), 1433, 1439, 1691, 1473
// Study ROOT
typedef enum {dbstring, dbint, dbdate} dbtype;

struct DICOM_SQLMapping
{
	DcmTagKey dicomtag;
	char *columnName;
	dbtype dataType;
	bool useLike;		// only valid for strings
};

const DICOM_SQLMapping PatientStudyLevelMapping [] = {
	{ DCM_StudyDate,							 "StudyDate", dbdate, false},
	{ DCM_AccessionNumber,						 "AccessionNumber", dbstring, false},
	{ DCM_PatientName,                           "PatientName",  dbstring, true},
	{ DCM_PatientID,                             "PatientID", dbstring, false},
	{ DCM_StudyID,							     "StudyID", dbstring, false},
	{ DCM_StudyInstanceUID,						 "StudyInstanceUID", dbstring, false},
    { DCM_ModalitiesInStudy,                     "ModalitiesInStudy", dbstring, true},
	{ DCM_ReferringPhysicianName,			     "ReferringPhysicianName", dbstring, false},
	{ DCM_StudyDescription,			             "StudyDescription", dbstring, true},
	{ DCM_PatientBirthDate,                      "PatientBirthDate", dbdate, false},
	{ DCM_PatientSex,                            "PatientSex", dbstring, false},
	{ DCM_CommandGroupLength, NULL, dbstring, false}
};

const DICOM_SQLMapping SeriesLevelMapping [] = {
	{ DCM_StudyInstanceUID,						 "StudyInstanceUID", dbstring, false},
	{ DCM_SeriesDate,                            "SeriesDate", dbdate, false},
	{ DCM_Modality,                              "Modality", dbstring, false},
	{ DCM_SeriesDescription,                     "SeriesDescription", dbstring, false},
	{ DCM_SeriesNumber,                          "SeriesNumber", dbint, false},
	{ DCM_SeriesInstanceUID,                     "SeriesInstanceUID", dbstring, false},
	{ DCM_CommandGroupLength, NULL, dbstring, false}
};

const DICOM_SQLMapping InstanceLevelMapping [] = {	
	{ DCM_SeriesInstanceUID,                     "SeriesInstanceUID", dbstring, false},
	{ DCM_InstanceNumber,                        "InstanceNumber", dbint, false},
	{ DCM_SOPInstanceUID,                        "SOPInstanceUID", dbstring, false},
	{ DCM_CommandGroupLength, NULL, dbstring, false}
};

void Study_DICOMQueryToSQL(char *tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, statement &st);

void FindHandler::FindCallback(OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail)
{
	// Determine the data source's current status.
	DIC_US dbstatus = STATUS_Pending;

	// If this is the first time this callback function is called, we need to do open the recordset
	if ( responseCount == 1 )
	{			
		std::stringstream log;
		log << "Find SCP Request Identifiers:\n";
		requestIdentifiers->print(log);
		log << "-------";
		DCMNET_INFO(log);
				
		// support only study level model
		if (strcmp(request->AffectedSOPClassUID, UID_FINDStudyRootQueryRetrieveInformationModel) != 0)
		{
			response->DimseStatus = STATUS_FIND_Refused_SOPClassNotSupported;
			return;
		}

		try
		{
			// open the db
			session dbconnection(Config::getConnectionString());		
			statement st(dbconnection);
			OFString retrievelevel;
			requestIdentifiers->findAndGetOFString(DCM_QueryRetrieveLevel, retrievelevel);
			if (retrievelevel == "STUDY")
			{
				querylevel = patientstudyroot;
				st.exchange_for_rowset(soci::into(row_));
				Study_DICOMQueryToSQL("patientstudies", PatientStudyLevelMapping, requestIdentifiers, st);	
				st.execute();
				itr = soci::rowset_iterator<soci::row>(st, row_);			
			} 
			else if (retrievelevel == "SERIES")
			{
				querylevel = seriesroot;
				st.exchange_for_rowset(soci::into(row_));
				Study_DICOMQueryToSQL("series", SeriesLevelMapping, requestIdentifiers, st);			
				st.execute();
				itr = soci::rowset_iterator<soci::row>(st, row_);
			}
			else if(retrievelevel == "IMAGE")
			{
				querylevel = instanceroot;
				st.exchange_for_rowset(soci::into(row_));
				Study_DICOMQueryToSQL("instances", InstanceLevelMapping, requestIdentifiers, st);						
				st.execute();
				itr = soci::rowset_iterator<soci::row>(st, row_);
			}			
			else
				dbstatus = STATUS_FIND_Failed_UnableToProcess;		
		}
		catch(std::exception &e)
		{
			DCMNET_ERROR(e.what());
		}
	}

	// If we encountered a C-CANCEL-RQ and if we have pending
	// responses, the search shall be cancelled
	if (cancelled && DICOM_PENDING_STATUS(dbstatus))
	{
		dbstatus = STATUS_FIND_Cancel_MatchingTerminatedDueToCancelRequest;

		// normally we close the db, but we queried in one go.		
	}

	// If the dbstatus is "pending" try to select another matching record.
	if (DICOM_PENDING_STATUS(dbstatus))
	{
		if(querylevel == patientstudyroot)
		{			
			dbstatus = FindStudyLevel(itr, requestIdentifiers, responseIdentifiers);
		}
		else if(querylevel == seriesroot)
		{
			dbstatus = FindSeriesLevel(itr, requestIdentifiers, responseIdentifiers);
		}
		else if(querylevel == instanceroot)		
		{
			dbstatus = FindInstanceLevel(itr, requestIdentifiers, responseIdentifiers);
		}
	}

	DCMNET_INFO("Find SCP Response " << responseCount << " [status: " << DU_cfindStatusString( (Uint16)dbstatus ) << "]");

	if( *responseIdentifiers != NULL && (*responseIdentifiers)->card() > 0 )
	{
		// log it		
		std::stringstream log;
		log << "Response Identifiers #" << responseCount;
		(*responseIdentifiers)->print(log);
		log << "-------";
		DCMNET_INFO(log);
		log.str("");
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


/// Reads the DcmDataset and set up the statement with sql and bindings
void Study_DICOMQueryToSQL(char *tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, statement &st)
{
	int parameters = 0;
	OFString somestring;

	std::string sqlcommand;

	sqlcommand = "SELECT * FROM ";
	sqlcommand += tablename;
	sqlcommand += " LIMIT 1000 ";

	int i = 0;

	while(sqlmapping[i].columnName != NULL)
	{
		DcmElement *element;
		if(requestIdentifiers->findAndGetElement(sqlmapping[i].dicomtag, element).good() && element->getLength() > 0)
		{
			if(sqlmapping[i].dataType == dbstring)
			{
				requestIdentifiers->findAndGetOFString(sqlmapping[i].dicomtag, somestring);

				if (somestring.length() > 0 && !(somestring.length() == 1 && somestring[0] == '*'))
				{
					(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
					sqlcommand += sqlmapping[i].columnName;

					if(sqlmapping[i].useLike)
					{
						sqlcommand += " LIKE ('%' + :";
						sqlcommand += sqlmapping[i].columnName;
						sqlcommand += " + '%')";
					}
					else
					{
						sqlcommand += " = :";
						sqlcommand += sqlmapping[i].columnName;
					}

					if(somestring[somestring.length() - 1] == '*')
						somestring.erase(somestring.length() - 1, 1);

					st.exchange(use(std::string(somestring.c_str())));
					parameters++;
				}
			}
			else if(sqlmapping[i].dataType == dbint)
			{
				(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
				sqlcommand += sqlmapping[i].columnName;				
				sqlcommand += " = :";
				sqlcommand += sqlmapping[i].columnName;

				Sint32 someint = 0;
				requestIdentifiers->findAndGetSint32(sqlmapping[i].dicomtag, someint);

				st.exchange(use(someint));				
				parameters++;
			}
			else if(sqlmapping[i].dataType == dbdate)
			{
				OFDate datebuf;
				datebuf = getDate(requestIdentifiers, sqlmapping[i].dicomtag);
				if(datebuf.isValid())
				{				
					(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
					sqlcommand += sqlmapping[i].columnName;

					// todo range
					sqlcommand += " = :";
					sqlcommand += sqlmapping[i].columnName;				
					st.exchange(use(to_tm(date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()))));					
					parameters++;
				}
			}
		}

		i++;
	}	

	DCMNET_INFO("SQL " << sqlcommand);

	st.alloc();
	st.prepare(sqlcommand);
	st.define_and_bind();
}

DcmEVR GetEVR(const DcmTagKey &tagkey)
{
	DcmTag a(tagkey);
	return a.getEVR();
}

/// Create a response using the requested identifiers with data from the result
void Study_SQLToDICOMResponse(const DICOM_SQLMapping *sqlmapping, row &result, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	std::string somestring;

	int i = 0;
	while(sqlmapping[i].columnName != NULL)
	{
		DcmElement *element;
		if(requestIdentifiers->findAndGetElement(sqlmapping[i].dicomtag, element).good())
		{
			if(sqlmapping[i].dataType == dbstring)
			{			
				responseIdentifiers->putAndInsertString(sqlmapping[i].dicomtag, result.get<std::string>(sqlmapping[i].columnName).c_str());
			}
			else if(sqlmapping[i].dataType == dbint)
			{
				int numberbuf = result.get<int>(sqlmapping[i].columnName);
				DcmElement *e = newDicomElement(sqlmapping[i].dicomtag);
				if(e->getVR() == EVR_IS)
				{
					std::stringstream mycountstr;
					mycountstr << numberbuf;
					responseIdentifiers->putAndInsertString(sqlmapping[i].dicomtag, mycountstr.str().c_str());
				}
				else if(e->getVR() == EVR_SL)
					responseIdentifiers->putAndInsertSint32(sqlmapping[i].dicomtag, numberbuf);
				else if(e->getVR() == EVR_SS)
					responseIdentifiers->putAndInsertSint16(sqlmapping[i].dicomtag, numberbuf);
				else if(e->getVR() == EVR_UL)
					responseIdentifiers->putAndInsertUint32(sqlmapping[i].dicomtag, numberbuf);
				else if(e->getVR() == EVR_US)
					responseIdentifiers->putAndInsertUint16(sqlmapping[i].dicomtag, numberbuf);

				responseIdentifiers->insert(e);
			}
			else if(sqlmapping[i].dataType == dbdate)
			{
				std::tm datetimebuf = result.get<std::tm>(sqlmapping[i].columnName);
				
				OFDate datebuf;
				datebuf.setDate(datetimebuf.tm_year + 1900, datetimebuf.tm_mon + 1, datetimebuf.tm_mday);
				DcmDate *dcmdate = new DcmDate(sqlmapping[i].dicomtag);
				dcmdate->setOFDate(datebuf);
				responseIdentifiers->insert(dcmdate);
				
			}			
		}

		i++;
	}
}


// find the next record and create the response
DIC_US FindHandler::FindStudyLevel(rowset_iterator<row> &itr, DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{    
	try
    {		
		soci::rowset_iterator<soci::row> end;
        if(itr != end)
        {
			*responseIdentifiers = new DcmDataset;
			
			(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
			(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, aetitle.c_str());
			Study_SQLToDICOMResponse(PatientStudyLevelMapping, *itr, requestIdentifiers, *responseIdentifiers);
						
			// add non conforming field for PACSSCAN
			/*
			int pos = -1;
			CString modalityname;			
			context->m_pRs.GetFieldValue(_T("DCM_ModalitiesInStudy"), modalityname);
			if((pos = modalityname.Find(L"\\")) != -1)
			{				
				modalityname.Delete(pos, modalityname.GetLength());
			}

			(*responseIdentifiers)->putAndInsertString(DCM_Modality, CW2A(modalityname));
			*/
            ++itr;
			return STATUS_Pending;
        }
		else
		{			
			return STATUS_Success;
		}

    }
    catch (std::exception &e)
    {
		DCMNET_ERROR("Exception in FindStudyLevel." << e.what());
    }
	
    return STATUS_FIND_Failed_UnableToProcess;
}

DIC_US FindHandler::FindSeriesLevel(rowset_iterator<row> &itr, DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{   
    try
    {
		soci::rowset_iterator<soci::row> end;
        if(itr != end)
        {
			*responseIdentifiers = new DcmDataset;

			(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "SERIES");
			(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, aetitle.c_str());
			Study_SQLToDICOMResponse(SeriesLevelMapping, *itr, requestIdentifiers, *responseIdentifiers);
			++itr;            
			return STATUS_Pending;
        }
		else
		{
			return STATUS_Success;
		}

    }
    catch (std::exception &e)
    {
		DCMNET_ERROR("Exception in FindSeriesLevel. " << e.what());
    }
    
    return STATUS_FIND_Failed_UnableToProcess;
}

DIC_US  FindHandler::FindInstanceLevel(rowset_iterator<row> &itr, DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{    
    try
    {		
		soci::rowset_iterator<soci::row> end;
        if(itr != end)
        {
			*responseIdentifiers = new DcmDataset;

			(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "IMAGE");
			(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, aetitle.c_str());
			Study_SQLToDICOMResponse(InstanceLevelMapping, *itr, requestIdentifiers, *responseIdentifiers);
			++itr;
			return STATUS_Pending;
        }
		else
		{						
			return STATUS_Success;
		}

    }
    catch (std::exception &e)
    {
		DCMNET_ERROR("Exception in FindInstanceLevel. " << e.what());
    }
    	
    return STATUS_FIND_Failed_UnableToProcess;
}