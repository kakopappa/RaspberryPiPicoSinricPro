add_library(WebSocket INTERFACE)

target_sources(WebSocket INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/WebSocket.c
)

target_include_directories(WebSocket INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(WebSocket INTERFACE 
    pico_stdlib
    ${ADDITIONAL_LIBS}
)