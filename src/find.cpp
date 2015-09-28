#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/shared_ptr.hpp>
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
// typedef enum {soci::dt_string, soci::dt_integer, soci::dt_date} dbtype;

struct DICOM_SQLMapping
{
	DcmTagKey dicomtag;
	char *columnName;
	enum data_type dataType;
	bool useLike;		// only valid for strings
};

const DICOM_SQLMapping PatientStudyLevelMapping [] = {
	{ DCM_StudyDate,							 "StudyDate", soci::dt_date, false},
	{ DCM_AccessionNumber,						 "AccessionNumber", soci::dt_string, false},
	{ DCM_PatientName,                           "PatientName",  soci::dt_string, true},
	{ DCM_PatientID,                             "PatientID", soci::dt_string, false},
	{ DCM_StudyID,							     "StudyID", soci::dt_string, false},
	{ DCM_StudyInstanceUID,						 "StudyInstanceUID", soci::dt_string, false},
	{ DCM_ModalitiesInStudy,                     "ModalitiesInStudy", soci::dt_string, true},
	{ DCM_ReferringPhysicianName,			     "ReferringPhysicianName", soci::dt_string, false},
	{ DCM_StudyDescription,			             "StudyDescription", soci::dt_string, true},
	{ DCM_PatientBirthDate,                      "PatientBirthDate", soci::dt_date, false},
	{ DCM_PatientSex,                            "PatientSex", soci::dt_string, false},
	{ DCM_CommandGroupLength, NULL, soci::dt_string, false}
};

const DICOM_SQLMapping SeriesLevelMapping [] = {
	{ DCM_StudyInstanceUID,						 "StudyInstanceUID", soci::dt_string, false},
	{ DCM_SeriesDate,                            "SeriesDate", soci::dt_date, false},
	{ DCM_Modality,                              "Modality", soci::dt_string, false},
	{ DCM_SeriesDescription,                     "SeriesDescription", soci::dt_string, false},
	{ DCM_SeriesNumber,                          "SeriesNumber", soci::dt_integer, false},
	{ DCM_SeriesInstanceUID,                     "SeriesInstanceUID", soci::dt_string, false},
	{ DCM_CommandGroupLength, NULL, soci::dt_string, false}
};

const DICOM_SQLMapping InstanceLevelMapping [] = {	
	{ DCM_SeriesInstanceUID,                     "SeriesInstanceUID", soci::dt_string, false},
	{ DCM_InstanceNumber,                        "InstanceNumber", soci::dt_integer, false},
	{ DCM_SOPInstanceUID,                        "SOPInstanceUID", soci::dt_string, false},
	{ DCM_CommandGroupLength, NULL, soci::dt_string, false}
};

