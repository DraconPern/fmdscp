#ifndef __MODEL_H__
#define __MODEL_H__

#include <string>
// #include <boost/date_time/gregorian/gregorian.hpp>
#include "Poco/Data/TypeHandler.h"

// rails generate model PatientStudy DCM_StudyInstanceUID:string DCM_PatientsName:string DCM_PatientID:string DCM_StudyDate:date DCM_ModalitiesInStudy:string DCM_StudyDescription:string DCM_PatientsSex:string DCM_PatientsBirthDate:date
// index on DCM_StudyInstanceUID
class PatientStudy
{
public:
	PatientStudy() { id = 0;
	}
	int id;
	std::string StudyInstanceUID;
	std::string StudyID;
	std::string AccessionNumber;
	std::string PatientName;
	std::string PatientID;
	Poco::DateTime StudyDate;
	std::string ModalitiesInStudy;
	std::string StudyDescription;
	std::string PatientSex;
	Poco::DateTime PatientBirthDate;
	std::string ReferringPhysicianName;
	Poco::DateTime created_at;
	Poco::DateTime updated_at;
};

// rails generate model Series DCM_SeriesInstanceUID:string DCM_StudyInstanceUID:string DCM_Modality:string DCM_SeriesDescription:string
// index on DCM_SeriesInstanceUID

class Series
{
public:
	Series(int patient_study_id = 0) { id = 0;
	SeriesNumber = 0;
	this->patient_study_id = patient_study_id;
	}
	int id;
	std::string SeriesInstanceUID;
	std::string Modality;
	std::string SeriesDescription;
	int SeriesNumber;
	Poco::DateTime SeriesDate;
	int patient_study_id;	// fk
	Poco::DateTime created_at;
	Poco::DateTime updated_at;	
};

// rails generate model Instance SOPInstanceUID:string DCM_SeriesInstanceUID:string
// index on SOPInstanceUID

class Instance
{
public:
	Instance(int series_id = 0) { id = 0;
	InstanceNumber = 0;
	this->series_id = series_id;
	}
	int id;
	std::string SOPInstanceUID;
	int InstanceNumber;
	int series_id;		// fk
	Poco::DateTime created_at;
	Poco::DateTime updated_at;	
};

class Destination
{
public:
	Destination() { id = 0;
	destinationport = 0;
	}
	int id;
	std::string name;
	std::string destinationhost;
	int destinationport;
	std::string destinationAE;
	std::string sourceAE;
	Poco::DateTime created_at;
	Poco::DateTime updated_at;
};

class OutgoingSession
{
public:
	OutgoingSession() { id = 0;
	queued = destination_id = 0;
	}
	int id;
	std::string uuid;
	int queued;
	std::string StudyInstanceUID;
	std::string PatientName;
	std::string PatientID;
	int destination_id;	//fk
	std::string status;
	Poco::DateTime created_at;
	Poco::DateTime updated_at;
};

namespace Poco {
	namespace Data {
		template <>
		class TypeHandler<class PatientStudy>
		{
		public:
			static void bind(std::size_t pos, const PatientStudy& obj, AbstractBinder::Ptr pBinder, AbstractBinder::Direction dir)
			{
				poco_assert_dbg (!pBinder.isNull());
				TypeHandler<int>::bind(pos++, obj.id, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.StudyInstanceUID, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.StudyID, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.AccessionNumber, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.PatientName, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.PatientID, pBinder, dir);
				TypeHandler<DateTime>::bind(pos++, obj.StudyDate, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.ModalitiesInStudy, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.StudyDescription, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.PatientSex, pBinder, dir);
				TypeHandler<DateTime>::bind(pos++, obj.PatientBirthDate, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.ReferringPhysicianName, pBinder, dir);
				TypeHandler<DateTime>::bind(pos++, obj.created_at, pBinder, dir);
				TypeHandler<DateTime>::bind(pos++, obj.updated_at, pBinder, dir);
			}

