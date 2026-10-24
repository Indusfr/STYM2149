# Strict warnings for the project's code (not for third_party).
if(MSVC)
  set(STYM_WARNING_OPTIONS /W4 /permissive- /utf-8)
else()
  set(STYM_WARNING_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

function(stym_warnings target)
  target_compile_options(${target} PRIVATE ${STYM_WARNING_OPTIONS})
endfunction()

# The source code contains UTF-8 comments: MSVC must know it everywhere.
if(MSVC)
  add_compile_options(/utf-8)
endif()
