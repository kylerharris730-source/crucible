@echo off
REM ==========================================================================
REM build_deps.bat -- the third-party libraries online play is built on.
REM
REM   libdatachannel  WebRTC data channels (the browser build's transport,
REM                   natively), with its own libjuice and usrsctp
REM   mbedTLS         the encryption WebRTC requires (DTLS)
REM
REM Fetched at PINNED tags and built as static libraries into third_party\,
REM which is ignored by git: the sources are other people's projects, and
REM a few hundred megabytes of them do not belong in this repository's
REM history. build.bat runs this by itself when the libraries are missing,
REM so a fresh checkout needs nothing done by hand beyond having git, CMake
REM and a MinGW-w64 GCC 7 or newer (see scripts\toolchain.bat).
REM
REM Changing a tag or a patch here means deleting third_party\ so everything
REM is rebuilt; nothing here notices a change on its own.
REM ==========================================================================
setlocal
pushd "%~dp0.."

set LDC_TAG=v0.24.5
set MBED_TAG=v3.6.7

set T=%CD%\third_party
if not exist "%T%\src" mkdir "%T%\src"

call scripts\toolchain.bat || goto fail
where cmake >nul 2>nul || (echo build_deps: CMake is not on PATH & goto fail)
where git   >nul 2>nul || (echo build_deps: git is not on PATH & goto fail)

REM Ninja when there is one (WinLibs ships it); MinGW Makefiles otherwise,
REM which the Chocolatey MinGW used by CI provides.
set GEN=MinGW Makefiles
where ninja >nul 2>nul && set GEN=Ninja

if not exist "%T%\src\mbedtls\CMakeLists.txt" (
    git clone -q --depth 1 --branch %MBED_TAG% https://github.com/Mbed-TLS/mbedtls.git "%T%\src\mbedtls" || goto fail
    git -C "%T%\src\mbedtls" submodule update --init --depth 1 framework || goto fail
)
if not exist "%T%\src\libdatachannel\CMakeLists.txt" (
    git clone -q --depth 1 --branch %LDC_TAG% https://github.com/paullouisageneau/libdatachannel.git "%T%\src\libdatachannel" || goto fail
    git -C "%T%\src\libdatachannel" submodule update --init --depth 1 deps/plog deps/usrsctp deps/libjuice || goto fail
)

REM Local fixes to libdatachannel, in scripts\patches. Each is applied unless it
REM already is, so running this again over an existing checkout is harmless.
REM   libdatachannel-buffered-amount-race.patch -- a data race in its own
REM     shutdown: closing a connection replaces a callback that another thread
REM     may be calling at that moment. Found while chasing a crash on close
REM     (which turned out to be the compiler -- see scripts\toolchain.bat), and
REM     kept because the race is real on its own. Not fixed upstream as of
REM     v0.24.5 / master in September 2026.
for %%p in ("%CD%\scripts\patches\libdatachannel-*.patch") do (
    git -C "%T%\src\libdatachannel" apply --reverse --check "%%~p" >nul 2>nul || (
        git -C "%T%\src\libdatachannel" apply "%%~p" || goto fail
    )
)

REM mbedTLS configuration, as defines given to BOTH builds rather than by
REM editing mbedTLS's config header: they change the layout of mbedTLS's
REM structures, so the two must agree, and a flag on the command line is
REM visible here instead of hidden in a patched file.
REM
REM   MBEDTLS_SSL_DTLS_SRTP  off by default; libdatachannel calls into it even
REM                          with media disabled, and will not compile without.
REM   MBEDTLS_THREADING_*    off by default, and without them mbedTLS's shared
REM                          state is unlocked -- but libdatachannel drives it
REM                          from several threads, one connection per guest.
REM                          The result was memory corruption: crashes a few
REM                          seconds after a guest joined or left, in whatever
REM                          code next touched the damage. libdatachannel's own
REM                          CI uses Homebrew's mbedTLS, which has these on.
set MBED_DEFS=-DMBEDTLS_SSL_DTLS_SRTP -DMBEDTLS_THREADING_C -DMBEDTLS_THREADING_PTHREAD

cmake -S "%T%\src\mbedtls" -B "%T%\obj\mbedtls" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_FLAGS=%MBED_DEFS%" "-DCMAKE_INSTALL_PREFIX=%T%\install" ^
    -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF ^
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DMBEDTLS_FATAL_WARNINGS=OFF >"%T%\mbedtls.log" 2>&1 || goto faillog
cmake --build "%T%\obj\mbedtls" -j 8 >>"%T%\mbedtls.log" 2>&1 || goto faillog
cmake --install "%T%\obj\mbedtls" >>"%T%\mbedtls.log" 2>&1 || goto faillog

cmake -S "%T%\src\libdatachannel" -B "%T%\obj\libdatachannel" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_C_FLAGS=%MBED_DEFS%" "-DCMAKE_CXX_FLAGS=%MBED_DEFS%" ^
    "-DCMAKE_PREFIX_PATH=%T%\install" "-DMbedTLS_ROOT=%T%\install" ^
    -DBUILD_SHARED_LIBS=OFF -DUSE_MBEDTLS=ON -DNO_MEDIA=ON -DNO_WEBSOCKET=ON ^
    -DNO_EXAMPLES=ON -DNO_TESTS=ON >"%T%\libdatachannel.log" 2>&1 || goto faillog
cmake --build "%T%\obj\libdatachannel" --target datachannel-static -j 8 >>"%T%\libdatachannel.log" 2>&1 || goto faillog

REM libdatachannel installs only its shared library, so the static archives
REM and the headers are copied into place by hand.
if not exist "%T%\install\include\rtc" mkdir "%T%\install\include\rtc"
copy /y "%T%\src\libdatachannel\include\rtc\*.h" "%T%\install\include\rtc\" >nul || goto fail
copy /y "%T%\obj\libdatachannel\libdatachannel-static.a" "%T%\install\lib\" >nul || goto fail
copy /y "%T%\obj\libdatachannel\deps\libjuice\libjuice-static.a" "%T%\install\lib\" >nul || goto fail
copy /y "%T%\obj\libdatachannel\deps\usrsctp\usrsctplib\libusrsctp.a" "%T%\install\lib\" >nul || goto fail

echo Built online-play libraries into third_party\install
popd
exit /b 0

:faillog
echo build_deps: a library failed to build -- see the logs in third_party\
:fail
popd
exit /b 1
