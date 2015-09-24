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
			dbstatus = FindImageLevel(requestIdentifiers, responseIdentifiers);
		}
		else if (retrievelevel == "SERIES")
		{
			dbstatus = FindSeriesLevel(requestIdentifiers, responseIdentifiers);
		}
		else if (retrievelevel == "STUDY")
		{
			dbstatus = FindStudyLevel(requestIdentifiers, responseIdentifiers);
		}
		else
			dbstatus = STATUS_FIND_Failed_UnableToProcess;
	}

	// DEBUGLOG(sessionguid, DB_INFO, "Worklist Find SCP Response %d [status: %s]\r\n", responseCount, CA2W(DU_cfindStatusString( (Uint16)dbstatus )));

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


// page 299 (sample only), 1433, 1439, 1691, 1473
// Study ROOT
typedef enum {dbstring, dbint, dbdate, dbtime} dbtype;

struct DICOM_SQLMapping
{
	DcmTagKey dicomtag;
	char *columnName;
	dbtype dataType;
	bool useLike;		// only valid for strings
};

const DICOM_SQLMapping PatientStudyLevelMapping [] = {
	{ DCM_StudyDate,							 "DCM_StudyDate", dbdate, false},
	{ DCM_StudyTime,							 "DCM_StudyTime", dbtime, false},
	{ DCM_AccessionNumber,						 "DCM_AccessionNumber", dbstring, false},
	{ DCM_PatientName,                          "DCM_PatientsName",  dbstring, true},
	{ DCM_PatientID,                             "DCM_PatientID", dbstring, false},
	{ DCM_StudyID,							     "DCM_StudyID", dbstring, false},
	{ DCM_StudyInstanceUID,						 "DCM_StudyInstanceUID", dbstring, false},
	{ DCM_ModalitiesInStudy,					 "DCM_ModalitiesInStudy", dbstring, true},
	{ DCM_SOPClassesInStudy,					 "DCM_SOPClassesInStudy", dbstring, true},
	{ DCM_ReferringPhysicianName,			     "DCM_ReferringPhysiciansName", dbstring, false},
	{ DCM_StudyDescription,						 "DCM_StudyDescription", dbstring, true},
	/*{ DCM_ProcedureCodeSequence
	>DCM_CodeValue
	>DCM_CodingSchemeDesignator
	>DCM_CodingSchemeVersion
	>DCM_CodeMeaning*/
	{ DCM_NameOfPhysiciansReadingStudy,			 "DCM_NameOfPhysiciansReadingStudy", dbstring, false},
	{ DCM_AdmittingDiagnosesDescription,		 "DCM_AdmittingDiagnosesDescription", dbstring, false},
	/*DCM_ReferencedStudySequence
	>DCM_ReferencedSOPClassUID
	>DCM_ReferencedSOPInstanceUID
	DCM_ReferencedPatientSequence
	>DCM_ReferencedSOPClassUID
	>DCM_ReferencedSOPInstanceUID*/
	{ DCM_IssuerOfPatientID,					 "DCM_IssuerOfPatientID", dbstring, false},
	{ DCM_PatientBirthDate,                     "DCM_PatientsBirthDate", dbdate, false},
	{ DCM_PatientBirthTime,                     "DCM_PatientsBirthTime", dbtime, false},
	{ DCM_PatientSex,                           "DCM_PatientsSex", dbstring, false},
	{ DCM_OtherPatientIDs,                       "DCM_OtherPatientIDs", dbstring, true},
	{ DCM_OtherPatientNames,                     "DCM_OtherPatientNames", dbstring, false},
	{ DCM_PatientAge,							 "DCM_PatientsAge", dbstring, false},
	{ DCM_PatientSize,							 "DCM_PatientsSize", dbstring, false},
	{ DCM_PatientWeight,                        "DCM_PatientsWeight", dbstring, false},
	{ DCM_EthnicGroup,                           "DCM_EthnicGroup", dbstring, false},
	{ DCM_Occupation,                            "DCM_Occupation", dbstring, false},
	{ DCM_AdditionalPatientHistory,              "DCM_AdditionalPatientHistory", dbstring, false},
	{ DCM_PatientComments,                       "DCM_PatientComments", dbstring, false},
	{ DCM_RETIRED_OtherStudyNumbers,                     "DCM_OtherStudyNumbers", dbstring, false},
	{ DCM_NumberOfPatientRelatedStudies,         "DCM_NumberOfPatientRelatedStudies", dbint, false},
	{ DCM_NumberOfPatientRelatedSeries,          "DCM_NumberOfPatientRelatedSeries", dbint, false},
	{ DCM_NumberOfPatientRelatedInstances,       "DCM_NumberOfPatientRelatedInstances", dbint, false},
	{ DCM_NumberOfStudyRelatedSeries,            "DCM_NumberOfStudyRelatedSeries", dbint, false},
	{ DCM_NumberOfStudyRelatedInstances,         "DCM_NumberOfStudyRelatedInstances", dbint, false},
	{ DCM_RETIRED_InterpretationAuthor,                  "DCM_InterpretationAuthor", dbstring, false},
	{ DCM_CommandGroupLength, NULL, dbstring, false}
};

