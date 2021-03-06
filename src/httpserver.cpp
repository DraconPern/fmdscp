#include "config.h"

#include "httpserver.h"
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>
#include <codecvt>
#include "model.h"
#include "util.h"
#include "jwtpp.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

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
#include "dcmtk/dcmimgle/dcmimage.h"
#include "dcmtk/dcmjpeg/dipijpeg.h"  // jpeg
#include "dcmtk/dcmsr/dsrdoc.h"
#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "Poco/Data/Session.h"
using namespace Poco::Data::Keywords;

HttpServer::HttpServer(boost::function< void(void) > shutdownCallback, CloudClient &cloudclient, SenderService &senderservice, DBPool &dbpool) :
	SimpleWeb::Server<SimpleWeb::HTTP>(8080, 10),
	shutdownCallback(shutdownCallback),
	cloudclient(cloudclient),
	senderservice(senderservice),
	dbpool(dbpool),
	destinationscontroller(cloudclient, dbpool, *this)
{
	resource["^/studies\\?(.+)$"]["GET"] = boost::bind(&HttpServer::WADO_URI, this, _1, _2);
	resource["^/api/studies\\?(.+)$"]["GET"] = boost::bind(&HttpServer::SearchForStudies, this, _1, _2);
	resource["^/api/studies/([0123456789\\.]+)"]["GET"] = boost::bind(&HttpServer::StudyInfo, this, _1, _2);
	resource["^/image\\?(.+)$"]["GET"] = boost::bind(&HttpServer::GetImage, this, _1, _2);

	resource["^/api/studies/([0123456789\\.]+)/send"]["POST"] = boost::bind(&HttpServer::SendStudy, this, _1, _2);
	resource["^/api/outsessions/cancel"]["POST"] = boost::bind(&HttpServer::CancelSend, this, _1, _2);
	resource["^/api/outsessions"]["GET"] = boost::bind(&HttpServer::GetOutSessions, this, _1, _2);
	resource["^/api/version"]["GET"] = boost::bind(&HttpServer::Version, this, _1, _2);
// 	resource["^/api/shutdown"]["POST"] = boost::bind(&HttpServer::Shutdown, this, _1, _2);
	default_resource["GET"] = boost::bind(&HttpServer::NotFound, this, _1, _2);
}

void HttpServer::Version(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	// jwt test
	jwt jtest;
	int value = jtest.jwt_decode("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWV9.TJVA95OrM7E2cBab30RMHrHDcEfxjoYZgeFONFh7HgQ", "secret");

	boost::property_tree::ptree pt, children;

	std::ostringstream ver;
	ver << FMDSCP_VERSION;
	pt.put("version", ver.str());

	std::ostringstream buf;
	boost::property_tree::json_parser::write_json(buf, pt, true);
	std::string content = buf.str();
	*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
}

void HttpServer::Shutdown(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	shutdownCallback();
	std::string content = "Stoping";
	*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
}

void HttpServer::NotFound(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::string content = "Path not found: " + request->path;
	*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
}

