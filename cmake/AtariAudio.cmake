# AtariAudio 1.26 (third_party/AtariAudio, MIT licence; only change: the observer of the chip writes of AtariMachine,
# see PROVENANCE.md).
set(STYM_ATARIAUDIO_DIR "${CMAKE_SOURCE_DIR}/third_party/AtariAudio/src")

# Header of the tables (envelope shapes, measured mixing table), used by ymcore.
add_library(stym_atariaudio_tables INTERFACE)
target_include_directories(stym_atariaudio_tables SYSTEM INTERFACE "${STYM_ATARIAUDIO_DIR}")

function(stym_atariaudio_options target)
  target_include_directories(${target} SYSTEM PUBLIC "${STYM_ATARIAUDIO_DIR}")
  target_compile_features(${target} PUBLIC cxx_std_20)
  # AtariAudio stops on assert(false) for YM6 effect codes it ignores in Release (YmRenderer.cpp, l. 530) and for
  # the GEMDOS or XBIOS calls it does not emulate (AtariMachine.cpp): it must behave as in Release whatever the
  # configuration.
  target_compile_definitions(${target} PRIVATE NDEBUG)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W0)
  else()
    target_compile_options(${target} PRIVATE -w)
  endif()
endfunction()

# AtariAudio's Atari machine (Musashi 68000, MFP 68901, STE DAC, blitter, chip): reading of SNDH files by ymio
# (M7, D-159), and oracle of the tests. Musashi is AtariAudio's version, one instance per machine (thread safe).
add_library(stym_atariaudio_machine STATIC
  "${STYM_ATARIAUDIO_DIR}/AtariMachine.cpp"
  "${STYM_ATARIAUDIO_DIR}/Blitter.cpp"
  "${STYM_ATARIAUDIO_DIR}/Mk68901.cpp"
  "${STYM_ATARIAUDIO_DIR}/SteDac.cpp"
  "${STYM_ATARIAUDIO_DIR}/ym2149c.cpp"
  "${STYM_ATARIAUDIO_DIR}/external/Musashi/m68k.cpp"
  "${STYM_ATARIAUDIO_DIR}/external/Musashi/m68kcpu.cpp"
  "${STYM_ATARIAUDIO_DIR}/external/Musashi/m68kops.cpp")
stym_atariaudio_options(stym_atariaudio_machine)
set_target_properties(stym_atariaudio_machine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Rest of AtariAudio, compiled as is: oracle of the tests (S1, S2, S9, LZH, SNDH). Never linked into the plugin. Its
# LZH decompressor (H. Okumura, K. F. Medina, no formal licence) only serves the oracle: the product uses the port
# of pylzh (src/ymio/src/Lzh.cpp, D-043). The same goes for its ICE decompressor and its SNDH duration database
# (timedb): the product has its own bounded ICE decompressor (src/ymio/src/Ice.cpp).
if(STYM_BUILD_TESTS)
  add_library(stym_atariaudio STATIC
    "${STYM_ATARIAUDIO_DIR}/AtariAudioRenderer.cpp"
    "${STYM_ATARIAUDIO_DIR}/SndhRenderer.cpp"
    "${STYM_ATARIAUDIO_DIR}/YmRenderer.cpp"
    "${STYM_ATARIAUDIO_DIR}/timedb.cpp"
    "${STYM_ATARIAUDIO_DIR}/external/lzh.cpp"
    "${STYM_ATARIAUDIO_DIR}/external/ice_24.cpp")
  stym_atariaudio_options(stym_atariaudio)
  target_link_libraries(stym_atariaudio PUBLIC stym_atariaudio_machine)
endif()