const DICOM_SQLMapping SeriesLevelMapping [] = {
	{ DCM_StudyInstanceUID,						 "DCM_StudyInstanceUID", dbstring, false},
	{ DCM_SeriesDate,                            "DCM_SeriesDate", dbdate, false},
	{ DCM_SeriesTime,						     "DCM_SeriesTime", dbtime, false},
	{ DCM_Modality,                              "DCM_Modality", dbstring, false},
	{ DCM_SeriesDescription,                     "DCM_SeriesDescription", dbstring, false},
	{ DCM_SeriesNumber,                          "DCM_SeriesNumber", dbint, false},
	{ DCM_SeriesInstanceUID,                     "DCM_SeriesInstanceUID", dbstring, false},
	{ DCM_NumberOfSeriesRelatedInstances,        "DCM_NumberOfSeriesRelatedInstances", dbint, false},
	{ DCM_CommandGroupLength, NULL, dbstring, false}
};

const DICOM_SQLMapping ImageLevelMapping [] = {
	{ DCM_StudyInstanceUID,						 "DCM_StudyInstanceUID", dbstring, false},
	{ DCM_SeriesInstanceUID,                     "DCM_SeriesInstanceUID", dbstring, false},
	{ DCM_InstanceNumber,                        "DCM_InstanceNumber", dbint, false},
	{ DCM_SOPInstanceUID,                        "DCM_SOPInstanceUID", dbstring, false},
	{ DCM_SOPClassUID,                           "DCM_SOPClassUID", dbstring, false},
	/*DCM_AlternateRepresentationSequence
	>DCM_SeriesInstanceUID
	>DCM_ReferencedSOPClassUID
	>DCM_ReferencedSOPInstanceUID
	>DCM_PurposeOfReferenceCodeSequence
	>>DCM_CodeValue
	>>DCM_CodingSchemeDesignator
	>>DCM_CodingSchemeVersion
	>>DCM_CodeMeaning*/
	{ DCM_RelatedGeneralSOPClassUID,             "DCM_RelatedGeneralSOPClassUID", dbstring, false},
	/*DCM_ConceptNameCodeSequence
	>DCM_CodeValue
	>DCM_CodingSchemeDesignator
	>DCM_CodingSchemeVersion
	>DCM_CodeMeaning
	DCM_ContentTemplateSequence
	>DCM_TemplateIdentifier
	>DCM_MappingResource*/
	{ DCM_CommandGroupLength, NULL, dbstring, false}
};


