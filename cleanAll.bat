@echo on

rmdir /S /Q Install
rmdir /S /Q Debug
rmdir /S /Q Release

del /F *.log *.wrn *.err

cd VirtIO
call clean.bat
cd ..

cd viogpudo
call clean.bat
cd ..