			static std::size_t size()
			{
				return 14;
			}

			static void prepare(std::size_t pos, const PatientStudy& obj, AbstractPreparator::Ptr pPrepare)
			{
				poco_assert_dbg (!pPrepare.isNull());
				TypeHandler<int>::prepare(pos++, obj.id, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.StudyInstanceUID, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.StudyID, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.AccessionNumber, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.PatientName, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.PatientID, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.StudyDate, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.ModalitiesInStudy, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.StudyDescription, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.PatientSex, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.PatientBirthDate, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.ReferringPhysicianName, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.created_at, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.updated_at, pPrepare);
			}

			static void extract(std::size_t pos, PatientStudy& obj, const PatientStudy& defVal, AbstractExtractor::Ptr pExt)
				/// obj will contain the result, defVal contains values we should use when one column is NULL
			{
				poco_assert_dbg (!pExt.isNull());
				TypeHandler<int>::extract(pos++, obj.id, defVal.id, pExt);
				TypeHandler<std::string>::extract(pos++, obj.StudyInstanceUID, defVal.StudyInstanceUID, pExt);
				TypeHandler<std::string>::extract(pos++, obj.StudyID, defVal.StudyID, pExt);
				TypeHandler<std::string>::extract(pos++, obj.AccessionNumber, defVal.AccessionNumber, pExt);
				TypeHandler<std::string>::extract(pos++, obj.PatientName, defVal.PatientName, pExt);
				TypeHandler<std::string>::extract(pos++, obj.PatientID, defVal.PatientName, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.StudyDate, defVal.StudyDate, pExt);
				TypeHandler<std::string>::extract(pos++, obj.ModalitiesInStudy, defVal.ModalitiesInStudy, pExt);
				TypeHandler<std::string>::extract(pos++, obj.StudyDescription, defVal.StudyDescription, pExt);
				TypeHandler<std::string>::extract(pos++, obj.PatientSex, defVal.PatientSex, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.PatientBirthDate, defVal.PatientBirthDate, pExt);
				TypeHandler<std::string>::extract(pos++, obj.ReferringPhysicianName, defVal.ReferringPhysicianName, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.created_at, defVal.created_at, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.updated_at, defVal.updated_at, pExt);
			}

		private:
			TypeHandler();
			~TypeHandler();
			TypeHandler(const TypeHandler&);
			TypeHandler& operator=(const TypeHandler&);
		};

		template <>
		class TypeHandler<class Series>
		{
		public:
			static void bind(std::size_t pos, const Series& obj, AbstractBinder::Ptr pBinder, AbstractBinder::Direction dir)
			{
				poco_assert_dbg (!pBinder.isNull());
				TypeHandler<int>::bind(pos++, obj.id, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.SeriesInstanceUID, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.Modality, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.SeriesDescription, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.SeriesNumber, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.SeriesDate, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.patient_study_id, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.created_at, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.updated_at, pBinder, dir);
			}

			static std::size_t size()
			{
				return 9;
			}

			static void prepare(std::size_t pos, const Series& obj, AbstractPreparator::Ptr pPrepare)
			{
				poco_assert_dbg (!pPrepare.isNull());
				TypeHandler<int>::prepare(pos++, obj.id, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.SeriesInstanceUID, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.Modality, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.SeriesDescription, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.SeriesNumber, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.SeriesDate, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.patient_study_id, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.created_at, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.updated_at, pPrepare);				
			}

