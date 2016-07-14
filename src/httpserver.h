#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include "server_http.hpp"
#include <functional>

class HttpServer : public SimpleWeb::Server<SimpleWeb::HTTP>
{
public:
	HttpServer(std::function< void(void) > shutdownCallback);
	
	void Version(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void Shutdown(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void NotFound(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void NotAcceptable(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void SearchForStudies(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void WADO(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);

protected:
	void decode_query(const std::string &content, std::map<std::string, std::string> &nvp);
	bool url_decode(const std::string& in, std::string& out);

	std::function< void(void) > shutdownCallback;
};

#endif