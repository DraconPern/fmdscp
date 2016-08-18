

#include "store.h"
#include "model.h"
#include "config.h"
#include "util.h"
#include <boost/algorithm/string.hpp>
#include <set>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "boost/date_time/local_time/local_time.hpp"
#include <codecvt>

#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <aws/transfer/TransferClient.h>
#include <aws/transfer/UploadFileRequest.h>
#include <aws/s3/model/GetBucketLocationRequest.h>
using namespace Aws::Client;
using namespace Aws::S3;
using namespace Aws::S3::Model;
using namespace Aws::Transfer;


// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/oflog/ndc.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "poco/Data/Session.h"
using namespace Poco::Data::Keywords;


using namespace boost::gregorian;
using namespace boost::posix_time;
using namespace boost::local_time;

OFCondition StoreHandler::handleSTORERequest(boost::filesystem::path filename)
{
	OFCondition status = EC_IllegalParameter;

	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(filename.c_str());

	OFString sopuid, seriesuid, studyuid;
	dfile.getDataset()->findAndGetOFString(DCM_StudyInstanceUID, studyuid);
	dfile.getDataset()->findAndGetOFString(DCM_SeriesInstanceUID, seriesuid);
	dfile.getDataset()->findAndGetOFString(DCM_SOPInstanceUID, sopuid);

	if (studyuid.length() == 0 || seriesuid.length() == 0 || sopuid.length() == 0)
	{
		// DEBUGLOG(sessionguid, DB_ERROR, L"No SOP UID\r\n");
		boost::filesystem::remove(filename);
		return OFCondition(OFM_dcmqrdb, 1, OF_error, "One or more uid is blank");;
	}

	boost::filesystem::path newpath = config::getStoragePath();
	newpath /= studyuid.c_str();
	newpath /= seriesuid.c_str();
	boost::filesystem::create_directories(newpath);
	newpath /= std::string(sopuid.c_str()) + ".dcm";


	std::stringstream msg;
#ifdef _WIN32
	// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
	msg << "Saving file: " << newpath.string(std::codecvt_utf8<boost::filesystem::path::value_type>());
#else
	msg << "Saving file: " << newpath.string();
#endif
	DCMNET_INFO(msg.str());

	dfile.getDataset()->chooseRepresentation(EXS_JPEGLSLossless, NULL);
	if (dfile.getDataset()->canWriteXfer(EXS_JPEGLSLossless))
	{
		dfile.getDataset()->loadAllDataIntoMemory();

		dfile.saveFile(newpath.c_str(), EXS_JPEGLSLossless);

		DCMNET_INFO("Changed to JPEG LS lossless");
	}
	else
	{
		boost::filesystem::copy(filename, newpath);

		DCMNET_INFO("Copied");
	}

	// tell upstream about the object and get an S3 upload info
	
	// UploadToS3(newpath, sopuid.c_str(), seriesuid.c_str(), studyuid.c_str());
	
	// now try to add the file into the database
	if(!AddDICOMFileInfoToDatabase(newpath))
	{
		status = OFCondition(OFM_dcmqrdb, 1, OF_error, "Database error");
	}
	else
		status = EC_Normal;

	// delete the temp file
	boost::filesystem::remove(filename);
	
	return status;
}

