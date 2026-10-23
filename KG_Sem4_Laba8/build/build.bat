@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d "%~dp0..\cmake-build-debug"
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
nmake
