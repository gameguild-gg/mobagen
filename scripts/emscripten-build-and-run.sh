#!/usr/bin/env bash

DIRECTORY="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

cd $DIRECTORY/../

rm -rf bin-emscripten

cd $DIRECTORY

./emscripten-build.sh

cd $DIRECTORY/../bin-emscripten/bin

open "http://localhost:8000"

python3 -m http.server