			static void extract(std::size_t pos, Series& obj, const Series& defVal, AbstractExtractor::Ptr pExt)
				/// obj will contain the result, defVal contains values we should use when one column is NULL
			{
				poco_assert_dbg (!pExt.isNull());
				TypeHandler<int>::extract(pos++, obj.id, defVal.id, pExt);
				TypeHandler<std::string>::extract(pos++, obj.SeriesInstanceUID, defVal.SeriesInstanceUID, pExt);
				TypeHandler<std::string>::extract(pos++, obj.Modality, defVal.Modality, pExt);
				TypeHandler<std::string>::extract(pos++, obj.SeriesDescription, defVal.SeriesDescription, pExt);
				TypeHandler<int>::extract(pos++, obj.SeriesNumber, defVal.SeriesNumber, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.SeriesDate, defVal.SeriesDate, pExt);
				TypeHandler<int>::extract(pos++, obj.patient_study_id, defVal.patient_study_id, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.created_at, defVal.created_at, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.updated_at, defVal.updated_at, pExt);				
			}

		private:
			TypeHandler();
			~TypeHandler();
			TypeHandler(const TypeHandler&);
			TypeHandler& operator=(const TypeHandler&);
		};

		template <>
		class TypeHandler<class Instance>
		{
		public:
			static void bind(std::size_t pos, const Instance& obj, AbstractBinder::Ptr pBinder, AbstractBinder::Direction dir)
			{
				poco_assert_dbg (!pBinder.isNull());
				TypeHandler<int>::bind(pos++, obj.id, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.SOPInstanceUID, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.InstanceNumber, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.series_id, pBinder, dir);
				TypeHandler<DateTime>::bind(pos++, obj.created_at, pBinder, dir);
				TypeHandler<DateTime>::bind(pos++, obj.updated_at, pBinder, dir);				
			}

			static std::size_t size()
			{
				return 6;
			}

			static void prepare(std::size_t pos, const Instance& obj, AbstractPreparator::Ptr pPrepare)
			{
				poco_assert_dbg (!pPrepare.isNull());
				TypeHandler<int>::prepare(pos++, obj.id, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.SOPInstanceUID, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.InstanceNumber, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.series_id, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.created_at, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.updated_at, pPrepare);				
			}

			static void extract(std::size_t pos, Instance& obj, const Instance& defVal, AbstractExtractor::Ptr pExt)
				/// obj will contain the result, defVal contains values we should use when one column is NULL
			{
				poco_assert_dbg (!pExt.isNull());
				TypeHandler<int>::extract(pos++, obj.id, defVal.id, pExt);
				TypeHandler<std::string>::extract(pos++, obj.SOPInstanceUID, defVal.SOPInstanceUID, pExt);
				TypeHandler<int>::extract(pos++, obj.InstanceNumber, defVal.InstanceNumber, pExt);
				TypeHandler<int>::extract(pos++, obj.series_id, defVal.series_id, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.created_at, defVal.created_at, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.updated_at, defVal.updated_at, pExt);				
			}

		private:
			TypeHandler();
			~TypeHandler();
			TypeHandler(const TypeHandler&);
			TypeHandler& operator=(const TypeHandler&);
		};

		template <>
		class TypeHandler<class Destination>
		{
		public:
			static void bind(std::size_t pos, const Destination& obj, AbstractBinder::Ptr pBinder, AbstractBinder::Direction dir)
			{
				poco_assert_dbg (!pBinder.isNull());
				TypeHandler<int>::bind(pos++, obj.id, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.name, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.destinationhost, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.destinationport, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.destinationAE, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.sourceAE, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.created_at, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.updated_at, pBinder, dir);
			}

			static std::size_t size()
			{
				return 8;
			}

			static void prepare(std::size_t pos, const Destination& obj, AbstractPreparator::Ptr pPrepare)
			{
				poco_assert_dbg (!pPrepare.isNull());
				TypeHandler<int>::prepare(pos++, obj.id, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.name, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.destinationhost, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.destinationport, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.destinationAE, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.sourceAE, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.created_at, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.updated_at, pPrepare);
			}

