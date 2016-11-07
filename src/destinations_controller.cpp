#include "config.h"

#include "destinations_controller.h"
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>
#include <codecvt>
#include "model.h"
#include "util.h"

#include "Poco/Data/Session.h"
using namespace Poco::Data::Keywords;

destinations_controller::destinations_controller(CloudClient &cloudclient, DBPool &dbpool, std::unordered_map<std::string, std::unordered_map<std::string,
	std::function<void(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response>, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request>)> > > &resource) :
	cloudclient(cloudclient), dbpool(dbpool)
{
	resource["^/api/destinations"]["GET"] = boost::bind(&destinations_controller::api_destinations_list, this, _1, _2);
	resource["^/api/destinations"]["POST"] = boost::bind(&destinations_controller::api_destinations_create, this, _1, _2);
	resource["^/api/destinations/([0123456789abcdef\\-]+)"]["GET"] = boost::bind(&destinations_controller::api_destinations_get, this, _1, _2);
	resource["^/api/destinations/([0123456789abcdef\\-]+)"]["POST"] = boost::bind(&destinations_controller::api_destinations_update, this, _1, _2);
	resource["^/api/destinations/([0123456789abcdef\\-]+)/delete"]["POST"] = boost::bind(&destinations_controller::api_destinations_delete, this, _1, _2);
}

