@echo off
if "%1"=="" goto usage

FOR /R %%d IN (%1) DO 7zippo %%d
goto exit

:usage
echo batch.bat: convert a set of archives to 7z format one by one
echo examples:
echo   batch *.zip : convert all the .zip files to .7z
echo   batch *.rar : convert all the .rar files to .7z

:exit
