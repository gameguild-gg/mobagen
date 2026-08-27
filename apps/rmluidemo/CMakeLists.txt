file(GLOB RMLUIDEMO_INC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

file(GLOB RMLUIDEMO_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

add_executable(rmluidemo ${RMLUIDEMO_SRC} ${RMLUIDEMO_INC})

target_include_directories(rmluidemo PUBLIC ${CORE_INC_DIR})
target_link_libraries(rmluidemo PUBLIC core)

# macOS/iOS app bundle Info.plist — use Xcode build-setting references so the bundle ID set via
# CMAKE_XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER is picked up.
if(APPLE)
  set_target_properties(rmluidemo PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "gg.gameguild.rmluidemo")
endif()
if(IOS)
  set_target_properties(
    rmluidemo PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/iOSInfo.plist.in"
  )
endif()
