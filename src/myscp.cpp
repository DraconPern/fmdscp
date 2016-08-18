#include <boost/filesystem.hpp>
#include <codecvt>
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

#include "myscp.h"
#include "config.h"
#include "store.h"
#include "find.h"
#include "move.h"

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/diutil.h"
#include "dcmtk/oflog/ndc.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif


MySCP::MySCP()
	: DcmThreadSCP()
{
	// do per association initialization	
}

MySCP::~MySCP()
{
	dcmtk::log4cplus::threadCleanup();
}

void MySCP::setUUID(boost::uuids::uuid uuid)
{
	uuid_ = uuid;
}

OFCondition MySCP::run(T_ASC_Association* incomingAssoc)
{
	// save a copy for DIMSE_ because SCP's m_assoc is private :(   also note DcmThreadSCP is friend class of SCP, and that's how m_assoc is set
	assoc_ = incomingAssoc;
	return DcmThreadSCP::run(incomingAssoc);	
}

OFCondition MySCP::negotiateAssociation()
{
	OFCondition result = EC_Normal;
	// Check whether there is something to negotiate...
	if (assoc_ == NULL)
		return DIMSE_ILLEGALASSOCIATION;

	T_ASC_SC_ROLE acceptedRole = ASC_SC_ROLE_DEFAULT;

	int numContexts = ASC_countPresentationContexts(assoc_->params);

	// traverse list of presentation contexts
	for (int i = 0; i < numContexts; ++i)
	{
		// retrieve presentation context
		T_ASC_PresentationContext pc;
		result = ASC_getPresentationContext(assoc_->params, i, &pc);
		if (result.bad()) return result;

		if (IsStorageAbstractSyntax(pc.abstractSyntax) || IsSupportedOperationAbstractSyntax(pc.abstractSyntax))
		{
			// loop through list of transfer syntaxes in presentation context, accept it all
			for (char j = 0; j < pc.transferSyntaxCount; ++j)
			{
				result = ASC_acceptPresentationContext(
					assoc_->params, pc.presentationContextID,
					pc.proposedTransferSyntaxes[j], acceptedRole);

				// SCP/SCU role selection failed, reject presentation context
				if (result == ASC_SCPSCUROLESELECTIONFAILED)
				{
					result = ASC_refusePresentationContext(assoc_->params,
						pc.presentationContextID, ASC_P_NOREASON);
				}

				if (result.bad()) 
					return result;
			}
		}
	}

	return result;
}

bool MySCP::IsStorageAbstractSyntax(DIC_UI abstractsyntax)
{
	for (int i = 0; i < numberOfAllDcmStorageSOPClassUIDs; i++)
	{
		if (strcmp(dcmAllStorageSOPClassUIDs[i], abstractsyntax) == 0)
			return true;
	}
		
	return false;
}

bool MySCP::IsSupportedOperationAbstractSyntax(DIC_UI abstractsyntax)
{
	if (strcmp(UID_VerificationSOPClass, abstractsyntax) == 0)
		return true;

	if (strcmp(UID_FINDStudyRootQueryRetrieveInformationModel, abstractsyntax) == 0)
		return true;

	if (strcmp(UID_MOVEStudyRootQueryRetrieveInformationModel, abstractsyntax) == 0)
		return true;

	return false;
}

OFCondition MySCP::handleIncomingCommand(T_DIMSE_Message *incomingMsg,
										 const DcmPresentationContextInfo &presInfo)
{
	OFCondition status = EC_IllegalParameter;
	if (incomingMsg != NULL)
	{
		// check whether we've received a supported command
		if (incomingMsg->CommandField == DIMSE_C_ECHO_RQ)
		{
			// handle incoming C-ECHO request
			status = handleECHORequest(incomingMsg->msg.CEchoRQ, presInfo.presentationContextID);
		}
		else if (incomingMsg->CommandField == DIMSE_C_STORE_RQ)
		{
			// handle incoming C-STORE request
			status = handleSTORERequest(incomingMsg->msg.CStoreRQ, presInfo.presentationContextID);
		}
		else if (incomingMsg->CommandField == DIMSE_C_FIND_RQ)
		{
			// handle incoming C-FIND request
			status = handleFINDRequest(incomingMsg->msg.CFindRQ, presInfo.presentationContextID);
		}
		else if (incomingMsg->CommandField == DIMSE_C_MOVE_RQ)
		{
			// handle incoming C-MOVE request
			status = handleMOVERequest(incomingMsg->msg.CMoveRQ, presInfo.presentationContextID);
		} else {
			// unsupported command
			OFString tempStr;
			DCMNET_ERROR("cannot handle this kind of DIMSE command (0x"
				<< STD_NAMESPACE hex << STD_NAMESPACE setfill('0') << STD_NAMESPACE setw(4)
				<< OFstatic_cast(unsigned int, incomingMsg->CommandField)
				<< ")");
			DCMNET_DEBUG(DIMSE_dumpMessage(tempStr, *incomingMsg, DIMSE_INCOMING));
			// TODO: provide more information on this error?
			status = DIMSE_BADCOMMANDTYPE;
		}
	}
	return status;
}

