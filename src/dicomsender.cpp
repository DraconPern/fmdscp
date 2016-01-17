#include <winsock2.h>
#include "dicomsender.h"
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
#include "dcmtk/dcmdata/dctk.h"
#include "dcmtk/dcmnet/dimse.h"
#include "dcmtk/dcmnet/diutil.h"

// check DCMTK functionality
#if !defined(WIDE_CHAR_FILE_IO_FUNCTIONS) && defined(_WIN32)
#error "DCMTK and this program must be compiled with DCMTK_WIDE_CHAR_FILE_IO_FUNCTIONS"
#endif

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

class DICOMSenderImpl
{	

public:
	DICOMSenderImpl(void);
	~DICOMSenderImpl(void);

	void Initialize(int outgoingsessionid,
		std::string destinationHost, unsigned int destinationPort, std::string destinationAETitle, std::string ourAETitle);	
	
	void SetFileList(const naturalset &files);

	static void DoSendThread(void *obj);

	std::string ReadLog();

	void Cancel();
	bool IsDone();
	
	// list of dicom images to send, all images should be in the same patient
	typedef std::set<boost::filesystem::path, doj::alphanum_less<boost::filesystem::path> > naturalset;	
	naturalset filestosend;
	
	int totalfiles;
	std::string m_destinationHost;
	unsigned int m_destinationPort;
	std::string m_destinationAETitle;
	std::string m_ourAETitle;

protected:
	
	void DoSend();	

	int SendABatch();

	bool IsCanceled();
	void SetDone(bool state);

	boost::mutex mutex;
	bool cancelEvent, doneEvent;

	class GUILog
	{
	public:
		void Write(const char *msg);
		void Write(const OFCondition &cond);
		void Write(std::stringstream &stream);
		std::string Read();	

		void SetStatus(std::string msg);
		int m_outgoingsessionid;
	};

	GUILog log;

	OFCondition storeSCU(T_ASC_Association * assoc, const boost::filesystem::path &fname);	
	void replacePatientInfoInformation(DcmDataset* dataset);
	// void replaceSOPInstanceInformation(DcmDataset* dataset);
	OFCondition addStoragePresentationContexts(T_ASC_Parameters *params, OFList<OFString>& sopClasses);	
	OFString makeUID(OFString basePrefix, int counter);
	bool updateStringAttributeValue(DcmItem* dataset, const DcmTagKey& key, OFString value);

	// scan files to build sopClassUID
	void scanFiles();
	bool scanFile(boost::filesystem::path currentFilename);
		
	OFList<OFString> sopClassUIDList;    // the list of sop classes

	OFCmdUnsignedInt opt_maxReceivePDULength;
	OFCmdUnsignedInt opt_maxSendPDULength;
	E_TransferSyntax opt_networkTransferSyntax;

	bool opt_combineProposedTransferSyntaxes;
	
	T_DIMSE_BlockingMode opt_blockMode;
	int opt_dimse_timeout;	
	int opt_timeout;

	OFCmdUnsignedInt opt_compressionLevel;		

	static void progressCallback(void * callbackData, T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req);
};

DICOMSenderImpl::DICOMSenderImpl()
{			
	opt_maxSendPDULength = 0;
	opt_networkTransferSyntax = EXS_Unknown;
	// opt_networkTransferSyntax = EXS_JPEG2000LosslessOnly;

	
	opt_combineProposedTransferSyntaxes = true;

	opt_timeout = 10;

	opt_compressionLevel = 0;	
}

DICOMSenderImpl::~DICOMSenderImpl()
{
}

void DICOMSenderImpl::Initialize(int outgoingsessionid,
		std::string destinationHost, unsigned int destinationPort, std::string destinationAETitle, std::string ourAETitle)
{
	cancelEvent = doneEvent = false;
	
	this->log.m_outgoingsessionid = outgoingsessionid;
	this->m_destinationHost = destinationHost;
	this->m_destinationPort = destinationPort;
	this->m_destinationAETitle = destinationAETitle;
	this->m_ourAETitle = ourAETitle;
}

