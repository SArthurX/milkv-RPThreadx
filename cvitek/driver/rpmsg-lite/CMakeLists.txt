cmake_minimum_required(VERSION 3.16.5)
project(rpmsg_lite C)

# --- 1. 配置選項 ---
# 使用 option 讓之後編譯時可以透過 -DENABLE_RPMSG_DEBUG=ON 開關訊息
option(ENABLE_RPMSG_DEBUG "Show RPMsg build sources" OFF)

# --- 2. 建立 Library Target ---
add_library(rpmsg-lite STATIC)

target_compile_definitions(rpmsg-lite PRIVATE
    VRING_TX_ADDRESS=0x8FDC1000UL
    VRING_RX_ADDRESS=0x8FDC3000UL
    RPMSG_BUFFER_SIZE=2048
    CVITEK_RPMSG_DIAG=1
    # VRING_SIZE=64
)

target_sources(rpmsg-lite PRIVATE
    # Core
    "lib/rpmsg_lite/rpmsg_lite.c"
    "lib/rpmsg_lite/rpmsg_queue.c"
    "lib/rpmsg_lite/rpmsg_ns.c"
    # VirtIO
    "lib/virtio/virtqueue.c"
    # Common
    "lib/common/llist.c"
    # Porting: Platform (CVITEK)
    "lib/rpmsg_lite/porting/platform/cvitek/rpmsg_platform.c"
    # Porting: Environment (ThreadX)
    "lib/rpmsg_lite/porting/environment/rpmsg_env_threadx.c"
)

target_include_directories(rpmsg-lite 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/include/environment/threadx
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/rpmsg_lite/porting/platform/cvitek
        ${TOP_DIR}/install/include/kernel
        ${TOP_DIR}/install/include/arch
        ${TOP_DIR}/install/include/common
)

# --- 6. 調試訊息 ---
if(ENABLE_RPMSG_DEBUG)
    get_target_property(TEMP_SOURCES rpmsg-lite SOURCES)
    message(STATUS "RPMsg-Lite Sources: ${TEMP_SOURCES}")
endif()

# --- 7. 安裝配置 ---
install(TARGETS rpmsg-lite 
        ARCHIVE DESTINATION lib)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib/include/ 
        DESTINATION include/rpmsg_lite
        FILES_MATCHING PATTERN "*.h")