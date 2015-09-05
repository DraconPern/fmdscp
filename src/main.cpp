#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/Util/HelpFormatter.h"
#include "Poco/Task.h"
#include "Poco/TaskManager.h"
#include "Poco/DateTimeFormatter.h"
#include <iostream>

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

#include "myscp.h"

using Poco::Util::Application;
using Poco::Util::ServerApplication;
using Poco::Util::Option;
using Poco::Util::OptionSet;
using Poco::Util::OptionCallback;
using Poco::Util::HelpFormatter;
using Poco::Task;
using Poco::TaskManager;
using Poco::DateTimeFormatter;

/* DICOM standard transfer syntaxes */
static const char* transferSyntaxes[] = {
      UID_LittleEndianImplicitTransferSyntax, /* default xfer syntax first */
      UID_LittleEndianExplicitTransferSyntax,
      UID_JPEGProcess1TransferSyntax,
      UID_JPEGProcess2_4TransferSyntax,
      UID_JPEGProcess3_5TransferSyntax,
      UID_JPEGProcess6_8TransferSyntax,
      UID_JPEGProcess7_9TransferSyntax,
      UID_JPEGProcess10_12TransferSyntax,
      UID_JPEGProcess11_13TransferSyntax,
      UID_JPEGProcess14TransferSyntax,
      UID_JPEGProcess15TransferSyntax,
      UID_JPEGProcess16_18TransferSyntax,
      UID_JPEGProcess17_19TransferSyntax,
      UID_JPEGProcess20_22TransferSyntax,
      UID_JPEGProcess21_23TransferSyntax,
      UID_JPEGProcess24_26TransferSyntax,
      UID_JPEGProcess25_27TransferSyntax,
      UID_JPEGProcess28TransferSyntax,
      UID_JPEGProcess29TransferSyntax,
      UID_JPEGProcess14SV1TransferSyntax,
      UID_RLELosslessTransferSyntax,
      UID_JPEGLSLosslessTransferSyntax,
      UID_JPEGLSLossyTransferSyntax,
      UID_DeflatedExplicitVRLittleEndianTransferSyntax,
      UID_JPEG2000LosslessOnlyTransferSyntax,
      UID_JPEG2000TransferSyntax,
      UID_MPEG2MainProfileAtMainLevelTransferSyntax,
      UID_MPEG2MainProfileAtHighLevelTransferSyntax,
      UID_JPEG2000Part2MulticomponentImageCompressionLosslessOnlyTransferSyntax,
      UID_JPEG2000Part2MulticomponentImageCompressionTransferSyntax,
      UID_MPEG4HighProfileLevel4_1TransferSyntax,
      UID_MPEG4BDcompatibleHighProfileLevel4_1TransferSyntax,
      UID_MPEG4HighProfileLevel4_2_For2DVideoTransferSyntax,
      UID_MPEG4HighProfileLevel4_2_For3DVideoTransferSyntax,
      UID_MPEG4StereoHighProfileLevel4_2TransferSyntax
};

class SampleTask: public Task
{
public:

	SampleTask(): Task("SampleTask")
	{
	}
	
	void runTask()
	{	
		Application& app = Application::instance();

		storageSCP.getConfig().setConnectionBlockingMode(DUL_NOBLOCK);
		storageSCP.getConfig().setConnectionTimeout(1);

		OFList<OFString> syntaxes;
		for(int i = 0; i < DIM_OF(transferSyntaxes); i++)
			syntaxes.push_back(transferSyntaxes[i]);		

		storageSCP.getConfig().addPresentationContext(UID_VerificationSOPClass, syntaxes);

		for(int i = 0; i < numberOfAllDcmStorageSOPClassUIDs; i++)
			storageSCP.getConfig().addPresentationContext(dcmAllStorageSOPClassUIDs[i], syntaxes);


		storageSCP.listen();
	}

	void cancel()
	{
		storageSCP.stopAfterCurrentAssociations();
	}

	DcmSCPPool<MySCP> storageSCP;
};

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
			TaskManager tm;
			tm.start(new SampleTask);
			waitForTerminationRequest();
			tm.cancelAll();
			tm.joinAll();
		}
		return Application::EXIT_OK;
	}
	
private:
	bool _helpRequested;
};


POCO_SERVER_MAIN(SampleServer)
