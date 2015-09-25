#ifndef MYSCP_H
#define MYSCP_H

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"  /* make sure OS specific configuration is included first */
#include "dcmtk/dcmnet/scpthrd.h"
#include "dcmtk/dcmnet/scppool.h"
#include "dcmtk/dcmnet/diutil.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.

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
	virtual OFCondition handleIncomingCommand(T_DIMSE_Message *incomingMsg,
		const DcmPresentationContextInfo &presInfo);

	virtual OFCondition handleSTORERequest(T_DIMSE_C_StoreRQ &reqMessage, const T_ASC_PresentationContextID presID);
	virtual OFCondition handleFINDRequest(T_DIMSE_C_FindRQ &reqMessage, const T_ASC_PresentationContextID presID);
	virtual OFCondition handleMOVERequest(T_DIMSE_C_MoveRQ &reqMessage, const T_ASC_PresentationContextID presID);

	boost::uuids::uuid uuid_;
	T_ASC_Association *assoc_;	//copy of the association
};

/// Provides the glue code to glue the thread pool, thread worker and the SCP together
template<typename SCP = MySCP, typename BaseSCPPool = DcmBaseSCPPool, typename BaseSCPWorker = OFTypename BaseSCPPool::DcmBaseSCPWorker>
class MyDcmSCPPool : public BaseSCPPool
{
public:

    /** Default construct a DcmSCPPool object.
     */
    MyDcmSCPPool() : BaseSCPPool()
    {
		setMaxThreads(50);
    }

	virtual OFCondition listen()
	{
		DCMNET_INFO("Listening.");
		OFCondition result = BaseSCPPool::listen();
		DCMNET_INFO("Stopped listening.");
		return result;
	}
private:

    /** Helper class to use any class as an SCPWorker as long as it is a model
     *  of the @ref SCPThread_Concept.
     */
    struct MySCPWorker : public BaseSCPWorker
                     , private SCP
    {
        /** Construct a SCPWorker for being used by the given DcmSCPPool.
         *  @param pool the DcmSCPPool object this Worker belongs to.
         */
        MySCPWorker(MyDcmSCPPool& pool)
          : BaseSCPWorker(pool)
          , SCP()
        {			
        }

        /** Set the shared configuration for this worker.
         *  @param config a DcmSharedSCPConfig object to be used by this worker.
         *  @return the result of the underlying SCP implementation.
         */
        virtual OFCondition setSharedConfig(const DcmSharedSCPConfig& config)
        {
            return SCP::setSharedConfig(config);
        }
		
		virtual OFCondition setAssociation(T_ASC_Association* assoc)
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
			return BaseSCPWorker::setAssociation(assoc);
		}
        /** Determine if the Worker is currently handling any request.
         *  @return OFTrue if the underlying SCP implementation is currently
         *          handling a request, OFFalse otherwise.
         */
        virtual OFBool busy()
        {
            return SCP::isConnected();
        }

        /** Perform SCP's duties on an already accepted (TCP/IP) connection.
         *  @param assoc The association to be run
         *  @return Returns EC_Normal if negotiation could take place and no
         *          serious network error has occurred or the given association
         *          is invalid. Error code otherwise.
         */
        virtual OFCondition workerListen(T_ASC_Association* const assoc)
        {
			// use uuid as the ndc, the logger is set up to use <ndc>.txt as the filename
			dcmtk::log4cplus::NDCContextCreator ndc(boost::uuids::to_string(uuid_).c_str());
			DCMNET_INFO("Request from hostname " << assoc->params->DULparams.callingPresentationAddress);
			OFString info;
			ASC_dumpConnectionParameters(info, assoc);
			DCMNET_INFO(info);
			ASC_dumpParameters(info, assoc->params, ASC_ASSOC_RQ);
			DCMNET_INFO(info);
            return SCP::run(assoc);
        }
    };

    /** Create a worker to be used for handling a request.
     *  @return a pointer to a newly created SCP worker.
     */
    virtual BaseSCPWorker* createSCPWorker()
    {		
        return new MySCPWorker(*this);
    }
};

#endif // MYSCP_H
