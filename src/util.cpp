#include <vector>
#include "util.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/Timezone.h"

#include <boost/algorithm/string.hpp>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"   /* make sure OS specific configuration is included first */
#include "dcmtk/dcmdata/dcitem.h"
#include "dcmtk/dcmdata/dcdeftag.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

OFDate getDate(DcmDataset *dataset, const DcmTagKey& tagKey)
{
	OFDate result;	
	DcmElement *element;
	if(dataset && dataset->findAndGetElement(tagKey, element).good())
	{
		DcmDate *d = (DcmDate *) element;
		d->getOFDate(result);		
	}
	return result;
}

OFTime getTime(DcmDataset *dataset, const DcmTagKey& tagKey)
{
	OFTime result;
	DcmElement *element; 	
	if(dataset && dataset->findAndGetElement(tagKey, element).good())
	{
		DcmTime *t = (DcmTime *) element;
		t->getOFTime(result);
	}
	return result;
}

bool getDateTime(DcmDataset *dataset, const DcmTagKey& tagKeyDate, const DcmTagKey& tagKeyTime, Poco::DateTime &result)
{
	OFDate da;
	DcmElement *element;
	if (dataset && dataset->findAndGetElement(tagKeyDate, element).good())
	{
		DcmDate *d = (DcmDate *)element;
		d->getOFDate(da);
	}
	else
		return 0;

	if (!da.isValid())
		return 0;

	OFTime ti;
	if (dataset && dataset->findAndGetElement(tagKeyTime, element).good())
	{
		DcmTime *t = (DcmTime *)element;
		t->getOFTime(ti);
	}
	else
		ti.clear();

	/*
	since the date/time in the file is always 'on the wall clock' time.. not reason to change to UTC.
	// set timezone to machine timezone
	double offset = (double) Poco::Timezone::utcOffset() / 3600;
	if (dataset && dataset->findAndGetElement(DCM_TimezoneOffsetFromUTC, element).good())
	{
		OFString dicomTimeZone;
		
		element->getOFString(dicomTimeZone, 0);
		DcmTime::getTimeZoneFromString(dicomTimeZone, offset);		
	}
	ti.setTimeZone(offset);
	*/

	result.assign(da.getYear(), da.getMonth(), da.getDay(), ti.getHour(), ti.getMinute(), ti.getSecond());
	// result.makeUTC(offset);
	
	return 1;
}

std::string ToJSON(Poco::DateTime &datetime)
{
	return Poco::DateTimeFormatter::format(datetime, "%Y-%m-%dT%H:%M:%S%z");
}


void decode_query(const std::string &content, std::map<std::string, std::string> &nvp)
{
	// split into a map
	std::vector<std::string> pairs;
	boost::split(pairs, content, boost::is_any_of("&"), boost::token_compress_on);

	for (int i = 0; i < pairs.size(); i++)
	{
		std::vector<std::string> namevalue;
		boost::split(namevalue, pairs[i], boost::is_any_of("="), boost::token_compress_on);

		if (namevalue.size() != 2)
			continue;

		std::string name, value;
		url_decode(namevalue[0], name);
		url_decode(namevalue[1], value);

		nvp[name] = value;
	}
}

bool url_decode(const std::string& in, std::string& out)
{
	out.clear();
	out.reserve(in.size());
	for (std::size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] == '%')
		{
			if (i + 3 <= in.size())
			{
				int value = 0;
				std::istringstream is(in.substr(i + 1, 2));
				if (is >> std::hex >> value)
				{
					out += static_cast<char>(value);
					i += 2;
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else if (in[i] == '+')
		{
			out += ' ';
		}
		else
		{
			out += in[i];
		}
	}
	return true;
}