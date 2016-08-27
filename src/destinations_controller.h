#ifndef __DESTINATIONS_CONTROLLER_H__
#define __DESTINATIONS_CONTROLLER_H__

#include "server_http.hpp"
#include <functional>
#include "cloudclient.h"

class destinations_controller
{
public:
	destinations_controller(CloudClient &cloudclient);
		
	void api_destinations_list(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_create(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_update(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_delete(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);

protected:
	
	CloudClient &cloudclient;
};

#endif