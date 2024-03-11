set CurrDir=%CD%
for %%* in (.) do set CurrDirName=%%~nx*

REM --------------------------------------------------------
REM Get Common folder content: works only for Git repo
REM --------------------------------------------------------
IF NOT EXIST %CurrDir%\sample\external (mkdir %CurrDir%\sample\external)

python %CurrDir%\fetch_dependencies.py

IF EXIST %CurrDir%\bin   (rmdir bin /s /q)
IF EXIST %CurrDir%\build (rmdir build /s /q)
mkdir build 

cd build
cmake -G "Visual Studio 17 2022" ..\..\%CurrDirName% -DOPTION_BUILD_SAMPLE=ON -DOPTION_SAMPLE_ENABLE_GPU=ON

cd %CurrDir%