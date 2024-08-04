cmake_minimum_required(VERSION 3.20)

project(smct
    DESCRIPTION "Simplified Map Compile Tools"
    LANGUAGES CXX
)

message("${CMAKE_PROJECT_NAME} > Starting configuration for ${CMAKE_PROJECT_DESCRIPTION}")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tools)

#================
# Configuration
#================

set(DIR ${CMAKE_SOURCE_DIR}/src)

#================
# Common
#================

set(COMMON_DIR ${DIR}/common)

set(COMMON_SOURCES
    ${COMMON_DIR}/arguments.cpp
    ${COMMON_DIR}/blockmem.cpp
    ${COMMON_DIR}/bspfile.cpp
    ${COMMON_DIR}/cmdlib.cpp
    ${COMMON_DIR}/filelib.cpp
    ${COMMON_DIR}/log.cpp
    ${COMMON_DIR}/mathlib.cpp
    ${COMMON_DIR}/messages.cpp
    ${COMMON_DIR}/maplib.cpp
    ${COMMON_DIR}/threads.cpp
    ${COMMON_DIR}/winding.cpp
)

set(COMMON_HEADERS
    ${COMMON_DIR}/arguments.h
    ${COMMON_DIR}/blockmem.h
    ${COMMON_DIR}/boundingbox.h
    ${COMMON_DIR}/bspfile.h
    ${COMMON_DIR}/cmdlib.h
    ${COMMON_DIR}/filelib.h
    ${COMMON_DIR}/hlassert.h
    ${COMMON_DIR}/log.h
    ${COMMON_DIR}/mathlib.h
    ${COMMON_DIR}/mathtypes.h
    ${COMMON_DIR}/messages.h
    ${COMMON_DIR}/maplib.h
    ${COMMON_DIR}/threads.h
    ${COMMON_DIR}/win32fix.h
    ${COMMON_DIR}/winding.h
)

#================
# CSG
#================

set(CSG_DIR ${DIR}/sCSG)

set(CSG_SOURCES
    ${COMMON_SOURCES}
    ${CSG_DIR}/brush.cpp
    ${CSG_DIR}/hullfile.cpp
    ${CSG_DIR}/map.cpp
    ${CSG_DIR}/hlcsg.cpp
    ${CSG_DIR}/textures.cpp
)

set(CSG_HEADERS
    ${COMMON_HEADERS}
    ${CSG_DIR}/hlcsg.h
    ${CSG_DIR}/textures.h
    ${CSG_DIR}/map.h
)


#================
# BSP
#================

set(BSP_DIR ${DIR}/sBSP)

set(BSP_SOURCES
    ${COMMON_SOURCES}
    ${BSP_DIR}/brink.cpp
    ${BSP_DIR}/merge.cpp
    ${BSP_DIR}/outside.cpp
    ${BSP_DIR}/portals.cpp
    ${BSP_DIR}/hlbsp.cpp
    ${BSP_DIR}/solidbsp.cpp
    ${BSP_DIR}/surfaces.cpp
    ${BSP_DIR}/tjunc.cpp
    ${BSP_DIR}/writebsp.cpp
)

set(BSP_HEADERS
    ${COMMON_HEADERS}
    ${BSP_DIR}/hlbsp.h
)

#================
# VIS
#================

set(VIS_DIR ${DIR}/sVIS)

set(VIS_SOURCES
    ${COMMON_SOURCES}
    ${VIS_DIR}/flow.cpp
    ${VIS_DIR}/hlvis.cpp
)

set(VIS_HEADERS
    ${COMMON_HEADERS}
    ${VIS_DIR}/hlvis.h
)


#================
# RAD
#================

set(RAD_DIR ${DIR}/sRAD)

set(RAD_SOURCES
    ${COMMON_SOURCES}
    ${RAD_DIR}/compress.cpp
    ${RAD_DIR}/lerp.cpp
    ${RAD_DIR}/lightmap.cpp
    ${RAD_DIR}/loadtextures.cpp
    ${RAD_DIR}/mathutil.cpp
    ${RAD_DIR}/hlrad.cpp
    ${RAD_DIR}/hlradutil.cpp
    ${RAD_DIR}/sparse.cpp
    ${RAD_DIR}/trace.cpp
    ${RAD_DIR}/transfers.cpp
    ${RAD_DIR}/transparency.cpp
    ${RAD_DIR}/vismatrix.cpp
    ${RAD_DIR}/vismatrixutil.cpp
)

set(RAD_HEADERS
    ${COMMON_HEADERS}
    ${RAD_DIR}/compress.h
    ${RAD_DIR}/hlrad.h
)

#================
# Include
#================

include_directories(
    ${DIR}/common
)

#================
# System config
#================

# Enforce 64-bit build
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64")
    add_compile_options(
        -Wall
        -O2
        -fno-strict-aliasing
        -pthread
        -pipe
        -Wextra
    )
    message("${CMAKE_PROJECT_NAME} > Configuring for ${CMAKE_SYSTEM_NAME} (64-bit)")
else()
    add_compile_definitions(VERSION_OTHER)
    message("${CMAKE_PROJECT_NAME} > WARNING: Unknown system \"${CMAKE_SYSTEM_NAME}\"")
endif()

#================
# Targets
#================

add_executable(BSP ${BSP_SOURCES} ${BSP_HEADERS})
add_executable(CSG ${CSG_SOURCES} ${CSG_HEADERS})
add_executable(RAD ${RAD_SOURCES} ${RAD_HEADERS})
add_executable(VIS ${VIS_SOURCES} ${VIS_HEADERS})

set_target_properties(BSP CSG RAD VIS
    PROPERTIES
        PREFIX "s"
)

target_compile_definitions(BSP PRIVATE SBSP DOUBLEVEC_T)
target_compile_definitions(CSG PRIVATE SCSG DOUBLEVEC_T)
target_compile_definitions(RAD PRIVATE SRAD)
target_compile_definitions(VIS PRIVATE SVIS)
