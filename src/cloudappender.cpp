#include "cloudappender.h"
#include <fstream>
#include <iostream>

using namespace std;

CloudAppender::CloudAppender(CloudClient &cloudclient) :
	cloudclient(cloudclient)
{

}

CloudAppender::~CloudAppender()
{

}

void CloudAppender::close()
{

}

void CloudAppender::append(const spi::InternalLoggingEvent& event)
{	
	// file open and output 
	std::string context;
	if(event.getNDC().length() != 0)
		context = event.getNDC().c_str();
	else
		context = "listener";
	stringstream message;	
	layout->formatAndAppend(message, event);
	cloudclient.sendlog(context, message.str());
}