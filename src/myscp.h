#ifndef MYSCP_H
#define MYSCP_H

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include <winsock2.h>	// include winsock2 before network includes
#include "dcmtk/config/osconfig.h"  /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/scpthrd.h"
#include "dcmtk/dcmnet/scppool.h"
#include "dcmtk/dcmnet/diutil.h"


#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include <boost/uuid/uuid.hpp>            // uuid class

/// The class that provides the 
class MySCP : public DcmThreadSCP
{

public:

	/** Constructor. Initializes internal member variables.
	*/
	MySCP();

	/** Virtual destructor, frees internal memory.
	*/
	virtual ~MySCP();

	void setUUID(boost::uuids::uuid uuid);

	// override run simply because we need a copy of assoc
	virtual OFCondition run(T_ASC_Association* incomingAssoc);
protected:	
	virtual OFCondition negotiateAssociation();
	bool IsStorageAbstractSyntax(DIC_UI abstractsyntax);
	bool IsSupportedOperationAbstractSyntax(DIC_UI abstractsyntax);
	virtual OFCondition handleIncomingCommand(T_DIMSE_Message *incomingMsg,
		const DcmPresentationContextInfo &presInfo);

	virtual OFCondition handleSTORERequest(T_DIMSE_C_StoreRQ &reqMessage, const T_ASC_PresentationContextID presID);
	virtual OFCondition handleFINDRequest(T_DIMSE_C_FindRQ &reqMessage, const T_ASC_PresentationContextID presID);
	virtual OFCondition handleMOVERequest(T_DIMSE_C_MoveRQ &reqMessage, const T_ASC_PresentationContextID presID);

	boost::uuids::uuid uuid_;
	T_ASC_Association *assoc_;	//copy of the association
};

/// Provides the glue code to glue the thread pool, thread worker and the MySCP together
// similar to DcmSCPPool
class MyDcmSCPPool : public DcmBaseSCPPool
{
public:

    /** Default construct a DcmSCPPool object.
     */
    MyDcmSCPPool();

	virtual OFCondition listen();

private:

    // similar to DcmSCPPool
    class MySCPWorker : public DcmBaseSCPPool::DcmBaseSCPWorker
                     , private MySCP
    {
	public:
        /** Construct a SCPWorker for being used by the given DcmSCPPool.
         *  @param pool the DcmSCPPool object this Worker belongs to.
         */
        MySCPWorker(MyDcmSCPPool& pool)
          : DcmBaseSCPPool::DcmBaseSCPWorker(pool)
          , MySCP()
        {			
        }

        /** Set the shared configuration for this worker.
         *  @param config a DcmSharedSCPConfig object to be used by this worker.
         *  @return the result of the underlying MySCP implementation.
         */
        virtual OFCondition setSharedConfig(const DcmSharedSCPConfig& config)
        {
            return MySCP::setSharedConfig(config);
        }
		
		virtual OFCondition setAssociation(T_ASC_Association* assoc);

        virtual OFBool busy()
        {
            return MySCP::isConnected();
        }

        virtual OFCondition workerListen(T_ASC_Association* const assoc);		
    };

    /** Create a worker to be used for handling a request.
     *  @return a pointer to a newly created MySCP worker.
     */
    virtual DcmBaseSCPPool::DcmBaseSCPWorker* createSCPWorker()
    {		
        return new MySCPWorker(*this);
    }
};

#endif // MYSCP_H
