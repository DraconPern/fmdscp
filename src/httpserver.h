#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include "server_http.hpp"


class HttpServer : public SimpleWeb::Server<SimpleWeb::HTTP>
{
public:
	HttpServer();
	
	static void NotFound(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	static void SearchForStudies(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	static void WADO(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	static void decode_query(const std::string &content, std::map<std::string, std::string> &nvp);
	static bool url_decode(const std::string& in, std::string& out);
};

#endif