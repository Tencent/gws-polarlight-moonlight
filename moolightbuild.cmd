rem build Gleam.........

set BUILD_ROOT=%1
set BUILD_FOLDER=%BUILD_ROOT%/moonlight-qt.pro
set BUILD_GUI=%BUILD_ROOT%\app\gui
set BUILD_DLL=%BUILD_ROOT%\libs\windows\lib\x64
set BUILD_NAME=Gleam

set BUILD_QT=D:\Qt6.7\6.7.1\msvc2019_64\bin
set BUILD_QMAKE=%BUILD_QT%/qmake.exe
set BUILD_QWINDEPLOYQT=%BUILD_QT%/windeployqt.exe

set BUILD_QJOM=D:\Qt6.7\Tools\QtCreator\bin\jom\jom.exe

IF NOT EXIST package (
    mkdir package
)

cd package

IF EXIST %BUILD_NAME% (
    rem "join path"
)else (
    mkdir %BUILD_NAME%
)

cd %BUILD_NAME% 

IF EXIST C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat (
    CALL "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
)

%BUILD_QMAKE% %BUILD_FOLDER% -spec win32-msvc "CONFIG+=qtquickcompiler" && %BUILD_QJOM% qmake_all
D:\Qt6.7\Tools\QtCreator\bin\jom\jom.exe


%BUILD_QWINDEPLOYQT% app\release\Gleam.exe --qmldir %BUILD_GUI%

IF EXIST %BUILD_DLL% (
    rem copy dll to release
    copy %BUILD_DLL%\*.dll app\release
) 
IF EXIST AntiHooking\release\AntiHooking.dll (
    copy AntiHooking\release\AntiHooking.dll app\release
)