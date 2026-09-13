# imgui module — Dear ImGui GUI layer for the core app host.
#
# ImGuiLayer (app::GuiLayerView): wraps the ImGui SDL3 platform backend +
# WebGPU renderer backend boot/frame/shutdown so hosted apps get ImGui by
# attaching one layer. The external IMGUI target (backends included, Dawn
# flavor) is linked PRIVATE — imgui includes stay inside this module and the
# public header stays imgui-free.

add_library(mobagen_core_imgui STATIC imgui_layer.cpp)
add_library(mobagen::core_imgui ALIAS mobagen_core_imgui)
# Include root is core/sources so consumers include "imgui/imgui_layer.hpp".
target_include_directories(mobagen_core_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_imgui PUBLIC cxx_std_23)
target_link_libraries(mobagen_core_imgui PRIVATE mobagen::core_app IMGUI)
