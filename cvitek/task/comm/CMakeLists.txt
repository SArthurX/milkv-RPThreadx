file(GLOB _SOURCES "src/${RUN_ARCH}/*.c")

include_directories(include)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/arch)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/common)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/kernel)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/driver/spinlock)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/driver/rtos_cmdqu)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/driver/ssd1306)
include_directories(${CMAKE_INSTALL_INC_PREFIX}/hal/config)

# RPMsg-Lite includes
include_directories(${TOP_DIR}/driver/rpmsg-lite/lib/include)
include_directories(${TOP_DIR}/driver/rpmsg-lite/lib/include/environment/threadx)
include_directories(${TOP_DIR}/driver/rpmsg-lite/lib/rpmsg_lite/porting/platform/cvitek)


add_library(comm STATIC ${_SOURCES})
install(TARGETS comm DESTINATION lib)

