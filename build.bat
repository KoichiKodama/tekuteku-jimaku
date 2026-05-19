@echo off
setlocal
set CXXFLAGS= -nologo -MD -Ox -EHsc -std:c++17
set CXXINCLUDES= -I. -IC:\mnt\disk_z\usr\boost\boost_1_84_0
set CXXDEFS= -DNDEBUG
set LDFLAGS= -link -subsystem:windows -entry:mainCRTStartup
set LIBS= -libpath:C:\mnt\disk_z\usr\boost\boost_1_84_0/stage/lib
cl %CXXFLAGS% %CXXINCLUDES% %CXXDEFS% tekuteku-jimaku.cpp %LDFLAGS% %LIBS%
mt -manifest utf-8.manifest -outputresource:tekuteku-jimaku.exe;#1
del tekuteku-jimaku.obj
endlocal
exit /b