void DICOMSenderImpl::SetFileList(const naturalset &files)
{
	filestosend = files;
	totalfiles = filestosend.size();
}

void DICOMSenderImpl::DoSendThread(void *obj)
{
	DICOMSenderImpl *me = (DICOMSenderImpl *) obj;
	if (me)
	{
		me->SetDone(false);
		me->DoSend();
		me->SetDone(true);
	}
}

std::string DICOMSenderImpl::ReadLog()
{
	return log.Read();
}

void DICOMSenderImpl::GUILog::Write(const char *msg)
{
	DCMNET_INFO(msg);
}

void DICOMSenderImpl::GUILog::Write(const OFCondition &cond)
{
	OFString dumpmsg; DimseCondition::dump(dumpmsg, cond);
	DCMNET_INFO(dumpmsg);
}

void DICOMSenderImpl::GUILog::Write(std::stringstream &stream)
{	
	Write(stream.str().c_str());
	stream.str(std::string());
}

std::string DICOMSenderImpl::GUILog::Read()
{	
	return "";
}

void DICOMSenderImpl::GUILog::SetStatus(std::string msg)
{
	Poco::Data::Session dbconnection(config::getConnectionString());
	dbconnection << "UPDATE outgoing_sessions SET status = ?, updated_at = NOW() WHERE id = ?", use(msg), use(m_outgoingsessionid), now;
}

