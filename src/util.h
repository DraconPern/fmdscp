#ifndef UTIL_H
#define UTIL_H

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
std::string ToJSON(Poco::DateTime &datetime);

#endif // UTIL_H
