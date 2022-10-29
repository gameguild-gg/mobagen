#!/usr/bin/env bash

DIRECTORY="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

mkdir -p "${DIRECTORY}"/../bin-emscripten/bin/

cd $DIRECTORY/../external/emsdk

source ./emsdk_env.sh

export EMSCRIPTEN=${EMSDK}/upstream/emscripten

cd $DIRECTORY/../

pwd

#cmake -DCMAKE_C_COMPILER=emcc -DCMAKE_CXX_COMPILER=em++ -DCMAKE_MAKE_PROGRAM=cmake -DCMAKE_C_ABI_COMPILED=ON -DCMAKE_CXX_ABI_COMPILED=ON -DCMAKE_CROSSCOMPILING=ON -DCMAKE_TOOLCHAIN_FILE=$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake -DCMAKE_STATIC_LIBRARY_SUFFIX=".bc" -DCMAKE_SHARED_LIBRARY_SUFFIX=".bc" -DEMSCRIPTEN=1 -DCMAKE_BUILD_TYPE=MinSizeRel -H. -Bbuild

cmake --version

emcmake cmake --version

emcmake cmake -DCMAKE_C_ABI_COMPILED=ON -DCMAKE_CXX_ABI_COMPILED=ON -DEMSCRIPTEN=1 -DCMAKE_BUILD_TYPE=MinSizeRel -H. -Bbin-emscripten

cmake --build bin-emscripten/ -j 20