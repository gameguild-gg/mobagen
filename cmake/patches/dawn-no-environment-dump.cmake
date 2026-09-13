function(mobagen_patch_dawn_environment_dump dawn_source_dir)
  set(patch_target "${dawn_source_dir}/third_party/CopyWindowsSDKDLL.cmake")
  if(NOT EXISTS "${patch_target}")
    message(FATAL_ERROR "Dawn patch target does not exist: ${patch_target}")
  endif()

  set(vulnerable_block_lf
      [=[    message(STATUS "Display environment variables:")
    execute_process(COMMAND ${CMAKE_COMMAND} -E environment COMMAND_ECHO STDOUT)

]=]
  )
  set(patch_marker_lf
      "    # Mobagen: upstream environment dump removed to protect configure-time secrets.\n"
  )
  string(REPLACE "\n" "\r\n" vulnerable_block_crlf "${vulnerable_block_lf}")
  string(REPLACE "\n" "\r\n" patch_marker_crlf "${patch_marker_lf}")

  file(READ "${patch_target}" dawn_copy_script)
  set(vulnerable_block "${vulnerable_block_lf}")
  set(patch_marker "${patch_marker_lf}")
  string(FIND "${dawn_copy_script}" "${vulnerable_block}" vulnerable_position)
  if(vulnerable_position EQUAL -1)
    set(vulnerable_block "${vulnerable_block_crlf}")
    set(patch_marker "${patch_marker_crlf}")
    string(FIND "${dawn_copy_script}" "${vulnerable_block}" vulnerable_position)
  endif()
  if(NOT vulnerable_position EQUAL -1)
    string(REPLACE "${vulnerable_block}" "${patch_marker}" patched_copy_script
                   "${dawn_copy_script}"
    )
    file(WRITE "${patch_target}" "${patched_copy_script}")
    return()
  endif()

  string(FIND "${dawn_copy_script}"
              "Mobagen: upstream environment dump removed to protect configure-time secrets."
              marker_position
  )
  string(FIND "${dawn_copy_script}" "Display environment variables:" status_position)
  string(FIND "${dawn_copy_script}" "-E environment COMMAND_ECHO STDOUT" command_position)
  if(marker_position EQUAL -1
     OR NOT status_position EQUAL -1
     OR NOT command_position EQUAL -1
  )
    message(
      FATAL_ERROR
        "Dawn CopyWindowsSDKDLL.cmake does not match the audited source; review the pinned Dawn release before configuring"
    )
  endif()
endfunction()
