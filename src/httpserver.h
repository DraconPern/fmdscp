#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include "server_http.hpp"
#include <functional>

class HttpServer : public SimpleWeb::Server<SimpleWeb::HTTP>
{
public:
	HttpServer(std::function< void(void) > shutdownCallback);
	
	void Version(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);
	void Shutdown(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);
	void NotFound(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	void NotAcceptable(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	void SearchForStudies(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	void WADO(HttpServer::Response& response, std::shared_ptr<HttpServer::Request> request);	
	void decode_query(const std::string &content, std::map<std::string, std::string> &nvp);
	bool url_decode(const std::string& in, std::string& out);

	std::function< void(void) > shutdownCallback;
};

#endif