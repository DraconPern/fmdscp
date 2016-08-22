#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/shared_ptr.hpp>
#include <Poco/Data/Session.h>
using namespace Poco::Data::Keywords;

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
typedef enum { dt_string, dt_integer, dt_date} dbtype;

struct DICOM_SQLMapping
{
	DcmTagKey dicomtag;
	char *columnName;
	dbtype dataType;
	bool useLike;		// only valid for strings
};

const DICOM_SQLMapping PatientStudyLevelMapping [] = {
	{ DCM_StudyDate,							 "StudyDate", dt_date, false},
	{ DCM_AccessionNumber,						 "AccessionNumber", dt_string, false},
	{ DCM_PatientName,                           "PatientName",  dt_string, true},
	{ DCM_PatientID,                             "PatientID", dt_string, false},
	{ DCM_StudyID,							     "StudyID", dt_string, false},
	{ DCM_StudyInstanceUID,						 "StudyInstanceUID", dt_string, false},
	{ DCM_ModalitiesInStudy,                     "ModalitiesInStudy", dt_string, true},
	{ DCM_ReferringPhysicianName,			     "ReferringPhysicianName", dt_string, false},
	{ DCM_StudyDescription,			             "StudyDescription", dt_string, true},
	{ DCM_PatientBirthDate,                      "PatientBirthDate", dt_date, false},
	{ DCM_PatientSex,                            "PatientSex", dt_string, false},
	{ DCM_CommandGroupLength, NULL, dt_string, false}
};

const DICOM_SQLMapping SeriesLevelMapping [] = {
	{ DCM_StudyInstanceUID,						 "StudyInstanceUID", dt_string, false},
	{ DCM_SeriesDate,                            "SeriesDate", dt_date, false},
	{ DCM_Modality,                              "Modality", dt_string, false},
	{ DCM_SeriesDescription,                     "SeriesDescription", dt_string, false},
	{ DCM_SeriesNumber,                          "SeriesNumber", dt_integer, false},
	{ DCM_SeriesInstanceUID,                     "SeriesInstanceUID", dt_string, false},
	{ DCM_CommandGroupLength, NULL, dt_string, false}
};

const DICOM_SQLMapping InstanceLevelMapping [] = {	
	{ DCM_SeriesInstanceUID,                     "SeriesInstanceUID", dt_string, false},
	{ DCM_InstanceNumber,                        "InstanceNumber", dt_integer, false},
	{ DCM_SOPInstanceUID,                        "SOPInstanceUID", dt_string, false},
	{ DCM_CommandGroupLength, NULL, dt_string, false}
};

void Study_DICOMQueryToSQL(std::string tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, Poco::Data::Statement &st,
						   std::vector<boost::shared_ptr<std::string> > &shared_string, std::vector<boost::shared_ptr<int> > &shared_int, std::vector<boost::shared_ptr<std::tm> > &shared_tm);

