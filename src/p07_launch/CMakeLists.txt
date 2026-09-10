cmake_minimum_required(VERSION 3.8)
project(p07_launch)

find_package(ament_cmake REQUIRED)

# launch 包没有 C++ 代码，唯一易错点是这句 install：
# 漏了它，ros2 launch 就报 "file not found"——新手第一坑
install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME})

ament_package()
