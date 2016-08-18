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


// Visual Leak Detector
#if defined(_WIN32) && defined(_DEBUG) && !defined(_WIN64)
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
		logger().information("starting up");
	}

	void uninitialize()
	{
		logger().information("shutting down");
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
		helpFormatter.setHeader("A sample server application that demonstrates some of the features of the Util::ServerApplication class.");
		helpFormatter.format(std::cout);
	}

	int main(const ArgVec& args)
	{	
		if (!_helpRequested)
		{			
			Aws::SDKOptions options;
			options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;
			Aws::InitAPI(options);

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
