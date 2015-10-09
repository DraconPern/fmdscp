#ifndef __SENDER_H__
#define __SENDER_H__

#include <boost/thread/mutex.hpp>
#include "model.h"

class SenderService
{
public:
	SenderService();
	void run();
	void stop();
	bool shouldShutdown();

	bool getQueued(OutgoingSession &outgoingsession);
protected:

	boost::mutex mutex;
	bool shutdownEvent;
};

#endif