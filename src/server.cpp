#include "server.h"
#include "config.h"

#include "Poco/Data/MySQL/Connector.h"
#include <boost/asio/io_service.hpp>
#include "ndcappender.h"
#include "cloudappender.h"

server::server(boost::function< void(void) > shutdownCallback) :
	httpserver(shutdownCallback),
	cloudclient(shutdownCallback)
{
	// configure logging
	dcmtk::log4cplus::SharedAppenderPtr logfile(new NDCAsFilenameAppender("C:\\PACS\\Log"));
	dcmtk::log4cplus::SharedAppenderPtr cloud(new CloudAppender(cloudclient));

	dcmtk::log4cplus::Logger my_log = dcmtk::log4cplus::Logger::getRoot();
	my_log.addAppender(logfile);
	my_log.addAppender(cloud);

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

}

server::~server()
{
	config::deregisterCodecs();

	Poco::Data::MySQL::Connector::unregisterConnector();
}

void server::run_async()
{	
	// connect to cloud
	// cloudclient.connect("http://localhost:8090");

	// add scp
	threads.create_thread(boost::bind(&MyDcmSCPPool::listen, &storageSCP));
	
	// add sender
	threads.create_thread(boost::bind(&SenderService::run, &senderService));

	httpserver.start();
}

void server::stop()
{
	setStop(true);

	// stop webserver
	httpserver.stop();

	// tell senderservice to stop
	senderService.stop();
	
	// tell scp to stop
	storageSCP.stopAfterCurrentAssociations();

	// stop socketio to cloud
	cloudclient.stop();

	threads.join_all();
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