void Study_DICOMQueryToSQL(char *tablename, const DICOM_SQLMapping *sqlmapping, DcmDataset *requestIdentifiers, CADOCommand &command)
{
	int parameters = 0;
	OFString somestring;

	std::string sqlcommand;

	sqlcommand = "SELECT * FROM ";
	sqlcommand += tablename;

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
						sqlcommand += " LIKE ('%' + ? + '%')";
					}
					else
						sqlcommand += " = ?";

					if(somestring[somestring.length() - 1] == '*')
						somestring.erase(somestring.length() - 1, 1);

					command.AddParameter(_T(""), dbstring, ADODB::adParamInput, somestring.length(), CString(somestring.c_str()));
					parameters++;
				}
			}
			else if(sqlmapping[i].dataType == dbint)
			{
				(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
				sqlcommand += sqlmapping[i].columnName;

				Sint32 someint = 0;
				requestIdentifiers->findAndGetSint32(sqlmapping[i].dicomtag, someint);

				sqlcommand += " = ?";

				command.AddParameter(_T(""), dbint, ADODB::adParamInput, sizeof(Sint32), someint);
				parameters++;
			}
			else if(sqlmapping[i].dataType == dbdate)
			{
				OFDate datebuf;
				datebuf = getDate(requestIdentifiers, sqlmapping[i].dicomtag);
				if(datebuf.isValid())
				{
					COleDateTime a(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay(), 0, 0, 0);

					(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
					sqlcommand += sqlmapping[i].columnName;

					// todo range
					sqlcommand += " = ?";

					command.AddParameter(_T(""), dbdate, ADODB::adParamInput, sizeof(a), a);
					parameters++;
				}
			}
			else if(sqlmapping[i].dataType == dbtime)
			{
				OFTime timebuf;
				timebuf = getTime(requestIdentifiers, sqlmapping[i].dicomtag);
				if(timebuf.isValid())
				{
					COleDateTime a(1900, 1, 1, timebuf.getHour(), timebuf.getMinute(), timebuf.getIntSecond());

					(parameters == 0)?(sqlcommand += " WHERE "):(sqlcommand += " AND ");
					sqlcommand += sqlmapping[i].columnName;

					// todo range
					sqlcommand += " = ?";

					command.AddParameter(_T(""), dbtime, ADODB::adParamInput, sizeof(a), a);
					parameters++;
				}
			}
		}

		i++;
	}

	command.SetText(sqlcommand);
}

DcmEVR GetEVR(const DcmTagKey &tagkey)
{
	DcmTag a(tagkey);
	return a.getEVR();
}

/*
Create a response using the requested identifiers with data from the result
*/
void Study_SQLToDICOMResponse(const DICOM_SQLMapping *sqlmapping, CADORecordset &result, DcmDataset *requestIdentifiers, DcmDataset *responseIdentifiers)
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
				CString textbuf;
				result.GetFieldValue(sqlmapping[i].columnName, textbuf);
				responseIdentifiers->putAndInsertString(sqlmapping[i].dicomtag, CW2A(textbuf));
			}
			else if(sqlmapping[i].dataType == dbint)
			{
				long numberbuf;
				result.GetFieldValue(sqlmapping[i].columnName, numberbuf);
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
				COleDateTime datetimebuf;
				result.GetFieldValue(sqlmapping[i].columnName, datetimebuf);
				if (datetimebuf.GetStatus() == COleDateTime::valid)
				{
					OFDate datebuf;
					datebuf.setDate(datetimebuf.GetYear(), datetimebuf.GetMonth(), datetimebuf.GetDay());
					DcmDate *dcmdate = new DcmDate(sqlmapping[i].dicomtag);
					dcmdate->setOFDate(datebuf);
					responseIdentifiers->insert(dcmdate);
				}
			}
			else if(sqlmapping[i].dataType == dbtime)
			{
				COleDateTime datetimebuf;
				result.GetFieldValue(sqlmapping[i].columnName, datetimebuf);
				if (datetimebuf.GetStatus() == COleDateTime::valid)
				{
					OFTime timebuf;
					timebuf.setTime(datetimebuf.GetHour(), datetimebuf.GetMinute(), datetimebuf.GetSecond());
					DcmTime *dcmtime = new DcmTime(sqlmapping[i].dicomtag);
					dcmtime->setOFTime(timebuf);
					responseIdentifiers->insert(dcmtime);
				}
			}
		}

		i++;
	}
}


