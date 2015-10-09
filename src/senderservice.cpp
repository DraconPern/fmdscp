
#include <winsock2.h>	// include winsock2 before soci
#include "config.h"
#include "model.h"
#include "senderservice.h"
#include "dicomsender.h"
#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"


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

SenderService::SenderService()
{
	shutdownEvent = false;


}

bool SenderService::getQueued(OutgoingSession &outgoingsession)
{
	try
	{
		soci::session dbconnection(Config::getConnectionString());		

		soci::session &queueselect = dbconnection;
		
		queueselect << "SELECT id, uuid, queued, StudyInstanceUID, PatientName, PatientID, destination_id, status,"            
			"created_at,updated_at"
			" FROM outgoing_sessions WHERE queued = 1 LIMIT 1",
			soci::into(outgoingsession);

		if(queueselect.got_data())
		{
			dbconnection << "UPDATE outgoing_sessions SET queued = 0 WHERE id = :id AND queued = 1", soci::use(outgoingsession.id);				
			int rowcount = 0;
			dbconnection << "SELECT ROW_COUNT()", soci::into(rowcount);

			if(rowcount > 0)
			{
				return true;
			}
			
		}
	}
	catch(std::exception &e)
	{
		std::string what = e.what();		
		DCMNET_ERROR(what);
	}

	return false;
}

void SenderService::run()
{

	dcmtk::log4cplus::NDCContextCreator ndc("senderservice");
	while(!shouldShutdown())
	{
		OutgoingSession outgoingsession;
		if(getQueued(outgoingsession))
		{

		}		
		Sleep(200);

	}
}

/*
void Sender::join()
{


}
*/

void SenderService::stop()
{
	boost::mutex::scoped_lock lk(mutex);
	shutdownEvent = true;
}

bool SenderService::shouldShutdown()
{
	boost::mutex::scoped_lock lk(mutex);
	return shutdownEvent;
}

