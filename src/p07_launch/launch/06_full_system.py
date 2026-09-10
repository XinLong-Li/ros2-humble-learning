# 06_full_system.py —— 一键拉起 p03~p06 的全部 8 个节点
#
# 这是 launch 的真正价值：把"一堆 ros2 run"变成一个可重复、可分享的系统描述。
# 起好后用 ros2 node list 数一数：应该正好 8 个节点。
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        # p03 话题：机器人状态 + QoS 演示
        Node(package='p03_topics', executable='status_publisher', output='screen'),
        Node(package='p03_topics', executable='status_subscriber', output='screen'),
        Node(package='p03_topics', executable='qos_publisher', output='screen'),
        Node(package='p03_topics', executable='qos_subscriber', output='screen'),
        # p04 服务
        Node(package='p04_services', executable='compute_sum_server', output='screen'),
        # p05 动作
        Node(package='p05_actions', executable='countdown_server', output='screen'),
        Node(package='p05_actions', executable='navigate_server', output='screen'),
        # p06 参数
        Node(package='p06_params', executable='param_demo_node', output='screen'),
    ])
