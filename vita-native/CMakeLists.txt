cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to the native VitaSDK directory")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE FILEPATH "VitaSDK toolchain")
endif()
project(robot_unicorn_native LANGUAGES C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(VITA_TITLEID "RUA103001" CACHE STRING "Vita application ID")
set(VITA_VERSION "01.00" CACHE STRING "Vita application version")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

foreach(asset data/original.rup data/game.mp3 data/opening.mp3 src/original_data.hpp sce_sys/icon0.png)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
    message(FATAL_ERROR "Missing ${asset}. Run tools/import_original.py against your local original Android assets first.")
  endif()
endforeach()

add_executable(robot_unicorn
  src/main.cpp src/assets.cpp src/audio.cpp src/save.cpp src/game.cpp src/presentation.cpp)
target_compile_options(robot_unicorn PRIVATE -O2 -Wall -Wextra -Wpedantic
  -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp -ffunction-sections -fdata-sections
  -fno-fast-math -ffp-contract=off)
target_link_options(robot_unicorn PRIVATE -Wl,-q -Wl,--pic-veneer -Wl,--gc-sections
  "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/robot_unicorn.map")
target_link_libraries(robot_unicorn PRIVATE -Wl,--start-group
  vita2d mpg123 z m stdc++
  SceGxm_stub SceDisplay_stub SceSysmodule_stub ScePgf_stub SceAppMgr_stub SceCommonDialog_stub
  SceCtrl_stub SceTouch_stub SceAudio_stub ScePower_stub SceIofilemgr_stub
  SceProcessmgr_stub SceKernelThreadMgr_stub SceKernelModulemgr_stub
  SceSysmem_stub -Wl,--end-group)

vita_create_self(eboot.bin robot_unicorn UNSAFE)
vita_create_vpk(robot-unicorn-native.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION} NAME "Robot Unicorn Attack"
  FILE "${CMAKE_CURRENT_SOURCE_DIR}/data/original.rup" data/original.rup
       "${CMAKE_CURRENT_SOURCE_DIR}/data/game.mp3" data/game.mp3
       "${CMAKE_CURRENT_SOURCE_DIR}/data/opening.mp3" data/opening.mp3
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png" sce_sys/icon0.png
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png
       "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml
       "${CMAKE_CURRENT_SOURCE_DIR}/../README.md" README.md)
