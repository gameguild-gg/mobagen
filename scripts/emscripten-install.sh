#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo $DIR

git clone https://github.com/emscripten-core/emsdk.git $DIR/../external/emsdk/

$DIR/../external/emsdk/emsdk install latest
$DIR/../external/emsdk/emsdk activate latest

read -t 5 -p "I am going to wait for 5 seconds only ..."
