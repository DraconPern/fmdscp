# fmdscp
Windows [![Build Status](http://home.draconpern.com:8080/buildStatus/icon?job=fmdscp.debug)](http://home.draconpern.com:8080/job/fmdscp.debug)

Simple DICOM SCP

- Available on Windows
- Supports Unicode file and path.
- No dll's need to be distributed.
- Native, no Java required.

## Download
Source https://github.com/DraconPern/fmdscp

## Development notes
The program is http://utf8everywhere.org/

## Requirements
- CMake http://www.cmake.org/download/
- XCode on OS X
- Visual Studio 2013 or higher on Windows
- gcc on Linux

## Third party dependency
- poco http://pocoproject.org please extract under ./poco
- DCMTK http://dicom.offis.de/ please use snapshot or git, and extract under ./dcmtk
- boost http://www.boost.org/ please extract under ./boost
- Visual Leak Detector https://vld.codeplex.com/ installed for debug release
- zlib please extract under ./zlib
- openjpeg http://www.openjpeg.org please extract under ./openjpeg
- fmjpeg2koj https://github.com/DraconPern/fmjpeg2koj please extract under ./fmjpeg2koj
- aws SDK

## Author
Ing-Long Eric Kuo <draconpern@hotmail.com>

## License
This software is licensed under the GPL.
