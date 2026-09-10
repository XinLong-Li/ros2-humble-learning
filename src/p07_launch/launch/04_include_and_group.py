# 04_include_and_group.py —— 包含其他 launch 文件 + 命名空间分组
#
# ROS 1 对照：
#   <include file="$(find p07_launch)/launch/02_args_and_remap.py"/>  （XML include）
#   <group ns="group_a"> ... </group>
from launch import LaunchDescription
from launch.actions import GroupAction, IncludeLaunchDescription, LogInfo
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import PathJoinSubstitution
from launch_ros.actions import Node, PushRosNamespace
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # 复用 02 的 launch 文件；被包含的文件里声明的参数必须在这里传值，
    # 否则 launch 会因为"参数未声明但有默认值冲突"报警
    included = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            PathJoinSubstitution([
                FindPackageShare('p07_launch'),
                'launch',
                '02_args_and_remap.py',
            ]),
        ]),
        launch_arguments=[('topic_name', 'included_chatter')],
    )

    # GroupAction + PushRosNamespace：组内所有节点的名字/话题前都会加 /group_a 前缀
    grouped = GroupAction(
        actions=[
            PushRosNamespace('group_a'),
            included,
        ],
    )

    return LaunchDescription([
        LogInfo(msg='组内节点的名字与话题都在 /group_a 命名空间下'),
        grouped,
        # 组外节点不受影响
        Node(
            package='p01_hello_ros2',
            executable='hello_subscriber',
            name='outside_subscriber',
            output='screen',
            remappings=[('/chatter', '/group_a/included_chatter')],
        ),
    ])
