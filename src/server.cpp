#include "server.h"
#include "config.h"

#include "soci/mysql/soci-mysql.h"
#include <boost/asio/io_service.hpp>
#include "ndcappender.h"

server::server()
{
	// configure logging
	dcmtk::log4cplus::SharedAppenderPtr logfile(new NDCAsFilenameAppender("C:\\PACS\\Log"));
	dcmtk::log4cplus::Logger my_log = dcmtk::log4cplus::Logger::getRoot();
	my_log.addAppender(logfile);

	// do server wide init
	soci::register_factory_mysql();

	Config::registerCodecs();

	Config::createDBPool();

	std::string errormsg;
	if(!Config::test(errormsg))
	{
		/*
		app.logger().error(errormsg);
		app.logger().information("Exiting");
		*/
		throw new std::exception(errormsg.c_str());
	}

	// add scp
	init_scp();
	io_service_.post(boost::bind(&DcmSCPPool<MySCP>::listen, &storageSCP));	

	// add sender


	
}

server::~server()
{
	Config::deregisterCodecs();
}

void server::init_scp()
{
	// set listening socket option
	storageSCP.getConfig().setConnectionBlockingMode(DUL_NOBLOCK);
	storageSCP.getConfig().setConnectionTimeout(1);
	storageSCP.getConfig().setDIMSEBlockingMode(DIMSE_NONBLOCKING);
	storageSCP.getConfig().setDIMSETimeout(1);
	storageSCP.getConfig().setACSETimeout(1);
	
	storageSCP.getConfig().setHostLookupEnabled(true);

	// DICOM standard transfer syntaxes
	const char* transferSyntaxes[] = {
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

	OFList<OFString> syntaxes;
	for(int i = 0; i < DIM_OF(transferSyntaxes); i++)
		syntaxes.push_back(transferSyntaxes[i]);		

	storageSCP.getConfig().addPresentationContext(UID_VerificationSOPClass, syntaxes);

	for(int i = 0; i < numberOfAllDcmStorageSOPClassUIDs; i++)
		storageSCP.getConfig().addPresentationContext(dcmAllStorageSOPClassUIDs[i], syntaxes);

	
	
	
	/*
	std::string errormsg;
	if(!StoreHandler::Test(errormsg))
		{
			app.logger().error(errormsg);
			app.logger().information("Exiting");
			ServerApplication::terminate();
			return;
		}		

		app.logger().information("Set up done.  Listening.");-*/
}

void server::run_async()
{	
	// Create a pool of threads to run all of the tasks assigned to io_services.
	for (std::size_t i = 0; i < 3; ++i)
	{
		boost::shared_ptr<boost::thread> thread(new boost::thread(
			boost::bind(&boost::asio::io_service::run, &io_service_)));
		threads.push_back(thread);
	}

	
}

void server::join()
{
	// Wait for all threads in the pool to exit.
	for (std::size_t i = 0; i < threads.size(); ++i)
		threads[i]->join();
}

void server::stop()
{
	stop(true);

	// also tell ioservice to stop servicing
	io_service_.stop();

	// also tell scp to stop
	storageSCP.stopAfterCurrentAssociations();
}

void server::stop(bool flag)
{
	boost::mutex::scoped_lock lk(event_mutex);
	stopEvent = flag;
}

bool server::shouldStop()
{
	boost::mutex::scoped_lock lk(event_mutex);
	return stopEvent;
}
