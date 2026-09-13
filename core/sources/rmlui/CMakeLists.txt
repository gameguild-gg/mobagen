# rmlui module — RmlUi GUI layer for the core app host (ImGui-bridge rendering).
#
# RmlUiLayer (app::GuiLayerView): boots RmlUi with the SDL platform interface
# and the embedded debugger fonts, and bridges its geometry into ImGui's
# background draw lists (via the composed ImGuiLayer), so hosted apps render
# HTML/CSS UI through the host's existing WebGPU pass. rmlui_all (core +
# debugger + SDL3 platform backend) and IMGUI (the bridge emits into ImGui
# draw lists directly) are linked PRIVATE — external includes stay inside
# this module and the public header stays toolkit-free. The parent
# core/CMakeLists.txt only adds this subdirectory when the optional rmlui_all
# target exists, so non-rmlui builds never see it.

add_library(mobagen_core_rmlui STATIC rmlui_layer.cpp)
add_library(mobagen::core_rmlui ALIAS mobagen_core_rmlui)
# Include root is core/sources so consumers include "rmlui/rmlui_layer.hpp".
target_include_directories(mobagen_core_rmlui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_rmlui PUBLIC cxx_std_23)
target_link_libraries(mobagen_core_rmlui PRIVATE mobagen::core_app mobagen::core_imgui IMGUI rmlui_all)
