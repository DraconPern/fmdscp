#ifndef __SERVER_H__
#define __SERVER_H__

#include <boost/thread/thread.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

#include "senderservice.h"
#include "myscp.h"

#include "httpserver.h"

class server
{
public:
	server();
	~server();

	void run_async();
	void join();
	void stop();
	bool shouldStop();
protected:

	void init_scp();

	void setStop(bool flag);
	boost::mutex event_mutex;
	bool stopEvent;

	// Create a pool of threads to run all of the io_services.
	std::vector<boost::shared_ptr<boost::thread> > threads;

	/// The io_service used to perform asynchronous operations.
	boost::asio::io_service io_service_;
			
	MyDcmSCPPool storageSCP;
	SenderService senderService;
	HttpServer httpserver;
};

#endif // __SERVER_H__