void HttpServer::NotAcceptable(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::string content = "Unable to convert";
	*response << std::string("HTTP/1.1 406 Not Acceptable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
}

bool SendAsDICOM(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid);
bool SendAsPDF(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid);
bool SendAsJPEG(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid, int width);
bool SendAsHTML(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid);

void HttpServer::WADO_URI(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{

	std::string content;
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;
	decode_query(querystring, queries);
	if(queries["requestType"] != "WADO")
	{
		content = "Not a WADO request";
		*response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	if(queries.find("studyUID") == queries.end() || queries.find("seriesUID") == queries.end() || queries.find("objectUID") == queries.end())
	{
		content = "Required fields studyUID, seriesUID, or objectUID missing.";
		*response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	int width = -1;

	if (queries.find("width") != queries.end())
	{
		width = boost::lexical_cast<int>(queries["width"]);
	}

	std::vector<PatientStudy> patient_studies_list;
	std::vector<Series> series_list;
	std::vector<Instance> instances;
	try
	{

		Poco::Data::Session dbconnection(dbpool.get());

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
			"NumberOfStudyRelatedInstances,"
			"createdAt,updatedAt"
			" FROM patient_studies WHERE StudyInstanceUID = ?",
			into(patient_studies_list),
			use(queries["studyUID"]);

		patientstudiesselect.execute();
		if(patient_studies_list.size() != 1)
		{
			content = "Unable to find study";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}

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
			use(queries["seriesUID"]);

		seriesselect.execute();
		if(series_list.size() != 1)
		{
			content = "Unable to find series";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}

		Poco::Data::Statement instanceselect(dbconnection);
		instanceselect << "SELECT id,"
			"SOPInstanceUID,"
			"InstanceNumber,"
			"series_id,"
			"createdAt,updatedAt"
			" FROM instances WHERE SOPInstanceUID = ?",
			into(instances),
			use(queries["objectUID"]);

		instanceselect.execute();
		if(instances.size() != 1)
		{
			content = "Unable to find instance";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}
	}
	catch(Poco::Data::DataException &e)
	{
		content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}

	boost::filesystem::path sourcepath = config::getStoragePath();
	sourcepath /= patient_studies_list[0].StudyInstanceUID;
	sourcepath /= series_list[0].SeriesInstanceUID;
	sourcepath /= instances[0].SOPInstanceUID + ".dcm";

	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(sourcepath.c_str());

	if(cond.bad())
	{
		content = "Problem loading instance";
		*response << std::string("HTTP/1.1 500 Internal Server Error\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	if(queries["anonymize"] == "yes")
	{
		content = "anonymization not supported";
		*response << std::string("HTTP/1.1 406 Not Acceptable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	OFString textbuf;
	dfile.getDataset()->findAndGetOFString(DCM_SOPClassUID, textbuf);
	std::string sopclass = textbuf.c_str();

	std::string contenttype = queries["contentType"];
	boost::algorithm::to_lower(contenttype);

	if(contenttype == "")
	{
		bool ok = false;
		if(sopclass == UID_EncapsulatedPDFStorage)
			ok = SendAsPDF(dfile, *response, instances[0].SOPInstanceUID);
		else if(sopclass.find("1.2.840.10008.5.1.4.1.1.88") != std::string::npos)
			ok = SendAsHTML(dfile, *response, instances[0].SOPInstanceUID);
		else
			ok = SendAsJPEG(dfile, *response, instances[0].SOPInstanceUID, width);

		if(!ok)
			if(!SendAsDICOM(dfile, *response, instances[0].SOPInstanceUID))
				NotAcceptable(response, request);
	}
	else if(contenttype == "application/dicom")
	{
		if(!SendAsDICOM(dfile, *response, instances[0].SOPInstanceUID))
			NotAcceptable(response, request);
	}
	else if(contenttype == "application/pdf")
	{
		if(sopclass == UID_EncapsulatedPDFStorage)
		{
			if(!SendAsPDF(dfile, *response, instances[0].SOPInstanceUID))
				NotAcceptable(response, request);
		}
		else
			NotAcceptable(response, request);
	}
	else if(contenttype == "image/jpeg")
	{
		if (!SendAsJPEG(dfile, *response, instances[0].SOPInstanceUID, width))
			NotAcceptable(response, request);
	}
	else if(contenttype == "text/html")
	{
		if(!SendAsHTML(dfile, *response, instances[0].SOPInstanceUID))
			NotAcceptable(response, request);
	}
	else
		if(!SendAsDICOM(dfile, *response, instances[0].SOPInstanceUID))
			NotAcceptable(response, request);
}

bool SendAsDICOM(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid)
{
	// decompress
	dfile.getDataset()->chooseRepresentation(EXS_LittleEndianExplicit, NULL);

	dfile.getDataset()->loadAllDataIntoMemory();

	boost::filesystem::path newpath = boost::filesystem::unique_path();
	if(dfile.getDataset()->canWriteXfer(EXS_LittleEndianExplicit))
	{
		dfile.saveFile(newpath.c_str(), EXS_LittleEndianExplicit);
	}
	else
		dfile.saveFile(newpath.c_str());

	try
	{
		if(boost::filesystem::file_size(newpath) > 0)
		{
			std::ifstream source(newpath.string(), std::ios::binary);
			response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << boost::filesystem::file_size(newpath) << "\r\n"
				<< "Content-Type: application/dicom\r\n"
				<< "Content-disposition: attachment; filename=\"" << sopuid << ".dcm" << "\"\r\n"
				<< "\r\n";

			std::istreambuf_iterator<char> begin_source(source);
			std::istreambuf_iterator<char> end_source;
			std::ostreambuf_iterator<char> begin_dest(response);
			std::copy(begin_source, end_source, begin_dest);
		}
	}
	catch(...)
	{
		std::string content = "Object not found on disk";
		response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return false;
	}

	return true;
}

bool SendAsJPEG(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid, int width)
{
	boost::filesystem::path newpath = boost::filesystem::unique_path();

	DiJPEGPlugin plugin;
	plugin.setQuality(90);
	plugin.setSampling(ESS_422);

	DicomImage di(dfile.getDataset(), EXS_Unknown);

	di.getFirstFrame();
	di.showAllOverlays();
	if (di.isMonochrome())
	{
		if (di.getWindowCount())
			di.setWindow(0);
		else
			di.setHistogramWindow();
	}

	DicomImage *imagetouse = &di;
	DicomImage *scaledimage = NULL;

	if (width != -1)
	{
		scaledimage = di.createScaledImage((unsigned long)width);
		imagetouse = scaledimage;
	}

	int result = 0;
	if (imagetouse)
	{
#ifdef _WIN32
		// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
		result = imagetouse->writePluginFormat(&plugin, newpath.string(std::codecvt_utf8<boost::filesystem::path::value_type>()).c_str());
#else
		result = imagetouse->writePluginFormat(&plugin, newpath.c_str());
#endif
	}

	if (scaledimage)
		delete scaledimage;

	if(result == 0)
		return false;

	try
	{
		if(boost::filesystem::file_size(newpath) > 0)
		{
			std::ifstream source(newpath.string(), std::ios::binary);
			response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << boost::filesystem::file_size(newpath) << "\r\n"
				<< "Content-Type: image/jpeg\r\n"
				<< "Content-disposition: filename=\"" << sopuid << ".jpg" << "\"\r\n"
				<< "\r\n";

			std::istreambuf_iterator<char> begin_source(source);
			std::istreambuf_iterator<char> end_source;
			std::ostreambuf_iterator<char> begin_dest(response);
			std::copy(begin_source, end_source, begin_dest);
		}

		boost::system::error_code ecode;
		boost::filesystem::remove(newpath, ecode);
	}
	catch(...)
	{
		return false;
	}

	return true;
}

bool SendAsPDF(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid)
{
	char *bytes = NULL;
	unsigned long size = 0;
	OFCondition cond = dfile.getDataset()->findAndGetUint8Array(DCM_EncapsulatedDocument, (const Uint8 *&)bytes, &size);
	if(cond.bad())
		return false;

	response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << size << "\r\n"
		<< "Content-Type: application/pdf\r\n"
		<< "Content-disposition: attachment; filename=\"" << sopuid << ".pdf" << "\"\r\n"
		<< "\r\n";
	std::ostreambuf_iterator<char> begin_dest(response);
	std::copy(&bytes[0], &bytes[size], begin_dest);

	return true;
}

bool SendAsHTML(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid)
{
	dfile.convertToUTF8();

	DSRDocument dsrdoc;
	OFCondition result = dsrdoc.read(*dfile.getDataset());

	if (result.bad())
		return false;

	std::stringstream strbuf;
	if(dsrdoc.renderHTML(strbuf).bad())
		return false;

	response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << strbuf.str().size() << "\r\n"
		<< "Content-Type: text/html\r\n"
		<< "Content-disposition: filename=\"" << sopuid << ".pdf" << "\"\r\n"
		<< "\r\n";

	response << strbuf.str();
	return true;
}

void HttpServer::SearchForStudies(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;
	decode_query(querystring, queries);

	// check for Accept = application / dicom + json

	std::vector<PatientStudy> patient_studies_list;
	try
	{
		Poco::Data::Session dbconnection(dbpool.get());

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
			"NumberOfStudyRelatedInstances,"
			"createdAt,updatedAt"
			" FROM patient_studies",
			into(patient_studies_list);

		int parameters = 0;

		if (queries.find("PatientName") != queries.end())
		{
			std::string sqlcommand;
			(parameters == 0) ? (sqlcommand = " WHERE ") : (sqlcommand = " AND ");
			sqlcommand += "PatientName LIKE ?";
			patientstudiesselect << sqlcommand, bind("%" + queries["PatientName"] + "%");
			parameters++;
		}

		if (queries.find("PatientID") != queries.end())
		{
			std::string sqlcommand;
			(parameters == 0) ? (sqlcommand = " WHERE ") : (sqlcommand = " AND ");
			sqlcommand += "PatientID LIKE ?";
			patientstudiesselect << sqlcommand, bind("%" + queries["PatientID"] + "%");
			parameters++;
		}

		if (queries.find("StudyDate") != queries.end())
		{
			Poco::DateTime d;
			int t;
			if (Poco::DateTimeParser::tryParse("%Y-%n-%e", queries["StudyDate"], d, t))
			{
				Poco::DateTimeFormatter format;
				std::string sqlcommand;
				(parameters == 0) ? (sqlcommand = " WHERE ") : (sqlcommand = " AND ");
				sqlcommand += "StudyDate BETWEEN ? AND ?";
				patientstudiesselect << sqlcommand, bind(format.format(d, "%Y-%m-%d 0:0:0")), bind(format.format(d, "%Y-%m-%d 23:59:59"));
				parameters++;
			}
		}

		if (queries.find("StudyInstanceUID") != queries.end())
		{
			std::string sqlcommand;
			(parameters == 0) ? (sqlcommand = " WHERE ") : (sqlcommand = " AND ");
			sqlcommand += "StudyInstanceUID = ?";
			patientstudiesselect << sqlcommand, bind(queries["StudyInstanceUID"]);
			parameters++;
		}

		if (parameters > 0)
		{
			patientstudiesselect.execute();
		}
		else
		{
			// don't perform search
		}


		/*if (patient_studies_list.size() == 0)
		{
			content = "Unable to find study";
			*response << std::string("HTTP/1.1 204 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}*/

		boost::property_tree::ptree pt, children;

		for (int i = 0; i < patient_studies_list.size(); i++)
		{
			boost::property_tree::ptree child;
			child.add("StudyInstanceUID", patient_studies_list[i].StudyInstanceUID);
			child.add("PatientName", patient_studies_list[i].PatientName);
			child.add("PatientID", patient_studies_list[i].PatientID);
			child.add("StudyDate", ToJSON(patient_studies_list[i].StudyDate));
			child.add("ModalitiesInStudy", patient_studies_list[i].ModalitiesInStudy);
			child.add("StudyDescription", patient_studies_list[i].StudyDescription);
			child.add("PatientSex", patient_studies_list[i].PatientSex);
			child.add("PatientBirthDate", ToJSON(patient_studies_list[i].PatientBirthDate));
			child.add("NumberOfStudyRelatedInstances", patient_studies_list[i].NumberOfStudyRelatedInstances);
			children.push_back(std::make_pair("", child));
		}

		pt.add_child("studies", children);

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

void HttpServer::StudyInfo(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::string studyinstanceuid = request->path_match[1];

	try
	{
		std::vector<PatientStudy> patient_studies_list;

		Poco::Data::Session dbconnection(dbpool.get());

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
			"NumberOfStudyRelatedInstances,"
			"createdAt,updatedAt"
			" FROM patient_studies WHERE StudyInstanceUID = ?",
			into(patient_studies_list),
			use(studyinstanceuid);

		patientstudiesselect.execute();
		if (patient_studies_list.size() != 1)
		{
			std::string content = "Study not found";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}

		boost::property_tree::ptree pt;

		for (int i = 0; i < 1; i++)
		{
			boost::property_tree::ptree studyitem;
			studyitem.add("StudyInstanceUID", patient_studies_list[i].StudyInstanceUID);
			studyitem.add("PatientName", patient_studies_list[i].PatientName);
			studyitem.add("PatientID", patient_studies_list[i].PatientID);
			studyitem.add("StudyDate", ToJSON(patient_studies_list[i].StudyDate));
			studyitem.add("ModalitiesInStudy", patient_studies_list[i].ModalitiesInStudy);
			studyitem.add("StudyDescription", patient_studies_list[i].StudyDescription);
			studyitem.add("PatientSex", patient_studies_list[i].PatientSex);
			studyitem.add("PatientBirthDate", ToJSON(patient_studies_list[i].PatientBirthDate));
			studyitem.add("NumberOfStudyRelatedInstances", patient_studies_list[i].NumberOfStudyRelatedInstances);

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
				" FROM series WHERE patient_study_id = ?",
				into(series_list),
				use(patient_studies_list[i].id);

			seriesselect.execute();

			boost::property_tree::ptree seriesarray;
			for (int i = 0; i < series_list.size(); i++)
			{
				boost::property_tree::ptree seriesitem;
				seriesitem.add("SeriesInstanceUID", series_list[i].SeriesInstanceUID);
				seriesitem.add("Modality", series_list[i].Modality);
				seriesitem.add("SeriesDescription", series_list[i].SeriesDescription);
				seriesitem.add("SeriesNumber", series_list[i].SeriesNumber);
				seriesitem.add("SeriesDate", ToJSON(series_list[i].SeriesDate));

				std::vector<Instance> instance_list;
				Poco::Data::Statement instanceselect(dbconnection);
				instanceselect << "SELECT id,"
					"SOPInstanceUID,"
					"InstanceNumber,"
					"series_id,"
					"createdAt,updatedAt"
					" FROM instances WHERE series_id = ?",
					into(instance_list),
					use(series_list[i].id);

				instanceselect.execute();

				boost::property_tree::ptree imagesarray;
				for (int j = 0; j < instance_list.size(); j++)
				{
					boost::property_tree::ptree imageitem;
					imageitem.put("", instance_list[j].SOPInstanceUID);
					imagesarray.push_back(std::make_pair("", imageitem));
				}

				seriesitem.add_child("SOPInstanceUIDs", imagesarray);
				seriesarray.push_back(std::make_pair("", seriesitem));
			}

			studyitem.add_child("series", seriesarray);

			pt.add_child("study", studyitem);
		}

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;

	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

void HttpServer::SendStudy(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::map<std::string, std::string> queries;
	decode_query(request->content.string(), queries);

	if (queries.find("destination") == queries.end())
	{
		std::string content = "Missing parameters";
		*response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	std::string studyinstanceuid = request->path_match[1];
	std::string destinationid = queries["destination"];

	try
	{
		std::vector<PatientStudy> patient_studies_list;

		Poco::Data::Session dbconnection(dbpool.get());

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
			"NumberOfStudyRelatedInstances,"
			"createdAt,updatedAt"
			" FROM patient_studies WHERE StudyInstanceUID = ?",
			into(patient_studies_list),
			use(studyinstanceuid);

		patientstudiesselect.execute();
		if (patient_studies_list.size() != 1)
		{
			std::string content = "Study not found";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}

		OutgoingSession out_session;
		boost::uuids::basic_random_generator<boost::mt19937> gen;
		boost::uuids::uuid u = gen();
		out_session.uuid = boost::lexical_cast<std::string>(u);
		out_session.queued = 1;
		out_session.StudyInstanceUID = studyinstanceuid;
		out_session.PatientName = patient_studies_list[0].PatientName;
		out_session.PatientID = patient_studies_list[0].PatientID;
		out_session.StudyDate = patient_studies_list[0].StudyDate;
		out_session.ModalitiesInStudy = patient_studies_list[0].ModalitiesInStudy;
		out_session.destination_id = boost::lexical_cast<int>(destinationid);
		out_session.status = "Queued";
		out_session.created_at = Poco::DateTime();
		out_session.updated_at = Poco::DateTime();

		Poco::Data::Statement insert(dbconnection);
		insert << "INSERT INTO outgoing_sessions (id, uuid, queued, StudyInstanceUID, PatientName, PatientID, StudyDate, ModalitiesInStudy, destination_id, status, createdAt, updatedAt) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
			use(out_session);
		insert.execute();

		dbconnection << "SELECT LAST_INSERT_ID()", into(out_session.id), now;

		boost::property_tree::ptree pt, children;

		std::ostringstream str;
		str << out_session.uuid;
		pt.add("result", str.str());

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;

	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

void HttpServer::CancelSend(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::map<std::string, std::string> queries;
	decode_query(request->content.string(), queries);

	if (queries.find("uuid") == queries.end())
	{
		std::string content = "Missing parameters";
		*response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	if (!senderservice.cancelSend(queries["uuid"]))
	{
		std::string content = "can't find session";
		*response << std::string("HTTP/1.1 404 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	std::string content = "Canceling send";
	*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
}

void HttpServer::GetImage(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{

	std::string content;
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;
	decode_query(querystring, queries);

	if (queries.find("sopuid") == queries.end())
	{
		content = "Required fields sopuid missing.";
		*response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	std::vector<Instance> instances;
	try
	{

		Poco::Data::Session dbconnection(dbpool.get());

		Poco::Data::Statement instanceselect(dbconnection);
		instanceselect << "SELECT id,"
			"SOPInstanceUID,"
			"InstanceNumber,"
			"series_id,"
			"createdAt,updatedAt"
			" FROM instances WHERE SOPInstanceUID = ?",
			into(instances),
			use(queries["sopuid"]);

		instanceselect.execute();
		if (instances.size() == 0)
		{
			content = "Unable to find instance";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}

		if (instances.size() > 1)
		{
			content = "More than one instance";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}


	}
	catch (Poco::Data::DataException &e)
	{
		content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}


}

void HttpServer::GetOutSessions(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	try
	{
		Poco::Data::Session dbconnection(dbpool.get());

		std::vector<OutgoingSession> out_sessions;
		Poco::Data::Statement outsessionsselect(dbconnection);
		outsessionsselect << "SELECT id,"
			"uuid,"
			"queued,"
			"StudyInstanceUID,"
			"PatientName,"
			"PatientID,"
			"StudyDate,"
			"ModalitiesInStudy,"
			"destination_id,"
			"status,"
			"createdAt,updatedAt"
			" FROM outgoing_sessions ORDER BY createdAt DESC LIMIT 100",
			into(out_sessions);

		outsessionsselect.execute();

		std::vector<Destination> destination_list;
		Poco::Data::Statement stselect(dbconnection);
		stselect << "SELECT id,"
			"name,"
			"destinationhost,"
			"destinationport,"
			"destinationAE,"
			"sourceAE,"
			"createdAt,updatedAt"
			" FROM destinations",
			into(destination_list);
		stselect.execute();

		boost::property_tree::ptree pt, children;

		for (int i = 0; i < out_sessions.size(); i++)
		{
			int destination_id = out_sessions[i].destination_id;
			auto it = find_if(destination_list.begin(), destination_list.end(), [&destination_id](const Destination& obj) {return obj.id == destination_id; });

			boost::property_tree::ptree child;
			child.add("id", out_sessions[i].id);
			child.add("uuid", out_sessions[i].uuid);
			child.add("StudyInstanceUID", out_sessions[i].StudyInstanceUID);
			child.add("PatientName", out_sessions[i].PatientName);
			child.add("PatientID", out_sessions[i].PatientID);
			child.add("StudyDate", ToJSON(out_sessions[i].StudyDate));
			child.add("ModalitiesInStudy", out_sessions[i].ModalitiesInStudy);
			child.add("destination_id", out_sessions[i].destination_id);
			if (it != destination_list.end())
				child.add("destination_name", it->name);
			else
				child.add("destination_name", "");
			child.add("status", out_sessions[i].status);
			child.add("updatedAt", ToJSON(out_sessions[i].updated_at));
			child.add("createdAt", ToJSON(out_sessions[i].created_at));
			children.push_back(std::make_pair("", child));
		}

		pt.add_child("sessions", children);

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;

	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}
