cmake_minimum_required(VERSION 3.8)
project(p08_tf2)

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
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)

# 可执行文件名 = 节点名，这是本仓库的约定
add_executable(static_tf_broadcaster src/static_tf_broadcaster.cpp)
target_link_libraries(static_tf_broadcaster
  rclcpp::rclcpp
  tf2::tf2
  tf2_ros::tf2_ros
  tf2_geometry_msgs::tf2_geometry_msgs
  geometry_msgs::geometry_msgs
)

add_executable(dynamic_tf_broadcaster src/dynamic_tf_broadcaster.cpp)
target_link_libraries(dynamic_tf_broadcaster
  rclcpp::rclcpp
  tf2::tf2
  tf2_ros::tf2_ros
  tf2_geometry_msgs::tf2_geometry_msgs
  geometry_msgs::geometry_msgs
)

add_executable(tf_listener src/tf_listener.cpp)
target_link_libraries(tf_listener
  rclcpp::rclcpp
  tf2::tf2
  tf2_ros::tf2_ros
  tf2_geometry_msgs::tf2_geometry_msgs
  geometry_msgs::geometry_msgs
)

add_executable(frame_marker_publisher src/frame_marker_publisher.cpp)
target_link_libraries(frame_marker_publisher
  rclcpp::rclcpp
  tf2::tf2
  tf2_ros::tf2_ros
  geometry_msgs::geometry_msgs
  visualization_msgs::visualization_msgs
)

# 漏掉 install(TARGETS ...) 的后果：编译通过，但 ros2 run 报 "No executable found"
install(TARGETS
  static_tf_broadcaster
  dynamic_tf_broadcaster
  tf_listener
  frame_marker_publisher
  DESTINATION lib/${PROJECT_NAME})

# rviz2 配置：install 后才能用 $(ros2 pkg prefix p08_tf2)/share/p08_tf2/config/... 打开
install(DIRECTORY config DESTINATION share/${PROJECT_NAME} OPTIONAL)

ament_package()
