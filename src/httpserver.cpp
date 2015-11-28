#include <winsock2.h>	// include winsock2 before network includes
#include "config.h"
#include "soci/soci.h"

#include "httpserver.h"
#include <boost/algorithm/string.hpp>
#include "model.h"


// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include <winsock2.h>	// include winsock2 before network includes
#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/oflog/ndc.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

HttpServer::HttpServer() : SimpleWeb::Server<SimpleWeb::HTTP>(8080, 10)
{	
	resource["^/studies\\?(.+)$"]["GET"] = SearchForStudies;
	resource["^/wado\\?(.+)$"]["GET"] = WADO;
	default_resource["GET"] = NotFound;
}


void HttpServer::NotFound(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request)
{
	std::string content = "Path not found: " + request->path;
	response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
}

void HttpServer::WADO(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request)
{	
	std::string content;
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;	
	decode_query(querystring, queries);	
	if(queries["requestType"] != "WADO")
	{
		content = "Not a WADO request";
		response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	soci::session dbconnection(config::getConnectionString());

	PatientStudy patientstudy;
	soci::session &patientstudiesselect = dbconnection;
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
		"created_at,updated_at"
		" FROM patient_studies WHERE StudyInstanceUID = :studyuid",
		soci::into(patientstudy),
		soci::use(queries["studyUID"]);
	
	Series series;
	soci::session &seriesselect = dbconnection;
	seriesselect << "SELECT id,"
		"SeriesInstanceUID,"
		"StudyInstanceUID,"
		"Modality,"
		"SeriesDescription,"
		"SeriesNumber,"
		"SeriesDate,"
		"created_at,updated_at"
		" FROM series WHERE SeriesInstanceUID = :seriesuid AND StudyInstanceUID = :studyuid",
		soci::into(series),
		soci::use(queries["seriesUID"]),
		soci::use(queries["studyUID"]);
	
	Instance instance;		
	soci::session &instanceselect = dbconnection;
	instanceselect << "SELECT id,"
		"SOPInstanceUID,"
		"SeriesInstanceUID,"
		"InstanceNumber,"
		"created_at,updated_at"
		" FROM instances WHERE SOPInstanceUID = :SOPInstanceUID AND SeriesInstanceUID = :seriesuid",
		soci::into(instance),
		soci::use(queries["objectUID"]),
		soci::use(queries["seriesUID"]);
	
	if(patientstudy.id == 0 || series.id == 0 || instance.id == 0)
	{
		content = "Object not found in database";
		response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	boost::filesystem::path sourcepath = config::getStoragePath();
	sourcepath /= patientstudy.StudyInstanceUID;
	sourcepath /= series.SeriesInstanceUID;	
	sourcepath /= instance.SOPInstanceUID + ".dcm";

	
	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(sourcepath.string().c_str());

	// now let's see what we need to output
	if(queries["contentType"] == "application/dicom")
	{
		// decompress
		dfile.getDataset()->chooseRepresentation(EXS_LittleEndianExplicit, NULL);

		boost::filesystem::path newpath = boost::filesystem::unique_path();
		if(dfile.getDataset()->canWriteXfer(EXS_LittleEndianExplicit))
		{
			dfile.getDataset()->loadAllDataIntoMemory();

			dfile.saveFile(newpath.string().c_str(), EXS_LittleEndianExplicit);
		}
		else
			boost::filesystem::copy(sourcepath, newpath);

		try
		{
			if(boost::filesystem::file_size(newpath) > 0)
			{
				std::ifstream source(newpath.string(), std::ios::binary);
				response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << boost::filesystem::file_size(newpath) << "\r\n" 
					<< "Content-Type: application/dicom\r\n"
					<< "Content-disposition: attachment; filename=\"" << instance.SOPInstanceUID + ".dcm" << "\"\r\n"
					<< "\r\n";
				
				std::istreambuf_iterator<char> begin_source(source);
				std::istreambuf_iterator<char> end_source;
				std::ostreambuf_iterator<char> begin_dest(response); 
				std::copy(begin_source, end_source, begin_dest);
			}		
		}
		catch(...)
		{
			content = "Object not found on disk";
			response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		}
	}
	else
	{
		//output jpeg
		boost::filesystem::path newpath = boost::filesystem::unique_path();

		try
		{
			if(boost::filesystem::file_size(newpath) > 0)
			{
				std::ifstream source(newpath.string(), std::ios::binary);
				response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << boost::filesystem::file_size(newpath) << "\r\n" 
					<< "Content-Type: application/dicom\r\n"
					<< "Content-disposition: attachment; filename=\"" << instance.SOPInstanceUID + ".dcm" << "\"\r\n"
					<< "\r\n";
				
				std::istreambuf_iterator<char> begin_source(source);
				std::istreambuf_iterator<char> end_source;
				std::ostreambuf_iterator<char> begin_dest(response); 
				std::copy(begin_source, end_source, begin_dest);
			}		
		}
		catch(...)
		{
			content = "Object not found on disk";
			response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		}
	}
}

void HttpServer::SearchForStudies(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request)
{
	std::string content;
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;	
	decode_query(querystring, queries);	
	if(queries["requestType"] != "WADO")
	{
		content = "Not a WADO request";
		response << std::string("HTTP/1.1 400 Bad Request\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}

	response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
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