#include <winsock2.h>
#include "sender.h"
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <codecvt>
#include "config.h"
#include "poco/Data/Session.h"
using namespace Poco::Data::Keywords;

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/ofstd/ofstd.h"
#include "dcmtk/oflog/oflog.h"
#include "dcmtk/dcmnet/scu.h"
#include "dcmtk/dcmnet/dimse.h"
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/oflog/ndc.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif


Sender::Sender(boost::uuids::uuid uuid)
{			
	this->uuid = uuid;

	cancelEvent = doneEvent = false;
}

Sender::~Sender()
{
}

void Sender::Initialize(Destination &destination)
{		
	this->destination = destination;	
}

void Sender::SetFileList(const naturalpathmap &instances)
{
	this->instances = instances;
	totalfiles = instances.size();
}

void Sender::SetStatus(std::string msg)
{
	Poco::Data::Session dbconnection(config::getConnectionString());
	dbconnection << "UPDATE outgoing_sessions SET status = ?, updated_at = NOW() WHERE uuid = ?", use(msg), boost::uuids::to_string(uuid), now;
}

void Sender::DoSendAsync()
{
	cancelEvent = doneEvent = false;

	boost::thread t(Sender::DoSendThread, this);
	t.detach();
}

void Sender::DoSendThread(void *obj)
{
	Sender *me = (Sender *)obj;
	dcmtk::log4cplus::NDCContextCreator ndc(boost::uuids::to_string(me->uuid).c_str());
	me->SetDone(false);
	me->DoSend();
	me->SetDone(true);
}

void Sender::DoSend()
{	
	std::stringstream msg;
	msg << "Sending to " << destination.destinationhost << ":" << destination.destinationport <<
		" destinationAE = " << destination.destinationAE << " sourceAE = " << destination.sourceAE;
	DCMNET_INFO(msg.str());
	
	DCMNET_INFO("Starting...");
	DCMNET_INFO("Loading files...");

	// Scan the files for info to be used later
	for (naturalpathmap::iterator itr = instances.begin(); itr != instances.end(); itr++)
		scanFile(itr->second);

	int retry = 0;
	int unsentcountbefore = 0;
	int unsentcountafter = 0;
	do
	{
		// get number of unsent images
		unsentcountbefore = instances.size();

		// batch send
		if (unsentcountbefore > 0)
			SendABatch();		

		unsentcountafter = instances.size();
		
		// only do a sleep if there's more to send, we didn't send anything out, and we still want to retry
		if (unsentcountafter > 0 && unsentcountbefore == unsentcountafter && retry < 10000)
		{
			retry++;			
			DCMNET_INFO("Waiting 5 mins before retry");

			// sleep loop with cancel check, 5 minutes
			int sleeploop = 5 * 60 * 5;
			while (sleeploop > 0)
			{
#ifdef _WIN32
				Sleep(200);
#else
                usleep(200);
#endif
                sleeploop--;
				if (IsCanceled())
					break;
			}
		}
		else		// otherwise, the next loop is not a retry
		{
			retry = 0;
		}
	}
	while (!IsCanceled() && unsentcountafter > 0 && retry < 10000);	 
}

// 
int Sender::SendABatch()
{
	DcmSCU scu;

	scu.setVerbosePCMode(true);
	scu.setAETitle(destination.sourceAE.c_str());
	scu.setPeerHostName(destination.destinationhost.c_str());
	scu.setPeerPort(destination.destinationport);
	scu.setPeerAETitle(destination.destinationAE.c_str());
	scu.setACSETimeout(30);
	scu.setDIMSETimeout(60);
	scu.setDatasetConversionMode(true);

	OFList<OFString> defaulttransfersyntax;
	defaulttransfersyntax.push_back(UID_LittleEndianExplicitTransferSyntax);

	// for every class..
	for (mapset::iterator it = sopclassuidtransfersyntax.begin(); it != sopclassuidtransfersyntax.end(); it++)
	{
		// make list of what's in the file, and propose it first.  default proposed as a seperate context
		OFList<OFString> transfersyntax;
		for (std::set<std::string>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			if (*it2 != UID_LittleEndianExplicitTransferSyntax)
				transfersyntax.push_back(it2->c_str());
		}

		if (transfersyntax.size() > 0)
			scu.addPresentationContext(it->first.c_str(), transfersyntax);

		// propose the default UID_LittleEndianExplicitTransferSyntax
		scu.addPresentationContext(it->first.c_str(), defaulttransfersyntax);
	}

	OFCondition cond;

	if (scu.initNetwork().bad())
		return 1;

	if (scu.negotiateAssociation().bad())
		return 1;

	naturalpathmap::iterator itr = instances.begin();
	while (itr != instances.end())
	{
		if (IsCanceled())
		{
			DCMNET_INFO("Send canceled\n");
			break;
		}

		Uint16 status;

		std::stringstream msg;
		msg << "Sending file: " << itr->second << "\n";
		DCMNET_INFO(msg.str());
		

		// load file
		DcmFileFormat dcmff;
		dcmff.loadFile(itr->second.c_str());		

		// do some precheck of the transfer syntax
		DcmXfer fileTransfer(dcmff.getDataset()->getOriginalXfer());
		OFString sopclassuid;
		dcmff.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid);

		msg << "File encoding: " << fileTransfer.getXferName() << "\n";
		DCMNET_INFO(msg.str());

		// out found.. change to 
		T_ASC_PresentationContextID pid = scu.findAnyPresentationContextID(sopclassuid, fileTransfer.getXferID());

		cond = scu.sendSTORERequest(pid, "", dcmff.getDataset(), status);
		if (cond.good())
			instances.erase(itr++);
		else if (cond == DUL_PEERABORTEDASSOCIATION)
			return 1;
		else			// some error? keep going
		{
			itr++;
		}
		

		DCMNET_INFO("\n");
	}

	scu.releaseAssociation();
	return 0;
}

bool Sender::scanFile(boost::filesystem::path currentFilename)
{	

	DcmFileFormat dfile;
	if(dfile.loadFile(currentFilename.c_str()).bad())
	{
		std::stringstream msg;
		msg << "cannot access file, ignoring: " << currentFilename << std::endl;
		DCMNET_INFO(msg.str());
		return true;
	}

	DcmXfer filexfer(dfile.getDataset()->getOriginalXfer());
	OFString transfersyntax = filexfer.getXferID();

	OFString sopclassuid;
	if(dfile.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid).bad())
	{	
		std::stringstream msg;
		msg << "missing SOP class in file, ignoring: " << currentFilename << std::endl;
		DCMNET_INFO(msg.str());
		return false;
	}
	
	sopclassuidtransfersyntax[sopclassuid.c_str()].insert(transfersyntax.c_str());
	
	return true;
}

void Sender::Cancel()
{
	boost::mutex::scoped_lock lk(mutex);
	cancelEvent = true;
}

bool Sender::IsDone()
{
	boost::mutex::scoped_lock lk(mutex);
	return doneEvent;
}

bool Sender::IsCanceled()
{
	boost::mutex::scoped_lock lk(mutex);
	return cancelEvent;
}

void Sender::SetDone(bool state)
{
	boost::mutex::scoped_lock lk(mutex);
	doneEvent = state;
}

