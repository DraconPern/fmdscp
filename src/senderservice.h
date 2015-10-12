#ifndef __SENDER_H__
#define __SENDER_H__

#include <boost/thread/mutex.hpp>
#include "model.h"
#include "dicomsender.h"

class SenderService
{
public:
	SenderService();
	void run();
	void stop();
	bool shouldShutdown();
	static void RunDICOMSender(boost::shared_ptr<DICOMSender> sender, std::string uuid);

	bool getQueued(OutgoingSession &outgoingsession);
	bool findDestination(int id, Destination &destination);
	bool GetFilesToSend(std::string studyinstanceuid, naturalset &result);
protected:

	boost::mutex mutex;
	bool shutdownEvent;
};

#endif