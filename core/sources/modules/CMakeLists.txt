add_library(mobagen_core_modules STATIC descriptor.cpp)
add_library(mobagen::core_modules ALIAS mobagen_core_modules)
target_include_directories(mobagen_core_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_modules PUBLIC cxx_std_20)
