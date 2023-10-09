#############################
# CMake - iSimulate Bridge - root/src
#############################

set(ISIMULATE_BRIDGE_SOURCES
   iSimulateBridge.cpp
   websocket_session.cpp
   service_discovery.c
   )

add_executable(mohses_isimulate_bridge ${ISIMULATE_BRIDGE_SOURCES})

target_include_directories(mohses_isimulate_bridge PUBLIC)

target_link_libraries(
   mohses_isimulate_bridge
   PUBLIC amm_std
   PUBLIC Boost::thread
   PUBLIC avahi-client
   PUBLIC avahi-common
)

install(TARGETS mohses_isimulate_bridge RUNTIME DESTINATION bin)
install(DIRECTORY ../config DESTINATION bin)
