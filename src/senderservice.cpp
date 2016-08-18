#include "config.h"
#include "model.h"
#include "senderservice.h"
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
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
			"createdAt,updatedAt"
			" FROM outgoing_sessions WHERE queued = 1 LIMIT 1",
			into(sessions);

		queueselect.execute();

		if (sessions.size() > 0)
		{
			dbconnection << "UPDATE outgoing_sessions SET queued = 0 WHERE id = :id AND queued = 1", sessions[0].id, now;
			int rowcount = 0;
			dbconnection << "SELECT ROW_COUNT()", into(rowcount), now;

			if (rowcount > 0)
			{
				outgoingsession = sessions[0];
				return true;
			}

		}
	}
	catch (Poco::Exception &e)
	{
		std::string what = e.displayText();		
		DCMNET_ERROR(what);
	}

	return false;
}

void SenderService::run_internal()
{
	dcmtk::log4cplus::NDCContextCreator ndc("senderservice");
	while (!shouldShutdown())
	{
		OutgoingSession outgoingsession;
		if (getQueued(outgoingsession))
		{
			DCMNET_INFO("Session " << outgoingsession.uuid << " Sending study " << outgoingsession.StudyInstanceUID);

			Destination destination;
			if (findDestination(outgoingsession.destination_id, destination))
			{
				DCMNET_INFO(" to " << destination.destinationhost << ":" << destination.destinationport <<
					" destinationAE = " << destination.destinationAE << " sourceAE = " << destination.sourceAE);

				boost::shared_ptr<Sender> sender(new Sender(boost::lexical_cast<boost::uuids::uuid>(outgoingsession.uuid)));
				sender->Initialize(destination);

				naturalpathmap files;
				if (GetFilesToSend(outgoingsession.StudyInstanceUID, files))
				{
					sender->SetFileList(files);

					// start the thread
					sender->DoSendAsync();

					// save the object to the main list so we can track it
					senders.push_back(sender);
				}
			}
		}
		else
		{
			Sleep(200);
		}

		// look at the senders and delete any that's already done
		sharedptrlist::iterator itr = senders.begin();
		while (itr != senders.end())
		{

			if ((*itr)->IsDone())
				senders.erase(itr++);
			else
				itr++;
		}
	}

	// exiting, signal senders
	sharedptrlist::iterator itr = senders.begin();
	while (itr != senders.end())
	{
		if ((*itr)->IsDone())
			senders.erase(itr++);
		else
		{
			(*itr)->Cancel();
			itr++;
		}
	}

	// wait... 
	// eh...

}

void SenderService::run()
{
	run_internal();
	dcmtk::log4cplus::threadCleanup();
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
			"createdAt,updatedAt"
			" FROM destinations WHERE id = :id LIMIT 1",
			into(dests),
			use(id), now;

		if (dests.size() > 0)
		{
			destination = dests[0];
			return true;
		}
	}
	catch (...)
	{

	}
	return false;
}

bool SenderService::GetFilesToSend(std::string studyinstanceuid, naturalpathmap &result)
{
	try
	{
		// open the db
		Poco::Data::Session dbconnection(config::getConnectionString());
		std::vector<PatientStudy> studies;
		dbconnection << "SELECT * FROM patient_studies WHERE StudyInstanceUID = :StudyInstanceUID LIMIT 1", into(studies),
			use(studyinstanceuid), now;
		if (studies.size() <= 0)
		{
			throw std::exception();
		}

		std::vector<Series> series_list;
		dbconnection << "SELECT * FROM series WHERE StudyInstanceUID = :StudyInstanceUID", into(series_list), use(studyinstanceuid), now;

		for (std::vector<Series>::iterator itr = series_list.begin(); itr != series_list.end(); itr++)
		{
			std::string seriesinstanceuid = (*itr).SeriesInstanceUID;
			std::vector<Instance> instances;
			dbconnection << "SELECT * FROM instances WHERE SeriesInstanceUID = :SeriesInstanceUID", into(instances), use(seriesinstanceuid), now;

			boost::filesystem::path serieslocation;
			serieslocation = config::getStoragePath();
			serieslocation /= studyinstanceuid;
			serieslocation /= seriesinstanceuid;

			for (std::vector<Instance>::iterator itr2 = instances.begin(); itr2 != instances.end(); itr2++)
			{
				boost::filesystem::path filename = serieslocation;
				filename /= (*itr2).SOPInstanceUID + ".dcm";
				result.insert(std::pair<std::string, boost::filesystem::path>((*itr2).SOPInstanceUID, filename));
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
