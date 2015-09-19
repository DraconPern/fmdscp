#include <boost/algorithm/string.hpp>

#include "store.h"

#include "poco/File.h"
#include "Poco/Data/Session.h"
#include "poco/Tuple.h"

using namespace Poco;
using namespace Poco::Data;
using namespace Poco::Data::Keywords;

#include "model.h"
#include "config.h"
#include "util.h"

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

	try
	{

		// create a session
		Session session(Config::getDBPool().get());
		// Session session("MySQL", Config::getConnectionString());

		//		if(cbdata->last_studyuid != studyuid)
		{

			std::vector<PatientStudy> patientstudies;
			Statement patientstudiesselect(session);
			patientstudiesselect << "SELECT id,"
				"DCM_StudyInstanceUID,"
				"DCM_PatientsName,"
				"DCM_PatientID,"
				"DCM_StudyDate,"
				"DCM_ModalitiesInStudy,"
				"DCM_StudyDescription,"
				"DCM_PatientsSex,"
				"DCM_PatientsBirthDate,"
				"created_at,updated_at"
				" FROM patient_studies WHERE DCM_StudyInstanceUID = ?",
				into(patientstudies),
				use(studyuid),
				now;

			if(patientstudies.size() == 0)
			{
				// insert
				patientstudies.push_back(PatientStudy());
			}
			else if(patientstudies.size() == 1)
			{
				// edit
			}

			patientstudies[0].set<1>(studyuid);

			dfile.getDataset()->findAndGetOFString(DCM_PatientName, textbuf);
			patientstudies[0].set<2>(textbuf.c_str());

			dfile.getDataset()->findAndGetOFString(DCM_PatientID, textbuf);
			patientstudies[0].set<3>(textbuf.c_str());

			datebuf = getDate(dfile.getDataset(), DCM_StudyDate);
			if(datebuf.isValid()) patientstudies[0].set<4>(Poco::Data::Date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()));

			// handle modality list...
			std::string modalitiesinstudy = patientstudies[0].get<5>();
			std::set<std::string> modalityarray;
			if(modalitiesinstudy.length() > 0)
				boost::split(modalityarray, modalitiesinstudy, boost::is_any_of("\\"));
			dfile.getDataset()->findAndGetOFStringArray(DCM_Modality, textbuf);
			modalityarray.insert(textbuf.c_str());
			patientstudies[0].set<5>(boost::join(modalityarray, "\\"));

			dfile.getDataset()->findAndGetOFString(DCM_StudyDescription, textbuf);
			patientstudies[0].set<6>(textbuf.c_str());

			dfile.getDataset()->findAndGetOFString(DCM_PatientSex, textbuf);
			patientstudies[0].set<7>(textbuf.c_str());

			datebuf = getDate(dfile.getDataset(), DCM_PatientBirthDate);
			if(datebuf.isValid()) patientstudies[0].set<8>(Poco::Data::Date(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay()));

			if(patientstudies[0].get<0>() == 0)
			{
				patientstudies[0].set<9>(Poco::DateTime());
				patientstudies[0].set<10>(Poco::DateTime());

				Statement insert(session);
				insert << "INSERT INTO patient_studies VALUES(?,?,?,?,?,?,?,?,?,?,?)",
					use(patientstudies), now;
			}
			else
			{
				patientstudies[0].set<10>(Poco::DateTime());

				Statement update(session);
				update << "UPDATE patient_studies SET id = ?,"
					"DCM_StudyInstanceUID = ?,"
					"DCM_PatientsName = ?,"
					"DCM_PatientID = ?,"
					"DCM_StudyDate = ?,"
					"DCM_ModalitiesInStudy = ?,"
					"DCM_StudyDescription = ?,"
					"DCM_PatientsSex = ?,"
					"DCM_PatientsBirthDate = ?,"
					"created_at = ?, updated_at = ?"
					" WHERE id = ?",
					use(patientstudies), use(patientstudies[0].get<0>()), now;
			}

		}

		//if(cbdata->last_seriesuid != seriesuid)
		{
			// update the series table
			std::vector<Series> series;
			Statement seriesselect(session);
			seriesselect << "SELECT id,"
				"DCM_SeriesInstanceUID,"
				"DCM_StudyInstanceUID,"
				"DCM_Modality,"
				"DCM_SeriesDescription,"
				"created_at,updated_at"
				" FROM series WHERE DCM_SeriesInstanceUID = ?",
				into(series),
				use(seriesuid),
				now;

			if(series.size() == 0)
			{
				// insert
				series.push_back(Series());
			}
			else if(series.size() == 1)
			{
				// edit
			}

			series[0].set<1>(seriesuid);

			dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, textbuf);
			series[0].set<2>(textbuf.c_str());

			dfile.getDataset()->findAndGetOFString(DCM_Modality, textbuf);
			series[0].set<3>(textbuf.c_str());

			dfile.getDataset()->findAndGetOFString(DCM_SeriesDescription, textbuf);
			series[0].set<4>(textbuf.c_str());

			if(series[0].get<0>() == 0)
			{
				series[0].set<5>(Poco::DateTime());
				series[0].set<6>(Poco::DateTime());

				Statement insert(session);
				insert << "INSERT INTO series VALUES(?,?,?,?,?,?,?)",
					use(series), now;
			}
			else
			{
				series[0].set<6>(Poco::DateTime());

				Statement update(session);
				update << "UPDATE series SET id = ?,"
					"DCM_SeriesInstanceUID = ?,"
					"DCM_StudyInstanceUID = ?,"
					"DCM_Modality = ?,"
					"DCM_SeriesDescription = ?,"
					"created_at = ?, updated_at = ?"
					" WHERE id = ?",
					use(series), use(series[0].get<0>()), now;
			}
		}

		// update the images table
		std::vector<Instance> instance;
		Statement instanceselect(session);
		instanceselect << "SELECT id,"
			"SOPInstanceUID,"
			"DCM_SeriesInstanceUID,"
			"created_at,updated_at"
			" FROM instances WHERE SOPInstanceUID = ?",
			into(instance),
			use(sopuid),
			now;

		if(instance.size() == 0)
		{
			// insert
			instance.push_back(Instance());
		}
		else if(instance.size() == 1)
		{
			// edit
		}

		instance[0].set<1>(sopuid);

		dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, textbuf);
		instance[0].set<2>(textbuf.c_str());

		if(instance[0].get<0>() == 0)
		{
			instance[0].set<3>(Poco::DateTime());
			instance[0].set<4>(Poco::DateTime());

			Statement insert(session);
			insert << "INSERT INTO instances VALUES(?,?,?,?,?)",
				use(instance), now;
		}
		else
		{
			instance[0].set<4>(Poco::DateTime());

			Statement update(session);
			update << "UPDATE instances SET id = ?,"
				"SOPInstanceUID = ?,"
				"DCM_SeriesInstanceUID = ?,"
				"created_at = ?, updated_at = ?"
				" WHERE id = ?",
				use(instance), use(instance[0].get<0>()), now;
		}
	}
	catch(Poco::DataException &e)
	{
		std::string what = e.displayText();
	}
	return true;
}
