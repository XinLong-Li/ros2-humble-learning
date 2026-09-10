cmake_minimum_required(VERSION 3.8)
project(p03_topics)

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
find_package(geometry_msgs REQUIRED)
# 自定义接口包：p02_interfaces 导出聚合目标 p02_interfaces::p02_interfaces
# （它把 C/C++ 各 typesupport 的目标打包在一起，link 它就够用）
find_package(p02_interfaces REQUIRED)

# 可执行文件名 = 节点名，这是本仓库的约定
add_executable(status_publisher src/status_publisher.cpp)
target_link_libraries(status_publisher
  rclcpp::rclcpp
  std_msgs::std_msgs
  p02_interfaces::p02_interfaces)

add_executable(status_subscriber src/status_subscriber.cpp)
target_link_libraries(status_subscriber
  rclcpp::rclcpp
  std_msgs::std_msgs
  p02_interfaces::p02_interfaces)

add_executable(qos_publisher src/qos_publisher.cpp)
target_link_libraries(qos_publisher
  rclcpp::rclcpp
  std_msgs::std_msgs)

add_executable(qos_subscriber src/qos_subscriber.cpp)
target_link_libraries(qos_subscriber
  rclcpp::rclcpp
  std_msgs::std_msgs)

# 漏掉 install(TARGETS ...) 的后果：编译通过，但 ros2 run 报 "No executable found"
install(TARGETS status_publisher status_subscriber qos_publisher qos_subscriber
  DESTINATION lib/${PROJECT_NAME})

# config/qos_demo.yaml 装到 share/p03_topics/config/，供 --params-file 使用
# （配合 --symlink-install，改 YAML 不用重新编译）
install(DIRECTORY config DESTINATION share/${PROJECT_NAME} OPTIONAL)

ament_package()
