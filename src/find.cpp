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
#include "dcmtk/dcmnet/scu.h"

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

FindHandler::FindHandler(std::string aetitle, DBPool &dbpool) : aetitle(aetitle), dbpool(dbpool)
{	
	
}


// page 299 (sample only), 1433, 1439, 1691, 1473
// Study ROOT
typedef enum { dt_string, dt_integer, dt_date} dbtype;

struct DICOM_SQLMapping
{
	DcmTagKey dicomtag;
	const char *columnName;
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
	{ DCM_PatientBirthDate,                      "PatientBirthDate", dt_date, false},
	{ DCM_NumberOfStudyRelatedInstances,         "NumberOfStudyRelatedInstances", dt_integer, false },
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
	if (cancelled)
	{
		response->DimseStatus = STATUS_FIND_Cancel_MatchingTerminatedDueToCancelRequest;
		return;
	}

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

		if (!QueryDatabase(requestIdentifiers))
		{
			response->DimseStatus = STATUS_FIND_Failed_UnableToProcess;
			return;
		}
	}
	
	DIC_US dbstatus = STATUS_Pending;
	
	if(querylevel == patientstudyroot)
	{			
		dbstatus = GetNextStudy(requestIdentifiers, responseIdentifiers);						
	}
	else if(querylevel == seriesroot)
	{
		dbstatus = GetNextSeries(requestIdentifiers, responseIdentifiers);
	}
	else if(querylevel == instanceroot)		
	{
		dbstatus = GetNextInstance(requestIdentifiers, responseIdentifiers);
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

	if (dbstatus == STATUS_Success)
	{
		// local query is done.  do migration query
		/*if (config.)
		
		// clear out the old stuff
		patientstudies.clear();
		series.clear();
		instances.clear();

		if (QueryMigrateSCP(requestIdentifiers))
			dbstatus = STATUS_Pending;
			*/
	}

	// Set response status
	response->DimseStatus = dbstatus;	
}

bool FindHandler::QueryDatabase(DcmDataset *requestIdentifiers)
{	
	try
	{
		// open the db
		Poco::Data::Session dbconnection(dbpool.get());
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
			st << "", into(patientstudies);
			Study_DICOMQueryToSQL("patient_studies", PatientStudyLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);
			st.execute();
			patientstudies_itr = patientstudies.begin();
		}
		else if (retrievelevel == "SERIES")
		{
			querylevel = seriesroot;
			st << "", into(series);
			Study_DICOMQueryToSQL("series", SeriesLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);
			st.execute();
			series_itr = series.begin();
		}
		else if (retrievelevel == "IMAGE")
		{
			querylevel = instanceroot;
			st << "", into(instances);
			Study_DICOMQueryToSQL("instances", InstanceLevelMapping, requestIdentifiers, st, shared_string, shared_int, shared_tm);
			st.execute();
			instances_itr = instances.begin();
		}
		else
			return false;
	}
	catch (std::exception &e)
	{
		DCMNET_ERROR(e.what());
		return false;
	}

	return true;
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

void blah2(std::string &member, const DcmTag &tag, DcmDataset *responseIdentifiers)
{	
	OFString p;
	if (responseIdentifiers->findAndGetOFString(tag, p).good())
	{				
		member = p.c_str();
	}
}

