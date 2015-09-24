#ifndef __MODEL_H__
#define __MODEL_H__

#include <string>
#include <boost/tuple/tuple.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>

// rails generate model PatientStudy DCM_StudyInstanceUID:string DCM_PatientsName:string DCM_PatientID:string DCM_StudyDate:date DCM_ModalitiesInStudy:string DCM_StudyDescription:string DCM_PatientsSex:string DCM_PatientsBirthDate:date
// index on DCM_StudyInstanceUID
typedef struct SPatientStudy
{
	int id;
	std::string StudyInstanceUID;
	std::string PatientName;
	std::string PatientID;
	boost::gregorian::date StudyDate;
	std::string ModalitiesInStudy;
	std::string StudyDescription;
	std::string PatientSex;
	boost::gregorian::date PatientBirthDate;
	std::tm created_at;
	std::tm updated_at;
} PatientStudy;

// rails generate model Series DCM_SeriesInstanceUID:string DCM_StudyInstanceUID:string DCM_Modality:string DCM_SeriesDescription:string
// index on DCM_SeriesInstanceUID

typedef struct SSeries
{
	int id;
	std::string SeriesInstanceUID;
	std::string StudyInstanceUID;
	std::string Modality;
	std::string SeriesDescription;
	std::tm created_at;
	std::tm updated_at;
} Series;

// rails generate model Instance SOPInstanceUID:string DCM_SeriesInstanceUID:string
// index on SOPInstanceUID

typedef struct SInstance
{
	int id;
	std::string SOPInstanceUID;
	std::string SeriesInstanceUID;
	std::tm created_at;
	std::tm updated_at;
} Instance;

#endif
