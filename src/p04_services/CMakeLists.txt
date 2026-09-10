cmake_minimum_required(VERSION 3.8)
project(p04_services)

# C++17：ROS 2 Humble 官方支持的最低标准
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
# 接口包：不 find_package 就没有 p02_interfaces/srv/compute_sum.hpp 的头文件路径
find_package(p02_interfaces REQUIRED)

# 可执行文件名 = 节点名，这是本仓库的约定
add_executable(compute_sum_server src/compute_sum_server.cpp)
target_link_libraries(compute_sum_server rclcpp::rclcpp p02_interfaces::p02_interfaces)

add_executable(compute_sum_server_mt src/compute_sum_server_mt.cpp)
target_link_libraries(compute_sum_server_mt rclcpp::rclcpp p02_interfaces::p02_interfaces)

add_executable(compute_sum_client src/compute_sum_client.cpp)
target_link_libraries(compute_sum_client rclcpp::rclcpp p02_interfaces::p02_interfaces)

add_executable(compute_sum_client_async src/compute_sum_client_async.cpp)
target_link_libraries(compute_sum_client_async rclcpp::rclcpp p02_interfaces::p02_interfaces)

# 若某个 ROS 版本提示找不到 p02_interfaces::p02_interfaces 目标，退回官方写法：
#   rosidl_get_typesupport_target(cpp_typesupport_target p02_interfaces "rosidl_typesupport_cpp")
#   target_link_libraries(compute_sum_server ${cpp_typesupport_target})
# （原理见 src/p02_interfaces/README.md）

# 漏掉 install(TARGETS ...) 的后果：编译通过，但 ros2 run 报 "No executable found"
install(TARGETS
  compute_sum_server
  compute_sum_server_mt
  compute_sum_client
  compute_sum_client_async
  DESTINATION lib/${PROJECT_NAME})

ament_package()
