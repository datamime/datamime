cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(dnn)

SET(CMAKE_CXX_FLAGS "-Wall -W -O3 -g")

find_package(Boost REQUIRED COMPONENTS filesystem)
include_directories(${Boost_INCLUDE_DIRS} 
    ${CMAKE_CURRENT_SOURCE_DIR}/../harness/
    )

message("boost lib: ${Boost_LIBRARIES}, inc:${Boost_INCLUDE_DIR}")

find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

add_executable(imagenet-inference imagenet-inference.cpp imagefolder_dataset.cpp image_io.cpp)
target_link_libraries(imagenet-inference "${TORCH_LIBRARIES}" ${Boost_LIBRARIES})
target_compile_features(imagenet-inference PRIVATE cxx_std_17)
set_property(TARGET imagenet-inference PROPERTY CXX_STANDARD 17)

add_executable(example example.cpp)
target_link_libraries(example "${TORCH_LIBRARIES}" ${Boost_LIBRARIES})
target_compile_features(example PRIVATE cxx_std_17)
set_property(TARGET example PROPERTY CXX_STANDARD 17)

add_executable(dnn_integrated server.cpp client.cpp main.cpp
               imagefolder_dataset.cpp image_io.cpp)
target_link_libraries(dnn_integrated
    "${TORCH_LIBRARIES}" 
    ${Boost_LIBRARIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../harness/client.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../harness/tbench_server_integrated.o
    -lrt
    -lpthread
    )
target_compile_features(dnn_integrated PRIVATE cxx_std_17)
set_property(TARGET dnn_integrated PROPERTY CXX_STANDARD 17)

add_executable(dnn_networked_server server.cpp main.cpp
               imagefolder_dataset.cpp image_io.cpp)
target_link_libraries(dnn_networked_server
    "${TORCH_LIBRARIES}" 
    ${Boost_LIBRARIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../harness/tbench_server_networked.o
    -lrt
    -lpthread
    )
target_compile_features(dnn_networked_server PRIVATE cxx_std_17)
set_property(TARGET dnn_networked_server PROPERTY CXX_STANDARD 17)

add_executable(dnn_networked_client client.cpp
               imagefolder_dataset.cpp image_io.cpp)
target_link_libraries(dnn_networked_client
    "${TORCH_LIBRARIES}" 
    ${Boost_LIBRARIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../harness/client.o
    ${CMAKE_CURRENT_SOURCE_DIR}/../harness/tbench_client_networked.o
    -lrt
    -lpthread
    )
target_compile_features(dnn_networked_client PRIVATE cxx_std_17)
set_property(TARGET dnn_networked_client PROPERTY CXX_STANDARD 17)
