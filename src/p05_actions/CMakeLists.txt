cmake_minimum_required(VERSION 3.8)
project(p05_actions)

# C++17：ROS 2 Humble 官方支持的最低标准
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 每个依赖的包都要 find_package —— 缺一个，编译或链接就会告诉你
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(p02_interfaces REQUIRED)

# 可执行文件名 = 节点名，这是本仓库的约定
add_executable(countdown_server src/countdown_server.cpp)
target_link_libraries(countdown_server
  rclcpp::rclcpp rclcpp_action::rclcpp_action p02_interfaces::p02_interfaces)

add_executable(countdown_client src/countdown_client.cpp)
target_link_libraries(countdown_client
  rclcpp::rclcpp rclcpp_action::rclcpp_action p02_interfaces::p02_interfaces)

add_executable(navigate_server src/navigate_server.cpp)
target_link_libraries(navigate_server
  rclcpp::rclcpp rclcpp_action::rclcpp_action p02_interfaces::p02_interfaces)

add_executable(navigate_client src/navigate_client.cpp)
target_link_libraries(navigate_client
  rclcpp::rclcpp rclcpp_action::rclcpp_action p02_interfaces::p02_interfaces)

# 漏掉 install(TARGETS ...) 的后果：编译通过，但 ros2 run 报 "No executable found"
install(TARGETS
  countdown_server
  countdown_client
  navigate_server
  navigate_client
  DESTINATION lib/${PROJECT_NAME})

ament_package()
