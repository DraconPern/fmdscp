#ifndef DBPOOL_H
#define DBPOOL_H

#include <Poco/Data/SessionPool.h>

class DBPool : public Poco::Data::SessionPool
{
public:
	DBPool();

	// Poco::Data::Session get() { return Poco::Data::Session("mysql", config::getConnectionString()); };
	Poco::Data::Session get() { return Poco::Data::SessionPool::get(); };
};

#endif // DBPOOL_H
