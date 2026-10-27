@echo off

REM pushd ..
Vendor\Binaries\Premake\Windows\premake5.exe --file=Build.lua vs2022
REM popd
pause