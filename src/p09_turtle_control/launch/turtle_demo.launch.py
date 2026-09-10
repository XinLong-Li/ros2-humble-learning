# turtle_demo.launch.py —— 基础演示：turtlesim + TF 广播 + 导航服务端（可选 rviz2）
#
# 用法：
#   ros2 launch p09_turtle_control turtle_demo.launch.py
#   ros2 launch p09_turtle_control turtle_demo.launch.py show_rviz:=true
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    show_rviz_arg = DeclareLaunchArgument(
        'show_rviz',
        default_value='false',
        description='是否同时启动 rviz2')

    return LaunchDescription([
        show_rviz_arg,
        # 平台：turtlesim 本体
        Node(
            package='turtlesim',
            executable='turtlesim_node',
            output='screen'),
        # 把乌龟位姿广播成 TF（world -> turtle1）
        Node(
            package='p09_turtle_control',
            executable='turtle_tf_broadcaster',
            output='screen'),
        # 动作服务端：接单后驱赶乌龟
        Node(
            package='p09_turtle_control',
            executable='navigate_to_server',
            output='screen'),
        # 可选：rviz2 观察 TF（IfCondition + LaunchConfiguration 是条件启动的标准姿势）
        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2',
            arguments=['-d', PathJoinSubstitution([
                FindPackageShare('p09_turtle_control'),
                'rviz2',
                'turtle_demo.rviz',
            ])],
            condition=IfCondition(LaunchConfiguration('show_rviz')),
            output='screen'),
    ])
