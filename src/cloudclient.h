#ifndef __CLOUDCLIENT_H__
#define __CLOUDCLIENT_H__

#include "sio_client.h"

class CloudClient : public sio::client
{
public:
	CloudClient(std::function< void(void) > shutdownCallback);

	// Initiate sending to cloud.  cloud will ask for chunks from us with OnUpload
	void StartUpload_dicom(/* some metadata about what we are going to upload, including total size */);
	void OnUpload_dicom(sio::event &event);
	void StartUpload_7z();	
	void OnUpload_7z(sio::event &event);
	
	// server wants to send data to us
	void StartDownload_DICOM(sio::event &event);
	void OnDownload_dicom(sio::event &event);
	void StartDownload_7z(sio::event &event);
	void OnDownload_7z(sio::event &event);

	// query
	void StartQuery(/* some query*/);
	
	// initiate a dicom send
	void StartSendSCU();

protected:
	void OnShutdown(sio::event &event);
	void OnConnection(sio::event &event);

	std::function< void(void) > shutdownCallback;
};

#endif