#include <boost/algorithm/string.hpp>
#include <set>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "store.h"

#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"

#include "model.h"
#include "config.h"
#include "util.h"

using namespace soci;
using namespace boost::gregorian;
using namespace boost::posix_time;

OFCondition StoreHandler::handleSTORERequest(boost::filesystem::path filename)
{
	OFCondition status = EC_IllegalParameter;

	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(filename.c_str());

	OFString sopuid, seriesuid, studyuid;
	dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, studyuid);
	dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, seriesuid);
	dfile.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, sopuid);

	if (studyuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No Study UID\r\n");
		return status;
	}
	if (seriesuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No Series UID\r\n");
		return status;
	}
	if (sopuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No SOP UID\r\n");
		return status;
	}

	boost::filesystem::path newpath = Config::getStoragePath();
	newpath /= studyuid.c_str();
	newpath /= seriesuid.c_str();
	boost::filesystem::create_directories(newpath);
	newpath /= (sopuid + OFString(".dcm")).c_str();

	dfile.getDataset()->chooseRepresentation(EXS_JPEGLSLossless, NULL);
	if(dfile.getDataset()->canWriteXfer(EXS_JPEGLSLossless))
	{
		dfile.getDataset()->loadAllDataIntoMemory();

		dfile.saveFile(newpath.string().c_str(), EXS_JPEGLSLossless);


		//DEBUGLOG(sessionguid, DB_INFO, L"Changed to JPEG LS Lossless: %s\r\n", newpath);
	}
	else
	{
		boost::filesystem::copy(filename, newpath);

		// DEBUGLOG(sessionguid, DB_INFO, L"Moved to: %s\r\n", newpath);
	}

	// now try to add the file into the database
	AddDICOMFileInfoToDatabase(newpath);

	// delete the temp file
	boost::filesystem::remove(filename);

	status = EC_Normal;

	return status;
}