void FindHandler::FindCallback(OFBool cancelled, T_DIMSE_C_FindRQ *request, DcmDataset *requestIdentifiers, int responseCount, T_DIMSE_C_FindRSP *response, DcmDataset **responseIdentifiers, DcmDataset **statusDetail)
{	
	// Determine the data source's current status.
	DIC_US dbstatus = STATUS_Pending;

	// If this is the first time this callback function is called, we need to do open the recordset
	if ( responseCount == 1 )
	{			
		std::stringstream log;
		log << "Find SCP Request Identifiers:";
		requestIdentifiers->print(log);
		log << "-------";
		DCMNET_INFO(log.str());

		// support only study level model
		if (strcmp(request->AffectedSOPClassUID, UID_FINDStudyRootQueryRetrieveInformationModel) != 0)
		{
			response->DimseStatus = STATUS_FIND_Refused_SOPClassNotSupported;
			return;
		}

		try
		{
			// open the db
			Poco::Data::Session dbconnection(config::getConnectionString());		
			Poco::Data::Statement st(dbconnection);						

			// storage of parameters since they must exist until all result are returned
			std::vector<boost::shared_ptr<std::string> > shared_string;
			std::vector<boost::shared_ptr<std::tm> > shared_tm;
			std::vector<boost::shared_ptr<int> > shared_int;

			OFString retrievelevel;			
			requestIdentifiers->findAndGetOFString(DCM_QueryRetrieveLevel, retrievelevel);
			if (retrievelevel == "STUDY")
			{																				
				querylevel = patientstudyroot;								
				st << "" , into(patientstudies);
				Study_DICOMQueryToSQL("patient_studies", PatientStudyLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);
				st.execute();				
				patientstudies_itr = patientstudies.begin();
			} 
			else if (retrievelevel == "SERIES")
			{
				querylevel = seriesroot;
				st << "" , into(series);
				Study_DICOMQueryToSQL("series", SeriesLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);			
				st.execute();											
				series_itr = series.begin();
			}
			else if(retrievelevel == "IMAGE")
			{
				querylevel = instanceroot;				
				st << "" , into(instances);
				Study_DICOMQueryToSQL("instances", InstanceLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);						
				st.execute();			
				instances_itr = instances.begin();
			}			
			else
				dbstatus = STATUS_FIND_Failed_UnableToProcess;		
		}
		catch(std::exception &e)
		{
			DCMNET_ERROR(e.what());
			dbstatus = STATUS_FIND_Failed_UnableToProcess;		
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
			dbstatus = FindStudyLevel(requestIdentifiers, responseIdentifiers);						
		}
		else if(querylevel == seriesroot)
		{
			dbstatus = FindSeriesLevel(requestIdentifiers, responseIdentifiers);
		}
		else if(querylevel == instanceroot)		
		{
			dbstatus = FindInstanceLevel(requestIdentifiers, responseIdentifiers);
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
		DCMNET_INFO(log.str());		
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
void Study_DICOMQueryToSQL(std::string tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, Poco::Data::Statement &st, 
						   std::vector<boost::shared_ptr<std::string> > &shared_string, std::vector<boost::shared_ptr<int> > &shared_int, std::vector<boost::shared_ptr<std::tm> > &shared_tm)
{
	int parameters = 0;
	OFString somestring;

	boost::shared_ptr<std::string> shared(new std::string);

	std::string &sqlcommand = *shared;
	
	sqlcommand = "SELECT * FROM ";
	sqlcommand += tablename;	
	st << sqlcommand;

	if (tablename == "series")
	{
		sqlcommand = " INNER JOIN patient_studies ON series.patient_study_id = patient_studies.id ";
		st << sqlcommand;
	} else if (tablename == "instances")
	{
		sqlcommand = " INNER JOIN series ON instances.series_id = series.id ";
		st << sqlcommand;
	}

	int i = 0;

	while(sqlmapping[i].columnName != NULL)
	{
		DcmElement *element;
		if(requestIdentifiers->findAndGetElement(sqlmapping[i].dicomtag, element).good() && element->getLength() > 0)
		{
			if(sqlmapping[i].dataType == dt_string)
			{
				requestIdentifiers->findAndGetOFString(sqlmapping[i].dicomtag, somestring);

				if (somestring.length() > 0 && !(somestring.length() == 1 && somestring[0] == '*'))
				{
					(parameters == 0)?(sqlcommand = " WHERE "):(sqlcommand = " AND ");
					sqlcommand += sqlmapping[i].columnName;

					if(sqlmapping[i].useLike)
					{
						sqlcommand += " LIKE ?";
					}
					else
					{
						sqlcommand += " = ?";
					}

					if(somestring[somestring.length() - 1] == '*')
						somestring.erase(somestring.length() - 1, 1);

					if (sqlmapping[i].useLike)
						somestring = '%' + somestring + '%';

					st << sqlcommand, bind(somestring.c_str());
					parameters++;
				}
			}
			else if(sqlmapping[i].dataType == dt_integer)
			{
				(parameters == 0)?(sqlcommand = " WHERE "):(sqlcommand = " AND ");
				sqlcommand += sqlmapping[i].columnName;				
				sqlcommand += " = ?";				

				Sint32 someint = 0;
				requestIdentifiers->findAndGetSint32(sqlmapping[i].dicomtag, someint);

				st << sqlcommand, bind(someint);							
				parameters++;
			}
			else if(sqlmapping[i].dataType == dt_date)
			{
				OFDate datebuf;
				datebuf = getDate(requestIdentifiers, sqlmapping[i].dicomtag);
				if(datebuf.isValid())
				{				
					(parameters == 0)?(sqlcommand = " WHERE "):(sqlcommand = " AND ");
					sqlcommand += sqlmapping[i].columnName;
					sqlcommand += " > ? AND ";
					sqlcommand += sqlmapping[i].columnName;
					sqlcommand += " < ?";

					st << sqlcommand, bind(Poco::DateTime(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay())), bind(Poco::DateTime(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay(), 23, 59, 59));
					parameters++;
				}
			}
		}

		i++;
	}	

	sqlcommand = " LIMIT 1000";
	st << sqlcommand;

	DCMNET_INFO("Generated SQL: " << st.toString());	
}

void blah(std::string &member, const DcmTag &tag, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	DcmElement *element;		
	if(requestIdentifiers->findAndGetElement(tag, element).good())
		responseIdentifiers->putAndInsertString(tag, member.c_str());
}

void blah(int &member, const DcmTag &tag, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	DcmElement *element;		
	if(requestIdentifiers->findAndGetElement(tag, element).good())
	{
		DcmElement *e = newDicomElement(tag);
		if(e->getVR() == EVR_IS)
		{
			std::stringstream mycountstr;
			mycountstr << member;
			responseIdentifiers->putAndInsertString(tag, mycountstr.str().c_str());
		}
		else if(e->getVR() == EVR_SL)
			responseIdentifiers->putAndInsertSint32(tag, member);
		else if(e->getVR() == EVR_SS)
			responseIdentifiers->putAndInsertSint16(tag, member);
		else if(e->getVR() == EVR_UL)
			responseIdentifiers->putAndInsertUint32(tag, member);
		else if(e->getVR() == EVR_US)
			responseIdentifiers->putAndInsertUint16(tag, member);

		responseIdentifiers->insert(e);
	}
}


void blah(Poco::DateTime &member, const DcmTag &tag, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	DcmElement *element;		
	if(requestIdentifiers->findAndGetElement(tag, element).good())
	{
		DcmElement *e = newDicomElement(tag);
		if(e->getVR() == EVR_DA)
		{
			OFDate datebuf(member.year(), member.month(), member.day());
			DcmDate *dcmdate = new DcmDate(tag);
			dcmdate->setOFDate(datebuf);
			responseIdentifiers->insert(dcmdate);		
		}
		else if(e->getVR() == EVR_TM)
		{
			OFTime timebuf(member.hour(), member.minute(), member.second());
			DcmTime *dcmtime = new DcmTime(tag);
			dcmtime->setOFTime(timebuf);
			responseIdentifiers->insert(dcmtime);		
		}
	}
}

DIC_US FindHandler::FindStudyLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{    
	if(patientstudies_itr != patientstudies.end())
	{
		PatientStudy &patientstudy = *patientstudies_itr;
		*responseIdentifiers = new DcmDataset;

		(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
		(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, aetitle.c_str());

		blah(patientstudy.StudyInstanceUID, DCM_StudyInstanceUID, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.StudyID, DCM_StudyID, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.AccessionNumber, DCM_AccessionNumber, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.PatientName, DCM_PatientName, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.PatientID, DCM_PatientID, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.StudyID, DCM_StudyID, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.StudyDate, DCM_StudyDate, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.StudyDate, DCM_StudyTime, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.ModalitiesInStudy, DCM_ModalitiesInStudy, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.StudyDescription, DCM_StudyDescription, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.PatientSex, DCM_PatientSex, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.PatientBirthDate, DCM_PatientBirthDate, requestIdentifiers, *responseIdentifiers);
		blah(patientstudy.ReferringPhysicianName, DCM_ReferringPhysicianName, requestIdentifiers, *responseIdentifiers);

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
		++patientstudies_itr;
		return STATUS_Pending;
	}
	else
	{			
		return STATUS_Success;
	}

	return STATUS_FIND_Failed_UnableToProcess;
}


DIC_US FindHandler::FindSeriesLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{   
	if(series_itr != series.end())
	{
		Series &series = *series_itr;
		*responseIdentifiers = new DcmDataset;

		(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "SERIES");
		(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, aetitle.c_str());
		blah(series.SeriesInstanceUID, DCM_SeriesInstanceUID,  requestIdentifiers, *responseIdentifiers);
		blah(series.Modality, DCM_Modality,  requestIdentifiers, *responseIdentifiers);
		blah(series.SeriesDescription, DCM_SeriesDescription,  requestIdentifiers, *responseIdentifiers);
		blah(series.SeriesNumber, DCM_SeriesNumber,  requestIdentifiers, *responseIdentifiers);
		blah(series.SeriesDate, DCM_SeriesDate,  requestIdentifiers, *responseIdentifiers);
		blah(series.SeriesDate, DCM_SeriesTime,  requestIdentifiers, *responseIdentifiers);

		++series_itr;            
		return STATUS_Pending;
	}
	else
	{
		return STATUS_Success;
	}

	return STATUS_FIND_Failed_UnableToProcess;
}

DIC_US  FindHandler::FindInstanceLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{        
	if(instances_itr != instances.end())
	{
		Instance &instance = *instances_itr;
		*responseIdentifiers = new DcmDataset;

		(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "IMAGE");
		(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, aetitle.c_str());
		blah(instance.SOPInstanceUID, DCM_SOPInstanceUID, requestIdentifiers, *responseIdentifiers);
		blah(instance.InstanceNumber, DCM_InstanceNumber, requestIdentifiers, *responseIdentifiers);

		++instances_itr;
		return STATUS_Pending;
	}
	else
	{						
		return STATUS_Success;
	}


	return STATUS_FIND_Failed_UnableToProcess;
}