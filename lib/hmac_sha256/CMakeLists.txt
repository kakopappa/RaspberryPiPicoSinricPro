add_library(hmac_sha256 INTERFACE)

target_sources(hmac_sha256 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/hmac_sha256.c
    ${CMAKE_CURRENT_LIST_DIR}/sha256.c
)

target_include_directories(hmac_sha256 INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(hmac_sha256 INTERFACE 
    pico_stdlib
    ${ADDITIONAL_LIBS}
)