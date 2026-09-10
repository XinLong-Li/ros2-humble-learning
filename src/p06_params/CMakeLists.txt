cmake_minimum_required(VERSION 3.8)
project(p06_params)

# C++17：ROS 2 Humble 官方支持的最低标准
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 每个依赖的包都要 find_package —— 缺了这里，编译才会告诉你
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)

# 可执行文件名 = 节点名，这是本仓库的约定
add_executable(param_demo_node src/param_demo_node.cpp)
target_link_libraries(param_demo_node rclcpp::rclcpp rcl_interfaces::rcl_interfaces)

add_executable(param_client_node src/param_client_node.cpp)
target_link_libraries(param_client_node rclcpp::rclcpp rcl_interfaces::rcl_interfaces)

# 漏掉 install(TARGETS ...) 的后果：编译通过，但 ros2 run 报 "No executable found"
install(TARGETS param_demo_node param_client_node
  DESTINATION lib/${PROJECT_NAME})

# 参数 YAML 装到 share/ 下，ros2 run --ros-args --params-file 才能找到它
# （--symlink-install 时 install 里是源码的软链，直接改源码 yaml 即生效）
install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
  OPTIONAL)

ament_package()
