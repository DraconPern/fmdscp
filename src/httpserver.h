#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include "server_http.hpp"
#include <boost/function.hpp>
#include "cloudclient.h"
#include "senderservice.h"
#include "destinations_controller.h"

class HttpServer : public SimpleWeb::Server<SimpleWeb::HTTP>
{
public:
	HttpServer(boost::function< void(void) > shutdownCallback, CloudClient &cloudclient, SenderService &senderservice);
	
	void Version(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void Shutdown(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void NotFound(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void NotAcceptable(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void SearchForStudies(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);	
	void SendStudy(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void CancelSend(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void WADO_URI(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void StudyInfo(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);
	void GetImage(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);

	void GetOutSessions(std::shared_ptr<HttpServer::Response> response, std::shared_ptr<HttpServer::Request> request);

protected:	
	boost::function< void(void) > shutdownCallback;
	CloudClient &cloudclient;
	destinations_controller destinations_controller;
	SenderService &senderservice;
};

#endif