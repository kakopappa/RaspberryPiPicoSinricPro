add_library(SinricPro INTERFACE)

target_sources(SinricPro INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/SinricPro.c
)

target_include_directories(SinricPro INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(SinricPro INTERFACE 
    pico_stdlib
    ${ADDITIONAL_LIBS}
)