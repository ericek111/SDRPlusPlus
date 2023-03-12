cmake_minimum_required(VERSION 3.13)
project(rtltcp_server)

file(GLOB SRC "src/*.cpp")

add_library(rtltcp_server SHARED ${SRC})
target_link_libraries(rtltcp_server PRIVATE sdrpp_core)
set_target_properties(rtltcp_server PROPERTIES PREFIX "")

target_include_directories(rtltcp_server PRIVATE "src/")
target_include_directories(rtltcp_server PRIVATE "../recorder/src")
target_include_directories(rtltcp_server PRIVATE "../../decoder_modules/radio/src")

if (MSVC)
    target_compile_options(rtltcp_server PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(rtltcp_server PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(rtltcp_server PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS rtltcp_server DESTINATION lib/sdrpp/plugins)
