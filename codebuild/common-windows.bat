
mkdir build
cd build
cmake %* -DBUILD_DEPS=ON -DCMAKE_BUILD_TYPE="Release" ../ || goto error
msbuild.exe aws-crt-cpp.vcxproj /p:Configuration=Release || goto error
msbuild.exe tests/aws-crt-cpp-tests.vcxproj /p:Configuration=Release
ctest -V || goto error

goto :EOF

:error
echo Failed with error #%errorlevel%.
exit /b %errorlevel%
