cmake_minimum_required(VERSION 2.8)

project(fmdscp)

set(VERSION "1.1.0")

SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

add_definitions(-DFMDSCP_VERSION="${VERSION}")

#VC++ stuff
add_definitions(-D_UNICODE)
add_definitions(-D_BIND_TO_CURRENT_VCLIBS_VERSION=1)

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     ON)
find_package(Boost REQUIRED thread filesystem system)

#enable dcmtk filename to have wide version, only affects WIN32.  http://utf8everywhere.org/
add_definitions(-DDCMTK_WIDE_CHAR_FILE_IO_FUNCTIONS=1)
find_package(dcmtk REQUIRED CONFIG HINTS ${DCMTK_DIR})

#include zlib
find_package(zlib REQUIRED ${ZLIB_ROOT})

#include openjpeg
find_package(openjpeg HINTS ${OPENJPEG})
add_definitions(-DOPJ_STATIC)

#include poco
#SET(poco_DIR ${POCO}/lib/cmake/Poco)
find_package(poco REQUIRED HINTS ${POCO})
add_definitions(-DPOCO_STATIC)
add_definitions(-DPOCO_NO_AUTOMATIC_LIBS)

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/sqlite3 ${Boost_INCLUDE_DIRS} ${FMJPEG2K}/include ${DCMTK_INCLUDE_DIRS} ${POCO}/include )
LINK_DIRECTORIES(${FMJPEG2K}/lib ${Boost_LIBRARY_DIRS} ${OPENJPEG}/lib ${POCO}/lib)

set(fmdscp_HEADERS
	sqlite3/sqlite3.h
)

set(fmdscp_SRCS
  ${fmdscp_HEADERS}
	src/main.cpp
	sqlite3/sqlite3.c
	sqlite3/sqlite3_exec_stmt.c
)

if(WIN32)
#  set(fmdscp_SRCS ${fmdscp_SRCS} src/win/dovo.rc)
endif(WIN32)

ADD_EXECUTABLE(fmdscp MACOSX_BUNDLE ${fmdscp_SRCS})

IF(WIN32)
  SET(OS_SPECIFIC_LIBS netapi32 wsock32 odbc32 Rpcrt4 Iphlpapi)
ELSE(WIN32)
  SET(OS_SPECIFIC_LIBS )
ENDIF(WIN32)

if(APPLE)
  set_target_properties(fmdscp PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/osx/Info.plist)
  set_target_properties(fmdscp PROPERTIES MACOSX_BUNDLE_ICON_FILE dovo)
	SET_SOURCE_FILES_PROPERTIES(${CMAKE_SOURCE_DIR}/src/osx/dovo.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
ENDIF()

TARGET_LINK_LIBRARIES(fmdscp
	fmjpeg2k

	${OPENJPEG_LIBRARIES}
	${ZLIB_LIBRARIES}
	${DCMTK_LIBRARIES}
	${Boost_LIBRARIES}
	${Poco_LIBRARIES}
	PocoDatad
PocoDataODBCd
PocoDataSQLited
PocoFoundationd
PocoJSONd
PocoMongoDBd
PocoNetd
PocoUtild
PocoXMLd
PocoZipd
	${OS_SPECIFIC_LIBS}
	)
