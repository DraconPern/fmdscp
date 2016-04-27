cmake_minimum_required(VERSION 2.8)

project(fmdscp)

set(VERSION "1.1.0")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

add_definitions(-DFMDSCP_VERSION="${VERSION}")

#VC++ stuff
#add_definitions(-D_WIN32_WINNT=0x0502)
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

find_package(openssl REQUIRED)

#include openjpeg
find_package(openjpeg HINTS ${OPENJPEG})
add_definitions(-DOPJ_STATIC)

#include fmjpeg2koj
find_package(fmjpeg2k REQUIRED HINTS ${FMJPEG2K})

#include fmjpeg2kjasper
find_package(fmjp2k REQUIRED HINTS ${FMJP2K})

#include mysql
find_package(MySQL REQUIRED)

#include poco
SET(poco_DIR ${POCO}/lib/cmake/Poco)
find_package(poco REQUIRED HINTS ${POCO})
add_definitions(-DPOCO_STATIC)
add_definitions(-DPOCO_NO_AUTOMATIC_LIBS)

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/Simple-Web-Server ${CMAKE_SOURCE_DIR}/Simple-WebSocket-Server ${CMAKE_SOURCE_DIR}/sqlite3 ${Boost_INCLUDE_DIRS} ${FMJPEG2K_INCLUDE_DIRS} ${FMJP2K_INCLUDE_DIRS}  ${DCMTK_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIR} ${POCO}/include ${ZLIB_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${SOCKETIO}/build/include)
LINK_DIRECTORIES(${FMJPEG2K}/lib ${FMJP2K}/lib ${JASPER}/lib ${Boost_LIBRARY_DIRS} ${OPENJPEG}/lib ${POCO}/lib ${MYSQL_LIB_DIR} ${SOCKETIO}/build/lib/${CMAKE_BUILD_TYPE})

set(fmdscp_HEADERS
	sqlite3/sqlite3.h
	src/myscp.h
	src/store.h
	src/find.h
	src/move.h
	src/model.h
	src/config.h
	src/util.h
	src/server.h
	src/ndcappender.h	
	src/alphanum.hpp
	src/senderservice.h
	Simple-Web-Server/server_http.hpp	
	src/httpserver.h
	src/cloudclient.h
	src/sender.h
)

set(fmdscp_SRCS
  ${fmdscp_HEADERS}
	src/main.cpp
	src/myscp.cpp
	src/store.cpp
	src/find.cpp
	src/move.cpp
	src/config.cpp
	src/util.cpp
	src/server.cpp
	src/ndcappender.cpp
	sqlite3/sqlite3.c
	sqlite3/sqlite3_exec_stmt.c	
	src/senderservice.cpp	
	src/httpserver.cpp
	src/cloudclient.cpp
	src/sender.cpp
)

if(WIN32)
#  set(fmdscp_SRCS ${fmdscp_SRCS} src/win/dovo.rc)
	add_definitions(-D_WIN32_WINNT=0x0501)
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
	${FMJPEG2K_LIBRARIES}
	${OPENJPEG_LIBRARIES}
	${FMJP2K_LIBRARIES}
	jasper
	${ZLIB_LIBRARIES}
	${DCMTK_LIBRARIES}
	${Boost_LIBRARIES}
	${Poco_LIBRARIES}	
	debug PocoDatad optimized PocoData
  #debug PocoDataODBCd optimized PocoDataODBC
  #debug PocoDataSQLited optimized PocoDataSQLite
  debug PocoDataMySQLd optimized PocoDataMySQL
  debug PocoFoundationd optimized PocoFoundation
  debug PocoJSONd optimized PocoJSON
  debug PocoNetd optimized PocoNet
  debug PocoUtild optimized PocoUtil
  debug PocoXMLd optimized PocoXML
  ${MYSQL_LIBRARIES}
# ${OPENSSL_LIBRARIES}
	${OS_SPECIFIC_LIBS}
	sioclient
	sioclient_tls
	)
