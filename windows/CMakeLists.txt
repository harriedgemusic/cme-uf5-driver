cmake_minimum_required(VERSION 3.15)
project(CMEUF5Windows LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN)

set(COMMON_SOURCES
    src/cme_protocol.c
    src/cme_winusb.c
    src/cme_virtual_midi.c
)

set(COMMON_LIBS
    winusb
    setupapi
    winmm
    gdiplus
    gdi32
    user32
    comctl32
    shell32
    advapi32
)

# CLI Tool
add_executable(cme-uf5-ctl
    src/cme_uf5_ctl.c
    ${COMMON_SOURCES}
)
target_link_libraries(cme-uf5-ctl PRIVATE ${COMMON_LIBS})

# Service / Background Bridge
add_executable(CMEUF5Bridge
    src/cme_service.c
    ${COMMON_SOURCES}
)
target_link_libraries(CMEUF5Bridge PRIVATE ${COMMON_LIBS})

# GUI Control Center
if (WIN32)
    add_executable(CMEUF5Control WIN32
        src/cme_gui_app.cpp
        ${COMMON_SOURCES}
        src/resource.rc
    )
else()
    add_executable(CMEUF5Control
        src/cme_gui_app.cpp
        ${COMMON_SOURCES}
    )
endif()
target_link_libraries(CMEUF5Control PRIVATE ${COMMON_LIBS})

