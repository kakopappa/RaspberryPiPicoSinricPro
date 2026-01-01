add_library(dnsclient INTERFACE)

target_sources(dnsclient INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/dnsclient.c
)

target_include_directories(dnsclient INTERFACE ${CMAKE_CURRENT_LIST_DIR})

# Pull in pico libraries that we need
target_link_libraries(dnsclient INTERFACE 
      pico_stdlib
      #pico_cyw43_arch_lwip_threadsafe_background 
      #pico_cyw43_arch_lwip_poll
)

