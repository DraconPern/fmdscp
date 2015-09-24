#include <boost/filesystem.hpp>
#include <codecvt>
#include "Poco/Path.h"

using namespace Poco;

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/diutil.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "myscp.h"
#include "config.h"
#include "store.h"
// #include "find.h"

MySCP::MySCP()
	: DcmThreadSCP()
{
	// do per association initialization
}

MySCP::~MySCP()
{

}

void MySCP::setUUID(boost::uuids::uuid uuid)
{
	uuid_ = uuid;
}

OFCondition MySCP::run(T_ASC_Association* incomingAssoc)
{
	// save a copy because SCP's m_assoc is private :(   also note DcmThreadSCP is friend class of SCP, and that's how m_assoc is set
	assoc_ = incomingAssoc;
	return DcmThreadSCP::run(incomingAssoc);	
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
	boost::filesystem::path filename = Config::getTempPath();
	filename /= dcmSOPClassUIDToModality(reqMessage.AffectedSOPClassUID) + std::string("-") + reqMessage.AffectedSOPInstanceUID + ".dcm";

	// generate filename with full path (and create subdirectories if needed)
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
	/*
	FindHandler handler;
	status = DIMSE_findProvider(assoc_, presID, &reqMessage, FindHandler::FindCallback, &handler, getDIMSEBlockingMode(), getDIMSETimeout());
	*/
	if (status.bad()) {
		// DCMNET_ERROR("Find SCP Failed: " << DimseCondition::dump(temp_str, cond));
	}
	return status;
}

OFCondition MySCP::handleMOVERequest(T_DIMSE_C_MoveRQ &reqMessage,
									 const T_ASC_PresentationContextID presID)
{
	OFCondition status = EC_IllegalParameter;

	return status;
}
