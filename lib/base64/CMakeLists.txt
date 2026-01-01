add_library(base64 INTERFACE)

target_sources(base64 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/base64encode.c
)

target_include_directories(base64 INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(base64 INTERFACE 
    pico_stdlib
    ${ADDITIONAL_LIBS}
)