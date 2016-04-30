#ifndef __SERVER_H__
#define __SERVER_H__

#include <boost/thread/thread.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

#include "senderservice.h"
#include "myscp.h"

#include "httpserver.h"
#include "cloudclient.h"

class server
{
public:
	server(boost::function< void(void) > shutdownCallback);
	~server();

	void run_async();
	void stop();		
	bool shouldStop();
protected:	

	void setStop(bool flag);
	boost::mutex event_mutex;
	bool stopEvent;
		
	boost::thread_group threads;	

	// all the background tasks
	MyDcmSCPPool storageSCP;
	SenderService senderService;
	HttpServer httpserver;

	CloudClient cloudclient;
};

#endif // __SERVER_H__