add_executable(swarm_benchmark main.cpp)
target_link_libraries(swarm_benchmark PRIVATE mith::mith)
target_compile_features(swarm_benchmark PRIVATE cxx_std_17)
