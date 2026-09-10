# turtle_mission.launch.py —— 结课验收：一键起全系统，乌龟自动巡航 YAML 航点
#
# 启动后无需任何手动操作：waypoint_follower 会按 config/turtle_params.yaml
# 里的航点顺序逐个请求 navigate_to_server，乌龟依次走完 A→B→C→D。
from launch import LaunchDescription
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='turtlesim',
            executable='turtlesim_node',
            output='screen'),
        Node(
            package='p09_turtle_control',
            executable='turtle_tf_broadcaster',
            output='screen'),
        Node(
            package='p09_turtle_control',
            executable='navigate_to_server',
            output='screen'),
        Node(
            package='p09_turtle_control',
            executable='waypoint_follower',
            output='screen',
            # 节点名必须与 YAML 顶层键一致（waypoint_follower）
            parameters=[PathJoinSubstitution([
                FindPackageShare('p09_turtle_control'),
                'config',
                'turtle_params.yaml',
            ])]),
    ])
