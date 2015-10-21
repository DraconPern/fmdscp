#include "server.h"
#include "config.h"

#include "soci/mysql/soci-mysql.h"
#include <boost/asio/io_service.hpp>
#include "ndcappender.h"

server::server()// : httpserver(8080, 10)
{
	// configure logging
	dcmtk::log4cplus::SharedAppenderPtr logfile(new NDCAsFilenameAppender("C:\\PACS\\Log"));
	dcmtk::log4cplus::Logger my_log = dcmtk::log4cplus::Logger::getRoot();
	my_log.addAppender(logfile);

	// do server wide init
	soci::register_factory_mysql();

	Config::registerCodecs();

	Config::createDBPool();

	std::string errormsg;
	if(!Config::test(errormsg))
	{
		/*
		app.logger().error(errormsg);
		app.logger().information("Exiting");
		*/
		throw new std::exception(errormsg.c_str());
	}

	// add scp
	init_scp();
	io_service_.post(boost::bind(&MyDcmSCPPool::listen, &storageSCP));

	// add sender
	io_service_.post(boost::bind(&SenderService::run, &senderService));	

	// add REST API
	io_service_.post(boost::bind(&HttpServer::start, &httpserver));
}

server::~server()
{
	Config::deregisterCodecs();
}

void server::init_scp()
{

	
	/*
	std::string errormsg;
	if(!StoreHandler::Test(errormsg))
		{
			app.logger().error(errormsg);
			app.logger().information("Exiting");
			ServerApplication::terminate();
			return;
		}		

		app.logger().information("Set up done.  Listening.");-*/
}

void server::run_async()
{	
	// Create a pool of threads to run all of the tasks assigned to io_services.
	for (std::size_t i = 0; i < 3; ++i)
	{
		boost::shared_ptr<boost::thread> thread(new boost::thread(
			boost::bind(&boost::asio::io_service::run, &io_service_)));
		threads.push_back(thread);
	}

	
}

void server::join()
{
	// Wait for all threads in the pool to exit.
	for (std::size_t i = 0; i < threads.size(); ++i)
		threads[i]->join();
}

void server::stop()
{
	stop(true);

	// also tell ioservice to stop servicing
	io_service_.stop();

	// also tell scp to stop
	storageSCP.stopAfterCurrentAssociations();

	// tell senderservice to stop
	senderService.stop();

	httpserver.stop();
}

void server::stop(bool flag)
{
	boost::mutex::scoped_lock lk(event_mutex);
	stopEvent = flag;
}

bool server::shouldStop()
{
	boost::mutex::scoped_lock lk(event_mutex);
	return stopEvent;
}
