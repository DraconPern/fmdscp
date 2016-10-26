
#include "dbpool.h"
#include "config.h"

DBPool::DBPool() : SessionPool("mysql", config::getConnectionString()) 
{
}

// Poco::Data::Session DBPool::get() { return Poco::Data::Session("mysql", config::getConnectionString()); };
Poco::Data::Session DBPool::get() { return Poco::Data::SessionPool::get(); };