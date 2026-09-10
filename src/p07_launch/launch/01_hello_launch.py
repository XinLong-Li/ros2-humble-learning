# 01_hello_launch.py —— 最小 launch 文件
#
# ROS 1 的 <launch><node .../></launch> XML 在 ROS 2 首选 Python 写法：
# 一个 launch 文件 = 一个 generate_launch_description() 函数，
# 返回 LaunchDescription（Action 列表，按顺序执行）。
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='p01_hello_ros2',
            executable='hello_publisher',
            output='screen',        # ROS 1 的 output="screen"；不写则日志进 ~/.ros/log
        ),
        Node(
            package='p01_hello_ros2',
            executable='hello_subscriber',
            output='screen',
        ),
    ])
