#include "util.h"

OFDate getDate(DcmDataset *dataset, const DcmTagKey& tagKey)
{
	OFDate result;	
	OFString strdate;	
	if(dataset && dataset->findAndGetOFString(tagKey, strdate).good())
		result.setISOFormattedDate(strdate);

	return result;
}

OFTime getTime(DcmDataset *dataset, const DcmTagKey& tagKey)
{
	OFTime result;
	OFString strtime;	
	if(dataset && dataset->findAndGetOFString(tagKey, strtime).good())
		result.setISOFormattedTime(strtime);

	return result;
}