OFCondition StoreHandler::UploadToS3(boost::filesystem::path filename, std::string studyuid, std::string sopuid, std::string seriesuid)
{
	// s3path += std::string("/") + seriesuid.c_str();
	// s3path += std::string("/") + std::string(sopuid.c_str()) + ".dcm";


	// upload to S3
	// s3.aws.amazon.com/siteid/studyuid/seriesuid/sopuid.dcm
	TransferClientConfiguration transferConfig;
	transferConfig.m_uploadBufferCount = 20;

	std::string bucket = "draconpern-buildcache";

	static const char* ALLOCATION_TAG = "TransferTests";
	ClientConfiguration config;
	config.region = Aws::Region::US_EAST_1;

	std::shared_ptr<S3Client> m_s3Client = Aws::MakeShared<S3Client>(ALLOCATION_TAG, config, false);

	/*GetBucketLocationRequest locationRequest;
	locationRequest.SetBucket(bucket);
	auto locationOutcome = m_s3Client->GetBucketLocation(locationRequest);
	config.region = locationOutcome.GetResult().GetLocationConstraint();
	*/

	std::shared_ptr<TransferClient> m_transferClient = Aws::MakeShared<TransferClient>(ALLOCATION_TAG, m_s3Client, transferConfig);

	// s3path += std::string("/") + seriesuid.c_str();
	// s3path += std::string("/") + std::string(sopuid.c_str()) + ".dcm";

	std::string s3path = std::string(sopuid.c_str()) + ".dcm";

	std::shared_ptr<UploadFileRequest> requestPtr = m_transferClient->UploadFile(filename.string(), bucket, s3path.c_str(), "", false, true);
	requestPtr->WaitUntilDone();
	if (!requestPtr->CompletedSuccessfully())
	{
		DCMNET_ERROR(requestPtr->GetFailure().c_str());
		return OFCondition(OFM_dcmqrdb, 1, OF_error, requestPtr->GetFailure().c_str());
	}
	else
	{
		// tell upstream we are done with S3 upload
		return EC_Normal;
	}

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
	long numberbuf;

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

		Poco::Data::Session dbconnection(config::getConnectionString());				
			
			std::vector<PatientStudy> patientstudies;
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
				"createdAt,updatedAt"
				" FROM patient_studies WHERE StudyInstanceUID = ?",
				into(patientstudies),
				use(studyuid);

			patientstudiesselect.execute();

			if(patientstudies.size() == 0)
			{
				// insert
				patientstudies.push_back(PatientStudy());
			}
			
			PatientStudy &patientstudy = patientstudies[0];

			patientstudy.StudyInstanceUID = studyuid;

			dfile.getDataset()->findAndGetOFString(DCM_PatientName, textbuf);
			patientstudy.PatientName = textbuf.c_str();
			
			dfile.getDataset()->findAndGetOFString(DCM_StudyID, textbuf);
			patientstudy.StudyID = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_AccessionNumber, textbuf);
			patientstudy.AccessionNumber = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_PatientID, textbuf);
			patientstudy.PatientID = textbuf.c_str();

			datebuf = getDate(dfile.getDataset(), DCM_StudyDate);
			timebuf = getTime(dfile.getDataset(), DCM_StudyTime);
			// use DCM_TimezoneOffsetFromUTC ?
			if(datebuf.isValid() && timebuf.isValid()) 
			{			
				patientstudy.StudyDate.assign(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay(), timebuf.getHour(), timebuf.getMinute(), timebuf.getSecond());
			}

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
			if(datebuf.isValid()) 
				patientstudy.PatientBirthDate.assign(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay());

			dfile.getDataset()->findAndGetOFString(DCM_ReferringPhysicianName, textbuf);
			patientstudy.ReferringPhysicianName = textbuf.c_str();

			patientstudy.updated_at = Poco::DateTime();

			if(patientstudy.id != 0)
			{				
				Poco::Data::Statement update(dbconnection);
				update << "UPDATE patient_studies SET "
					"id = ?,"
					"StudyInstanceUID = ?,"
					"StudyID = ?,"
					"AccessionNumber = ?,"
					"PatientName = ?,"
					"PatientID = ?,"
					"StudyDate = ?,"
					"ModalitiesInStudy = ?,"
					"StudyDescription = ?,"
					"PatientSex = ?,"
					"PatientBirthDate = ?,"
					"ReferringPhysicianName = ?,"
					"createdAt = ?, updatedAt = ?"
					" WHERE id = ?",
					use(patientstudy),
					use(patientstudy.id);
				update.execute();
			}
			else
			{				
				patientstudy.created_at = Poco::DateTime();

				Poco::Data::Statement insert(dbconnection);
				insert << "INSERT INTO patient_studies (id, StudyInstanceUID, StudyID, AccessionNumber, PatientName, PatientID, StudyDate, ModalitiesInStudy, StudyDescription, PatientSex, PatientBirthDate, ReferringPhysicianName, createdAt, updatedAt) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
					use(patientstudy);
				insert.execute();

				dbconnection << "SELECT LAST_INSERT_ID()", into(patientstudy.id), now;
			}
		
		
			// update the series table			
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
				" FROM series WHERE SeriesInstanceUID = ?",
				into(series_list),
				use(seriesuid);

			seriesselect.execute();

			if(series_list.size() == 0)
			{
				series_list.push_back(Series(patientstudy.id));
			}

			Series &series = series_list[0];

			series.SeriesInstanceUID = seriesuid;			

			dfile.getDataset()->findAndGetOFString(DCM_Modality, textbuf);
			series.Modality = textbuf.c_str();

			dfile.getDataset()->findAndGetOFString(DCM_SeriesDescription, textbuf);
			series.SeriesDescription = textbuf.c_str();

			dfile.getDataset()->findAndGetSint32(DCM_SeriesNumber, numberbuf);
			series.SeriesNumber = numberbuf;

			datebuf = getDate(dfile.getDataset(), DCM_SeriesDate);
			timebuf = getTime(dfile.getDataset(), DCM_SeriesTime);
			// use DCM_TimezoneOffsetFromUTC ?
			if(datebuf.isValid() && timebuf.isValid()) 
			{
				series.SeriesDate.assign(datebuf.getYear(), datebuf.getMonth(), datebuf.getDay(), timebuf.getHour(), timebuf.getMinute(), timebuf.getSecond());
				
			}
			
			series.updated_at = Poco::DateTime();

			if(series.id != 0)
			{
				Poco::Data::Statement update(dbconnection);
				update << "UPDATE series SET "
					"id = ?,"
					"SeriesInstanceUID = ?,"
					"Modality = ?,"
					"SeriesDescription = ?,"
					"SeriesNumber = ?,"
					"SeriesDate = ?,"
					"patient_study_id = ?,"
					"createdAt = ?, updatedAt = ?"
					" WHERE id = ?",
					use(series),
					use(series.id);
				update.execute();
			}
			else
			{				
				series.created_at = Poco::DateTime();		

				Poco::Data::Statement insert(dbconnection);
				insert << "INSERT INTO series (id, SeriesInstanceUID, Modality, SeriesDescription, SeriesNumber, SeriesDate, patient_study_id, createdAt, updatedAt) VALUES(?, ? , ? , ? , ? , ? , ? , ? , ?)",
					use(series);
				insert.execute();

				dbconnection << "SELECT LAST_INSERT_ID()", into(series.id), now;
			}
		
		
		// update the images table
		
		std::vector<Instance> instances;
		Poco::Data::Statement instanceselect(dbconnection);
		instanceselect << "SELECT id,"
			"SOPInstanceUID,"			
			"InstanceNumber,"
			"series_id,"
			"createdAt,updatedAt"
			" FROM instances WHERE SOPInstanceUID = ?",
			into(instances),
			use(sopuid);

		instanceselect.execute();

		if(instances.size() == 0)
		{
			instances.push_back(Instance(series.id));
		}

		Instance &instance = instances[0];

		instance.SOPInstanceUID = sopuid;

		dfile.getDataset()->findAndGetSint32(DCM_InstanceNumber, numberbuf);
		instance.InstanceNumber = numberbuf;

		instance.updated_at = Poco::DateTime();

		if(instance.id != 0)
		{
			Poco::Data::Statement update(dbconnection);
			update << "UPDATE instances SET "
				"id = ?,"
				"SOPInstanceUID = ?,"
				"InstanceNumber = ?,"
				"series_id = ?,"
				"createdAt = ?, updatedAt = ?"
				" WHERE id = ?",
				use(instance),
				use(instance.id);

			update.execute();
		}
		else
		{			
			instance.created_at = Poco::DateTime();
			
			Poco::Data::Statement insert(dbconnection);
			insert << "INSERT INTO instances (id, SOPInstanceUID, InstanceNumber, series_id, createdAt, updatedAt) VALUES(?, ?, ?, ?, ?, ?)",
				use(instance);
			insert.execute();

			dbconnection << "SELECT LAST_INSERT_ID()", into(series.id), now;
		}		
	}
	catch(Poco::Data::DataException &e)
	{
		std::string what = e.message();
		isOk = false;
		DCMNET_ERROR(what);
	}

	return isOk;
}