// find the next record and create the response
DIC_US FindHandler::FindStudyLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{
    const wchar_t *sessionguid = context->sessionguid;

	try
    {
		if(!context->m_pDb.IsOpen())
		{					
			OFString patientid, patientname;
			
			requestIdentifiers->findAndGetOFString(DCM_PatientName, patientname);	
			
			context->m_pDb.Open(getConnectionString());

			CADOCommand command(&context->m_pDb, _T(""), CADOCommand::typeCmdText);			
			Study_DICOMQueryToSQL(L"patientstudies", PatientStudyLevelMapping, requestIdentifiers, command);

			DEBUGLOG(sessionguid, DB_INFO, L"SQL %s\r\n", command.GetText());
			/*
			// build the command
			CADOCommand command(&context->m_pDb, _T(""), CADOCommand::typeCmdText);			
	        CString sqlcommand = _T("SELECT [DCM_StudyInstanceUID],[DCM_PatientsName],[DCM_PatientID],[DCM_ModalitiesInStudy],[DCM_StudyDate],[DCM_StudyTime],[DCM_StudyDescription],[DCM_PatientsSex],[DCM_PatientsBirthDate],[DCM_NumberOfStudyRelatedInstances] FROM patientstudies");

			int parameters = 0;

			// find if there's a name
			if (patientname.length() > 0 && patientname[patientname.length() - 1] == '*')
					patientname.erase(patientname.length() - 1, 1);

            if (patientname.length() > 0)
            {
                if(parameters == 0)
                    sqlcommand += L" WHERE ";
                else
                    sqlcommand += L" AND ";
                sqlcommand += L"[DCM_PatientsName] LIKE ('%' + ? + '%')";
				
				if(patientname[patientname.length() - 1] == '*')
					patientname.erase(patientname.length() - 1, 1);

                command.AddParameter(_T(""), ADODB::adVarWChar, ADODB::adParamInput, patientname.length(), CString(patientname.c_str()));
                parameters++;
            }

			requestIdentifiers->findAndGetOFString(DCM_PatientID, patientid);
            if (patientid.length())
            {
                if(parameters == 0)
                    sqlcommand += L" WHERE ";
                else
                    sqlcommand += L" AND ";
				sqlcommand += L"[DCM_PatientID] = ?";
				//command.SetText(sqlcommand);
                command.AddParameter(_T(""), ADODB::adVarWChar, ADODB::adParamInput, patientid.length(), CString(patientid.c_str()));
                parameters++;
			}

			DEBUGLOG(sessionguid, DB_INFO, L"%s\r\n", sqlcommand);
			command.SetText(sqlcommand);*/
			context->m_pRs = CADORecordset(&context->m_pDb);
			context->m_pRs.Open(&command);
		}

        if(!context->m_pRs.IsEOF())
        {			

			*responseIdentifiers = new DcmDataset;
			
			(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "STUDY");
			(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, context->ourAETitle);
			Study_SQLToDICOMResponse(PatientStudyLevelMapping, context->m_pRs, requestIdentifiers, *responseIdentifiers);
						
			// add non conforming field for PACSSCAN
			int pos = -1;
			CString modalityname;			
			context->m_pRs.GetFieldValue(_T("DCM_ModalitiesInStudy"), modalityname);
			if((pos = modalityname.Find(L"\\")) != -1)
			{				
				modalityname.Delete(pos, modalityname.GetLength());
			}

			(*responseIdentifiers)->putAndInsertString(DCM_Modality, CW2A(modalityname));

			/*
            CString studyuid, name, patientid, modalityname;			

			CString textbuf;			
			OFDate datebuf;
			OFTime timebuf;
			COleDateTime datetimebuf;
			DcmDate *dcmdate;
			DcmTime *dcmtime;

			
			context->m_pRs.GetFieldValue(_T("DCM_StudyInstanceUID"), studyuid);
			context->m_pRs.GetFieldValue(_T("DCM_PatientsName"), name);
			context->m_pRs.GetFieldValue(_T("DCM_PatientID"), patientid);
			context->m_pRs.GetFieldValue(_T("DCM_ModalitiesInStudy"), modalityname);									

	        (*responseIdentifiers)->putAndInsertString(DCM_StudyInstanceUID, CW2A(studyuid));		    
	        (*responseIdentifiers)->putAndInsertString(DCM_PatientsName, CW2A(name));
			(*responseIdentifiers)->putAndInsertString(DCM_PatientID, CW2A(patientid));
			(*responseIdentifiers)->putAndInsertString(DCM_ModalitiesInStudy, CW2A(modalityname));			

			//(*responseIdentifiers)->putAndInsertString(DCM_StudyID, "");			
			
			context->m_pRs.GetFieldValue(_T("DCM_StudyDate"), datetimebuf);	
			datebuf.setDate(datetimebuf.GetYear(), datetimebuf.GetMonth(), datetimebuf.GetDay());
			dcmdate = new DcmDate(DCM_StudyDate);
			dcmdate->setOFDate(datebuf);
			(*responseIdentifiers)->insert(dcmdate);
			
			context->m_pRs.GetFieldValue(_T("DCM_StudyTime"), datetimebuf);	
			timebuf.setTime(datetimebuf.GetHour(), datetimebuf.GetMinute(), datetimebuf.GetSecond());
			dcmtime = new DcmTime(DCM_StudyTime);
			dcmtime->setOFTime(timebuf);
			(*responseIdentifiers)->insert(dcmtime);
									
			context->m_pRs.GetFieldValue(_T("DCM_StudyDescription"), textbuf);
			(*responseIdentifiers)->putAndInsertString(DCM_StudyDescription, CW2A(textbuf));
			context->m_pRs.GetFieldValue(_T("DCM_PatientsSex"), textbuf);
			(*responseIdentifiers)->putAndInsertString(DCM_PatientsSex, CW2A(textbuf));

			context->m_pRs.GetFieldValue(_T("DCM_PatientsBirthDate"), datetimebuf);	
			if (datetimebuf.GetStatus() == COleDateTime::valid)	// some dicom files don't have birthday
			{
				datebuf.setDate(datetimebuf.GetYear(), datetimebuf.GetMonth(), datetimebuf.GetDay());
				dcmdate = new DcmDate(DCM_PatientsBirthDate);
				dcmdate->setOFDate(datebuf);
				(*responseIdentifiers)->insert(dcmdate);
			}
			else
			{
				(*responseIdentifiers)->insert(dcmdate);
			}

			long mycount = 0;
			context->m_pRs.GetFieldValue(_T("DCM_NumberOfStudyRelatedInstances"), mycount);
			std::stringstream mycountstr;
			mycountstr << mycount;
			(*responseIdentifiers)->putAndInsertString(DCM_NumberOfStudyRelatedInstances, mycountstr.str().c_str());
			*/
			/*
			std::stringstream log;
			(*responseIdentifiers)->print(log);
			log << "\n";
			DEBUGLOG(sessionguid, DB_INFO, CA2W(text2bin(log.str()).c_str()));
			*/
            context->m_pRs.MoveNext();
			return STATUS_Pending;
        }
		else
		{
			context->m_pRs.Close();
			context->m_pDb.Close();
			return STATUS_Success;
		}

    }
    catch (...)
    {
		DEBUGLOG(sessionguid, DB_INFO, L"Exception in FindStudyLevel.\r\n" );
    }

	DEBUGLOG(sessionguid, DB_INFO, L"error\r\n");
    return STATUS_FIND_Failed_UnableToProcess;
}

