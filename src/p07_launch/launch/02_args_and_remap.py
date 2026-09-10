# 02_args_and_remap.py —— launch 参数与话题重映射
#
# 用法：ros2 launch p07_launch 02_args_and_remap.py topic_name:=my_chatter
# ROS 1 对照：<arg name="topic_name" default="chatter"/> + $(arg topic_name)
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    # 声明 launch 参数：可以用 := 在命令行覆盖
    topic_name_arg = DeclareLaunchArgument(
        'topic_name',
        default_value='chatter',
        description='发布/订阅使用的话题名')

    return LaunchDescription([
        topic_name_arg,
        Node(
            package='p01_hello_ros2',
            executable='hello_publisher',
            output='screen',
            # remapping：把节点"内部"的话题 /chatter 映射到外部话题名。
            # ROS 1 对照：<remap from="/chatter" to="$(arg topic_name)"/>
            # 注意 from 一侧必须带前导斜杠
            remappings=[('/chatter', LaunchConfiguration('topic_name'))],
        ),
        Node(
            package='p01_hello_ros2',
            executable='hello_subscriber',
            output='screen',
            remappings=[('/chatter', LaunchConfiguration('topic_name'))],
        ),
    ])