void destinations_controller::api_destinations_list(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request)
{		
	std::vector<Destination> destination_list;
	try
	{
		Poco::Data::Session dbconnection(dbpool.get());

		Poco::Data::Statement stselect(dbconnection);
		stselect << "SELECT id,"
			"name,"
			"destinationhost,"
			"destinationport,"
			"destinationAE,"
			"sourceAE,"		
			"createdAt,updatedAt"
			" FROM destinations",
			into(destination_list);
		stselect.execute();

		boost::property_tree::ptree pt, children;

		for (int i = 0; i < destination_list.size(); i++)
		{
			boost::property_tree::ptree child;
			child.add("id", destination_list[i].id);
			child.add("name", destination_list[i].name);
			child.add("destinationhost", destination_list[i].destinationhost);			
			child.add("destinationport", destination_list[i].destinationport);
			child.add("destinationAE", destination_list[i].destinationAE);
			child.add("sourceAE", destination_list[i].sourceAE);
			children.push_back(std::make_pair("", child));
		}

		pt.add_child("destinations", children);

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}


void destinations_controller::api_destinations_get(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request)
{	
	std::string idstr = request->path_match[1];

	std::vector<Destination> destination_list;
	try
	{
		int id = boost::lexical_cast<int>(idstr);

		Poco::Data::Session dbconnection(dbpool.get());

		Poco::Data::Statement stselect(dbconnection);
		stselect << "SELECT id,"
			"name,"
			"destinationhost,"
			"destinationport,"
			"destinationAE,"
			"sourceAE,"
			"createdAt,updatedAt"
			" FROM destinations WHERE id = ?",
			into(destination_list),
			use(id);
		stselect.execute();

		if (destination_list.size() >= 1)
		{	
			boost::property_tree::ptree pt;
		
			boost::property_tree::ptree child;
			child.add("id", destination_list[0].id);
			child.add("name", destination_list[0].name);
			child.add("destinationhost", destination_list[0].destinationhost);
			child.add("destinationport", destination_list[0].destinationport);
			child.add("destinationAE", destination_list[0].destinationAE);
			child.add("sourceAE", destination_list[0].sourceAE);
			
			pt.add_child("destination", child);

			std::ostringstream buf;
			boost::property_tree::json_parser::write_json(buf, pt, true);
			std::string content = buf.str();
			*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}
		else
		{
			std::string content = "Destination not found";
			*response << std::string("HTTP/1.1 404 Not found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}
	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

void destinations_controller::api_destinations_create(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request)
{	
	std::map<std::string, std::string> queries;
	decode_query(request->content.string(), queries);

	try
	{		
		Destination dest;
		if (queries.find("name") != queries.end())
			dest.name = queries["name"];
		if (queries.find("destinationhost") != queries.end())
			dest.destinationhost = queries["destinationhost"];
		if (queries.find("destinationport") != queries.end())
			dest.destinationport = boost::lexical_cast<int>(queries["destinationport"]);
		if (queries.find("destinationAE") != queries.end())
			dest.destinationAE = queries["destinationAE"];
		if (queries.find("sourceAE") != queries.end())
			dest.sourceAE = queries["sourceAE"];
		
		dest.created_at = Poco::DateTime();
		dest.updated_at = Poco::DateTime();

		Poco::Data::Session dbconnection(dbpool.get());

		Poco::Data::Statement insert(dbconnection);
		insert << "INSERT INTO destinations (id, name, destinationhost, destinationport, destinationAE, sourceAE, createdAt, updatedAt) VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
			use(dest);
		insert.execute();

		dbconnection << "SELECT LAST_INSERT_ID()", into(dest.id), now;

		boost::property_tree::ptree pt, children;
		
		std::ostringstream str;
		str << dest.id;
		pt.add("result", str.str());

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

void destinations_controller::api_destinations_update(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request)
{
	std::string idstr = request->path_match[1];
	
	std::map<std::string, std::string> queries;
	decode_query(request->content.string(), queries);

	try
	{
		Poco::Data::Session dbconnection(dbpool.get());

		int id = boost::lexical_cast<int>(idstr);
		
		std::vector<Destination> destination_list;
		Poco::Data::Statement stselect(dbconnection);
		stselect << "SELECT id,"
			"name,"
			"destinationhost,"
			"destinationport,"
			"destinationAE,"
			"sourceAE,"
			"createdAt,updatedAt"
			" FROM destinations WHERE id = ?",
			into(destination_list),
			use(id);

		stselect.execute();

		if (destination_list.size() == 0)
		{
			std::string content = "item not found";
			*response << std::string("HTTP/1.1 404 Not Found\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
			return;
		}

		Destination &dest = destination_list[0];
		
		if (queries.find("name") != queries.end())
			dest.name = queries["name"];
		if (queries.find("destinationhost") != queries.end())
			dest.destinationhost = queries["destinationhost"];
		if (queries.find("destinationport") != queries.end())
			dest.destinationport = boost::lexical_cast<int>(queries["destinationport"]);
		if (queries.find("destinationAE") != queries.end())
			dest.destinationAE = queries["destinationAE"];
		if (queries.find("sourceAE") != queries.end())
			dest.sourceAE = queries["sourceAE"];

		dest.updated_at = Poco::DateTime();
		
		Poco::Data::Statement update(dbconnection);
		update << "UPDATE destinations SET "
			"id = ?,"
			"name = ?,"
			"destinationhost = ?,"
			"destinationport = ?,"
			"destinationAE = ?,"
			"sourceAE = ?,"
			"createdAt = ?, updatedAt = ?"
			" WHERE id = ?",
			use(dest),
			use(dest.id);
		update.execute();
		
		boost::property_tree::ptree pt, children;

		std::ostringstream str;
		str << dest.id;
		pt.add("result", str.str());

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

void destinations_controller::api_destinations_delete(std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Response> response, std::shared_ptr<SimpleWeb::ServerBase<SimpleWeb::HTTP>::Request> request)
{	
	std::string querystring = request->path_match[1];

	std::map<std::string, std::string> queries;
	decode_query(querystring, queries);
	
	try
	{
		Destination dest;
		if (queries.find("id") != queries.end())
			dest.id = boost::lexical_cast<int>(queries["id"]);

		Poco::Data::Session dbconnection(dbpool.get());

		Poco::Data::Statement stdelete(dbconnection);
		stdelete << "DELETE FROM destinations WHERE id = ?",
			use(dest.id);
		stdelete.execute();

		boost::property_tree::ptree pt, children;
		
		pt.add("result", "ok");

		std::ostringstream buf;
		boost::property_tree::json_parser::write_json(buf, pt, true);
		std::string content = buf.str();
		*response << std::string("HTTP/1.1 200 Ok\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		return;
	}
	catch (Poco::Data::DataException &e)
	{
		std::string content = "Database Error";
		*response << std::string("HTTP/1.1 503 Service Unavailable\r\nContent-Length: ") << content.length() << "\r\n\r\n" << content;
		cloudclient.sendlog(std::string("dberror"), e.displayText());
		return;
	}
}

