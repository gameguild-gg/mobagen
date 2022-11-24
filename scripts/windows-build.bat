cmake -H. -Bbuild -G "Visual Studio 16 2019" -T ClangCL -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_EDITOR=ON
cmake --build build --config MinSizeRel