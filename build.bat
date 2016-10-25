IF "%1"=="Release" (
SET TYPE=Release
) ELSE (
SET TYPE=Debug
)

SET BUILD_DIR=%CD%
SET DEVSPACE=%CD%
SET CL=/MP

IF "%2"=="32" (
SET GENERATOR="Visual Studio 12"
SET OPENSSLFLAG=VC-WIN32
) ELSE (
SET GENERATOR="Visual Studio 12 Win64"
SET OPENSSLFLAG=VC-WIN64A
SET BOOSTADDRESSMODEL=address-model=64
)

cd %DEVSPACE%
git clone --branch=master --single-branch --depth=1 https://github.com/madler/zlib.git
cd zlib
git pull
mkdir build-%TYPE%
cd build-%TYPE%
cmake.exe .. -G %GENERATOR% -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /D NDEBUG" -DCMAKE_C_FLAGS_DEBUG="/D_DEBUG /MTd /Od" -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\zlib\%TYPE%
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%
IF "%TYPE%" == "Release" copy /Y %DEVSPACE%\zlib\Release\lib\zlibstatic.lib %DEVSPACE%\zlib\Release\lib\zlib_o.lib
IF "%TYPE%" == "Debug"   copy /Y %DEVSPACE%\zlib\Debug\lib\zlibstaticd.lib %DEVSPACE%\zlib\Debug\lib\zlib_d.lib

cd %DEVSPACE%
git clone https://github.com/DraconPern/libiconv-cmake.git
cd libiconv-cmake
mkdir build-%TYPE%
cd build-%TYPE%
cmake.exe .. -G %GENERATOR% -DBUILD_SHARED_LIBS=0 -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /D NDEBUG" -DCMAKE_C_FLAGS_DEBUG="/D_DEBUG /MTd /Od" -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\libiconv\%TYPE%
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%
IF "%TYPE%" == "Debug"   copy /Y %DEVSPACE%\libiconv\Debug\lib\libiconv.lib %DEVSPACE%\libiconv\Debug\lib\libiconv_d.lib
IF "%TYPE%" == "Release" copy /Y %DEVSPACE%\libiconv\Release\lib\libiconv.lib %DEVSPACE%\libiconv\Release\lib\libiconv_o.lib

cd %DEVSPACE%
git clone https://github.com/openssl/openssl.git --branch OpenSSL_1_0_2-stable --single-branch --depth 1
cd openssl
SET OLDPATH=%PATH%
rem SET PATH=C:\Perl\bin;%PATH%
IF "%TYPE%" == "Release" perl Configure -D_CRT_SECURE_NO_WARNINGS=1 no-asm --prefix=%DEVSPACE%\openssl\Release %OPENSSLFLAG%
IF "%TYPE%" == "Debug"   perl Configure -D_CRT_SECURE_NO_WARNINGS=1 no-asm --prefix=%DEVSPACE%\openssl\Debug debug-%OPENSSLFLAG%
IF "%2"=="32" (
call ms\do_ms.bat
) ELSE (
call ms\do_win64a.bat
)
nmake -f ms\nt.mak install
SET OPENSSL_ROOT_DIR=%DEVSPACE%\openssl\%TYPE%
SET PATH=%OLDPATH%

cd %DEVSPACE%
git clone git://git.dcmtk.org/dcmtk.git
cd dcmtk
git pull
git checkout -f 5371e1d84526e7544ab7e70fb47e3cdb4e9231b2
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DDCMTK_WIDE_CHAR_FILE_IO_FUNCTIONS=1 -DCMAKE_CXX_FLAGS_RELEASE="/Zi" -DDCMTK_WITH_ZLIB=1 -DWITH_ZLIBINC=%DEVSPACE%\zlib\%TYPE% -DDCMTK_WITH_ICONV=1 -DWITH_LIBICONVINC=%DEVSPACE%\libiconv\%TYPE% -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\dcmtk\%TYPE%
msbuild /maxcpucount:8 /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %DEVSPACE%
git clone --branch=openjpeg-2.1 --single-branch --depth 1 https://github.com/uclouvain/openjpeg.git
cd openjpeg
git pull
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DBUILD_THIRDPARTY=1 -DBUILD_SHARED_LIBS=0 -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /D NDEBUG" -DCMAKE_C_FLAGS_DEBUG="/D_DEBUG /MTd /Od" -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\openjpeg\%TYPE%
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %DEVSPACE%
git clone --branch=master https://github.com/DraconPern/fmjpeg2koj.git
cd fmjpeg2koj
git pull
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DBUILD_SHARED_LIBS=OFF -DBUILD_THIRDPARTY=ON -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /D NDEBUG" -DCMAKE_CXX_FLAGS_DEBUG="/D_DEBUG /MTd /Od" -DOPENJPEG=%DEVSPACE%\openjpeg\%TYPE% -DDCMTK_DIR=%DEVSPACE%\dcmtk\%TYPE% -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\fmjpeg2koj\%TYPE%
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %DEVSPACE%
if NOT EXIST boost_1_61_0.zip wget -c http://downloads.sourceforge.net/project/boost/boost/1.61.0/boost_1_61_0.zip
if NOT EXIST boost_1_61_0 unzip -q -o boost_1_61_0.zip
cd boost_1_61_0
call bootstrap
SET COMMONb2Flag=toolset=msvc-12.0 %BOOSTADDRESSMODEL% asmflags=\safeseh runtime-link=static define=_BIND_TO_CURRENT_VCLIBS_VERSION=1 -j 4 stage
SET BOOSTmodules=--with-atomic --with-thread --with-filesystem --with-system --with-date_time --with-regex --with-context --with-coroutine --with-chrono --with-random
IF "%TYPE%" == "Release" b2 %COMMONb2Flag% %BOOSTmodules% release
IF "%TYPE%" == "Debug"   b2 %COMMONb2Flag% %BOOSTmodules% debug

