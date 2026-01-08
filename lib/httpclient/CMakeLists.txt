add_library(httpclient INTERFACE)

target_sources(httpclient INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/httpClient.c
)

target_include_directories(httpclient INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(httpclient INTERFACE 
      pico_stdlib
      #pico_cyw43_arch_lwip_threadsafe_background 
      #pico_cyw43_arch_lwip_poll
)

