
#include "senderservice.h"
#include "dicomsender.h"

SenderService::SenderService()
{
	shutdownEvent = false;


}

void SenderService::run()
{
	 while(!shouldShutdown())
	 {


	 }
}
/*
void Sender::join()
{


}
*/

void SenderService::stop()
{
	boost::mutex::scoped_lock lk(mutex);
	shutdownEvent = true;
}

bool SenderService::shouldShutdown()
{
	boost::mutex::scoped_lock lk(mutex);
	return shutdownEvent;
}

