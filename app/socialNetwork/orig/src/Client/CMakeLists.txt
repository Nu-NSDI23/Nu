set(NU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../)
set(CALADAN_DIR ${NU_DIR}/caladan/)
set(CMAKE_EXE_LINKER_FLAGS "-T ${CALADAN_DIR}/base/base.ld")

execute_process(COMMAND nproc OUTPUT_VARIABLE NCORES)
add_compile_definitions(NCORES=${NCORES})

add_executable(
    client
    client.cpp
    ${THRIFT_GEN_CPP_DIR}/UserTimelineService.cpp
    ${THRIFT_GEN_CPP_DIR}/HomeTimelineService.cpp
    ${THRIFT_GEN_CPP_DIR}/ComposePostService.cpp
    ${THRIFT_GEN_CPP_DIR}/SocialGraphService.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
)

target_include_directories(
    client PRIVATE
    ../third_party
    ${THRIFT_INCLUDE_DIR}
    ${NU_DIR}/inc
    ${CALADAN_DIR}/inc
    ${CALADAN_DIR}/bindings/cc
)

target_link_directories(
    client PRIVATE
    ${CALADAN_DIR}
    ${CALADAN_DIR}/bindings/cc
    ${CALADAN_DIR}/rdma-core/build/lib/
    ${CALADAN_DIR}/rdma-core/build/lib/statics/
    ${NU_DIR}
)

target_link_libraries(
    client
    ${THRIFT_STATIC_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    # Nu
    nu
    crypto
    # Caladan
    rt++
    runtime
    net
    base
    mlx5
    ibverbs
    nl-3
    nl-route-3
    pthread
    dl
)
