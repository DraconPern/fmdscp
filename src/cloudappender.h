#ifndef __CLOUDAPPENDER_H__
#define __CLOUDAPPENDER_H__

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/oflog/appender.h"
#include "dcmtk/oflog/spi/logevent.h"
using namespace dcmtk::log4cplus;

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "cloudclient.h"

class CloudAppender : public Appender
{
public:
	CloudAppender(CloudClient &cloudclient);
	virtual ~CloudAppender();

	virtual void close();

protected:
    virtual void append(const spi::InternalLoggingEvent& event);

	CloudClient &cloudclient;
};

#endif // __CLOUDAPPENDER_H__