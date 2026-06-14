add_executable(flocking_demo_3d main.cpp)
target_link_libraries(flocking_demo_3d PRIVATE mith::mith)
target_compile_features(flocking_demo_3d PRIVATE cxx_std_17)
