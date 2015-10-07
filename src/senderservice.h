#ifndef __SENDER_H__
#define __SENDER_H__

#include <boost/thread/mutex.hpp>

class SenderService
{
public:
	SenderService();
	void run();
	void stop();
	bool shouldShutdown();

protected:

	boost::mutex mutex;
	bool shutdownEvent;
};

#endif