#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include "server_http.hpp"
#include <functional>
#include "cloudclient.h"

#include "destinations_controller.h"

class HttpServer : public SimpleWeb::Server<SimpleWeb::HTTP>
{
public:
	HttpServer(std::function< void(void) > shutdownCallback, CloudClient &cloudclient);
	
	void Version(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void Shutdown(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void NotFound(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void NotAcceptable(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void SearchForStudies(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void SendStudy(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void WADO(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);

protected:	
	std::function< void(void) > shutdownCallback;
	CloudClient &cloudclient;
	destinations_controller destinations_controller;
};

#endif