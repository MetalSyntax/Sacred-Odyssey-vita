cmake_minimum_required(VERSION 3.14)

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(sacredodyssey C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# Vita application configuration
set(VITA_APP_NAME "Sacred Odyssey")
set(VITA_TITLEID "PSVSOTROA")
set(VITA_VPKNAME "sacredodyssey")
set(VITA_VERSION "01.00")
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")

# Optional: used for "send" and "dump" targets
set(PSVITAIP "192.168.3.15" CACHE STRING "PSVita IP (for FTP access)")
set(PARSECOREPATH "$ENV{PARSECOREPATH}" CACHE STRING "vita-parse-core main.py")

# Game-specific definitions
set(DATA_PATH "ux0:data/sacredodyssey/" CACHE STRING "Path to data (with trailing /)")
set(SO_PATH "${DATA_PATH}libsacredodyssey.so" CACHE STRING "Path to .so")
add_definitions(-DDATA_PATH="${DATA_PATH}" -DSO_PATH="${SO_PATH}")

option(USE_SCELIBC_IO "Use SceLibcBridge for IO and some other std functions" ON)
if (USE_SCELIBC_IO)
  add_definitions(-DUSE_SCELIBC_IO)
endif()

set(SHADER_FORMAT "GLSL" CACHE STRING "Preferred shader format (one of 'GLSL', 'CG', 'GXP')")
if (${SHADER_FORMAT} STREQUAL "GLSL")
  add_definitions(-DUSE_GLSL_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "CG")
  add_definitions(-DUSE_CG_SHADERS)
elseif (${SHADER_FORMAT} STREQUAL "GXP")
  add_definitions(-DUSE_GXP_SHADERS)
else()
  message(FATAL_ERROR "SHADER_FORMAT option can only be one of: 'GLSL', 'CG', 'GXP'")
endif()

set(valid_dump_formats GLSL CG)
option(DUMP_COMPILED_SHADERS "When using GLSL or CG shader format, cache compiled shaders on disk" OFF)
if (DUMP_COMPILED_SHADERS)
  if (SHADER_FORMAT IN_LIST valid_dump_formats)
    add_definitions(-DDUMP_COMPILED_SHADERS)
  else()
    message(WARNING "DUMP_COMPILED_SHADERS has no effect for the chosen shader format ${SHADER_FORMAT}")
  endif()
endif()

# Unique debug definition for loader's logging to not interfere with anything
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG_SOLOADER)
  # Uncomment this for verbose FalsoJNI logging:
  #add_definitions(-DFALSOJNI_DEBUGLEVEL=0)
endif()

# Space-separated flags passed to vitaGL's own Makefile (built from source,
# see the vitaGL_build target below -- not the vdpm prebuilt package).
# NO_SPLASHSCREEN=1 is required: this port depends on vitaGL not showing its
# own startup splashscreen (previously achieved by hand-patching the global
# VitaSDK install, which silently affected every other port sharing that SDK
# -- vendorizing vitaGL here removes that footgun). SOFTFP_ABI=1 matches this
# port's ABI (armeabi/soft-float, see PORTING_PLAN.md and the
# -mfloat-abi=softfp flag below), same as the sibling Gameloft "Glitch"-engine
# port Asphalt-5-Vita on this same ABI. HAVE_SHADER_CACHE=1: fast file cache
# (xxHash-checked) of compiled shaders -- safe (pure cache, no pipeline change)
# and directly attacks this port's slowest stall: every boot recompiles all of
# the engine's GLSL programs through ShaccCg inside a single nativeRender frame
# (the overlay bar visibly freezes for seconds). First boot after this change
# pays the cost once; later boots reuse the cache. Deliberately NOT enabling any
# *SPEEDHACK/NO_DEBUG-style flags (README VITAGL.md marks them all as potential
# crash/glitch sources) while the port is still stabilizing.
set(VITAGL_MAKE_FLAGS "SOFTFP_ABI=1 NO_SPLASHSCREEN=1 HAVE_SHADER_CACHE=1 NO_DEBUG=1 SAMPLERS_SPEEDHACK=1 CIRCULAR_POOL_SPEEDHACK=1 DRAW_SPEEDHACK=2 NO_TEX_COMBINER=1")

# makes sincos, sincosf, etc. visible
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O3 -g -ffast-math -mfloat-abi=softfp -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++20 -Wno-write-strings -Wno-psabi")

add_executable(${CMAKE_PROJECT_NAME}
               source/main.c
               source/dynlib.c
               source/java.c
               source/patch.c
               source/controls.c
               source/audio.c
               source/reimpl/bits/_ctype.c
               source/reimpl/errno.c
               source/reimpl/fmt.c
               source/reimpl/io.c
               source/reimpl/log.c
               source/reimpl/mem.c
               source/reimpl/pthr.c
               source/reimpl/sys.c
               source/reimpl/time64.c
               source/reimpl/asset_manager.cpp
               source/utils/dialog.c
               source/utils/embedded_shaders.c
               source/utils/glutil.c
               source/utils/init.c
               source/utils/logger.c
               source/utils/settings.c
               source/utils/utils.c
               lib/falso_jni/FalsoJNI.c
               lib/falso_jni/FalsoJNI_ImplBridge.c
               lib/falso_jni/FalsoJNI_Logger.c
               lib/sha1/sha1.c
               lib/fios/fios.c
               lib/so_util/so_util.c)

add_subdirectory(lib/libc_bridge)
add_dependencies(${CMAKE_PROJECT_NAME} SceLibcBridge)

