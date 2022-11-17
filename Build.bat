set CurrDir=%CD%
for %%* in (.) do set CurrDirName=%%~nx*

IF EXIST %CurrDir%\bin   (rmdir bin /s /q)
IF EXIST %CurrDir%\build (rmdir build /s /q)
mkdir build 

cd build
cmake -G "Visual Studio 16 2019" ..\..\%CurrDirName% -DOPTION_BUILD_SAMPLE=ON -DOPTION_SAMPLE_ENABLE_GPU=ON

cd %CurrDir%