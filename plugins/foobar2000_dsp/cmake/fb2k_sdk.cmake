# ---------------------------------------------------------------------------
# fb2k_sdk.cmake
#
# Builds the parts of the foobar2000 SDK a DSP component needs, as two static
# libraries, and exposes them through the interface target `fb2k::sdk`.
#
# helpers/, libPPUI/ and anything else that pulls in ATL or WTL are left out on
# purpose - see README.md.
# ---------------------------------------------------------------------------

if(NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/SDK/foobar2000.h")
    message(FATAL_ERROR "FB2K_SDK_DIR does not look like a foobar2000 SDK: ${FB2K_SDK_DIR}")
endif()

# --- pfc -------------------------------------------------------------------
file(GLOB FB2K_PFC_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/pfc/*.cpp")
# POSIX-only translation unit; win-objects.cpp is its counterpart.
list(FILTER FB2K_PFC_SOURCES EXCLUDE REGEX "synchro_nix\\.cpp$")

add_library(fb2k_pfc STATIC ${FB2K_PFC_SOURCES})
# Both roots, matching the stock .vcxproj files: the SDK sources use
# <pfc/...> and <libPPUI/...> from the top, and <SDK/...> from foobar2000/.
# SYSTEM (plus /external:W0 in the top level CMakeLists) so third party
# warnings do not bury our own.
target_include_directories(fb2k_pfc SYSTEM PUBLIC
    "${FB2K_SDK_DIR}"
    "${FB2K_SDK_DIR}/foobar2000")
# WIN32_LEAN_AND_MEAN is deliberately NOT set: pfc/timers.h calls timeGetTime,
# which only appears once windows.h has pulled in mmsystem.h.
target_compile_definitions(fb2k_pfc PUBLIC
    UNICODE _UNICODE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=${FOO_DSP_WIN32_WINNT}
    WINVER=${FOO_DSP_WIN32_WINNT})
target_link_libraries(fb2k_pfc PUBLIC winmm)

# --- SDK + component client ------------------------------------------------
file(GLOB FB2K_SDK_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/foobar2000/SDK/*.cpp")
list(APPEND FB2K_SDK_SOURCES
     "${FB2K_SDK_DIR}/foobar2000/foobar2000_component_client/component_client.cpp")

add_library(fb2k_sdk_core STATIC ${FB2K_SDK_SOURCES})
target_link_libraries(fb2k_sdk_core PUBLIC fb2k_pfc)

# shared.dll ships with foobar2000 itself; the SDK only carries import libs.
if(FOO_DSP_TARGET_ARCH STREQUAL "x64")
    set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-x64.lib")
elseif(FOO_DSP_TARGET_ARCH STREQUAL "ARM64EC")
    set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-ARM64EC.lib")
else()
    set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-Win32.lib")
endif()
if(NOT EXISTS "${_fb2k_shared_lib}")
    message(FATAL_ERROR "Missing foobar2000 import library: ${_fb2k_shared_lib}")
endif()
target_link_libraries(fb2k_sdk_core PUBLIC "${_fb2k_shared_lib}")

# --- umbrella --------------------------------------------------------------
add_library(fb2k_sdk INTERFACE)
target_link_libraries(fb2k_sdk INTERFACE fb2k_sdk_core)
add_library(fb2k::sdk ALIAS fb2k_sdk)

set_target_properties(fb2k_pfc fb2k_sdk_core PROPERTIES FOLDER "foobar2000 SDK")

# The SDK is third party code; do not let its warnings drown out ours.
# /wd4996 covers the SDK deliberately calling its own deprecated APIs.
if(MSVC)
    target_compile_options(fb2k_pfc       PRIVATE /W3 /wd4996)
    target_compile_options(fb2k_sdk_core  PRIVATE /W3 /wd4996)
endif()