# VitaGL built from lib/vitagl (git submodule) via its own Makefile, instead
# of linking the generic vdpm-installed prebuilt package -- so VITAGL_MAKE_FLAGS
# above (NO_SPLASHSCREEN=1 in particular) actually takes effect without
# mutating the global VitaSDK install shared by other ports.
separate_arguments(VITAGL_MAKE_ARGS UNIX_COMMAND "${VITAGL_MAKE_FLAGS}")

set(VITAGL_DIR ${CMAKE_SOURCE_DIR}/lib/vitagl)
set(VITAGL_LIB ${VITAGL_DIR}/libvitaGL.a)

# Force a full rebuild of vitaGL whenever VITAGL_MAKE_FLAGS change
set(VITAGL_FLAGS_STAMP ${CMAKE_BINARY_DIR}/vitagl_flags.stamp)
set(vitagl_old_flags "")
if (EXISTS ${VITAGL_FLAGS_STAMP})
  file(READ ${VITAGL_FLAGS_STAMP} vitagl_old_flags)
endif()
if (NOT vitagl_old_flags STREQUAL VITAGL_MAKE_FLAGS)
  file(GLOB_RECURSE vitagl_stale_objs ${VITAGL_DIR}/source/*.o)
  file(REMOVE ${vitagl_stale_objs} ${VITAGL_LIB})
  file(WRITE ${VITAGL_FLAGS_STAMP} "${VITAGL_MAKE_FLAGS}")
endif()

# Keep VitaGL builds deterministic and within the port's low-memory budget.
set(VITAGL_NPROC 1)

add_custom_target(vitaGL_build
                  COMMAND make -C ${VITAGL_DIR} -j${VITAGL_NPROC} ${VITAGL_MAKE_ARGS}
                  BYPRODUCTS ${VITAGL_LIB}
                  COMMENT "Building VitaGL"
                  VERBATIM)

add_library(vitaGL STATIC IMPORTED)
set_target_properties(vitaGL PROPERTIES
                      IMPORTED_LOCATION ${VITAGL_LIB}
                      INTERFACE_INCLUDE_DIRECTORIES ${VITAGL_DIR}/source)
add_dependencies(${CMAKE_PROJECT_NAME} vitaGL_build)

target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM
                           PUBLIC ${CMAKE_SOURCE_DIR}/lib
                           )

target_include_directories(${CMAKE_PROJECT_NAME}
                           PUBLIC ${CMAKE_SOURCE_DIR}/source
                           )

target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      stdc++
                      vitaGL
                      vitashark
                      SceShaccCgExt
                      taihen_stub
                      SceShaccCg_stub
                      z
                      m
                      ScePgf_stub
                      mathneon
                      kubridge_stub
                      ${CMAKE_BINARY_DIR}/lib/libc_bridge/stubs/libSceLibcBridge_stub.a
                      SceHid_stub
                      SceTouch_stub
                      SceMotion_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceNet_stub
                      SceNetCtl_stub
                      SceSysmodule_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceCommonDialog_stub
                      SceDisplay_stub
                      SceKernelDmacMgr_stub
                      SceFios2_stub
                      SceCtrl_stub
                      SceGxm_stub
                      ScePower_stub
                      SceTouch_stub
                      SceVshBridge_stub
                      )

vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE NOASLR)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                FILE "${CMAKE_SOURCE_DIR}/extras/cpuinfo" cpuinfo
                "${CMAKE_SOURCE_DIR}/extras/meminfo" meminfo
                "${CMAKE_SOURCE_DIR}/extras/PinkBadShaderVP.glsl" PinkBadShaderVP.glsl
                "${CMAKE_SOURCE_DIR}/extras/PinkBadShaderFP.glsl" PinkBadShaderFP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/ProfileCOMMON_emul_VS.glsl" shaders/ProfileCOMMON_emul_VS.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/ProfileCOMMON_emul_FS.glsl" shaders/ProfileCOMMON_emul_FS.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitOneTextureAndVertexColorVP.glsl" shaders/UnlitOneTextureAndVertexColorVP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitTexturedFP.glsl" shaders/UnlitTexturedFP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitTexturedBlendTextureAlphaFP.glsl" shaders/UnlitTexturedBlendTextureAlphaFP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitMultiTexturedFP.glsl" shaders/UnlitMultiTexturedFP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitVertexColorVP.glsl" shaders/UnlitVertexColorVP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitVertexColorFP.glsl" shaders/UnlitVertexColorFP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitMaterialColorVP.glsl" shaders/UnlitMaterialColorVP.glsl
                "${CMAKE_SOURCE_DIR}/extras/shaders/UnlitMaterialColorFP.glsl" shaders/UnlitMaterialColorFP.glsl
                "${CMAKE_SOURCE_DIR}/extras/livearea/icon0.png" sce_sys/icon0.png
                "${CMAKE_SOURCE_DIR}/extras/livearea/pic0.png" sce_sys/pic0.png
                "${CMAKE_SOURCE_DIR}/extras/livearea/startup.png" sce_sys/livearea/contents/startup.png
                "${CMAKE_SOURCE_DIR}/extras/livearea/bg0.png" sce_sys/livearea/contents/bg0.png
                "${CMAKE_SOURCE_DIR}/extras/livearea/template.xml" sce_sys/livearea/contents/template.xml
                )

add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(send_kvdb
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ux0:/app/${VITA_TITLEID}/eboot.bin | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(dump
                  COMMAND ${CMAKE_SOURCE_DIR}/extras/scripts/get_dump.sh ${PSVITAIP}
                  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
