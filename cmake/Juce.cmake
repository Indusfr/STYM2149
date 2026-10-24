# JUCE 8.0.15 (AGPLv3, licence choice D-001) and clap-juce-extensions (MIT), pinned (D-059, THIRD_PARTY.md).
# Local sources possible without download: -DFETCHCONTENT_SOURCE_DIR_JUCE=... and
# -DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=... (the commit is checked in every case).
include(FetchContent)

set(STYM_JUCE_TAG 8.0.15)
set(STYM_JUCE_COMMIT 91ad83ae34a81e0833b1a2b0866f54846370ae53)
set(STYM_CLAP_JUCE_COMMIT 7adee3a1bd4684d4caa5601100e364abccff4b4d)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG ${STYM_JUCE_TAG}
  GIT_SHALLOW TRUE)
FetchContent_Declare(clap_juce_extensions
  GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
  GIT_TAG ${STYM_CLAP_JUCE_COMMIT}
  GIT_SUBMODULES_RECURSE TRUE)

FetchContent_MakeAvailable(JUCE)

function(stym_check_commit dir expected what)
  find_package(Git QUIET REQUIRED)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${dir}"
                  OUTPUT_VARIABLE got OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0 OR NOT got STREQUAL expected)
    message(FATAL_ERROR "${what}: commit ${got} in ${dir}, ${expected} expected")
  endif()
endfunction()
stym_check_commit("${juce_SOURCE_DIR}" ${STYM_JUCE_COMMIT} "JUCE ${STYM_JUCE_TAG}")

FetchContent_MakeAvailable(clap_juce_extensions)
stym_check_commit("${clap_juce_extensions_SOURCE_DIR}" ${STYM_CLAP_JUCE_COMMIT} "clap-juce-extensions")
