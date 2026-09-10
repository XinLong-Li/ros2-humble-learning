# 05_timer_and_events.py —— 延迟启动（TimerAction）与进程退出事件（OnProcessExit）
#
# ROS 1 没有直接对应物：旧时代要靠 launch-prefix="bash -c 'sleep 3; ...'" 这类 hack。
# ROS 2 的事件系统把"谁先谁后"变成了显式 Action。
from launch import LaunchDescription
from launch.actions import LogInfo, RegisterEventHandler, TimerAction
from launch.event_handlers import OnProcessExit
from launch_ros.actions import Node


def generate_launch_description():
    publisher = Node(
        package='p01_hello_ros2',
        executable='hello_publisher',
        output='screen',
    )

    subscriber = Node(
        package='p01_hello_ros2',
        executable='hello_subscriber',
        output='screen',
    )

    return LaunchDescription([
        publisher,
        LogInfo(msg='3 秒后启动订阅者...'),
        # TimerAction：延迟 3 秒执行里面的 Action（延迟启动订阅者，
        # 体会 ROS 2 没有 master：先起的发布者一直在发，订阅者后到也能接上）
        TimerAction(
            period=3.0,
            actions=[
                LogInfo(msg='时间到，启动订阅者'),
                subscriber,
            ],
        ),
        # 事件处理器：当订阅者进程退出时（Ctrl-C 或崩溃）打印一条日志。
        # OnProcessExit 是 launch 事件系统的典型用法
        RegisterEventHandler(
            OnProcessExit(
                target_action=subscriber,
                on_exit=[LogInfo(msg='订阅者已退出')],
            ),
        ),
    ])
