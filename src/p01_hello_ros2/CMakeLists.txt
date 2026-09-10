cmake_minimum_required(VERSION 3.8)
project(p01_hello_ros2)

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
find_package(std_msgs REQUIRED)

# 可执行文件名 = 节点名，这是本仓库的约定
add_executable(hello_publisher src/hello_publisher.cpp)
target_link_libraries(hello_publisher rclcpp::rclcpp std_msgs::std_msgs)

add_executable(hello_subscriber src/hello_subscriber.cpp)
target_link_libraries(hello_subscriber rclcpp::rclcpp std_msgs::std_msgs)

# 漏掉 install(TARGETS ...) 的后果：编译通过，但 ros2 run 报 "No executable found"
install(TARGETS hello_publisher hello_subscriber
  DESTINATION lib/${PROJECT_NAME})

ament_package()
