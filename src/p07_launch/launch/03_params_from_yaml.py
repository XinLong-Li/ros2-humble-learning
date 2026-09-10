# 03_params_from_yaml.py —— 通过 launch 给节点传参数（YAML 文件 / 内联字典）
#
# ROS 1 对照：<param name="..." value="..."/> 或 <rosparam file="..."/>
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # FindPackageShare 等价于 ROS 1 的 $(find pkg)；PathJoinSubstitution 负责拼路径。
    # 用 PathJoinSubstitution 而不是字符串拼接，好处是 launch 能跟踪依赖
    # （文件变更会触发重载，纯字符串则不会）。
    params_file_arg = DeclareLaunchArgument(
        'params_file',
        default_value=PathJoinSubstitution([
            FindPackageShare('p07_launch'),
            'config',
            'p07_params.yaml',
        ]),
        description='参数文件完整路径')

    return LaunchDescription([
        params_file_arg,
        Node(
            package='p06_params',
            executable='param_demo_node',
            output='screen',
            # YAML 里是 <节点名>: {ros__parameters: {...}}，
            # 所以这里节点名必须与 YAML 顶层键一致（param_demo_node）
            parameters=[LaunchConfiguration('params_file')],
        ),
        Node(
            package='p06_params',
            executable='param_demo_node',
            name='param_demo_node_inline',    # 重命名节点，避免与上一个同名冲突
            output='screen',
            # 内联字典传参，等价于 --ros-args -p robot_name:=inline_bot ...
            parameters=[{
                'robot_name': 'inline_bot',
                'max_speed': 3.0,
                'enable_debug': True,
            }],
        ),
    ])
