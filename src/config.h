#ifndef CONFIG_H
#define CONFIG_H

#include <boost/filesystem.hpp>

#include "poco/AutoPtr.h"
#include "Poco/Data/SessionPool.h"

using Poco::AutoPtr;
using Poco::Data::SessionPool;

class Config
{

public:
	static boost::filesystem::path getTempPath();
	static boost::filesystem::path getStoragePath();
	static void registerCodecs();
	static void deregisterCodecs();
	static std::string getConnectionString();
	static void createDBPool();
	static Poco::Data::SessionPool &getDBPool();
	static bool test(std::string &errormsg);
protected:
	static Poco::AutoPtr<SessionPool> _pool;
};

#endif // CONFIG_H