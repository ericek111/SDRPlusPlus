cmake_minimum_required(VERSION 3.13)
project(mirisdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(mirisdr_source PRIVATE "C:/Program Files/PothosSDR/bin/")

    target_link_libraries(mirisdr_source PRIVATE mirisdr)
elseif (ANDROID)
    target_link_libraries(mirisdr_source PUBLIC
        /sdr-kit/${ANDROID_ABI}/lib/libusb1.0.so
        /sdr-kit/${ANDROID_ABI}/lib/libmirisdr.so
    )
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(LIBMIRISDR REQUIRED libmirisdr)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

    target_include_directories(mirisdr_source PRIVATE ${LIBMIRISDR_INCLUDE_DIRS} ${LIBUSB_INCLUDE_DIRS})
    target_link_directories(mirisdr_source PRIVATE ${LIBMIRISDR_LIBRARY_DIRS} ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(mirisdr_source PRIVATE ${LIBMIRISDR_LIBRARIES} ${LIBUSB_LIBRARIES})
endif ()
