#include "server.h"
#include "config.h"

#include "Poco/Data/MySQL/Connector.h"
#include <boost/asio/io_service.hpp>
#include "ndcappender.h"

server::server() : 
	httpserver(std::bind(&server::stop, this)),
	cloudclient(std::bind(&server::stop, this))
{
	// configure logging
	dcmtk::log4cplus::SharedAppenderPtr logfile(new NDCAsFilenameAppender("C:\\PACS\\Log"));
	dcmtk::log4cplus::Logger my_log = dcmtk::log4cplus::Logger::getRoot();
	my_log.addAppender(logfile);

	// do server wide init	
	Poco::Data::MySQL::Connector::registerConnector();

	config::registerCodecs();

	config::createDBPool();

	std::string errormsg;
	if(!config::test(errormsg))
	{		
		DCMNET_ERROR(errormsg);
		DCMNET_INFO("Exiting");
		
		throw new std::exception(errormsg.c_str());
	}

	// should probably use thread_group instead...
	
	// add sender
	// io_service_.post(boost::bind(&SenderService::run, &senderService));	

	// add web/rest API
	// io_service_.post(boost::bind(&HttpServer::start, &httpserver));

	// add websocket that connects to cloud
	// cloudclient.connect("http://home.draconpern.com");
}

server::~server()
{
	config::deregisterCodecs();

	Poco::Data::MySQL::Connector::unregisterConnector();
}

void server::run_async()
{	
	// add scp
	threads.create_thread(boost::bind(&MyDcmSCPPool::listen, &storageSCP));
	
	// add sender
	threads.create_thread(boost::bind(&SenderService::run, &senderService));

	cloudclient.connect("http://home.draconpern.com");
}

void server::join()
{
	threads.join_all();
}

void server::stop()
{
	setStop(true);
	
	// tell scp to stop
	storageSCP.stopAfterCurrentAssociations();

	// tell senderservice to stop
	senderService.stop();

	// stop webserver
	httpserver.stop();	

	// stop socketio to cloud
	cloudclient.close();		
	cloudclient.clear_con_listeners();
}

void server::setStop(bool flag)
{
	boost::mutex::scoped_lock lk(event_mutex);
	stopEvent = flag;
}

bool server::shouldStop()
{
	boost::mutex::scoped_lock lk(event_mutex);
	return stopEvent;
}
