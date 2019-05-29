#!/bin/sh -e
#
# gcc110: CentOS 7.4 (as of 13.4.2018)

PROJECTDIR="$PWD"
cd ..

PREFIX=x86_64-linux-gnu
export INSTALLDIR="$PWD/$PREFIX"
export PKG_CONFIG_PATH=$INSTALLDIR/lib/pkgconfig:/usr/$PREFIX/lib/pkgconfig
export CPPFLAGS="-I$INSTALLDIR/include"
export LDFLAGS="-L$INSTALLDIR/lib"

# Install Libmicrohttpd from source
if [ ! -d libmicrohttpd-http2 ]; then
#  git clone --recursive https://gnunet.org/git/libmicrohttpd.git
  git clone --depth 1 https://github.com/maru/libmicrohttpd-http2.git && cd libmicrohttpd-http2/
#  cd libmicrohttpd
  cd libmicrohttpd-http2
else
  cd libmicrohttpd-http2
  git pull
fi
./bootstrap
./configure --enable-http2 --prefix=$INSTALLDIR --disable-doc --disable-examples --enable-shared
make clean
make -j$(nproc)
make install
cd ..

# Test Wget2
cd ${PROJECTDIR}
./configure --prefix=$INSTALLDIR --disable-doc
make -j$(nproc)
make -j$(nproc) check
