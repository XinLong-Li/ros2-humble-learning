cmake_minimum_required(VERSION 3.8)
project(p02_interfaces)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)

set(msg_files
  "msg/RobotStatus.msg"
  "msg/Waypoint.msg"
)

set(srv_files
  "srv/ComputeSum.srv"
)

set(action_files
  "action/CountDown.action"
  "action/NavigateTo.action"
)

# ROS 1 的 add_message_files/add_service_files/add_action_files
# 在 ROS 2 统一为 rosidl_generate_interfaces
rosidl_generate_interfaces(${PROJECT_NAME}
  ${msg_files}
  ${srv_files}
  ${action_files}
  DEPENDENCIES std_msgs geometry_msgs
)

# 让下游包 link 本包时能拿到运行时依赖
ament_export_dependencies(rosidl_default_runtime)

ament_package()
