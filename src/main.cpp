#include "server.h"
#include "store.h"
#include "myscp.h"
#include "config.h"

#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Task.h"
#include "Poco/TaskManager.h"
#include "Poco/DateTimeFormatter.h"
#include <iostream>

#include <aws/core/Aws.h>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/scppool.h"   /* for DcmStorageSCP */

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include <boost/asio/ssl/detail/openssl_init.hpp>

// Visual Leak Detector
#if defined(_WIN32) && defined(_DEBUG) // && !defined(_WIN64)
#include <vld.h>
#endif

using namespace Poco;
using namespace Poco::Util;

class SampleServer: public ServerApplication
{
public:
	SampleServer(): _helpRequested(false)
	{
	}

	~SampleServer()
	{
	}

protected:
	void initialize(Application& self)
	{
		loadConfiguration(); // load default configuration files, if present
		ServerApplication::initialize(self);		
	}

	void uninitialize()
	{
		ServerApplication::uninitialize();
	}

	void defineOptions(OptionSet& options)
	{
		ServerApplication::defineOptions(options);

		options.addOption(
			Option("help", "h", "display help information on command line arguments")
			.required(false)
			.repeatable(false)
			.callback(OptionCallback<SampleServer>(this, &SampleServer::handleHelp)));
	}

	void handleHelp(const std::string& name, const std::string& value)
	{
		_helpRequested = true;
		displayHelp();
		stopOptionsProcessing();
	}

	void displayHelp()
	{
		HelpFormatter helpFormatter(options());
		helpFormatter.setCommand(commandName());
		helpFormatter.setUsage("OPTIONS");
		helpFormatter.setHeader("fmdscp DICOM server");
		helpFormatter.format(std::cout);
	}

	bool checksystem()
	{
		std::string errormsg;
		DBPool dbpool;
		if (!config::test(errormsg, dbpool))
		{
			DCMNET_ERROR(errormsg);
			DCMNET_INFO("Exiting");
			std::cerr << errormsg;
			return false;
		}		
		return true;
	}

	int main(const ArgVec& args)
	{
		if (args.size() > 0)
		{
			displayHelp();

			for (int i = 0; i < args.size(); i++)
				std::cout << "Unknown option: " << args[i] << std::endl;

			return Application::EXIT_OK;
		}

		if (!_helpRequested)
		{
			boost::asio::ssl::detail::openssl_init<> _openssl_init;

			Aws::SDKOptions options;
			Aws::InitAPI(options);

			boost::filesystem::path::codecvt();  // ensure VC++ does not race during initialization.

			if (!checksystem())
			{
				Aws::ShutdownAPI(options);
				return Application::EXIT_OK;
			}

			server s(boost::bind(ServerApplication::terminate));
			s.run_async();

			// wait for OS to tell us to stop
			waitForTerminationRequest();

			// stop server			
			s.stop();

			Aws::ShutdownAPI(options);		
		}
		return Application::EXIT_OK;
	}

private:
	bool _helpRequested;
};


POCO_SERVER_MAIN(SampleServer)
