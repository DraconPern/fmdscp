
#include "dbpool.h"
#include "config.h"

DBPool::DBPool() : SessionPool("mysql", config::getConnectionString()) 
{
}