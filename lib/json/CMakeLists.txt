add_library(json INTERFACE)

target_sources(json INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/json.c
    ${CMAKE_CURRENT_LIST_DIR}/tiny-json.c
    ${CMAKE_CURRENT_LIST_DIR}/json-maker.c
)

target_include_directories(json INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(json INTERFACE 
    pico_stdlib
    ${ADDITIONAL_LIBS}
)