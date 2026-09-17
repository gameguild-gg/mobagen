function(mobagen_write_license_disclaimer FILE_NAME PACKAGES)
  file(WRITE "${FILE_NAME}" "")
  set(PRINT_DELIMITER OFF)

  foreach(package IN LISTS PACKAGES)
    if(NOT DEFINED ${package}_SOURCE_DIR OR NOT IS_DIRECTORY "${${package}_SOURCE_DIR}")
      message(WARNING "license source directory is unavailable for package '${package}'")
      continue()
    endif()

    file(
      GLOB licenses
      LIST_DIRECTORIES false
      "${${package}_SOURCE_DIR}/LICENSE*" "${${package}_SOURCE_DIR}/LICENCE*"
      "${${package}_SOURCE_DIR}/COPYING*" "${${package}_SOURCE_DIR}/NOTICE*"
    )
    list(SORT licenses)
    list(LENGTH licenses LICENSE_COUNT)

    if(LICENSE_COUNT EQUAL 0)
      message(
        WARNING "no regular license file found for package '${package}' in ${${package}_SOURCE_DIR}"
      )
      continue()
    endif()

    if(PRINT_DELIMITER)
      file(APPEND "${FILE_NAME}" "\n-----\n")
    endif()

    list(GET licenses 0 license)
    file(READ "${license}" LICENSE_TEXT)
    file(APPEND "${FILE_NAME}"
         "The following software may be included in this product: ${package}. "
         "This software contains the following license and notice below:\n\n" "${LICENSE_TEXT}\n"
    )
    set(PRINT_DELIMITER ON)

    if(LICENSE_COUNT GREATER 1)
      message(
        WARNING
          "multiple license files found for package '${package}': ${licenses}. Only the first regular file will be used."
      )
    endif()
  endforeach()

  message(STATUS "Wrote licenses to ${FILE_NAME}")
endfunction()
