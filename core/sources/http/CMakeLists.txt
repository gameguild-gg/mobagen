add_library(mobagen_core_http INTERFACE)
add_library(mobagen::core_http ALIAS mobagen_core_http)
target_include_directories(mobagen_core_http INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_http INTERFACE cxx_std_20)

if(MOBAGEN_HTTP_BACKEND_CURL)
  add_library(mobagen_http_curl STATIC curl_client.cpp curl_client.hpp)
  add_library(mobagen::http_curl ALIAS mobagen_http_curl)
  target_include_directories(mobagen_http_curl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(mobagen_http_curl PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_http_curl
    PUBLIC mobagen::core_http
    PRIVATE ${MOBAGEN_CURL_TARGET}
  )
endif()