void Study_DICOMQueryToSQL(char *tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, statement &st,
						   std::vector<boost::shared_ptr<std::string> > &shared_string, std::vector<boost::shared_ptr<int> > &shared_int, std::vector<boost::shared_ptr<std::tm> > &shared_tm);

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
			session dbconnection(Config::getConnectionString());		
			statement st(dbconnection);						

			// storage of parameters since they must exist until all result are returned
			std::vector<boost::shared_ptr<std::string> > shared_string;
			std::vector<boost::shared_ptr<std::tm> > shared_tm;
			std::vector<boost::shared_ptr<int> > shared_int;

			OFString retrievelevel;			
			requestIdentifiers->findAndGetOFString(DCM_QueryRetrieveLevel, retrievelevel);
			if (retrievelevel == "STUDY")
			{																				
				querylevel = patientstudyroot;
				PatientStudy result;
				st.exchange(into(result));
				Study_DICOMQueryToSQL("patient_studies", PatientStudyLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);
				st.execute();
				soci::rowset_iterator<PatientStudy > itr(st, result);
				soci::rowset_iterator<PatientStudy > end;				
				std::copy(itr, end, std::back_inserter(patientstudies));				
				patientstudies_itr = patientstudies.begin();
			} 
			else if (retrievelevel == "SERIES")
			{
				querylevel = seriesroot;
				Series result;
				st.exchange(into(result));
				Study_DICOMQueryToSQL("series", SeriesLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);			
				st.execute();
				soci::rowset_iterator<Series > itr(st, result);
				soci::rowset_iterator<Series > end;				
				std::copy(itr, end, std::back_inserter(series));				
				series_itr = series.begin();
			}
			else if(retrievelevel == "IMAGE")
			{
				querylevel = instanceroot;
				Instance result;
				st.exchange(into(result));
				Study_DICOMQueryToSQL("instances", InstanceLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);						
				st.execute();
				soci::rowset_iterator<Instance > itr(st, result);
				soci::rowset_iterator<Instance > end;				
				std::copy(itr, end, std::back_inserter(instances));				
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
void Study_DICOMQueryToSQL(char *tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, statement &st, 
						   std::vector<boost::shared_ptr<std::string> > &shared_string, std::vector<boost::shared_ptr<int> > &shared_int, std::vector<boost::shared_ptr<std::tm> > &shared_tm)
{
	int parameters = 0;
	OFString somestring;

	boost::shared_ptr<std::string> shared(new std::string);

	std::string &sqlcommand = *shared;

	sqlcommand = "SELECT * FROM ";
	sqlcommand += tablename;	

	int i = 0;

	while(sqlmapping[i].columnName != NULL)
	{
		DcmElement *element;
		if(requestIdentifiers->findAndGetElement(sqlmapping[i].dicomtag, element).good() && element->getLength() > 0)
		{
			if(sqlmapping[i].dataType == soci::dt_string)
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

					boost::shared_ptr<std::string> shared(new std::string(somestring.c_str()));
					shared_string.push_back(shared);
					st.exchange(use(*shared));
					parameters++;
				}
			}
			else if(sqlmapping[i].dataType == soci::dt_integer)
			{
				(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
				sqlcommand += sqlmapping[i].columnName;				
				sqlcommand += " = :";
				sqlcommand += sqlmapping[i].columnName;

				Sint32 someint = 0;
				requestIdentifiers->findAndGetSint32(sqlmapping[i].dicomtag, someint);

				boost::shared_ptr<int> shared(new int(someint));
				shared_int.push_back(shared);
				st.exchange(use(*shared));				
				parameters++;
			}
			else if(sqlmapping[i].dataType == soci::dt_date)
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

					boost::shared_ptr<std::tm> shared(new std::tm(to_tm(date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()))));
					shared_tm.push_back(shared);
					st.exchange(use(*shared));					
					parameters++;
				}
			}
		}

		i++;
	}	

	sqlcommand += " LIMIT 1000";

	DCMNET_INFO("SQL " << sqlcommand);

	st.alloc();
	st.prepare(sqlcommand);
	st.define_and_bind();
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


void blah(std::tm &member, const DcmTag &tag, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	DcmElement *element;		
	if(requestIdentifiers->findAndGetElement(tag, element).good())
	{
		DcmElement *e = newDicomElement(tag);
		if(e->getVR() == EVR_DA)
		{
			OFDate datebuf(member.tm_year + 1900, member.tm_mon + 1, member.tm_mday);
			DcmDate *dcmdate = new DcmDate(tag);
			dcmdate->setOFDate(datebuf);
			responseIdentifiers->insert(dcmdate);		
		}
		else if(e->getVR() == EVR_TM)
		{
			OFTime timebuf(member.tm_hour, member.tm_min, member.tm_sec);
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
		blah(series.StudyInstanceUID, DCM_StudyInstanceUID,  requestIdentifiers, *responseIdentifiers);
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
		blah(instance.SeriesInstanceUID, DCM_SeriesInstanceUID, requestIdentifiers, *responseIdentifiers);
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