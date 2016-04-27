#include "cloudclient.h"


CloudClient::CloudClient(std::function< void(void) > shutdownCallback) :
	shutdownCallback(shutdownCallback)
{		
	set_reconnect_delay(5);

	socket()->on("shutdown", (sio::socket::event_listener) std::bind(&CloudClient::OnShutdown, this, std::placeholders::_1));
	socket()->on("connection", (sio::socket::event_listener) std::bind(&CloudClient::OnConnection, this, std::placeholders::_1));
	socket()->on("shutdown", (sio::socket::event_listener) std::bind(&CloudClient::OnShutdown, this, std::placeholders::_1));
}

void CloudClient::OnShutdown(sio::event &event)
{
	// call shutdown
	shutdownCallback();
}

void CloudClient::OnConnection(sio::event &event)
{
	//socket()->emit("something", username);
	//socket()->emit("something", username);
}