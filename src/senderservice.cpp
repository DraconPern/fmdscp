#include "config.h"
#include "model.h"
#include "senderservice.h"
#include <boost/thread.hpp>
#include <Poco/Data/Session.h>
using namespace Poco::Data::Keywords;

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
		Poco::Data::Session dbconnection(config::getConnectionString());		

		Poco::Data::Statement queueselect(dbconnection);
		std::vector<OutgoingSession> sessions;
		queueselect << "SELECT id, uuid, queued, StudyInstanceUID, PatientName, PatientID, destination_id, status,"            
			"created_at,updated_at"
			" FROM outgoing_sessions WHERE queued = 1 LIMIT 1",
			into(sessions);

		queueselect.execute();

		if(sessions.size() > 0)
		{
			dbconnection << "UPDATE outgoing_sessions SET queued = 0 WHERE id = :id AND queued = 1", sessions[0].id, now;				
			int rowcount = 0;
			dbconnection << "SELECT ROW_COUNT()", into(rowcount), now;

			if(rowcount > 0)
			{
				outgoingsession = sessions[0];
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
			DCMNET_INFO("Session " << outgoingsession.uuid << " Sending study " << outgoingsession.StudyInstanceUID);

			Destination destination;
			if(findDestination(outgoingsession.destination_id, destination))
			{
				DCMNET_INFO(" to " << destination.destinationhost << ":" << destination.destinationport <<
				" destinationAE = " << destination.destinationAE << " sourceAE = " << destination.sourceAE);

				boost::shared_ptr<DICOMSender> sender(new DICOMSender());
				sender->Initialize(outgoingsession.id,
					destination.destinationhost, destination.destinationport, destination.destinationAE, destination.sourceAE);

				naturalset files;
				if(GetFilesToSend(outgoingsession.StudyInstanceUID, files))
				{
					sender->SetFileList(files);					

					// start the thread
					boost::thread t(SenderService::RunDICOMSender, sender, outgoingsession.uuid);
					t.detach(); 
				}				
			}
		}
		else
		{
			Sleep(200);
		}

	}
}

void SenderService::RunDICOMSender(boost::shared_ptr<DICOMSender> sender, std::string uuid)
{
	dcmtk::log4cplus::NDCContextCreator ndc(uuid.c_str());	
	DICOMSender::DoSendThread(sender.get());
}


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


bool SenderService::findDestination(int id, Destination &destination)
{
	try
	{
		Poco::Data::Session dbconnection(config::getConnectionString());
		
		std::vector<Destination> dests;
		dbconnection << "SELECT id,"
			"name, destinationhost, destinationport, destinationAE, sourceAE,"
			"created_at,updated_at"
			" FROM destinations WHERE id = :id LIMIT 1",
			into(dests),
			use(id), now;

		if(dests.size() > 0)
		{
			destination = dests[0];
			return true;
		}
	}
	catch(...)
	{

	}
	return false;
}

bool SenderService::GetFilesToSend(std::string studyinstanceuid, naturalset &result)
{
	try
	{				
		// open the db
		Poco::Data::Session dbconnection(config::getConnectionString());
		std::vector<PatientStudy> studies;
		dbconnection << "SELECT * FROM patient_studies WHERE StudyInstanceUID = :StudyInstanceUID LIMIT 1", into(studies),
			use(studyinstanceuid), now;
		if(studies.size() <= 0)
		{			
			throw std::exception();
		}
		
		std::vector<Series> series_list;
		dbconnection << "SELECT * FROM series WHERE StudyInstanceUID = :StudyInstanceUID", into(series_list), use(studyinstanceuid), now;						
		
		for(std::vector<Series>::iterator itr = series_list.begin(); itr != series_list.end(); itr++)
		{
			std::string seriesinstanceuid = (*itr).SeriesInstanceUID;
			std::vector<Instance> instances;
			dbconnection << "SELECT * FROM instances WHERE SeriesInstanceUID = :SeriesInstanceUID", into(instances), use(seriesinstanceuid), now;			
					
			boost::filesystem::path serieslocation;
			serieslocation = config::getStoragePath();
			serieslocation /= studyinstanceuid;
			serieslocation /= seriesinstanceuid;

			for(std::vector<Instance>::iterator itr2 = instances.begin(); itr2 != instances.end(); itr2++)			
			{
				boost::filesystem::path filename = serieslocation;
				filename /= (*itr2).SOPInstanceUID + ".dcm";
				result.insert(filename);
				itr2++;
			}
		}
	}
	catch (std::exception& e)
	{
		return false;
	}

	return true;
}
