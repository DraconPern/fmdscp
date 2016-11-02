#ifndef CONFIG_H
#define CONFIG_H

#include <boost/filesystem.hpp>
#include "dbpool.h"

class config
{

public:
	static boost::filesystem::path getTempPath();
	static boost::filesystem::path getStoragePath();
	static int getDICOMListeningPort();
	static std::string getFrontEnd();
	static void registerCodecs();
	static void deregisterCodecs();
	static std::string getConnectionString();		
	static bool test(std::string &errormsg, DBPool &dbpool);

};

#endif // CONFIG_H
