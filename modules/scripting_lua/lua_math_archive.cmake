# mobagen_lua_math_archive.cmake — build the import-free math/ctype/time
# support archive for the Lua guest from the pinned wasi-sdk sysroot
# (dynamic-loading-all-platforms todo 17).
#
# WHY: wasm32 has NO transcendental instructions — clang lowers
# __builtin_sin/pow/log/... to libc CALLS, so the QuickJS shim's
# `__builtin_*` math definitions would compile to self-recursive calls.
# The wasi-sysroot libc.a carries musl's f64 libm as plain C
# (import-free); extracting the needed members gives correct math with
# zero WASI imports. Members resolve lazily, so the archive only costs
# size for what the guest actually references.
#
# Input variables (script mode):
#   MOBAGEN_LUA_WASI_SYSROOT — sysroot prefix (external/wasi-sdk-34/share/wasi-sysroot)
#   MOBAGEN_LUA_MATH_ARCHIVE — output archive path
#
# Member list notes:
#   - math-builtins.c.obj = ceil/fabs/floor/rint/sqrt/trunc/copysign as
#     raw wasm instructions (zero calls).
#   - snprintf/vsnprintf/vfprintf + fwrite/fputs + __towrite + strnlen:
#     Lua's l_sprintf routes through snprintf (string.format, %p, %.14g
#     number formatting). The REAL fwrite.c.obj must be included — its
#     __fwritex drives vfprintf's buffered writes into snprintf's
#     in-memory sink (sn_write); a drop-sink there would swallow ALL
#     formatted output. __lockfile/__unlockfile are SHIM definitions
#     (libc's pull futex/pthread machinery into a single-threaded guest).
#     The static stdout vtable still references fd_close/fd_seek
#     (unreachable from our exports) — wasm-opt strips those imports
#     POST_BUILD, exactly like the QuickJS guest.
#   - strtod needs __floatscan + __shlim (shgetc.c.obj exports both).
#   - strtof/strtof_l deliberately NOT included (Lua never calls them);
#     their objects reference __strtod_parser which drags in more state.
#   - gmtime/localtime/mktime/strftime/difftime/ctype/locale are
#     pure computation — no WASI imports.
#   - malloc/stdio syscall members are NEVER included: those import
#     (fd_write/sbrk/...) and the shim provides them.
if(NOT DEFINED MOBAGEN_LUA_WASI_SYSROOT OR NOT DEFINED MOBAGEN_LUA_MATH_ARCHIVE)
  message(FATAL_ERROR "mobagen_lua_math_archive.cmake: MOBAGEN_LUA_WASI_SYSROOT and MOBAGEN_LUA_MATH_ARCHIVE are required")
endif()

set(MOBAGEN_LUA_MATH_LIBC "${MOBAGEN_LUA_WASI_SYSROOT}/lib/wasm32-wasip1/libc.a")
if(NOT EXISTS "${MOBAGEN_LUA_MATH_LIBC}")
  message(FATAL_ERROR "mobagen_lua_math_archive.cmake: wasi-sysroot libc.a not found at ${MOBAGEN_LUA_MATH_LIBC}")
endif()

set(MOBAGEN_LUA_MATH_MEMBERS
    math-builtins
    fmin-fmax
    exp exp_data
    log log_data
    pow pow_data
    sin cos __sin __cos __tan sincos
    __rem_pio2 __rem_pio2_large
    __math_oflow __math_uflow __math_invalid __math_divzero __math_xflow
    asin acos atan atan2 tan
    log2 log2_data log10
    exp2 exp2f_data expm1 log1p
    cbrt hypot sinh cosh tanh asinh acosh atanh
    modf frexp ldexp scalbn lround lrint round finite logb significand fmod
    difftime gmtime gmtime_r mktime strftime
    isalnum isalpha iscntrl isdigit islower isprint ispunct isspace isupper isxdigit
    tolower toupper
    localeconv setlocale
    strtod floatscan shgetc __uflow __toread
    snprintf vsnprintf vfprintf fprintf
    strnlen strcoll wctomb wcrtomb __lctrans locale_map
    fwrite fputs __towrite stdin stdout stderr
    fclose feof ferror fread fflush __stdout_write ofl ofl_add
    libc)

