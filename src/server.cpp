#include "server.h"
#include "config.h"

#include "Poco/Data/MySQL/Connector.h"
#include <boost/asio/io_service.hpp>
#include "ndcappender.h"
#include "cloudappender.h"

server::server(boost::function< void(void) > shutdownCallback) :
	httpserver(shutdownCallback, cloudclient, senderService, dbpool),
	senderService(cloudclient, dbpool),
	storageSCP(cloudclient, dbpool),
	cloudclient(shutdownCallback)
{
	// configure logging
	dcmtk::log4cplus::SharedAppenderPtr logfile(new NDCAsFilenameAppender("C:\\PACS\\Log"));
	dcmtk::log4cplus::SharedAppenderPtr cloud(new CloudAppender(cloudclient));

	dcmtk::log4cplus::Logger my_log = dcmtk::log4cplus::Logger::getRoot();
	// my_log.removeAllAppenders();
	my_log.addAppender(logfile);
	// my_log.addAppender(cloud);

	OFLog::configure(OFLogger::INFO_LOG_LEVEL);
	

	// do server wide init	
	Poco::Data::MySQL::Connector::registerConnector();

	config::registerCodecs();
}

server::~server()
{
	config::deregisterCodecs();

	Poco::Data::MySQL::Connector::unregisterConnector();
}

void server::run_async()
{	
	// connect to cloud
	cloudclient.connect(config::getFrontEnd());

	// add scp
	threads.create_thread(boost::bind(&MyDcmSCPPool::listen, &storageSCP));
	
	// add sender
	threads.create_thread(boost::bind(&SenderService::run, &senderService));

	threads.create_thread(boost::bind(&HttpServer::start, &httpserver));
}

void server::stop()
{
	setStop(true);

	// tell senderservice to stop
	senderService.stop();

	// tell scp to stop
	storageSCP.stopAfterCurrentAssociations();

	// stop webserver
	httpserver.stop();

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
