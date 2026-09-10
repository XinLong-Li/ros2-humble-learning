cmake_minimum_required(VERSION 3.8)
project(p09_turtle_control)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(turtlesim REQUIRED)
find_package(p02_interfaces REQUIRED)

# 1) 乌龟位姿 → TF 广播（world -> turtle1）
add_executable(turtle_tf_broadcaster src/turtle_tf_broadcaster.cpp)
target_link_libraries(turtle_tf_broadcaster
  rclcpp::rclcpp tf2::tf2 tf2_ros::tf2_ros tf2_geometry_msgs::tf2_geometry_msgs
  geometry_msgs::geometry_msgs turtlesim::turtlesim)

# 2) 位姿监控：打印位姿 + 累计路程
add_executable(turtle_pose_monitor src/turtle_pose_monitor.cpp)
target_link_libraries(turtle_pose_monitor
  rclcpp::rclcpp turtlesim::turtlesim)

# 3) 画正方形：cmd_vel 定时器状态机
add_executable(turtle_square_drawer src/turtle_square_drawer.cpp)
target_link_libraries(turtle_square_drawer
  rclcpp::rclcpp geometry_msgs::geometry_msgs)

# 4) 服务客户端：spawn / set_pen / kill
add_executable(turtle_service_client src/turtle_service_client.cpp)
target_link_libraries(turtle_service_client
  rclcpp::rclcpp turtlesim::turtlesim)

# 5) 动作客户端：调用 turtlesim 官方动作 /turtle1/rotate_absolute
add_executable(turtle_rotate_client src/turtle_rotate_client.cpp)
target_link_libraries(turtle_rotate_client
  rclcpp::rclcpp rclcpp_action::rclcpp_action turtlesim::turtlesim)

# 6) 动作服务端：实现 p02 的 NavigateTo，用比例控制驱赶真乌龟
add_executable(navigate_to_server src/navigate_to_server.cpp)
target_link_libraries(navigate_to_server
  rclcpp::rclcpp rclcpp_action::rclcpp_action geometry_msgs::geometry_msgs
  turtlesim::turtlesim p02_interfaces::p02_interfaces)

# 7) 航点巡航：顺序发送 NavigateTo 目标
add_executable(waypoint_follower src/waypoint_follower.cpp)
target_link_libraries(waypoint_follower
  rclcpp::rclcpp rclcpp_action::rclcpp_action geometry_msgs::geometry_msgs
  p02_interfaces::p02_interfaces)

install(TARGETS
  turtle_tf_broadcaster turtle_pose_monitor turtle_square_drawer
  turtle_service_client turtle_rotate_client navigate_to_server waypoint_follower
  DESTINATION lib/${PROJECT_NAME})

# launch / config / rviz2 目录——漏了这句 ros2 launch 就报 file not found
install(DIRECTORY launch config rviz2
  DESTINATION share/${PROJECT_NAME})

ament_package()
