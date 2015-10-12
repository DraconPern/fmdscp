#ifndef _DICOMSENDER_
#define _DICOMSENDER_

#include "alphanum.hpp"
#include <set>
#include <boost/filesystem.hpp>

typedef std::set<boost::filesystem::path, doj::alphanum_less<boost::filesystem::path> > naturalset;

class DICOMSenderImpl;
class DICOMSender
{
public:
	DICOMSender();
	~DICOMSender();	

	void Initialize(int outgoingsessionid,
		std::string destinationHost, unsigned int destinationPort, std::string destinationAETitle, std::string ourAETitle);

	void SetFileList(const naturalset &files);

	static void DoSendThread(void *obj);

	std::string ReadLog();

	void Cancel();
	bool IsDone();	
private:
	DICOMSenderImpl *impl;
};

#endif