SHELL := bash
.SHELLFLAGS := -lc

EMSDK ?= C:/Users/MatheusMartins/emsdk
EMSDK_NODE := $(EMSDK)/node/22.16.0_64bit/bin/node.exe
EMSDK_PYTHON := $(EMSDK)/python/3.13.3_64bit/python.exe
EMSCRIPTEN_TOOLCHAIN := $(EMSDK)/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
EMSCRIPTEN_ENV := export EMSDK="$(EMSDK)"; export EMSDK_NODE="$(EMSDK_NODE)"; export EMSDK_PYTHON="$(EMSDK_PYTHON)"; export PATH="$(EMSDK):$(EMSDK)/upstream/emscripten:$$PATH";
NATIVE_BUILD_ARGS ?= /m:1
PORT ?= 8085
DICOM_DIR ?= assets/dicom

.PHONY: help
help:
	@printf '%s\n' \
	  'mobagen build targets (Bash/Makefile workflow)' \
	  '' \
	  '  make configure-wasm-webgpu   Configure browser WebGPU/Dawn build' \
	  '  make wasm-webgpu             Build browser WebGPU/Dawn renderer' \
	  '  make serve-webgpu            Serve build/wasm-webgpu/bin on PORT=8085' \
	  '  make configure-wasm-webgl    Configure browser WebGL2 renderer' \
	  '  make wasm-webgl              Build browser WebGL2 renderer' \
	  '  make native-webgpu           Build native WebGPU/Dawn renderer' \
	  '  make native-dicom            Configure native renderer + GDCM DICOM loader' \
	  '  make core                    Build reusable core library modules only' \
	  '  make core-examples           Build core examples/tests/benches outside core' \
	  '  make dicom-smoke             Build/run volume_io_test against DICOM_DIR' \
	  '  make volume-buffer-test      Build/run CPU volume memory ownership test' \
	  '  make all-web                 Build both browser renderers'

.PHONY: configure-wasm-webgpu
configure-wasm-webgpu:
	$(EMSCRIPTEN_ENV) cmake -S . -B build/wasm-webgpu -G Ninja -DCMAKE_TOOLCHAIN_FILE="$(EMSCRIPTEN_TOOLCHAIN)" -DCMAKE_BUILD_TYPE=Release -DUSE_WEBGPU=ON

.PHONY: wasm-webgpu
wasm-webgpu: configure-wasm-webgpu
	$(EMSCRIPTEN_ENV) cmake --build build/wasm-webgpu --target dicom_renderer

.PHONY: serve-webgpu
serve-webgpu:
	cd build/wasm-webgpu/bin && python -m http.server "$(PORT)" --bind 127.0.0.1

.PHONY: configure-wasm-webgl
configure-wasm-webgl:
	$(EMSCRIPTEN_ENV) cmake -S . -B build/wasm-webgl -G Ninja -DCMAKE_TOOLCHAIN_FILE="$(EMSCRIPTEN_TOOLCHAIN)" -DCMAKE_BUILD_TYPE=Release -DUSE_WEBGPU=OFF

.PHONY: wasm-webgl
wasm-webgl: configure-wasm-webgl
	$(EMSCRIPTEN_ENV) cmake --build build/wasm-webgl --target dicom_renderer

.PHONY: all-web
all-web: wasm-webgpu wasm-webgl

.PHONY: configure-native-webgpu
configure-native-webgpu:
	cmake -S . -B build/native-webgpu -DUSE_WEBGPU=ON

.PHONY: native-webgpu
native-webgpu: configure-native-webgpu
	MSYS_NO_PATHCONV=1 cmake --build build/native-webgpu --target dicom_renderer --config Release -- $(NATIVE_BUILD_ARGS)

.PHONY: native-dicom
native-dicom:
	cmake -S . -B build/native-dicom -DUSE_WEBGPU=ON -DUSE_GDCM=ON
	MSYS_NO_PATHCONV=1 cmake --build build/native-dicom --target dicom_renderer --config Release -- $(NATIVE_BUILD_ARGS)

.PHONY: core
core:
	cmake -S . -B build/core -DBUILD_RENDERER=OFF -DBUILD_CORE=ON -DBUILD_CORE_EXAMPLES=OFF
	MSYS_NO_PATHCONV=1 cmake --build build/core --target mobagen_core --config Release -- $(NATIVE_BUILD_ARGS)

.PHONY: core-examples
core-examples:
	cmake -S . -B build/core-examples -DBUILD_RENDERER=OFF -DBUILD_CORE_EXAMPLES=ON
	MSYS_NO_PATHCONV=1 cmake --build build/core-examples --target mobagen_core_examples --config Release -- $(NATIVE_BUILD_ARGS)

.PHONY: dicom-smoke
dicom-smoke:
	cmake -S . -B build/native-dicom -DUSE_GDCM=ON
	cmake --build build/native-dicom --target volume_io_test --config Release
	./build/native-dicom/bin/Release/volume_io_test.exe "$(DICOM_DIR)"

.PHONY: volume-buffer-test
volume-buffer-test:
	cmake -S . -B build/native
	cmake --build build/native --target volume_buffer_test --config Release
	./build/native/bin/Release/volume_buffer_test.exe
