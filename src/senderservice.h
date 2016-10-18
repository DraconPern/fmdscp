#ifndef __SENDER_H__
#define __SENDER_H__

#include <boost/thread/mutex.hpp>
#include "model.h"
#include "sender.h"
#include <map>

#include "cloudclient.h"
#include "dbpool.h"

class SenderService
{
public:
	SenderService(CloudClient &cloudclient, DBPool &dbpool);
	void run();
	void stop();
	bool cancelSend(std::string uuid);

protected:
	void run_internal();
	bool getQueued(OutgoingSession &outgoingsession);
	bool findDestination(int id, Destination &destination);
	bool GetFilesToSend(std::string studyinstanceuid, naturalpathmap &result);
	bool shouldShutdown();

	boost::mutex mutex;
	bool shutdownEvent;

	typedef std::list<boost::shared_ptr<Sender> > sharedptrlist;
	sharedptrlist senders;

	CloudClient &cloudclient;
	DBPool &dbpool;
};

#endif