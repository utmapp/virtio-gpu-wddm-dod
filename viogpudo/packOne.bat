@echo off
: Param1 - Win10 | Win8
setlocal
: Param2 - x86|x64|arm64
: Param3 - sys name
 
if "%2"=="x64" set %%2=amd64

if /i "%1"=="Win10" goto :checkarch
if /i "%1"=="Win8" goto :checkarch

goto :printerr
:checkarch
if /i "%2"=="x86" goto :makeinstall
if /i "%2"=="x64" goto :makeinstall
if /i "%2"=="arm64" goto :makeinstall
:printerr
echo wrong parameters (1)%1 (2)%2 (3)%3
pause
goto :eof

:makeinstall
set INST_OS=%1
set INST_ARC=%2
set SYS_NAME=%3

if /i "%INST_ARC%"=="x64" goto :set_x64
if /i "%INST_ARC%"=="arm64" goto :set_arm64

set INST_EXT=i386
set VC_ARG=x86
goto :startcopy

:set_x64
set INST_ARC=amd64
set INST_EXT=amd64
set VC_ARG=amd64
goto :startcopy

:set_arm64
set INST_ARC=arm64
set INST_EXT=arm64
set VC_ARG=amd64_arm64

:startcopy
set SYS_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%INST_EXT%\%SYS_NAME%.sys
set PDB_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%INST_EXT%\%SYS_NAME%.pdb
set INF_PATH_AND_NAME=objfre_%INST_OS%_%INST_ARC%\%INST_EXT%\%SYS_NAME%.inf
set DVL_PATH_AND_NAME=%SYS_NAME%.DVL.xml

rem echo makeinstall %1 %2 %3
mkdir ..\Install\%INST_OS%\%INST_ARC%
del /Q ..\Install\%INST_OS%\%INST_ARC%\%FILE_NAME%.*
copy /Y %SYS_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%
copy /Y %PDB_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%
copy /Y %INF_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%\%SYS_NAME%.inf

:create_cat
echo "Setting OS mask for:" %1 %2

if /i "%1"=="Win10" goto create_win10
if /i "%1"=="Win8" goto create_win8
goto error_inf2cat

:create_win8
if not exist %DVL_PATH_AND_NAME% goto do_the_job
if /i "%2"=="x64" copy /Y %DVL_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%\
:do_the_job
if /i "%2"=="x86" set _OSMASK_=8_X86
if /i "%2"=="x64" set _OSMASK_=8_X64,Server8_X64
goto run_inf2cat

:create_win10
setlocal
if not exist %DVL_PATH_AND_NAME% goto do_the_job
if /i "%2"=="x64" copy /Y %DVL_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%\
if /i "%2"=="arm64" copy /Y %DVL_PATH_AND_NAME% ..\Install\%INST_OS%\%INST_ARC%\
:do_the_job
if /i "%2"=="x86" set _OSMASK_=10_X86
if /i "%2"=="x64" set _OSMASK_=10_X64,Server10_X64
if /i "%2"=="arm64" set _OSMASK_=10_RS3_ARM64,Server10_ARM64
goto run_inf2cat

:error_inf2cat 
echo "Error setting OS mask for inf2cat"
goto after_inf2cat

:run_inf2cat
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" %VC_ARG%
inf2cat /driver:..\Install\%INST_OS%\%INST_ARC% /os:%_OSMASK_%
endlocal

:after_inf2cat
