
#include "sender.h"
#include "dicomsender.h"

Sender::Sender()
{
	shutdownEvent = false;


}

void Sender::run()
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

void Sender::stop()
{
	boost::mutex::scoped_lock lk(mutex);
	shutdownEvent = true;
}

bool Sender::shouldShutdown()
{
	boost::mutex::scoped_lock lk(mutex);
	return shutdownEvent;
}

