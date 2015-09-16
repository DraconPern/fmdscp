#ifndef __SERVER_H__
#define __SERVER_H__

#include <boost/thread/thread.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/scppool.h"   /* for DcmStorageSCP */

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif


#include "myscp.h"

class server
{
public:
	server();
	~server();

	void run_async();
	void join();
	void stop();
	bool shouldStop();
	//void testme();
protected:

	void init_scp();

	void stop(bool flag);
	boost::mutex event_mutex;
	bool stopEvent;

	// Create a pool of threads to run all of the io_services.
	std::vector<boost::shared_ptr<boost::thread> > threads;

	/// The io_service used to perform asynchronous operations.
	boost::asio::io_service io_service_;
	
	// scp
	// DcmSCPPool<MySCP> storageSCP;
	MyDcmSCPPool<> storageSCP;
};

#endif // __SERVER_H__