OFCondition MySCP::handleSTORERequest(T_DIMSE_C_StoreRQ &reqMessage,
									  const T_ASC_PresentationContextID presID)
{
	OFCondition status = EC_IllegalParameter;
	Uint16 rspStatusCode = STATUS_STORE_Error_CannotUnderstand;

	// get storage location or use temp	
	boost::filesystem::path filename = config::getTempPath();
	filename /= dcmSOPClassUIDToModality(reqMessage.AffectedSOPClassUID) + std::string("-") + reqMessage.AffectedSOPInstanceUID + ".dcm";

	std::stringstream msg;
#ifdef _WIN32
	// on Windows, boost::filesystem::path is a wstring, so we need to convert to utf8
	msg << "Receiving file: " << filename.string(std::codecvt_utf8<boost::filesystem::path::value_type>());
#else
	msg << "Receiving file: " << filename.string();
#endif
	DCMNET_INFO(msg.str());
	
	status = EC_Normal;
	if (status.good())
	{
		if (boost::filesystem::exists(filename))
			DCMNET_WARN("file already exists, overwriting: " << filename);

		// receive dataset directly to file
		std::string p = filename.string(std::codecvt_utf8<boost::filesystem::path::value_type>());
		status = receiveSTORERequest(reqMessage, presID, p.c_str());
		if (status.good())
		{
			// call the notification handler
			StoreHandler storehandler;
			status = storehandler.handleSTORERequest(filename);
			rspStatusCode = STATUS_Success;
		}
	}

	// send C-STORE response (with DIMSE status code)
	if (status.good())
		status = sendSTOREResponse(presID, reqMessage, rspStatusCode);
	else if (status == DIMSE_OUTOFRESOURCES)
	{
		// do not overwrite the previous error status
		sendSTOREResponse(presID, reqMessage, STATUS_STORE_Refused_OutOfResources);
	}

	return status;
}

OFCondition MySCP::handleFINDRequest(T_DIMSE_C_FindRQ &reqMessage,
									 const T_ASC_PresentationContextID presID)
{
	OFCondition status = EC_IllegalParameter;

	FindHandler handler(getAETitle().c_str());
	status = DIMSE_findProvider(assoc_, presID, &reqMessage, FindHandler::FindCallback, &handler, getDIMSEBlockingMode(), getDIMSETimeout());

	return status;
}

OFCondition MySCP::handleMOVERequest(T_DIMSE_C_MoveRQ &reqMessage,
									 const T_ASC_PresentationContextID presID)
{
	OFCondition status = EC_IllegalParameter;

	MoveHandler handler(getAETitle().c_str(), getPeerAETitle().c_str());
	status = DIMSE_moveProvider(assoc_, presID, &reqMessage, MoveHandler::MoveCallback, &handler, getDIMSEBlockingMode(), getDIMSETimeout());

	return status;
}

OFCondition MyDcmSCPPool::MySCPWorker::setAssociation(T_ASC_Association* assoc)
{
	// this call is still in the listening thread, make a log before we pass it
	boost::uuids::uuid uuid = boost::uuids::random_generator()();
	DCMNET_INFO("Request from hostname " << assoc->params->DULparams.callingPresentationAddress);
	OFString info;
	ASC_dumpConnectionParameters(info, assoc);
	DCMNET_INFO(info);
	ASC_dumpParameters(info, assoc->params, ASC_ASSOC_RQ);
	DCMNET_INFO(info);
	DCMNET_INFO("Passing association to worker thread, setting uuid to " << uuid);
	setUUID(uuid);
	return DcmBaseSCPPool::DcmBaseSCPWorker::setAssociation(assoc);
}

OFCondition MyDcmSCPPool::MySCPWorker::workerListen(T_ASC_Association* const assoc)
{
	// use uuid as the ndc, the logger is set up to use <ndc>.txt as the filename
	dcmtk::log4cplus::NDCContextCreator ndc(boost::uuids::to_string(uuid_).c_str());
	DCMNET_INFO("Request from hostname " << assoc->params->DULparams.callingPresentationAddress);
	OFString info;
	ASC_dumpConnectionParameters(info, assoc);
	DCMNET_INFO(info);
	ASC_dumpParameters(info, assoc->params, ASC_ASSOC_RQ);
	DCMNET_INFO(info);
	OFCondition cond = MySCP::run(assoc);
	return cond;
}

MyDcmSCPPool::MyDcmSCPPool() : DcmBaseSCPPool()
{
	setMaxThreads(50);
	getConfig().setConnectionBlockingMode(DUL_NOBLOCK);
	getConfig().setConnectionTimeout(1);
	getConfig().setDIMSEBlockingMode(DIMSE_NONBLOCKING);
	getConfig().setDIMSETimeout(1);
	getConfig().setACSETimeout(1);

	getConfig().setHostLookupEnabled(true);
	getConfig().setAETitle("FMDSCP");	
	
	getConfig().setPort(104);

}

OFCondition MyDcmSCPPool::listen()
{
	DCMNET_INFO("Listening.");
	OFCondition result = DcmBaseSCPPool::listen();
	DCMNET_INFO("Stopped listening.");

	dcmtk::log4cplus::threadCleanup();
	return result;
}