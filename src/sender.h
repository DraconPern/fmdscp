#ifndef _DICOMSENDER_
#define _DICOMSENDER_

#include "alphanum.hpp"
#include <set>
#include <map>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/thread.hpp>
#include "model.h"

// typedef std::set<boost::filesystem::path, doj::alphanum_less<boost::filesystem::path> > naturalset;
typedef std::map<std::string, boost::filesystem::path, doj::alphanum_less<std::string> > naturalpathmap;

class Sender
{

public:
	Sender(boost::uuids::uuid uuid);
	~Sender(void);

	void Initialize(Destination &destination);

	void SetFileList(const naturalpathmap &instances);

	void DoSendAsync();
	void DoSend();

	void Cancel();
	bool IsDone();
		
protected:
	boost::uuids::uuid uuid;
	void SetStatus(std::string msg);

	static void DoSendThread(void *obj);
	

	int SendABatch();

	bool IsCanceled();
	void SetDone(bool state);

	boost::mutex mutex;
	bool cancelEvent, doneEvent;

	// list of dicom images to send, all images should be in the same patient
	naturalpathmap instances;
	int totalfiles;

	Destination destination;

	// scan files to build sopclassuidtransfersyntax	
	bool scanFile(boost::filesystem::path currentFilename);	

	typedef std::map<std::string, std::set<std::string> > mapset;
	mapset sopclassuidtransfersyntax;
};


#endif