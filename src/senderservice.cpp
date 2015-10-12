
#include <winsock2.h>	// include winsock2 before soci
#include "config.h"
#include "model.h"
#include "senderservice.h"
#include "soci/soci.h"
#include "soci/mysql/soci-mysql.h"
#include <boost/thread.hpp>

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
		soci::session dbconnection(Config::getConnectionString());
		
		soci::session &destinationsselect = dbconnection;
		destinationsselect << "SELECT id,"
			"name, destinationhost, destinationport, destinationAE, sourceAE,"
			"created_at,updated_at"
			" FROM destinations WHERE id = :id LIMIT 1",
			soci::into(destination),
			soci::use(id);

		if(destinationsselect.got_data())
		{
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
		soci::session dbconnection(Config::getConnectionString());
		PatientStudy study;
		dbconnection << "SELECT * FROM patient_studies WHERE StudyInstanceUID = :StudyInstanceUID LIMIT 1", soci::into(study),
			soci::use(studyinstanceuid);
		if(!dbconnection.got_data())
		{			
			throw std::exception();
		}

		soci::statement seriesselect(dbconnection);
		Series series;
		seriesselect.exchange(soci::into(series));
		
		seriesselect.exchange(soci::use(studyinstanceuid));
		seriesselect.alloc();
		seriesselect.prepare("SELECT * FROM series WHERE StudyInstanceUID = :StudyInstanceUID");
		seriesselect.define_and_bind();
		seriesselect.execute();

		std::vector<Series> series_list;		
		std::copy(soci::rowset_iterator<Series >(seriesselect, series), soci::rowset_iterator<Series >(), std::back_inserter(series_list));
				
		for(std::vector<Series>::iterator itr = series_list.begin(); itr != series_list.end(); itr++)
		{
			soci::statement instanceselect(dbconnection);
			Instance instance;
			instanceselect.exchange(soci::into(instance));
			std::string seriesinstanceuid = (*itr).SeriesInstanceUID;
			instanceselect.exchange(soci::use(seriesinstanceuid));
			instanceselect.alloc();
			instanceselect.prepare("SELECT * FROM instances WHERE SeriesInstanceUID = :SeriesInstanceUID");
			instanceselect.define_and_bind();
			instanceselect.execute();
					
			boost::filesystem::path serieslocation;
			serieslocation = Config::getStoragePath();
			serieslocation /= studyinstanceuid;
			serieslocation /= seriesinstanceuid;

			soci::rowset_iterator<Instance > itr2(instanceselect, instance);
			soci::rowset_iterator<Instance > end2;
			while(itr2 != end2)
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