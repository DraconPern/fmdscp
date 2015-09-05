/*
*
*  Copyright (C) 2013, OFFIS e.V.
*  All rights reserved.  See COPYRIGHT file for details.
*
*  This software and supporting documentation were developed by
*
*    OFFIS e.V.
*    R&D Division Health
*    Escherweg 2
*    D-26121 Oldenburg, Germany
*
*
*  Module:  dcmnet
*
*  Author:  Michael Onken
*
*  Purpose: Class for implementing a threaded Service Class Provider worker.
*
*/

#ifndef MYSCP_H
#define MYSCP_H

#include "dcmtk/config/osconfig.h"  /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/scpthrd.h"


/** Base class for implementing a DICOM Service Class Provider (SCP) that can serve
*  as a worker in a thread pool, by offering functionality to run an association
*  from an already accepted TCP/IP connection.
*  @warning This class is EXPERIMENTAL. Be careful to use it in production environment.
*/
class MySCP : public DcmThreadSCP
{

public:

	/** Constructor. Initializes internal member variables.
	*/
	MySCP();

	/** Virtual destructor, frees internal memory.
	*/
	virtual ~MySCP();

protected:
	virtual OFCondition handleIncomingCommand(T_DIMSE_Message *incomingMsg,
		const DcmPresentationContextInfo &presInfo);

	OFCondition handleSTORERequest(T_DIMSE_C_StoreRQ &reqMessage, const T_ASC_PresentationContextID presID);
	OFCondition handleFINDRequest(T_DIMSE_C_FindRQ &reqMessage, const T_ASC_PresentationContextID presID);
	OFCondition handleMOVERequest(T_DIMSE_C_MoveRQ &reqMessage, const T_ASC_PresentationContextID presID);
};

#endif // MYSCP_H