# The clang that drives the guest build lives next to llvm-ar; derive the
# hint from the sysroot layout (sysroot = <wasi-sdk>/share/wasi-sysroot).
get_filename_component(MOBAGEN_LUA_WASI_SDK_DIR "${MOBAGEN_LUA_WASI_SYSROOT}" DIRECTORY)
get_filename_component(MOBAGEN_LUA_WASI_SDK_DIR "${MOBAGEN_LUA_WASI_SDK_DIR}" DIRECTORY)
find_program(MOBAGEN_LUA_LLVM_AR NAMES llvm-ar HINTS "${MOBAGEN_LUA_WASI_SDK_DIR}/bin" NO_CACHE)
if(NOT MOBAGEN_LUA_LLVM_AR)
  find_program(MOBAGEN_LUA_LLVM_AR NAMES llvm-ar NO_CACHE)
endif()
if(NOT MOBAGEN_LUA_LLVM_AR)
  message(FATAL_ERROR "mobagen_lua_math_archive.cmake: llvm-ar not found (wasi-sdk bin expected)")
endif()

set(MOBAGEN_LUA_MATH_STAMP "${MOBAGEN_LUA_MATH_ARCHIVE}.stamp")
if(EXISTS "${MOBAGEN_LUA_MATH_STAMP}" AND EXISTS "${MOBAGEN_LUA_MATH_ARCHIVE}")
  # Idempotent: the sysroot is content-pinned by scripts/toolchains.py.
  return()
endif()

file(REMOVE_RECURSE "${MOBAGEN_LUA_MATH_ARCHIVE}.extract")
file(MAKE_DIRECTORY "${MOBAGEN_LUA_MATH_ARCHIVE}.extract")
execute_process(
  COMMAND ${MOBAGEN_LUA_LLVM_AR} x "${MOBAGEN_LUA_MATH_LIBC}"
  WORKING_DIRECTORY "${MOBAGEN_LUA_MATH_ARCHIVE}.extract"
  RESULT_VARIABLE MOBAGEN_LUA_AR_RESULT
)
if(NOT MOBAGEN_LUA_AR_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${MOBAGEN_LUA_MATH_ARCHIVE}.extract")
  message(FATAL_ERROR "mobagen_lua_math_archive.cmake: llvm-ar extraction failed (${MOBAGEN_LUA_AR_RESULT})")
endif()

set(MOBAGEN_LUA_MATH_FOUND "")
set(MOBAGEN_LUA_MATH_MISSING "")
foreach(member ${MOBAGEN_LUA_MATH_MEMBERS})
  set(obj "${MOBAGEN_LUA_MATH_ARCHIVE}.extract/${member}.c.obj")
  if(EXISTS "${obj}")
    list(APPEND MOBAGEN_LUA_MATH_FOUND "${obj}")
  else()
    list(APPEND MOBAGEN_LUA_MATH_MISSING "${member}")
  endif()
endforeach()
if(MOBAGEN_LUA_MATH_MISSING)
  message(FATAL_ERROR "mobagen_lua_math_archive.cmake: sysroot libc.a lacks expected members: ${MOBAGEN_LUA_MATH_MISSING}")
endif()

execute_process(
  COMMAND ${MOBAGEN_LUA_LLVM_AR} rc "${MOBAGEN_LUA_MATH_ARCHIVE}" ${MOBAGEN_LUA_MATH_FOUND}
  RESULT_VARIABLE MOBAGEN_LUA_AR_RESULT
)
if(NOT MOBAGEN_LUA_AR_RESULT EQUAL 0)
  file(REMOVE_RECURSE "${MOBAGEN_LUA_MATH_ARCHIVE}.extract")
  message(FATAL_ERROR "mobagen_lua_math_archive.cmake: llvm-ar archive creation failed (${MOBAGEN_LUA_AR_RESULT})")
endif()
file(REMOVE_RECURSE "${MOBAGEN_LUA_MATH_ARCHIVE}.extract")
file(WRITE "${MOBAGEN_LUA_MATH_STAMP}" "generated by mobagen_lua_math_archive.cmake\n")
message(STATUS "Lua guest math archive: ${MOBAGEN_LUA_MATH_ARCHIVE}")
