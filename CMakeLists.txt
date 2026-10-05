cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(VitaXMB C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaXMB")
set(VITA_TITLEID  "VXMB00001")
set(VITA_VERSION  "00.10")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -std=gnu11")

add_executable(${PROJECT_NAME} src/main.c)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/vitashell_stub" "${CMAKE_SOURCE_DIR}/third_party/minimp3")
target_link_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/vitashell_stub")
target_include_directories(${PROJECT_NAME} PRIVATE "${VITASDK}/arm-vita-eabi/include/freetype2")

target_link_libraries(${PROJECT_NAME}
  vita2d
  harfbuzz freetype png jpeg z bz2 m c stdc++ pthread
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
  ScePgf_stub ScePvf_stub SceCommonDialog_stub
  SceAppMgr_stub SceRtc_stub ScePower_stub SceIofilemgr_stub
  SceKernelThreadMgr_stub SceProcessmgr_stub SceAudio_stub
  VitaShellUser_stub_weak taihen_stub SceVshBridge_stub SceLibKernel_stub
  SceNet_stub SceRegistryMgr_stub
)

# Bundle everything under assets/ at the same relative path inside app0:
file(GLOB_RECURSE ASSET_FILES RELATIVE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/assets/*")
set(ASSET_ARGS "")
foreach(f ${ASSET_FILES})
  list(APPEND ASSET_ARGS FILE ${f} ${f})
endforeach()

# Same auth ID VitaShell uses: without it the system refuses sceAppMgrLaunchAppByUri
# (error 0x80802026), so no game, system app or settings page could be launched.
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2808000000000000")
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${ASSET_ARGS}
  FILE sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