DIC_US FindHandler::FindSeriesLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{
    const wchar_t *sessionguid = context->sessionguid;

    try
    {
		if(!context->m_pDb.IsOpen())
		{		
			OFString studyuid;
			requestIdentifiers->findAndGetOFString(DCM_StudyInstanceUID, studyuid);						

			if(studyuid.length() == 0)
				return STATUS_MOVE_Failed_IdentifierDoesNotMatchSOPClass;
			
			context->m_pDb.Open(getConnectionString());

			CADOCommand command(&context->m_pDb, _T(""), CADOCommand::typeCmdText);
			Study_DICOMQueryToSQL(L"series", SeriesLevelMapping, requestIdentifiers, command);

			/*
	        command.SetText(_T("SELECT [DCM_SeriesInstanceUID], [DCM_Modality], [DCM_SeriesDescription], [DCM_SeriesDate] FROM series WHERE [DCM_StudyInstanceUID] = ?"));
		    command.AddParameter(_T(""), ADODB::adVarWChar, ADODB::adParamInput, studyuid.length(), CString(studyuid.c_str()));
			*/
			context->m_pRs = CADORecordset(&context->m_pDb);
			context->m_pRs.Open(&command);
		}

        if(!context->m_pRs.IsEOF())
        {
			*responseIdentifiers = new DcmDataset;

			(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "SERIES");
			(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, context->ourAETitle);
			Study_SQLToDICOMResponse(SeriesLevelMapping, context->m_pRs, requestIdentifiers, *responseIdentifiers);
			/*
            CString seriesuid, modalityname, seriesdesc;
			COleDateTime seriesdate;
            context->m_pRs.GetFieldValue(_T("DCM_SeriesInstanceUID"), seriesuid);
			context->m_pRs.GetFieldValue(_T("DCM_Modality"), modalityname);
			context->m_pRs.GetFieldValue(_T("DCM_SeriesDescription"), seriesdesc);	
			context->m_pRs.GetFieldValue(_T("DCM_SeriesDate"), seriesdate);
			(*responseIdentifiers)->putAndInsertString(DCM_SeriesInstanceUID, CW2A(seriesuid));
            (*responseIdentifiers)->putAndInsertString(DCM_Modality, CW2A(modalityname));
            (*responseIdentifiers)->putAndInsertString(DCM_SeriesDescription, CW2A(seriesdesc));
			//(*responseIdentifiers)->put(DCM_SeriesDate, seriesdate);
			*/

			/*
			std::stringstream log;
			(*responseIdentifiers)->print(log);
			log << "\n";
			DEBUGLOG(sessionguid, DB_INFO, CA2W(text2bin(log.str()).c_str()));
			*/

            context->m_pRs.MoveNext();
			return STATUS_Pending;
        }
		else
		{
			context->m_pRs.Close();
			context->m_pDb.Close();
			return STATUS_Success;
		}

    }
    catch (...)
    {
		DEBUGLOG(sessionguid, DB_INFO, L"Exception in FindSeriesLevel.\r\n" );
    }
    
    return STATUS_FIND_Failed_UnableToProcess;
}