bool StoreHandler::AddDICOMFileInfoToDatabase(boost::filesystem::path filename)
{
	// now try to add the file into the database
	if(!boost::filesystem::exists(filename))
		return false;

	DcmFileFormat dfile;
	if(dfile.loadFile(filename.c_str()).bad())
		return false;

	OFDate datebuf;
	OFTime timebuf;
	OFString textbuf;

	std::string sopuid, seriesuid, studyuid;
	dfile.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, textbuf);
	sopuid = textbuf.c_str();
	dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, textbuf);
	seriesuid = textbuf.c_str();
	dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, textbuf);
	studyuid = textbuf.c_str();

	bool isOk = true;
	try
	{

		// create a session
		session dbconnection(Config::getConnectionString());


		//		if(cbdata->last_studyuid != studyuid)
		{

			PatientStudy patientstudy;
			session &patientstudiesselect = dbconnection;
			patientstudiesselect << "SELECT id,"
				"StudyInstanceUID,"
				"PatientName,"
				"PatientID,"
				"StudyDate,"
				"ModalitiesInStudy,"
				"StudyDescription,"
				"PatientSex,"
				"PatientBirthDate,"
				"created_at,updated_at"
				" FROM patient_studies WHERE StudyInstanceUID = :studyuid",
				into(patientstudy),
				use(studyuid);

			patientstudy.StudyInstanceUID = studyuid;

			dfile.getDataset()->findAndGetOFString(DCM_PatientName, textbuf);
			patientstudy.PatientName = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_PatientID, textbuf);
			patientstudy.PatientID = textbuf.c_str();

			datebuf = getDate(dfile.getDataset(), DCM_StudyDate);
			if(datebuf.isValid()) patientstudy.StudyDate = to_tm(date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()));

			// handle modality list...
			std::string modalitiesinstudy = patientstudy.ModalitiesInStudy;
			std::set<std::string> modalityarray;
			if(modalitiesinstudy.length() > 0)
				boost::split(modalityarray, modalitiesinstudy, boost::is_any_of("\\"));
			dfile.getDataset()->findAndGetOFStringArray(DCM_Modality, textbuf);
			modalityarray.insert(textbuf.c_str());
			patientstudy.ModalitiesInStudy = boost::join(modalityarray, "\\");

			dfile.getDataset()->findAndGetOFString(DCM_StudyDescription, textbuf);
			patientstudy.StudyDescription = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_PatientSex, textbuf);
			patientstudy.PatientSex = textbuf.c_str();

			datebuf = getDate(dfile.getDataset(), DCM_PatientBirthDate);
			if(datebuf.isValid()) patientstudy.PatientBirthDate = to_tm(date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()));

			patientstudy.updated_at = to_tm(second_clock::universal_time());

			if(patientstudiesselect.got_data())
			{				
				soci::session &update = dbconnection;
				update << "UPDATE patient_studies SET "
					"StudyInstanceUID = :StudyInstanceUID,"
					"PatientName = :PatientName,"
					"PatientID = :PatientID,"
					"StudyDate = :StudyDate,"
					"ModalitiesInStudy = :ModalitiesInStudy,"
					"StudyDescription = :StudyDescription,"
					"PatientSex = :PatientSex,"
					"PatientBirthDate = :PatientBirthDate,"
					"created_at = :created_at, updated_at = :updated_at"
					" WHERE id = :id",
					use(patientstudy);
			}
			else
			{
				patientstudy.id = 0;
				patientstudy.created_at = to_tm(second_clock::universal_time());				

				soci::session &insert = dbconnection;
				insert << "INSERT INTO patient_studies VALUES(0, :StudyInstanceUID, :PatientName, :PatientID,"
					":StudyDate, :ModalitiesInStudy, :StudyDescription, :PatientSex, :PatientBirthDate,"
					":created_at, :updated_at)",
					use(patientstudy);
			}
		}
		
		//if(cbdata->last_seriesuid != seriesuid)
		{
			// update the series table
			Series series;
			session &seriesselect = dbconnection;
			seriesselect << "SELECT id,"
				"SeriesInstanceUID,"
				"StudyInstanceUID,"
				"Modality,"
				"SeriesDescription,"
				"created_at,updated_at"
				" FROM series WHERE SeriesInstanceUID = :seriesuid",
				into(series),
				use(seriesuid);

			series.SeriesInstanceUID = seriesuid;

			dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, textbuf);
			series.StudyInstanceUID = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_Modality, textbuf);
			series.Modality = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_SeriesDescription, textbuf);
			series.SeriesDescription = textbuf.c_str();

			series.updated_at = to_tm(second_clock::universal_time());

			if(seriesselect.got_data())
			{
				soci::session &update = dbconnection;
				update << "UPDATE series SET "
					"SeriesInstanceUID = :SeriesInstanceUID,"
					"StudyInstanceUID = :StudyInstanceUID,"
					"Modality = :Modality,"
					"SeriesDescription = :SeriesDescription,"
					"created_at = :created_at, updated_at = :updated_at"
					" WHERE id = :id",
					use(series);
			}
			else
			{
				series.id = 0;
				series.created_at = to_tm(second_clock::universal_time());				

				soci::session &insert = dbconnection;
				insert << "INSERT INTO series VALUES(0, :SeriesInstanceUID, :StudyInstanceUID, :Modality, :SeriesDescription,"
					":created_at, :updated_at)",
					use(series);
			}
		}

		// update the images table

		Instance instance;		
		session &instanceselect = dbconnection;
		instanceselect << "SELECT id,"
			"SOPInstanceUID,"
			"SeriesInstanceUID,"
			"created_at,updated_at"
			" FROM instances WHERE SOPInstanceUID = :SOPInstanceUID",
			into(instance),
			use(sopuid);

		instance.SOPInstanceUID = sopuid;

		dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, textbuf);
		instance.SeriesInstanceUID = textbuf.c_str();

		instance.updated_at = to_tm(second_clock::universal_time());

		if(instanceselect.got_data())
		{
			soci::session &update = dbconnection;			
			update << "UPDATE instances SET "
				"SOPInstanceUID = :SOPInstanceUID,"
				"SeriesInstanceUID = :SeriesInstanceUID,"
				"created_at = :created_at, updated_at = :updated_at"
				" WHERE id = :id",
				use(instance);
		}
		else
		{
			instance.id = 0;
			instance.created_at = to_tm(second_clock::universal_time());				
			
			soci::session &insert = dbconnection;
			insert << "INSERT INTO instances VALUES(0, :SOPInstanceUID, :SeriesInstanceUID,"
				":created_at, :updated_at)",
				use(instance);
		}
		
	}
	catch(std::exception &e)
	{
		std::string what = e.what();
		isOk = false;
	}

	return isOk;
}
