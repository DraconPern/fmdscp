#ifndef DBPOOL_H
#define DBPOOL_H

#include <Poco/Data/SessionPool.h>

// our session pool class
class DBPool : public Poco::Data::SessionPool
{
public:
	DBPool();

	// our version of get, that can return a pool or a new session
	Poco::Data::Session get();
};

#endif // DBPOOL_H
