#ifndef __DESTINATIONS_CONTROLLER_H__
#define __DESTINATIONS_CONTROLLER_H__

#include "server_http.hpp"
#include <functional>
#include "cloudclient.h"
#include "dbpool.h"

class DestinationsController
{
public:
	DestinationsController(CloudClient &cloudclient, DBPool &dbpool, std::unordered_map < std::string, std::unordered_map < std::string,
		std::function<void(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>)> > > &resource);
		
	void api_destinations_list(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_get(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_create(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_update(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);
	void api_destinations_delete(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request);

protected:
	
	CloudClient &cloudclient;
	DBPool &dbpool;
};

#endif