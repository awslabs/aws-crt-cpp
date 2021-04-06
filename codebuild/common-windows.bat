
git submodule update --init
mkdir build
cd build

REM Building deps to %TEMP% to avoid max path length errors during build
cmake %* -DAWS_DEPS_BUILD_DIR="%TEMP%" -DCMAKE_BUILD_TYPE="Release" ../ || goto error
msbuild.exe aws-crt-cpp.vcxproj /p:Configuration=Release || goto error
msbuild.exe tests/aws-crt-cpp-tests.vcxproj /p:Configuration=Release
ctest --output-on-failure || goto error

goto :EOF

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%
