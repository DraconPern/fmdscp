#include "cloudclient.h"
#include <boost/function.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>
#include "util.h"

// using namespace std::placeholders;

CloudClient::CloudClient(boost::function< void(void) > shutdownCallback) :
	shutdownCallback(shutdownCallback)
{		
	set_reconnect_delay(5);

	set_socket_open_listener(boost::bind(&CloudClient::OnConnection, this));
	socket()->on("send", (sio::socket::event_listener_aux) boost::bind(&CloudClient::OnSend, this, _1, _2, _3, _4));
	socket()->on("shutdown", (sio::socket::event_listener) boost::bind(&CloudClient::OnShutdown, this, _1));

	socket()->on("new_access_token", (sio::socket::event_listener_aux) boost::bind(&CloudClient::OnNewAccessToken, this, _1, _2, _3, _4));	
}

void CloudClient::OnNewAccessToken(const std::string& name, message::ptr const& message, bool need_ack, message::list& ack_message)
{
	if (message->get_flag() == message::flag_object)
	{
		std::string access_token = message->get_map()["access_token"]->get_string();
		int expires_in = message->get_map()["expires_in"]->get_int();
	}


}

void CloudClient::stop()
{
	socket()->off_all();
	socket()->off_error();
	
	clear_con_listeners();
	clear_socket_listeners();
	sync_close();
}

void CloudClient::OnShutdown(sio::event &event)
{
	// call shutdown
	shutdownCallback();
}

void CloudClient::OnSend(const std::string& name, message::ptr const& message, bool need_ack, message::list& ack_message)
{
	if (message->get_flag() == message::flag_object)
	{
		std::string msg = message->get_map()["hello"]->get_string();
		std::string wow = msg;
	}
}

void CloudClient::OnConnection()
{		
	message::ptr o = object_message::create();	
	o->get_map()["access_token"] = string_message::create("Dkdjfj3kd9LKjf");
	
	// tell the server we are an agent, and also give them our access token
	socket()->emit("agent_auth", o);
	//socket()->emit("something", username);
}

void CloudClient::sendlog(std::string &context, std::string &message)
{
	message::ptr o = object_message::create();
	o->get_map()["context"] = string_message::create(context);
	o->get_map()["message"] = string_message::create(message);

	socket()->emit("log", o);
}


void CloudClient::send_updateoutsessionitem(OutgoingSession &outgoingsession, std::string &destination_name)
{
	message::ptr o = object_message::create();
	o->get_map()["uuid"] = string_message::create(outgoingsession.uuid);
	o->get_map()["StudyInstanceUID"] = string_message::create(outgoingsession.StudyInstanceUID);
	o->get_map()["PatientName"] = string_message::create(outgoingsession.PatientName);
	o->get_map()["PatientID"] = string_message::create(outgoingsession.PatientID);
	o->get_map()["destination_id"] = string_message::create(boost::lexical_cast<std::string>(outgoingsession.destination_id));
	o->get_map()["destination_name"] = string_message::create(destination_name);
	o->get_map()["updatedAt"] = string_message::create(ToJSON(outgoingsession.updated_at));
	o->get_map()["createdAt"] = string_message::create(ToJSON(outgoingsession.created_at));
	o->get_map()["status"] = string_message::create(outgoingsession.status);

	socket()->emit("updateoutsessionitem", o);
}
