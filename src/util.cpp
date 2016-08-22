#include "util.h"
#include "Poco/DateTimeFormatter.h"

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

std::string ToJSON(Poco::DateTime &datetime)
{
	Poco::DateTimeFormatter f;
	return f.format(datetime, "%Y-%m-%dT%H:%M:%S%z");
}