cd %DEVSPACE%
wget -c https://dev.mysql.com/get/Downloads/Connector-C/mysql-connector-c-6.1.6-src.zip
unzip -n mysql-connector-c-6.1.6-src.zip
cd mysql-connector-c-6.1.6-src
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DCMAKE_CXX_FLAGS_RELEASE="/Zi /MT /O2 /D NDEBUG" -DCMAKE_CXX_FLAGS_DEBUG="/D_DEBUG /MTd /Od /Zi" -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\mysql-connector-c-6.1.6-src\%TYPE%
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%
SET MYSQL_DIR=%DEVSPACE%\mysql-connector-c-6.1.6-src\%TYPE%

cd %DEVSPACE%
git clone --single-branch --depth=1 https://github.com/awslabs/aws-sdk-cpp.git
cd aws-sdk-cpp
git pull
mkdir build-%TYPE%
cd build-%TYPE%
SET AWSMODULE="s3;transfer"
cmake .. -G %GENERATOR% -DFORCE_SHARED_CRT=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_ONLY=%AWSMODULE% -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\aws-sdk-cpp\%TYPE%
msbuild /maxcpucount:8 /p:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %DEVSPACE%
git clone https://github.com/pocoproject/poco.git --branch poco-1.6.1 --single-branch --depth=1
cd poco
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DPOCO_STATIC=ON -DENABLE_NETSSL=OFF -DENABLE_CRYPTO=OFF -DCMAKE_CXX_FLAGS_RELEASE="/Zi /MT /O2 /D NDEBUG" -DCMAKE_CXX_FLAGS_DEBUG="/D_DEBUG /MTd /Od /Zi" -DMYSQL_LIB=%DEVSPACE%\mysql-connector-c-6.1.6-src\%TYPE%\lib\mysqlclient -DCMAKE_INSTALL_PREFIX=%DEVSPACE%\poco\%TYPE%
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %DEVSPACE%
git clone --recurse-submodules --depth=1 https://github.com/socketio/socket.io-client-cpp.git
cd socket.io-client-cpp
powershell "gci . CMakeLists.txt | ForEach { (Get-Content $_ | ForEach {$_ -replace 'Boost_USE_STATIC_RUNTIME OFF', 'Boost_USE_STATIC_RUNTIME ON'}) | Set-Content $_ }"
REM remove problematic line
powershell "gci . CMakeLists.txt | ForEach { (Get-Content $_ | ForEach {$_ -replace 'install\(FILES \${Boost_LIBRARIES\}', 'install(FILES ${Boost_LIBRARIES_DISABLE}'}) | Set-Content $_ }"
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DCMAKE_BUILD_TYPE=%TYPE% -DBOOST_ROOT=%DEVSPACE%\boost_1_61_0 -DBOOST_VER="" -DCMAKE_CXX_FLAGS_RELEASE="/Zi /MT /O2 /D NDEBUG" -DCMAKE_CXX_FLAGS_DEBUG="/D_DEBUG /MTd /Od"
msbuild /P:Configuration=%TYPE% INSTALL.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %DEVSPACE%
git clone https://github.com/eidheim/Simple-Web-Server.git
cd Simple-Web-Server
git pull

cd %BUILD_DIR%
mkdir build-%TYPE%
cd build-%TYPE%
cmake .. -G %GENERATOR% -DCMAKE_BUILD_TYPE=%TYPE% -DBOOST_ROOT=%DEVSPACE%\boost_1_61_0 -DDCMTK_DIR=%DEVSPACE%\dcmtk\%TYPE% -DZLIB_ROOT=%DEVSPACE%\zlib\%TYPE% -DFMJPEG2K=%DEVSPACE%\fmjpeg2koj\%TYPE% -DOPENJPEG=%DEVSPACE%\openjpeg\%TYPE% -DPOCO=%DEVSPACE%\poco\%TYPE% -DSOCKETIO=%DEVSPACE%\socket.io-client-cpp -DAWS=%DEVSPACE%\aws-sdk-cpp\%TYPE% -DVLD="C:\Program Files (x86)\Visual Leak Detector"
msbuild /P:Configuration=%TYPE% ALL_BUILD.vcxproj
if ERRORLEVEL 1 exit /B %ERRORLEVEL%

cd %BUILD_DIR%
