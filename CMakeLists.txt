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
set(VITA_VERSION  "01.31")
set(APP_VERSION   "1.3.1")      # the same release as VITA_VERSION, as the updater compares it with the GitHub tag

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -std=gnu11")

add_executable(${PROJECT_NAME} src/main.c)

# Development only: trace.txt, a file-based remote control and screenshots. Off in releases.
option(VITAXMB_DEBUG "Enable debug tracing and the remote-control file" OFF)
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${APP_VERSION}")
if(VITAXMB_DEBUG)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VITAXMB_DEBUG)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/vitashell_stub" "${CMAKE_SOURCE_DIR}/third_party/minimp3")
target_link_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/vitashell_stub")
target_include_directories(${PROJECT_NAME} PRIVATE "${VITASDK}/arm-vita-eabi/include/freetype2")

target_link_libraries(${PROJECT_NAME}
  vita2d
  mbedtls mbedx509 mbedcrypto harfbuzz freetype png jpeg z bz2 m c stdc++ pthread
  SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
  ScePgf_stub ScePvf_stub SceCommonDialog_stub
  SceAppMgr_stub SceRtc_stub ScePower_stub SceIofilemgr_stub
  SceKernelThreadMgr_stub SceProcessmgr_stub SceAudio_stub
  VitaShellUser_stub_weak taihen_stub SceVshBridge_stub SceLibKernel_stub
  SceNet_stub SceNetCtl_stub ScePromoterUtil_stub SceRegistryMgr_stub
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

# The updater: a small separate app (title VXMBUPDTR) that installs a downloaded update while VitaXMB is closed, since an app cannot be
# replaced while it runs. Its package is carried inside VitaXMB's own as updater.vpk and installed the first time it is needed.
add_executable(VitaXMBUpdater updater/main.c)
target_link_libraries(VitaXMBUpdater vita2d freetype png jpeg z bz2 m c SceAppMgr_stub ScePromoterUtil_stub SceSysmodule_stub SceIofilemgr_stub
  SceProcessmgr_stub SceKernelThreadMgr_stub SceLibKernel_stub SceDisplay_stub SceGxm_stub SceCtrl_stub ScePgf_stub ScePvf_stub SceCommonDialog_stub)
vita_create_self(VitaXMBUpdater.self VitaXMBUpdater UNSAFE)
vita_create_vpk(VitaXMBUpdater.vpk VXMBUPDTR VitaXMBUpdater.self
  VERSION 01.03
  NAME "VitaXMB Updater"
  FILE sce_sys/icon0.png sce_sys/icon0.png
)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  ${ASSET_ARGS}
  FILE ${CMAKE_BINARY_DIR}/VitaXMBUpdater.vpk updater.vpk
       sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

# the updater's package must exist before VitaXMB's, which carries it
if(${CMAKE_VERSION} VERSION_LESS "3.20.0")
  add_dependencies(${PROJECT_NAME}.vpk VitaXMBUpdater.vpk)
else()
  add_dependencies(${PROJECT_NAME}.vpk-vpk VitaXMBUpdater.vpk-vpk)
endif()
