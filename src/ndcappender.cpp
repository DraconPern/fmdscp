#include "ndcappender.h"
#include <fstream>
#include <iostream>

using namespace std;

NDCAsFilenameAppender::NDCAsFilenameAppender(const tstring& path)
{
	path_ = path.c_str();
}

NDCAsFilenameAppender::~NDCAsFilenameAppender()
{

}

void NDCAsFilenameAppender::close()
{

}

void NDCAsFilenameAppender::append(const spi::InternalLoggingEvent& event)
{
	// file open and output 
	boost::filesystem::path filename = path_;
	if(event.getNDC().length() != 0)
		filename /= (event.getNDC() + ".txt").c_str();
	else
		filename /= "_listener.txt";
	ofstream myfile;
	myfile.open(filename.c_str(), ios::app | ios::out);
	layout->formatAndAppend(myfile, event);    
}