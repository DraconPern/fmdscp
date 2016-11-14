#!/bin/bash
set -xe

if [ "$1" == "Release" ]
then
  TYPE=Release
else
  TYPE=Debug
fi

BUILD_DIR=`pwd`
DEVSPACE=`pwd`
unamestr=`uname`

if [ "$unamestr" == 'Darwin' ] ; then
cd $DEVSPACE
[[ -d openssl ]] || git clone https://github.com/openssl/openssl.git --branch OpenSSL_1_0_2-stable --single-branch --depth 1
cd openssl
git pull
./Configure darwin64-x86_64-cc  --prefix=$DEVSPACE/openssl/$TYPE --openssldir=$DEVSPACE/openssl/$TYPE/openssl no-shared
make install
export OPENSSL_ROOT_DIR=$DEVSPACE/openssl/$TYPE
fi

if [ "$unamestr" == 'Linux' ] ; then
cd $DEVSPACE
[[ -d libiconv ]] || git clone git://git.savannah.gnu.org/libiconv.git
cd libiconv
alias autoconf='autoconf-2.69'
alias aclocal='aclocal-1.15'
alias automake='automake-1.15'
./autogen.sh
mkdir -p build-$TYPE
cd build-$TYPE
../configure --prefix=$DEVSPACE/libiconv/$TYPE --enable-static=yes --enable-shared=no
make install-lib
fi

cd $DEVSPACE
[[ -d dcmtk ]] || git clone git://git.dcmtk.org/dcmtk.git
cd dcmtk
git checkout -f 5371e1d84526e7544ab7e70fb47e3cdb4e9231b2
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DCMAKE_BUILD_TYPE=$TYPE -DDCMTK_WIDE_CHAR_FILE_IO_FUNCTIONS=1 -DDCMTK_WITH_TIFF=OFF -DDCMTK_WITH_PNG=OFF -DDCMTK_WITH_OPENSSL=ON -DWITH_OPENSSLINC=$DEVSPACE/openssl/$TYPE -DDCMTK_WITH_XML=OFF -DDCMTK_WITH_ZLIB=ON -DDCMTK_WITH_SNDFILE=OFF -DDCMTK_WITH_ICONV=ON -DWITH_ICONVINC=$DEVSPACE/libiconv/$TYPE -DDCMTK_WITH_WRAP=OFF -DCMAKE_INSTALL_PREFIX=$DEVSPACE/dcmtk/$TYPE
make -j8 install

cd $DEVSPACE
[[ -d openjpeg ]] || git clone --branch=openjpeg-2.1 https://github.com/uclouvain/openjpeg.git
cd openjpeg
git pull
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=$TYPE -DBUILD_THIRDPARTY=ON -DCMAKE_INSTALL_PREFIX=$DEVSPACE/openjpeg/$TYPE
make -j8 install

cd $DEVSPACE
[[ -d fmjpeg2koj ]] || git clone --branch=master https://github.com/DraconPern/fmjpeg2koj.git
cd fmjpeg2koj
git pull
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=$TYPE -DOPENJPEG=$DEVSPACE/openjpeg/$TYPE -DDCMTK_DIR=$DEVSPACE/dcmtk/$TYPE -DCMAKE_INSTALL_PREFIX=$DEVSPACE/fmjpeg2koj/$TYPE
make -j8 install

cd $DEVSPACE
[[ -f boost_1_61_0.zip ]] || wget -c http://downloads.sourceforge.net/project/boost/boost/1.61.0/boost_1_61_0.zip
unzip -n boost_1_61_0.zip
cd boost_1_61_0
./bootstrap.sh
COMMONb2Flag="-j 4 link=static runtime-link=static stage"
BOOSTModule="--with-locale --with-thread --with-filesystem --with-system --with-date_time --with-regex --with-random"
if [ "$TYPE" = "Release" ] ; then
  ./b2 $COMMONb2Flag $BOOSTModule variant=release
elif [ "$TYPE" = "Debug" ] ; then
  ./b2 $COMMONb2Flag $BOOSTModule variant=debug
fi

cd $DEVSPACE
wget -c https://dev.mysql.com/get/Downloads/Connector-C/mysql-connector-c-6.1.6-src.zip
unzip -n mysql-connector-c-6.1.6-src.zip
cd mysql-connector-c-6.1.6-src
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=$TYPE -DCMAKE_INSTALL_PREFIX=$DEVSPACE/mysql-connector-c-6.1.6-src/$TYPE
make -j8 install
MYSQL_DIR=$DEVSPACE/mysql-connector-c-6.1.6-src/$TYPE

cd $DEVSPACE
[[ -d aws-sdk-cpp ]] || git clone https://github.com/awslabs/aws-sdk-cpp.git
cd aws-sdk-cpp
git pull
mkdir -p build-$TYPE
cd build-$TYPE
AWSMODULE="s3;transfer;dynamodb"
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=$TYPE -DBUILD_ONLY=$AWSMODULE -DCMAKE_INSTALL_PREFIX=$DEVSPACE/aws-sdk-cpp/$TYPE
make -j8 install

cd $DEVSPACE
[[ -d poco ]] || git clone https://github.com/pocoproject/poco.git --branch poco-1.6.1 --single-branch --depth=1
cd poco
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=$TYPE -DPOCO_STATIC=ON -DENABLE_NETSSL=OFF -DENABLE_CRYPTO=OFF -DMYSQL_LIB=$DEVSPACE/mysql-connector-c-6.1.6-src/$TYPE/lib/mysqlclient -DCMAKE_INSTALL_PREFIX=$DEVSPACE/poco/$TYPE
make -j8 install


cd $DEVSPACE
[[ -d socket.io-client-cpp ]] || git clone --recurse-submodules --depth=1 https://github.com/socketio/socket.io-client-cpp.git
cd socket.io-client-cpp
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DCMAKE_BUILD_TYPE=$TYPE -DCMAKE_BUILD_TYPE=$TYPE -DBOOST_ROOT=$DEVSPACE/boost_1_61_0 -DBOOST_VER=""
make -j8 install

cd $DEVSPACE
[[ -d Simple-Web-Server ]] || git clone https://github.com/eidheim/Simple-Web-Server.git
cd Simple-Web-Server
git pull

cd $BUILD_DIR
mkdir -p build-$TYPE
cd build-$TYPE
cmake .. -DCMAKE_BUILD_TYPE=$TYPE -DBOOST_ROOT=$DEVSPACE/boost_1_61_0 -DDCMTK_DIR=$DEVSPACE/dcmtk/$TYPE -DFMJPEG2K=$DEVSPACE/fmjpeg2koj/$TYPE -DOPENJPEG=$DEVSPACE/openjpeg/$TYPE -DPOCO=$DEVSPACE/poco/$TYPE -DSOCKETIO=$DEVSPACE/socket.io-client-cpp -DAWS=$DEVSPACE/aws-sdk-cpp/$TYPE
make -j8
