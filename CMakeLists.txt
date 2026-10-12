cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(amoled_radio)

# Keep our board-specific I2S state fix outside managed dependencies.
idf_component_get_property(codec_lib espressif__esp_codec_dev COMPONENT_LIB)
get_target_property(codec_sources ${codec_lib} SOURCES)
list(FILTER codec_sources EXCLUDE REGEX "audio_codec_data_i2s\\.c$")
set_property(TARGET ${codec_lib} PROPERTY SOURCES "${codec_sources}")
target_sources(${codec_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/main/vendor/audio_codec_data_i2s.c")