void DICOMSenderImpl::DoSend()
{	
	std::stringstream msg;
	msg << "Sending to " << m_destinationHost << ":" << m_destinationPort <<
				" destinationAE = " << m_destinationAETitle << " sourceAE = " << m_ourAETitle;
	log.Write(msg);
	
	log.SetStatus("Starting...");
	log.Write("Loading files...");

	// Scan the files for info to used later	
	scanFiles();

	int retry = 0;
	int unsentcountbefore = 0;
	int unsentcountafter = 0;
	do
	{
		// get number of unsent images
		unsentcountbefore = filestosend.size();

		// batch send
		if (unsentcountbefore > 0)
			SendABatch();		

		unsentcountafter = filestosend.size();
		
		// only do a sleep if there's more to send, we didn't send anything out, and we still want to retry
		if (unsentcountafter > 0 && unsentcountbefore == unsentcountafter && retry < 10000)
		{
			retry++;			
			log.Write("Waiting 5 mins before retry");

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
int DICOMSenderImpl::SendABatch()
{
	T_ASC_Network *net = NULL;
	T_ASC_Parameters *params = NULL;
	T_ASC_Association *assoc = NULL;

	try
	{
		// init network
		int acse_timeout = 20;
		OFCondition cond = ASC_initializeNetwork(NET_REQUESTOR, 0, acse_timeout, &net);
		if (cond.bad())
		{		
			log.Write(cond);
			throw std::runtime_error("ASC_initializeNetwork");
		}

		cond = ASC_createAssociationParameters(&params, ASC_DEFAULTMAXPDU);
		if (cond.bad())
		{		
			log.Write(cond);
			throw std::runtime_error("ASC_createAssociationParameters");
		}

		ASC_setAPTitles(params, m_ourAETitle.c_str(), m_destinationAETitle.c_str(), NULL);	

		DIC_NODENAME localHost;
		gethostname(localHost, sizeof(localHost) - 1);
		std::stringstream peerHost;
		peerHost << m_destinationHost << ":" << m_destinationPort;
		ASC_setPresentationAddresses(params, localHost, peerHost.str().c_str());

		// add presentation contexts		
		cond = addStoragePresentationContexts(params, sopClassUIDList);
		if (cond.bad())
		{
			log.Write(cond);
			throw std::runtime_error("addStoragePresentationContexts");
		}

		log.Write("Requesting Association");

		cond = ASC_requestAssociation(net, params, &assoc);
		if (cond.bad())
		{
			if (cond == DUL_ASSOCIATIONREJECTED)
			{				
				T_ASC_RejectParameters rej;
				ASC_getRejectParameters(params, &rej);
				std::stringstream msg;
				msg << "Association Rejected:\n";
				ASC_printRejectParameters(msg, &rej);
				log.Write(msg);
				throw std::runtime_error("ASC_requestAssociation");
			}
			else
			{
				log.Write("Association Request Failed:");			
				log.Write(cond);
				throw std::runtime_error("ASC_requestAssociation");
			}
		}

		// display the presentation contexts which have been accepted/refused	
		std::stringstream msg;
		msg << "Association Parameters Negotiated:\n";
		ASC_dumpParameters(params, msg);
		log.Write(msg);

		/* count the presentation contexts which have been accepted by the SCP */
		/* If there are none, finish the execution */
		if (ASC_countAcceptedPresentationContexts(params) == 0)
		{
			log.Write("No Acceptable Presentation Contexts");
			throw new std::runtime_error("ASC_countAcceptedPresentationContexts");
		}

		/* do the real work, i.e. for all files which were specified in the */
		/* command line, transmit the encapsulated DICOM objects to the SCP. */
		cond = EC_Normal;

		naturalset::iterator itr = filestosend.begin();
		while(itr != filestosend.end())
		{			
			if (IsCanceled())
				break;
			cond = storeSCU(assoc, *itr);

			if(cond.bad())
			{
				// skip this if it's bad data or invalid presentation
				if(cond == DIMSE_BADDATA || cond == DIMSE_NOVALIDPRESENTATIONCONTEXTID)
				{
					log.Write("Skipping file ");
#ifdef _WIN32
					// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
					log.Write((*itr).string(std::codecvt_utf8<boost::filesystem::path::value_type>()).c_str());
#else
					log.Write((*itr).c_str());
#endif					
					log.Write(" due to bad data or no valid presentation context id\n");
					filestosend.erase(itr++);
				}
				else
				{
					// disconnection or.. ?
					break;
				}
			}
			else
			{
				// delete and go to next
				filestosend.erase(itr++);
			}

			std::stringstream status;
			status << (totalfiles - filestosend.size()) << " of " << totalfiles << " completed";
			log.SetStatus(status.str());
		}			

		// tear down association, i.e. terminate network connection to SCP 
		if (cond == EC_Normal)
		{
			log.Write("Releasing Association\n");

			cond = ASC_releaseAssociation(assoc);
			if (cond.bad())
			{			
				log.Write("Association Release Failed:\n");
				log.Write(cond);			
			}
		}
		else if (cond == DUL_PEERREQUESTEDRELEASE)
		{
			log.Write("Protocol Error: peer requested release (Aborting)\n");		
			ASC_abortAssociation(assoc);		
		}
		else if (cond == DUL_PEERABORTEDASSOCIATION)
		{
			log.Write("Peer Aborted Association\n");		
		}
		else
		{
			log.Write("SCU Failed:\n");
			log.Write(cond);		
			log.Write("Aborting Association\n");

			ASC_abortAssociation(assoc);		
		}
	}
	catch(...)
	{

	}

	if(assoc)
		ASC_destroyAssociation(&assoc);	
	
	if(net)
		ASC_dropNetwork(&net);


	return 0;
}


static bool
	isaListMember(OFList<OFString>& lst, OFString& s)
{	
	bool found = false;

	for(OFListIterator(OFString) itr = lst.begin(); itr != lst.end() && !found; itr++)
	{
		found = (s == *itr);	
	}

	return found;
}

static OFCondition
	addPresentationContext(T_ASC_Parameters *params,
	int presentationContextId, const OFString& abstractSyntax,
	const OFString& transferSyntax,
	T_ASC_SC_ROLE proposedRole = ASC_SC_ROLE_DEFAULT)
{
	const char* c_p = transferSyntax.c_str();
	OFCondition cond = ASC_addPresentationContext(params, presentationContextId,
		abstractSyntax.c_str(), &c_p, 1, proposedRole);
	return cond;
}

static OFCondition
	addPresentationContext(T_ASC_Parameters *params,
	int presentationContextId, const OFString& abstractSyntax,
	const OFList<OFString>& transferSyntaxList,
	T_ASC_SC_ROLE proposedRole = ASC_SC_ROLE_DEFAULT)
{
	// create an array of supported/possible transfer syntaxes
	const char** transferSyntaxes = new const char*[transferSyntaxList.size()];
	int transferSyntaxCount = 0;
	OFListConstIterator(OFString) s_cur = transferSyntaxList.begin();
	OFListConstIterator(OFString) s_end = transferSyntaxList.end();
	while (s_cur != s_end)
	{
		transferSyntaxes[transferSyntaxCount++] = (*s_cur).c_str();
		++s_cur;
	}

	OFCondition cond = ASC_addPresentationContext(params, presentationContextId,
		abstractSyntax.c_str(), transferSyntaxes, transferSyntaxCount, proposedRole);

	delete[] transferSyntaxes;
	return cond;
}

OFCondition DICOMSenderImpl::addStoragePresentationContexts(T_ASC_Parameters *params, OFList<OFString>& sopClasses)
{
	OFList<OFString> preferredTransferSyntaxes;
	preferredTransferSyntaxes.push_back(UID_JPEGLSLosslessTransferSyntax);	
	preferredTransferSyntaxes.push_back(UID_JPEG2000LosslessOnlyTransferSyntax);
	preferredTransferSyntaxes.push_back(UID_JPEGProcess14SV1TransferSyntax);
		
	OFList<OFString> fallbackSyntaxes;	
	fallbackSyntaxes.push_back(UID_LittleEndianExplicitTransferSyntax);	
	fallbackSyntaxes.push_back(UID_LittleEndianImplicitTransferSyntax);		
	
	OFListIterator(OFString) s_cur, s_end;

	// thin out the sop classes to remove any duplicates.
	OFList<OFString> sops;
	s_cur = sopClasses.begin();
	s_end = sopClasses.end();
	while (s_cur != s_end)
	{
		if (!isaListMember(sops, *s_cur))
		{
			sops.push_back(*s_cur);
		}
		++s_cur;
	}
		
	// add a presentations context for each sop class x transfer syntax pair
	OFCondition cond = EC_Normal;
	int pid = 1; // presentation context id
	s_cur = sops.begin();
	s_end = sops.end();
	while (s_cur != s_end && cond.good())
	{
		if (pid > 255)
		{
			DCMNET_ERROR("Too many presentation contexts");
			return ASC_BADPRESENTATIONCONTEXTID;
		}

		// created a list of transfer syntaxes combined from the preferred and fallback syntaxes
		OFList<OFString> combinedSyntaxes(fallbackSyntaxes);
		OFListIterator(OFString) s_cur2 = preferredTransferSyntaxes.begin();
		OFListIterator(OFString) s_end2 = preferredTransferSyntaxes.end();		
		while (s_cur2 != s_end2)
		{
			combinedSyntaxes.push_front(*s_cur2);			
			++s_cur2;
		}

		cond = addPresentationContext(params, pid, *s_cur, combinedSyntaxes);
		pid += 2;   /* only odd presentation context id's */
		
		++s_cur;
	}

	return cond;
}

bool DICOMSenderImpl::updateStringAttributeValue(DcmItem* dataset, const DcmTagKey& key, OFString value)
{
	DcmStack stack;
	DcmTag tag(key);

	OFCondition cond = EC_Normal;
	cond = dataset->search(key, stack, ESM_fromHere, false);
	if (cond != EC_Normal)
	{
		std::stringstream msg;
		msg << "Error: updateStringAttributeValue: cannot find: " << tag.getTagName() << " " << key << ": " << cond.text() << std::endl;
		log.Write(msg);
		return false;
	}

	DcmElement* elem = (DcmElement*) stack.top();

	DcmVR vr(elem->ident());
	if (elem->getLength() > vr.getMaxValueLength())
	{
		std::stringstream msg;
		msg << "error: updateStringAttributeValue: " << tag.getTagName()
			<< " " << key << ": value too large (max "
			<< vr.getMaxValueLength() << ") for " << vr.getVRName() << " value: " << value << std::endl;
		log.Write(msg);
		return false;
	}

	cond = elem->putOFStringArray(value);
	if (cond != EC_Normal)
	{
		std::stringstream msg;
		msg << "error: updateStringAttributeValue: cannot put string in attribute: " << tag.getTagName()
			<< " " << key << ": " << cond.text() << std::endl;
		log.Write(msg);
		return false;
	}

	return true;
}

void DICOMSenderImpl::progressCallback(void * callbackData, T_DIMSE_StoreProgress *progress, T_DIMSE_C_StoreRQ * req)
{
	DICOMSenderImpl *sender = (DICOMSenderImpl *)callbackData;
	/*
	switch (progress->state)
	{
	case DIMSE_StoreBegin:
		sender->log.Write("XMIT:");
		break;
	case DIMSE_StoreEnd:
		sender->log.Write("|\n");
		break;
	default:
		sender->log.Write(".");
		break;
	}*/
}

OFCondition DICOMSenderImpl::storeSCU(T_ASC_Association * assoc, const boost::filesystem::path &fname)
{
	DIC_US msgId = assoc->nextMsgID++;
	T_DIMSE_C_StoreRQ req;
	T_DIMSE_C_StoreRSP rsp;
	DIC_UI sopClass;
	DIC_UI sopInstance;
	DcmDataset *statusDetail = NULL;	

	std::stringstream msg;
#ifdef _WIN32
	// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
	msg << "Sending file: " << fname.string(std::codecvt_utf8<boost::filesystem::path::value_type>());
#else
	msg << "Sending file: " << fname.string();
#endif
	log.Write(msg);

	DcmFileFormat dcmff;
	OFCondition cond = dcmff.loadFile(fname.c_str());

	if (cond.bad())
	{		
		log.Write(cond);
		return cond;
	}	

	/* figure out which SOP class and SOP instance is encapsulated in the file */
	if (!DU_findSOPClassAndInstanceInDataSet(dcmff.getDataset(), sopClass, sopInstance, false))
	{		
		log.Write("No SOP Class & Instance UIDs\n");
		return DIMSE_BADDATA;
	}

	DcmXfer filexfer(dcmff.getDataset()->getOriginalXfer());

	/* special case: if the file uses an unencapsulated transfer syntax (uncompressed
	* or deflated explicit VR) and we prefer deflated explicit VR, then try
	* to find a presentation context for deflated explicit VR first.
	*/
	if (filexfer.isNotEncapsulated() && opt_networkTransferSyntax == EXS_DeflatedLittleEndianExplicit)
	{
		filexfer = EXS_DeflatedLittleEndianExplicit;
	}

	T_ASC_PresentationContextID presId;
	if (filexfer.getXfer() != EXS_Unknown)
		presId = ASC_findAcceptedPresentationContextID(assoc, sopClass, filexfer.getXferID());
	else 
		presId = ASC_findAcceptedPresentationContextID(assoc, sopClass);

	if (presId == 0)
	{
		const char *modalityName = dcmSOPClassUIDToModality(sopClass);
		if (!modalityName)
			modalityName = dcmFindNameOfUID(sopClass);
		if (!modalityName)
			modalityName = "unknown SOP class";

		std::stringstream msg;
		msg << "No presentation context for: " << sopClass << " = " << modalityName << std::endl;
		log.Write(msg);
		return DIMSE_NOVALIDPRESENTATIONCONTEXTID;
	}

	DcmXfer fileTransfer(dcmff.getDataset()->getOriginalXfer());
	T_ASC_PresentationContext pc;
	ASC_findAcceptedPresentationContext(assoc->params, presId, &pc);
	DcmXfer netTransfer(pc.acceptedTransferSyntax);

	msg << "Transfer: " << dcmFindNameOfUID(fileTransfer.getXferID()) << " -> " << dcmFindNameOfUID(netTransfer.getXferID());
	log.Write(msg);

	if(fileTransfer.getXferID() != netTransfer.getXferID())
	{
		if(dcmff.getDataset()->chooseRepresentation(netTransfer.getXfer(), NULL) != EC_Normal)
		{
			log.Write("Unable to choose Representation\n");
		}
	}

	/* prepare the transmission of data */
	bzero((char*)&req, sizeof(req));
	req.MessageID = msgId;
	strcpy(req.AffectedSOPClassUID, sopClass);
	strcpy(req.AffectedSOPInstanceUID, sopInstance);
	req.DataSetType = DIMSE_DATASET_PRESENT;
	req.Priority = DIMSE_PRIORITY_LOW;	

	// send it!
	cond = DIMSE_storeUser(assoc, presId, &req,
		NULL, dcmff.getDataset(), progressCallback, this,
		DIMSE_NONBLOCKING, 10,
		&rsp, &statusDetail, NULL, boost::filesystem::file_size(fname));	
		
	if (cond.bad())
	{		
		log.Write("Store Failed\n");
		log.Write(cond);		
	}

	if (statusDetail != NULL)
	{
		msg << "Status Detail:\n";
		statusDetail->print(msg);
		delete statusDetail;
		log.Write(msg);
	}

	
	return cond;
}

bool DICOMSenderImpl::scanFile(boost::filesystem::path currentFilename)
{	

	DcmFileFormat dfile;
	OFCondition cond = dfile.loadFile(currentFilename.c_str());
	if (cond.bad())
	{
		std::stringstream msg;
		msg << "cannot access file, ignoring: " << currentFilename << std::endl;
		log.Write(msg);
		return true;
	}

	char sopClassUID[128];
	char sopInstanceUID[128];

	if (!DU_findSOPClassAndInstanceInDataSet(dfile.getDataset(), sopClassUID, sopInstanceUID))
	{	
		std::stringstream msg;
		msg << "missing SOP class (or instance) in file, ignoring: " << currentFilename << std::endl;
		log.Write(msg);
		return false;
	}

	if (!dcmIsaStorageSOPClassUID(sopClassUID))
	{		
		std::stringstream msg;
		msg << "unknown storage sop class in file, ignoring: " << currentFilename << " : " << sopClassUID << std::endl;
		log.Write(msg);
		return false;
	}

	sopClassUIDList.push_back(sopClassUID);	

	return true;
}

void DICOMSenderImpl::scanFiles()
{	
	for(naturalset::iterator itr = filestosend.begin(); itr != filestosend.end(); itr++)
		scanFile(*itr);	
}

void DICOMSenderImpl::Cancel()
{
	boost::mutex::scoped_lock lk(mutex);
	cancelEvent = true;
}

bool DICOMSenderImpl::IsDone()
{
	boost::mutex::scoped_lock lk(mutex);
	return doneEvent;
}

bool DICOMSenderImpl::IsCanceled()
{
	boost::mutex::scoped_lock lk(mutex);
	return cancelEvent;
}

void DICOMSenderImpl::SetDone(bool state)
{
	boost::mutex::scoped_lock lk(mutex);
	doneEvent = state;
}

DICOMSender::DICOMSender(void)
{
	impl = new DICOMSenderImpl;
}

DICOMSender::~DICOMSender(void)
{
	delete impl;
}

void DICOMSender::Initialize(int outgoingsessionid,
							 std::string destinationHost, unsigned int destinationPort, std::string destinationAETitle, std::string ourAETitle)
{
	impl->Initialize(outgoingsessionid,
		destinationHost, destinationPort, destinationAETitle, ourAETitle);
}

void DICOMSender::SetFileList(const naturalset &files)
{
	impl->SetFileList(files);
}

void DICOMSender::DoSendThread(void *obj)
{
	DICOMSenderImpl::DoSendThread(((DICOMSender *) obj)->impl);
}

std::string DICOMSender::ReadLog()
{
	return impl->ReadLog();
}

void DICOMSender::Cancel()
{
	impl->Cancel();
}

bool DICOMSender::IsDone()
{
	return impl->IsDone();
}