DIC_US FindHandler::FindImageLevel(DcmDataset *requestIdentifiers, DcmDataset **responseIdentifiers)
{    
    try
    {
		if(!context->m_pDb.IsOpen())
		{					
			OFString seriesuid, studyuid;
			requestIdentifiers->findAndGetOFString(DCM_SeriesInstanceUID, seriesuid);
			requestIdentifiers->findAndGetOFString(DCM_StudyInstanceUID, studyuid);

			if(seriesuid.length() == 0)
				return STATUS_MOVE_Failed_IdentifierDoesNotMatchSOPClass;

			context->m_pDb.Open(getConnectionString());
			CADOCommand command(&context->m_pDb, _T(""), CADOCommand::typeCmdText);
			Study_DICOMQueryToSQL(L"imagesflat", ImageLevelMapping, requestIdentifiers, command);

	        /*command.SetText(_T("SELECT [DCM_SOPInstanceUID], [DCM_SeriesInstanceUID] FROM images WHERE [DCM_SeriesInstanceUID] = ?"));
		    command.AddParameter(_T(""), ADODB::adVarWChar, ADODB::adParamInput, seriesuid.length(), CString(seriesuid.c_str()));
			//command.AddParameter(_T(""), ADODB::adVarWChar, ADODB::adParamInput, studyuid.length(), CString(studyuid.c_str()));
			*/
			context->m_pRs = CADORecordset(&context->m_pDb);
			context->m_pRs.Open(&command);			
		}

        if(!context->m_pRs.IsEOF())
        {
			*responseIdentifiers = new DcmDataset;

			(*responseIdentifiers)->putAndInsertString(DCM_QueryRetrieveLevel, "IMAGE");
			(*responseIdentifiers)->putAndInsertString(DCM_RetrieveAETitle, context->ourAETitle);
			Study_SQLToDICOMResponse(ImageLevelMapping, context->m_pRs, requestIdentifiers, *responseIdentifiers);
			/*
            CString sopuid, seriesuid;
			COleDateTime seriesdate;
            context->m_pRs.GetFieldValue(_T("DCM_SOPInstanceUID"), sopuid);			
            context->m_pRs.GetFieldValue(_T("DCM_SeriesInstanceUID"), seriesuid);			
			(*responseIdentifiers)->putAndInsertString(DCM_SOPInstanceUID, CW2A(sopuid));
			(*responseIdentifiers)->putAndInsertString(DCM_SeriesInstanceUID, CW2A(seriesuid));            
			*/
			/*
			std::stringstream log;
			(*responseIdentifiers)->print(log);
			log << "\n";
			DEBUGLOG(sessionguid, DB_INFO, CA2W(text2bin(log.str()).c_str()));
			*/
            context->m_pRs.MoveNext();			
			return STATUS_Pending;
        }
		else
		{			
			context->m_pRs.Close();
			context->m_pDb.Close();
			return STATUS_Success;
		}

    }
    catch (...)
    {
		DEBUGLOG(sessionguid, DB_INFO, L"Exception in FindImageLevel.\r\n" );
    }
    	
    return STATUS_FIND_Failed_UnableToProcess;
}