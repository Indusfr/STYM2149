# STYM2149 tests (fidelity thresholds S1-S11, DECISIONS.md and docs/m0/AUDIT_DSP.md §5).
include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# Analysis tools and reference tunes.
add_library(stym_testsupport STATIC
  support/Dsp.cpp
  support/Metrics.cpp
  support/RefData.cpp
  support/Scenarios.cpp)
target_include_directories(stym_testsupport PUBLIC support)
target_link_libraries(stym_testsupport PUBLIC stym_tools_common)
target_compile_definitions(stym_testsupport PRIVATE STYM_REFDATA_DEFAULT="${STYM_REFDATA}")
stym_warnings(stym_testsupport)

# Unit tests: S4-S8, S10, YM reading, LZH decompression.
add_executable(stym_unit
  unit/test_registers.cpp
  unit/test_envelope.cpp
  unit/test_noise.cpp
  unit/test_frequency.cpp
  unit/test_resampler.cpp
  unit/test_renderer.cpp
  unit/test_ymfile.cpp
  unit/test_lzh.cpp
  unit/test_smf.cpp
  unit/test_midi_files.cpp
  unit/test_wav_zip.cpp
  unit/test_audio_export.cpp
  unit/test_register_export.cpp
  unit/test_ym_writer.cpp
  unit/test_sndh_writer.cpp
  unit/test_sndh.cpp
  unit/test_xm.cpp
  unit/test_xm_import.cpp
  unit/test_mfp.cpp
  unit/test_board.cpp
  unit/test_virtualchannel.cpp
  unit/test_kit.cpp
  unit/test_patches.cpp
  unit/test_project_files.cpp
  unit/test_midilearn.cpp
  unit/test_instruments.cpp
  unit/test_engine.cpp
  unit/test_engine_m3.cpp
  unit/test_engine_expression.cpp
  unit/test_plugincore.cpp
  unit/test_ym_player.cpp
  unit/test_proto_engine.cpp
  unit/test_proto_seq.cpp
  unit/test_engine_seq.cpp
  unit/test_engine_meters.cpp
  unit/test_engine_host.cpp
  unit/test_engine_instruments.cpp
  unit/test_engine_sequences.cpp
  unit/test_engine_lfo.cpp
  unit/test_engine_delay.cpp
  unit/test_engine_echo.cpp
  unit/test_engine_cmd.cpp
  unit/test_engine_ticks.cpp
  unit/test_engine_timerwave.cpp
  unit/test_engine_pitch.cpp
  unit/test_engine_buzzer.cpp
  unit/test_engine_dma.cpp
  unit/test_engine_recorder.cpp
  unit/test_engine_song.cpp
  unit/test_song_state.cpp
  unit/test_recorder.cpp
  unit/test_ticks_state.cpp
  unit/test_timerwave_state.cpp
  unit/test_buzzer_state.cpp
  unit/test_dma_state.cpp
  unit/test_digidrum_editor.cpp
  unit/test_sequences_state.cpp
  unit/test_lfo_state.cpp
  unit/test_echo_state.cpp
  unit/test_ymseq.cpp
  unit/test_ymseq_edit.cpp)
