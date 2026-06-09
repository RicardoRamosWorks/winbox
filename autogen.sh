#!/bin/sh
export LDFLAGS="-static-libgcc -static-libstdc++ -mwindows -Wl,--gc-sections,-s"
export CFLAGS="-D_WIN32_WINNT=0x0501 -D_WIN32_IE=0x0600 -DNTDDI_VERSION=0x05010000 -msse2 -march=pentium-m -mtune=pentium-m -O2 -pipe -fomit-frame-pointer -fno-strict-aliasing -mfpmath=sse -fno-unwind-tables -fno-asynchronous-unwind-tables -falign-functions=16 -falign-loops=16 -mpreferred-stack-boundary=4"
export CXXFLAGS="$CFLAGS"
export LIBS="-Wl,-Bstatic -lSDL -Wl,-Bdynamic -lwinmm -ldsound -ldxguid -lddraw -ldinput -luser32 -lgdi32"

echo "Generating build information using aclocal, autoheader, automake and autoconf"
echo "This may take a while ..."

# Regenerate configuration files.

aclocal
autoheader
automake --include-deps --add-missing --copy 
autoconf

echo "Now you are ready to run ./configure."
echo "You can also run  ./configure --help for extra features to enable/disable."

./configure --disable-debug --disable-dynrec --disable-fpu-x64 --disable-screenshots --disable-recording --disable-core-inline --disable-sdltest --disable-alsatest --enable-core-inline
make -j$(nproc)
mv ./src/dosbox.exe ./src/Winbox.exe