void blah(int &member, const DcmTag &tag, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	DcmElement *element;		
	if(requestIdentifiers->findAndGetElement(tag, element).good())
	{
		DcmElement *e = DcmItem::newDicomElement(tag);
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

void blah2(int &member, const DcmTag &tag, DcmDataset *responseIdentifiers)
{
	Sint32 v;
	if (responseIdentifiers->findAndGetSint32(tag, v).good())
	{		
		member = v;		
	}
}

void blah(Poco::DateTime &member, const DcmTag &tag, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
{
	DcmElement *element;		
	if(requestIdentifiers->findAndGetElement(tag, element).good())
	{
		DcmElement *e = DcmItem::newDicomElement(tag);
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

void blah2(Poco::DateTime &member, const DcmTag &tag, DcmDataset *responseIdentifiers)
{
	DcmElement *element;
	if (responseIdentifiers->findAndGetElement(tag, element).good())
	{		
		if (element->getVR() == EVR_DA)
		{
			DcmDate *dcmdate = (DcmDate *)element;
			OFDate datebuf;
			if (dcmdate->getOFDate(datebuf).good())
				member.assign(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay());
		}
		else if (element->getVR() == EVR_TM)
		{
			OFTime timebuf;
			DcmTime *dcmtime = (DcmTime *)element;
			if (dcmtime->getOFTime(timebuf).good())
				member.assign(0, 0, 0, timebuf.getHour(), timebuf.getMinute(), timebuf.getMinute());
		}
	}
}

DIC_US FindHandler::GetNextStudy(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
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
		blah(patientstudy.NumberOfStudyRelatedInstances, DCM_NumberOfStudyRelatedInstances, requestIdentifiers, *responseIdentifiers);
		
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


DIC_US FindHandler::GetNextSeries(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
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

DIC_US  FindHandler::GetNextInstance(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
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


bool FindHandler::QueryMigrateSCP(DcmDataset *requestIdentifiers, Destination &destination)
{
	class MyDcmSCU : public DcmSCU
	{
	public:
		MyDcmSCU(FindHandler &finder) : finder(finder) {}		
		FindHandler &finder;
		OFCondition handleFINDResponse(const T_ASC_PresentationContextID presID,
			QRResponse *response,
			OFBool &waitForNextResponse)
		{
			OFCondition ret = DcmSCU::handleFINDResponse(presID, response, waitForNextResponse);

			if (ret.good() && response->m_dataset != NULL)
			{
				if (finder.querylevel == patientstudyroot)
				{
					PatientStudy patientstudy;
					blah2(patientstudy.StudyInstanceUID, DCM_StudyInstanceUID, response->m_dataset);
					blah2(patientstudy.StudyID, DCM_StudyID, response->m_dataset);
					blah2(patientstudy.AccessionNumber, DCM_AccessionNumber, response->m_dataset);
					blah2(patientstudy.PatientName, DCM_PatientName, response->m_dataset);
					blah2(patientstudy.PatientID, DCM_PatientID, response->m_dataset);
					blah2(patientstudy.StudyID, DCM_StudyID, response->m_dataset);
					blah2(patientstudy.StudyDate, DCM_StudyDate, response->m_dataset);
					blah2(patientstudy.StudyDate, DCM_StudyTime, response->m_dataset);
					blah2(patientstudy.ModalitiesInStudy, DCM_ModalitiesInStudy, response->m_dataset);
					blah2(patientstudy.StudyDescription, DCM_StudyDescription, response->m_dataset);
					blah2(patientstudy.PatientSex, DCM_PatientSex, response->m_dataset);
					blah2(patientstudy.PatientBirthDate, DCM_PatientBirthDate, response->m_dataset);
					blah2(patientstudy.ReferringPhysicianName, DCM_ReferringPhysicianName, response->m_dataset);
					blah2(patientstudy.NumberOfStudyRelatedInstances, DCM_NumberOfStudyRelatedInstances, response->m_dataset);

					finder.patientstudies.push_back(patientstudy);
				}
				else if (finder.querylevel == seriesroot)
				{
					Series series;
					blah2(series.SeriesInstanceUID, DCM_SeriesInstanceUID, response->m_dataset);
					blah2(series.Modality, DCM_Modality, response->m_dataset);
					blah2(series.SeriesDescription, DCM_SeriesDescription, response->m_dataset);
					blah2(series.SeriesNumber, DCM_SeriesNumber, response->m_dataset);
					blah2(series.SeriesDate, DCM_SeriesDate, response->m_dataset);
					blah2(series.SeriesDate, DCM_SeriesTime, response->m_dataset);

					finder.series.push_back(series);
				}
				else if (finder.querylevel == instanceroot)
				{
					Instance instance;
					blah2(instance.SOPInstanceUID, DCM_SOPInstanceUID, response->m_dataset);
					blah2(instance.InstanceNumber, DCM_InstanceNumber, response->m_dataset);

					finder.instances.push_back(instance);
				}
			}


			return ret;
		}
	};

	MyDcmSCU scu(*this);
	
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

	scu.addPresentationContext(UID_FINDStudyRootQueryRetrieveInformationModel, defaulttransfersyntax);

	OFCondition cond;

	if (scu.initNetwork().bad())
		return false;

	if (scu.negotiateAssociation().bad())
		return false;

	T_ASC_PresentationContextID pid = scu.findAnyPresentationContextID(UID_FINDStudyRootQueryRetrieveInformationModel, UID_LittleEndianExplicitTransferSyntax);

	DcmDataset query(*requestIdentifiers);
	query.putAndInsertString(DCM_StudyInstanceUID, "", false);
	query.putAndInsertString(DCM_PatientName, "", false);
	query.putAndInsertString(DCM_PatientID, "", false);
	query.putAndInsertString(DCM_StudyDate, "", false);
	query.putAndInsertString(DCM_StudyDescription, "", false);
	query.putAndInsertSint16(DCM_NumberOfStudyRelatedInstances, 0, false);
	scu.sendFINDRequest(pid, &query, NULL);

	scu.releaseAssociation();

}