target_link_libraries(stym_unit PRIVATE ymio ymengine ymseq stym_plugincore stym_testsupport Catch2::Catch2WithMain)
target_compile_definitions(stym_unit PRIVATE STYM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stym_warnings(stym_unit)
add_test(NAME unit COMMAND stym_unit)
set_tests_properties(unit PROPERTIES TIMEOUT 600)

# GEM interface (M4 to M6): text, alerts, menus, PNG, floating windows, scroll bars, and visual regression against
# the captures of the prototype.
add_executable(stym_ui
  gemui/test_arp_window.cpp
  gemui/test_curve_window.cpp
  gemui/test_kbd_window.cpp
  gemui/test_gemui.cpp
  gemui/test_help_window.cpp
  gemui/test_menu_columns.cpp
  gemui/test_confirm.cpp
  gemui/test_main_tracks.cpp
  gemui/test_main_controls.cpp
  gemui/test_seq_grid.cpp
  gemui/test_seq_keys.cpp
  gemui/test_mixer_window.cpp
  gemui/test_kit_window.cpp
  gemui/test_midi_learn.cpp
  gemui/test_mod_window.cpp
  gemui/test_patches_window.cpp
  gemui/test_file_menu.cpp
  gemui/test_midi_menu.cpp
  gemui/test_xm_menu.cpp
  gemui/test_audio_menu.cpp
  gemui/test_register_menu.cpp
  gemui/test_settings_window.cpp
  gemui/test_sound_engine_menu.cpp
  gemui/test_tooltip_menu.cpp
  gemui/test_instruments_window.cpp
  gemui/test_seq_window.cpp
  gemui/test_seq_cmd.cpp
  gemui/test_step_sound.cpp
  gemui/test_seq_daw12.cpp
  gemui/test_seq_ticks.cpp
  gemui/test_sequences_panel.cpp
  gemui/test_lfo_panel.cpp
  gemui/test_song_window.cpp
  gemui/test_timer_wave.cpp
  gemui/test_buzzer_frame.cpp
  gemui/test_dma_window.cpp
  gemui/test_recorder_window.cpp
  gemui/test_digidrum_window.cpp
  gemui/test_echo_window.cpp
  gemui/test_desktop.cpp
  gemui/test_main_rows.cpp
  gemui/test_visual.cpp
  gemui/test_windows.cpp
  gemui/test_ym_window.cpp)
target_link_libraries(stym_ui PRIVATE gemui stym_gemshot_scenes stym_tools_common Catch2::Catch2WithMain)
target_compile_definitions(stym_ui PRIVATE STYM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
stym_warnings(stym_ui)
# A MainWindow weighs nearly 400 KB and some tests build two in a row (window reopened at its place): the 1 MB
# stack of MSVC overflows (M7), so it gets the 8 MB of Linux. The plugin creates its window on the heap.
if(MSVC)
  target_link_options(stym_ui PRIVATE /STACK:8388608)
endif()
add_test(NAME ui COMMAND stym_ui)

# S11: real time (replaced allocation operators: separate executable).
option(STYM_RTSAN "S11 with RealtimeSanitizer (clang >= 20)" OFF)
add_executable(stym_realtime realtime/test_realtime.cpp)
target_link_libraries(stym_realtime PRIVATE ymio ymengine stym_plugincore stym_testsupport)
stym_warnings(stym_realtime)
if(STYM_RTSAN)
  target_compile_definitions(stym_realtime PRIVATE STYM_RTSAN=1)
  target_compile_options(stym_realtime PRIVATE -fsanitize=realtime)
  target_link_options(stym_realtime PRIVATE -fsanitize=realtime)
else()
  target_compile_definitions(stym_realtime PRIVATE STYM_RTSAN=0)
endif()
add_test(NAME realtime COMMAND stym_realtime)

# AtariAudio 1.26 oracle (S1, S2, S9, LZH, SNDH): original sources compiled as they are (machine: with the
# observer of the chip writes, see third_party/AtariAudio/PROVENANCE.md).
add_executable(stym_oracle
  oracle/ZeroingNew.cpp
  oracle/OracleCommon.cpp
  oracle/test_oracle_chip.cpp
  oracle/test_oracle_files.cpp
  oracle/test_oracle_lzh.cpp
  oracle/test_oracle_mfp.cpp
  oracle/test_oracle_sndh.cpp)
target_include_directories(stym_oracle PRIVATE oracle)
target_link_libraries(stym_oracle PRIVATE ymio ymengine stym_testsupport stym_atariaudio Catch2::Catch2WithMain)
stym_warnings(stym_oracle)
add_test(NAME oracle COMMAND stym_oracle "~[S9]")
add_test(NAME oracle_s9 COMMAND stym_oracle "[S9]")
set_tests_properties(oracle oracle_s9 PROPERTIES TIMEOUT 1800)

# S3: secondary oracle ym2149-rs (Rust crate built by cargo, tests only).
if(STYM_ORACLE_CRATE)
  find_program(STYM_CARGO cargo REQUIRED)
  find_package(Threads REQUIRED)
  set(YMFFI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/oracle_crate/ymffi")
  set(YMFFI_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/ymffi-target")
  if(WIN32)
    set(YMFFI_LIB "${YMFFI_TARGET_DIR}/release/ymffi.lib")
  else()
    set(YMFFI_LIB "${YMFFI_TARGET_DIR}/release/libymffi.a")
  endif()
  add_custom_command(OUTPUT "${YMFFI_LIB}"
    COMMAND "${STYM_CARGO}" build --release --locked --manifest-path "${YMFFI_DIR}/Cargo.toml" --target-dir "${YMFFI_TARGET_DIR}"
    DEPENDS "${YMFFI_DIR}/Cargo.toml" "${YMFFI_DIR}/Cargo.lock" "${YMFFI_DIR}/src/lib.rs"
    COMMENT "cargo build ymffi (ym2149-rs 0.9.1)"
    VERBATIM)
  add_custom_target(ymffi_build DEPENDS "${YMFFI_LIB}")
  add_library(ymffi STATIC IMPORTED)
  set_target_properties(ymffi PROPERTIES IMPORTED_LOCATION "${YMFFI_LIB}")
  add_dependencies(ymffi ymffi_build)

  add_executable(stym_oracle_crate oracle_crate/test_crate.cpp)
  target_include_directories(stym_oracle_crate PRIVATE oracle_crate)
  target_link_libraries(stym_oracle_crate PRIVATE ymcore stym_testsupport ymffi Catch2::Catch2WithMain Threads::Threads ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(stym_oracle_crate PRIVATE ws2_32 userenv ntdll bcrypt)
  endif()
  stym_warnings(stym_oracle_crate)
  add_test(NAME oracle_crate COMMAND stym_oracle_crate)
endif()

# Plugin processor (M3) and editor (M4) with JUCE: shared code of the formats, replaced allocation operators.
if(STYM_BUILD_PLUGIN)
  add_executable(stym_plugin_tests plugin/test_processor.cpp plugin/test_editor.cpp plugin/test_stress.cpp
    plugin/test_tooltips.cpp plugin/test_juce_painter.cpp)
  # JUCE headers and definitions of the shared code (its modules are linked into it privately, as JUCE recommends).
  target_include_directories(stym_plugin_tests SYSTEM PRIVATE $<TARGET_PROPERTY:STYM2149,INCLUDE_DIRECTORIES>)
  target_include_directories(stym_plugin_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/plugin")
  target_compile_definitions(stym_plugin_tests PRIVATE $<TARGET_PROPERTY:STYM2149,COMPILE_DEFINITIONS>
                             STYM_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
                             STYM_LANGUAGES_DIR="${PROJECT_SOURCE_DIR}/languages")
  target_link_libraries(stym_plugin_tests PRIVATE STYM2149 stym_plugincore Catch2::Catch2WithMain)
  stym_warnings(stym_plugin_tests)
  add_test(NAME plugin COMMAND stym_plugin_tests)
  set_tests_properties(plugin PROPERTIES TIMEOUT 600)
endif()
