#include "Poco/Util/WinRegistryConfiguration.h"
#include "Poco/Path.h"

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "myscp.h"       /* for base class DcmSCP */
#include "dcmtk/dcmnet/diutil.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

using Poco::Util::WinRegistryConfiguration;
using Poco::AutoPtr;
using Poco::Path;

MySCP::MySCP()
	: DcmThreadSCP()    
{

}

MySCP::~MySCP()
{

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
	AutoPtr<WinRegistryConfiguration> pConf(new WinRegistryConfiguration("HKEY_LOCAL_MACHINE\\SOFTWARE\\FrontMotion\\fmdscp"));

	std::string tempPath = pConf->getString("TempPath", "%SystemRoot%\\TEMP");
	Path filepath = Path::expand(tempPath);
	filepath.append("fmdscp");
	filepath.append(dcmSOPClassUIDToModality(reqMessage.AffectedSOPClassUID) + std::string("-") + reqMessage.AffectedSOPInstanceUID + ".dcm");

	OFString filename = filepath.toString().c_str();
	// generate filename with full path (and create subdirectories if needed)
	status = EC_Normal;
	if (status.good())
	{
		if (OFStandard::fileExists(filename))
			DCMNET_WARN("file already exists, overwriting: " << filename);
		// receive dataset directly to file
		status = receiveSTORERequest(reqMessage, presID, filename);
		if (status.good())
		{
			// call the notification handler (default implementation outputs to the logger)
			// notifyInstanceStored(filename, reqMessage.AffectedSOPClassUID, reqMessage.AffectedSOPInstanceUID);
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

	return status;
}

OFCondition MySCP::handleMOVERequest(T_DIMSE_C_MoveRQ &reqMessage,
									  const T_ASC_PresentationContextID presID)
{
	OFCondition status = EC_IllegalParameter;

	return status;
}