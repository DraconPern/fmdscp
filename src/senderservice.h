#ifndef __SENDER_H__
#define __SENDER_H__

#include <boost/thread/mutex.hpp>
#include "model.h"
#include "sender.h"
#include <map>

class SenderService
{
public:
	SenderService();	
	void run();
	void run_internal();
	void stop();
	bool shouldShutdown();
	
	bool getQueued(OutgoingSession &outgoingsession);
	bool findDestination(int id, Destination &destination);
	bool GetFilesToSend(std::string studyinstanceuid, naturalpathmap &result);
protected:

	boost::mutex mutex;
	bool shutdownEvent;

	typedef std::list<boost::shared_ptr<Sender> > sharedptrlist;
	sharedptrlist senders;
};

#endif