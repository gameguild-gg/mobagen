cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
project(MobagenPluginSdkConsumer LANGUAGES C)

find_package(MobagenPluginSdk CONFIG REQUIRED)

add_library(ExternalMobagenPlugin MODULE external_plugin.c)
target_link_libraries(ExternalMobagenPlugin PRIVATE mobagen::plugin_sdk)
target_compile_definitions(ExternalMobagenPlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
set_target_properties(ExternalMobagenPlugin PROPERTIES PREFIX "")
