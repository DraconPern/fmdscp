#ifndef __MODEL_H__
#define __MODEL_H__

#include "soci/soci.h"
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
	std::tm StudyDate;
	std::string ModalitiesInStudy;
	std::string StudyDescription;
	std::string PatientSex;
	std::tm PatientBirthDate;
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

namespace soci
{
    template<>
    struct type_conversion<PatientStudy>
    {
        typedef values base_type;

        static void from_base(values const & v, indicator /* ind */, PatientStudy & p)
        {
            p.id = v.get<int>("id");
            p.StudyInstanceUID = v.get<std::string>("StudyInstanceUID");
            p.PatientName = v.get<std::string>("PatientName");
            p.PatientID = v.get<std::string>("PatientID");
          	p.StudyDate = v.get<std::tm>("StudyDate");
          	p.ModalitiesInStudy = v.get<std::string>("ModalitiesInStudy");
          	p.StudyDescription = v.get<std::string>("StudyDescription");
          	p.PatientSex = v.get<std::string>("PatientSex");
          	p.PatientBirthDate = v.get<std::tm>("PatientBirthDate");
          	p.created_at = v.get<std::tm>("created_at");
          	p.updated_at = v.get<std::tm>("updated_at");
        }

        static void to_base(const PatientStudy & p, values & v, indicator & ind)
        {
            v.set("id", p.id);
            v.set("StudyInstanceUID", p.StudyInstanceUID);
            v.set("PatientName", p.PatientName);
            v.set("PatientID", p.PatientID);
            v.set("StudyDate", p.StudyDate);
            v.set("ModalitiesInStudy", p.ModalitiesInStudy);
            v.set("StudyDescription", p.StudyDescription);
            v.set("PatientSex", p.PatientSex);
            v.set("PatientBirthDate", p.PatientBirthDate);
            v.set("created_at", p.created_at);
            v.set("updated_at", p.updated_at);
            ind = i_ok;
        }
    };

    template<>
    struct type_conversion<Series>
    {
        typedef values base_type;

        static void from_base(values const & v, indicator /* ind */, Series & p)
        {
            p.id = v.get<int>("id");
            p.SeriesInstanceUID = v.get<std::string>("SeriesInstanceUID");
            p.StudyInstanceUID = v.get<std::string>("StudyInstanceUID");
            p.Modality = v.get<std::string>("Modality");
          	p.SeriesDescription = v.get<std::string>("SeriesDescription");
          	p.created_at = v.get<std::tm>("created_at");
          	p.updated_at = v.get<std::tm>("updated_at");
        }

        static void to_base(const Series & p, values & v, indicator & ind)
        {
            v.set("id", p.id);
            v.set("SeriesInstanceUID", p.SeriesInstanceUID);
            v.set("StudyInstanceUID", p.StudyInstanceUID);
            v.set("Modality", p.Modality);
            v.set("SeriesDescription", p.SeriesDescription);
            v.set("created_at", p.created_at);
            v.set("updated_at", p.updated_at);
            ind = i_ok;
        }
    };

    template<>
    struct type_conversion<Instance>
    {
        typedef values base_type;

        static void from_base(values const & v, indicator /* ind */, Instance & p)
        {
            p.id = v.get<int>("id");
            p.SOPInstanceUID = v.get<std::string>("SOPInstanceUID");
            p.SeriesInstanceUID = v.get<std::string>("SeriesInstanceUID");
          	p.created_at = v.get<std::tm>("created_at");
          	p.updated_at = v.get<std::tm>("updated_at");
        }

        static void to_base(const Instance & p, values & v, indicator & ind)
        {
            v.set("id", p.id);
            v.set("SOPInstanceUID", p.SOPInstanceUID);
            v.set("SeriesInstanceUID", p.SeriesInstanceUID);
            v.set("created_at", p.created_at);
            v.set("updated_at", p.updated_at);
            ind = i_ok;
        }
    };
}

#endif
