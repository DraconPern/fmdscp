#include "config.h"

#include "httpserver.h"
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <codecvt>
#include "model.h"
#include "util.h"

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

#include "poco/Data/Session.h"
using namespace Poco::Data::Keywords;

HttpServer::HttpServer(std::function< void(void) > shutdownCallback) :
	SimpleWeb::Server<SimpleWeb::HTTP>(8080, 10),
	shutdownCallback(shutdownCallback)
{
	resource["^/studies\\?(.+)$"]["GET"] = boost::bind(&HttpServer::SearchForStudies, this, _1, _2);
	resource["^/wado\\?(.+)$"]["GET"] = boost::bind(&HttpServer::WADO, this, _1, _2);
	resource["^/api/version"]["GET"] = boost::bind(&HttpServer::Version, this, _1, _2);
	resource["^/api/shutdown"]["POST"] = boost::bind(&HttpServer::Shutdown, this, _1, _2);
	default_resource["GET"] = boost::bind(&HttpServer::NotFound, this, _1, _2);
}

void HttpServer::Version(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
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
bool SendAsJPEG(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid);
bool SendAsHTML(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid);

void HttpServer::WADO(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
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

	std::vector<PatientStudy> patient_studies_list;
	std::vector<Series> series_list;
	std::vector<Instance> instances;
	try
	{

		Poco::Data::Session dbconnection(config::getConnectionString());	

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
			ok = SendAsJPEG(dfile, *response, instances[0].SOPInstanceUID);

		if(!ok)
			SendAsDICOM(dfile, *response, instances[0].SOPInstanceUID);
	}	
	else if(contenttype == "application/dicom")
	{
		SendAsDICOM(dfile, *response, instances[0].SOPInstanceUID);
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
		if(!SendAsJPEG(dfile, *response, instances[0].SOPInstanceUID))
			NotAcceptable(response, request);
	}
	else if(contenttype == "text/html")
	{
		if(!SendAsHTML(dfile, *response, instances[0].SOPInstanceUID))
			NotAcceptable(response, request);
	}
	else
		SendAsDICOM(dfile, *response, instances[0].SOPInstanceUID);
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

bool SendAsJPEG(DcmFileFormat &dfile, HttpServer::Response& response, std::string sopuid)
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
		if(di.getWindowCount())
			di.setWindow(0);
		else
			di.setHistogramWindow();
	}

#ifdef _WIN32
	// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
	int result = di.writePluginFormat(&plugin, newpath.string(std::codecvt_utf8<boost::filesystem::path::value_type>()).c_str());
#else
	int result = di.writePluginFormat(&plugin, newpath.c_str());
#endif					

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
	std::stringstream strbuf;
	dsrdoc.renderHTML(strbuf);

	response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << strbuf.str().size() << "\r\n" 
		<< "Content-Type: text/html\r\n"
		<< "Content-disposition: filename=\"" << sopuid << ".pdf" << "\"\r\n"
		<< "\r\n";

	response << strbuf.str();		
	return true;
}

void HttpServer::SearchForStudies(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request)
{
	std::string content;
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;	
	decode_query(querystring, queries);	
	

	// check for Accept = application / dicom + json
	
	std::vector<PatientStudy> patient_studies_list;
	try
	{
		Poco::Data::Session dbconnection(config::getConnectionString());

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
			if (Poco::DateTimeParser::tryParse(queries["StudyDate"], d, t))
			{
				Poco::DateTimeFormatter format;
				std::string sqlcommand;
				(parameters == 0) ? (sqlcommand = " WHERE ") : (sqlcommand = " AND ");
				sqlcommand += "StudyDate BETWEEN (? AND ?)";
				patientstudiesselect << sqlcommand, bind(format.format(d, "%Y%m%d 0:0:0")), bind(format.format(d, "%Y%m%d 23:59:59"));
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

		boost::property_tree::ptree pt;
		
		boost::property_tree::ptree children;

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
			children.push_back(std::make_pair("", child));
		}
		
		pt.add_child("result", children);		

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}
	catch (Poco::Data::DataException &e)
	{
		content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}		
}


void HttpServer::decode_query(const std::string &content, std::map<std::string, std::string> &nvp)
{
	// split into a map
	std::vector<std::string> pairs;
 	boost::split(pairs, content, boost::is_any_of("&"), boost::token_compress_on);

	for(int i = 0; i < pairs.size(); i++)
	{
		std::vector<std::string> namevalue;
		boost::split(namevalue, pairs[i], boost::is_any_of("="), boost::token_compress_on);

		if(namevalue.size() != 2)
			continue;

		std::string name, value;
		url_decode(namevalue[0], name);
		url_decode(namevalue[1], value);

		nvp[name] = value;
	}
}

bool HttpServer::url_decode(const std::string& in, std::string& out)
{
	out.clear();
	out.reserve(in.size());
	for (std::size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] == '%')
		{
			if (i + 3 <= in.size())
			{
				int value = 0;
				std::istringstream is(in.substr(i + 1, 2));
				if (is >> std::hex >> value)
				{
					out += static_cast<char>(value);
					i += 2;
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else if (in[i] == '+')
		{
			out += ' ';
		}
		else
		{
			out += in[i];
		}
	}
	return true;
}