			static void extract(std::size_t pos, Destination& obj, const Destination& defVal, AbstractExtractor::Ptr pExt)
				/// obj will contain the result, defVal contains values we should use when one column is NULL
			{
				poco_assert_dbg (!pExt.isNull());
				TypeHandler<int>::extract(pos++, obj.id, defVal.id, pExt);
				TypeHandler<std::string>::extract(pos++, obj.name, defVal.name, pExt);
				TypeHandler<std::string>::extract(pos++, obj.destinationhost, defVal.destinationhost, pExt);
				TypeHandler<int>::extract(pos++, obj.destinationport, defVal.destinationport, pExt);
				TypeHandler<std::string>::extract(pos++, obj.destinationAE, defVal.destinationAE, pExt);
				TypeHandler<std::string>::extract(pos++, obj.sourceAE, defVal.sourceAE, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.created_at, defVal.created_at, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.updated_at, defVal.updated_at, pExt);
			}

		private:
			TypeHandler();
			~TypeHandler();
			TypeHandler(const TypeHandler&);
			TypeHandler& operator=(const TypeHandler&);
		};

		template <>
		class TypeHandler<class OutgoingSession>
		{
		public:
			static void bind(std::size_t pos, const OutgoingSession& obj, AbstractBinder::Ptr pBinder, AbstractBinder::Direction dir)
			{
				poco_assert_dbg (!pBinder.isNull());
				TypeHandler<int>::bind(pos++, obj.id, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.uuid, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.queued, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.StudyInstanceUID, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.PatientName, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.PatientID, pBinder, dir);
				TypeHandler<int>::bind(pos++, obj.destination_id, pBinder, dir);
				TypeHandler<std::string>::bind(pos++, obj.status, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.created_at, pBinder, dir);
				TypeHandler<Poco::DateTime>::bind(pos++, obj.updated_at, pBinder, dir);
			}

			static std::size_t size()
			{
				return 10;
			}

			static void prepare(std::size_t pos, const OutgoingSession& obj, AbstractPreparator::Ptr pPrepare)
			{
				poco_assert_dbg (!pPrepare.isNull());
				TypeHandler<int>::prepare(pos++, obj.id, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.uuid, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.queued, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.StudyInstanceUID, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.PatientName, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.PatientID, pPrepare);
				TypeHandler<int>::prepare(pos++, obj.destination_id, pPrepare);
				TypeHandler<std::string>::prepare(pos++, obj.status, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.created_at, pPrepare);
				TypeHandler<Poco::DateTime>::prepare(pos++, obj.updated_at, pPrepare);
			}

			static void extract(std::size_t pos, OutgoingSession& obj, const OutgoingSession& defVal, AbstractExtractor::Ptr pExt)
				/// obj will contain the result, defVal contains values we should use when one column is NULL
			{
				poco_assert_dbg (!pExt.isNull());
				TypeHandler<int>::extract(pos++, obj.id, defVal.id, pExt);
				TypeHandler<std::string>::extract(pos++, obj.uuid, defVal.uuid, pExt);
				TypeHandler<int>::extract(pos++, obj.queued, defVal.queued, pExt);
				TypeHandler<std::string>::extract(pos++, obj.StudyInstanceUID, defVal.StudyInstanceUID, pExt);
				TypeHandler<std::string>::extract(pos++, obj.PatientName, defVal.PatientName, pExt);
				TypeHandler<std::string>::extract(pos++, obj.PatientID, defVal.PatientID, pExt);
				TypeHandler<int>::extract(pos++, obj.destination_id, defVal.destination_id, pExt);
				TypeHandler<std::string>::extract(pos++, obj.status, defVal.status, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.created_at, defVal.created_at, pExt);
				TypeHandler<Poco::DateTime>::extract(pos++, obj.updated_at, defVal.updated_at, pExt);
			}

		private:
			TypeHandler();
			~TypeHandler();
			TypeHandler(const TypeHandler&);
			TypeHandler& operator=(const TypeHandler&);
		};
	}
} // namespace Poco::Data

#endif
