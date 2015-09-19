#ifndef __MODEL_H__
#define __MODEL_H__

#include <string>
#include "poco/Tuple.h"

using namespace Poco;


// rails generate model PatientStudy DCM_StudyInstanceUID:string DCM_PatientsName:string DCM_PatientID:string DCM_StudyDate:date DCM_ModalitiesInStudy:string DCM_StudyDescription:string DCM_PatientsSex:string DCM_PatientsBirthDate:date
// index on DCM_StudyInstanceUID
typedef Poco::Tuple<
	int, // id
	std::string, // DCM_StudyInstanceUID
	std::string, // DCM_PatientName
	std::string, // DCM_PatientID
	Poco::Data::Date, // DCM_StudyDate
	std::string, // DCM_ModalitiesInStudy
	std::string, // DCM_StudyDescription
	std::string, // DCM_PatientSex
	Poco::Data::Date, // DCM_PatientBirthDate
	Poco::DateTime, // created_at
	Poco::DateTime  // updated_at
> PatientStudy;

// rails generate model Series DCM_SeriesInstanceUID:string DCM_StudyInstanceUID:string DCM_Modality:string DCM_SeriesDescription:string
// index on DCM_SeriesInstanceUID

typedef Poco::Tuple<
	int, // id
	std::string, // DCM_SeriesInstanceUID
	std::string, // DCM_StudyInstanceUID
	std::string, // DCM_Modality
	std::string, // DCM_SeriesDescription
	Poco::DateTime, // created_at
	Poco::DateTime  // updated_at
> Series;

// rails generate model Instance SOPInstanceUID:string DCM_SeriesInstanceUID:string
// index on SOPInstanceUID

typedef Poco::Tuple<
	int, // id
	std::string, // SOPInstanceUID
	std::string, // DCM_SeriesInstanceUID
	Poco::DateTime, // created_at
	Poco::DateTime  // updated_at
> Instance;

#endif
