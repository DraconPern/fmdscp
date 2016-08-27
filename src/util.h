#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <map>

// work around the fact that dcmtk doesn't work in unicode mode, so all string operation needs to be converted from/to mbcs
#ifdef _UNICODE
#undef _UNICODE
#undef UNICODE
#define _UNDEFINEDUNICODE
#endif

#include "dcmtk/config/osconfig.h"  /* make sure OS specific configuration is included first */
#include "dcmtk/dcmdata/dcdatset.h"
#include "dcmtk/dcmdata/dcvrda.h"
#include "dcmtk/dcmdata/dcvrtm.h"

#ifdef _UNDEFINEDUNICODE
#define _UNICODE 1
#define UNICODE 1
#endif

#include "Poco/DateTime.h"

OFDate getDate(DcmDataset *dataset, const DcmTagKey& tagKey);
OFTime getTime(DcmDataset *dataset, const DcmTagKey& tagKey);
bool getDateTime(DcmDataset *dataset, const DcmTagKey& tagKeyDate, const DcmTagKey& tagKeyTime, Poco::DateTime &result);
std::string ToJSON(Poco::DateTime &datetime);
void decode_query(const std::string &content, std::map<std::string, std::string> &nvp);
bool url_decode(const std::string& in, std::string& out);

#endif // UTIL_H
