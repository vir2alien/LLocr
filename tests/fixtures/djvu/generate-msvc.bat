@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
rem Run from tests/fixtures/djvu. All build products stay in this directory.
set "DJVU=..\..\..\build\deps\djvulibre-3.5.30"
set "PREFIX=..\..\..\build\deps\djvulibre-msvc-x64"
cl /nologo /EHsc /MD /O2 /DHAVE_NAMESPACES /DWIN32 /D_CRT_SECURE_NO_WARNINGS /I"%DJVU%\libdjvu" "%DJVU%\tools\cpaldjvu.cpp" "%DJVU%\tools\jb2tune.cpp" "%DJVU%\tools\jb2cmp\classify.cpp" "%DJVU%\tools\jb2cmp\cuts.cpp" "%DJVU%\tools\jb2cmp\frames.cpp" "%DJVU%\tools\jb2cmp\patterns.cpp" /Fecpaldjvu.exe /link "%PREFIX%\lib\Release\libdjvulibre.lib"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /MD /O2 /DHAVE_NAMESPACES /DWIN32 /D_CRT_SECURE_NO_WARNINGS /I"%DJVU%\libdjvu" "%DJVU%\tools\djvm.cpp" /Fedjvm.exe /link "%PREFIX%\lib\Release\libdjvulibre.lib"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /MD /std:c++17 generate.cpp /Fegenerate.exe
if errorlevel 1 exit /b 1
set "PATH=%CD%;%CD%\%PREFIX%\bin\Release;%PATH%"
generate.exe
if errorlevel 1 exit /b 1
del /q cpaldjvu.exe djvm.exe generate.exe cpaldjvu.obj jb2tune.obj djvm.obj generate.obj classify.obj cuts.obj frames.obj patterns.obj
