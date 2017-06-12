#include "sender.h"
#include <boost/thread/mutex.hpp>
#include <boost/lexical_cast.hpp>
#include <codecvt>
#include "config.h"
#include "Poco/Data/Session.h"
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


Sender::Sender(boost::uuids::uuid uuid, CloudClient &cloudclient, DBPool &dbpool) :
	cloudclient(cloudclient), dbpool(dbpool)
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
	Poco::Data::Session dbconnection(dbpool.get());
	Poco::DateTime rightnow;

	std::string uuidstring = boost::uuids::to_string(uuid);
	
	std::vector<OutgoingSession> out_sessions;
	Poco::Data::Statement outsessionsselect(dbconnection);
	outsessionsselect << "SELECT id,"
		"uuid,"
		"queued,"
		"StudyInstanceUID,"
		"PatientName,"
		"PatientID,"
		"StudyDate,"
		"ModalitiesInStudy,"
		"destination_id,"
		"status,"
		"createdAt,updatedAt"
		" FROM outgoing_sessions WHERE uuid = ?",
		into(out_sessions), use(uuidstring);
	 
	outsessionsselect.execute();

	if (out_sessions.size() == 1)
	{
		out_sessions[0].status = msg;
		out_sessions[0].updated_at = rightnow;

		Poco::Data::Statement update(dbconnection);
		update << "UPDATE outgoing_sessions SET status = ?, updatedAt = ? WHERE uuid = ?", use(msg), use(rightnow), use(out_sessions[0].uuid);
		update.execute();

		cloudclient.send_updateoutsessionitem(out_sessions[0], destination.name);
	}
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
	{
		std::stringstream msg;
		msg << "Sending to " << destination.destinationhost << ":" << destination.destinationport <<
			" destinationAE = " << destination.destinationAE << " sourceAE = " << destination.sourceAE;
		DCMNET_INFO(msg.str());
	}

	SetStatus("starting");
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

			SetStatus("waiting 5 mins before retry. " + boost::lexical_cast<std::string>(totalfiles - instances.size()) + " of " + boost::lexical_cast<std::string>(totalfiles)+" sent");

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

	if (unsentcountafter == 0)
	{
		SetStatus(boost::lexical_cast<std::string>(totalfiles) + " of " + boost::lexical_cast<std::string>(totalfiles)+" sent. Done.");
	}
	else
	{
		SetStatus(boost::lexical_cast<std::string>(totalfiles - instances.size()) + " of " + boost::lexical_cast<std::string>(totalfiles)+" sent. Skipped some. Done.");
	}

	if (IsCanceled())
	{
		SetStatus(boost::lexical_cast<std::string>(totalfiles)+" of " + boost::lexical_cast<std::string>(totalfiles)+" sent. Canceled");
		DCMNET_INFO("Canceled");
	}
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

		{
			std::stringstream msg;
			msg << "Sending file: " << itr->second;
			DCMNET_INFO(msg.str());
		}

		// load file
		DcmFileFormat dcmff;
		dcmff.loadFile(itr->second.c_str());		

		// do some precheck of the transfer syntax
		DcmXfer fileTransfer(dcmff.getDataset()->getOriginalXfer());
		OFString sopclassuid;
		dcmff.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid);

		{
			std::stringstream msg;
			msg << "File encoding: " << fileTransfer.getXferName();
			DCMNET_INFO(msg.str());
		}
		
		// out found.. change to 
		T_ASC_PresentationContextID pid = scu.findAnyPresentationContextID(sopclassuid, fileTransfer.getXferID());
		if (pid == 0)
		{
			DCMNET_INFO("Unable to find any usable presentation context. Skipping.\n");
			itr++;
			continue;
		}

		cond = scu.sendSTORERequest(pid, "", dcmff.getDataset(), status);
		if (cond.good() && (status == 0 || (status & 0xf000) == 0xb000))
			instances.erase(itr++);
		else if ((cond == NET_EC_InvalidSOPClassUID) ||
			(cond == NET_EC_UnknownStorageSOPClass) ||
			(cond == NET_EC_InvalidSOPInstanceUID) ||
			(cond == NET_EC_InvalidTransferSyntaxUID) ||
			(cond == NET_EC_UnknownTransferSyntax) ||
			(cond == NET_EC_NoPresentationContextsDefined) ||
			(cond == NET_EC_NoAcceptablePresentationContexts) ||
			(cond == NET_EC_NoSOPInstancesToSend) ||
			(cond == NET_EC_NoSuchSOPInstance) ||
			(cond == NET_EC_InvalidDatasetPointer) ||
			(cond == NET_EC_AlreadyConnected) ||
			(cond == NET_EC_InsufficientPortPrivileges))
		{
			// keep going for instance related errors
			itr++;
		}
		else
			return 1;
		
		SetStatus(boost::lexical_cast<std::string>(totalfiles - instances.size()) + " of " + boost::lexical_cast<std::string>(totalfiles)+" sent");		
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
		msg << "cannot access file, ignoring: " << currentFilename;
		DCMNET_INFO(msg.str());
		return true;
	}

	DcmXfer filexfer(dfile.getDataset()->getOriginalXfer());
	OFString transfersyntax = filexfer.getXferID();

	OFString sopclassuid;
	if(dfile.getDataset()->findAndGetOFString(DCM_SOPClassUID, sopclassuid).bad())
	{	
		std::stringstream msg;
		msg << "missing SOP class in file, ignoring: " << currentFilename;
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

bool Sender::isUUID(std::string uuid)
{
	std::string uuidstring = boost::uuids::to_string(this->uuid);
	return uuid == uuidstring;
}