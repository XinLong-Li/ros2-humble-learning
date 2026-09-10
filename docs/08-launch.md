# launch 启动系统（p07）

> 对应代码：`src/p07_launch/`。launch 文件按 01→06 递进，建议按序读。

## 1. 从 roslaunch 到 launch：为什么换

ROS 1 的 roslaunch XML 有三个痛点：动态行为要绕道（bash -c hack）、复用靠 include 字符串拼接、
多机靠 `<machine>` 标签（ROS 2 已删）。ROS 2 的 launch 是一个**事件驱动的进程编排框架**：
所有东西都是 Action，启动/停止/退出都是事件，可以定时、可以条件、可以回调。

XML 仍然可用（见 `legacy_style.launch.xml`），但**语法已变**且官方推荐 Python。

## 2. 最小骨架

```python
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(package='p01_hello_ros2', executable='hello_publisher', output='screen'),
        Node(package='p01_hello_ros2', executable='hello_subscriber', output='screen'),
    ])
```

`ros2 launch p07_launch 01_hello_launch.py` 等价于两个 `ros2 run`。
`output='screen'` 不加时日志进 `~/.ros/log/`（ROS 1 的 output="screen" 同理）。

## 3. roslaunch → launch.py 翻译表

| roslaunch XML | launch.py |
|---|---|
| `<node pkg=".." type=".." name=".."/>` | `Node(package=.., executable=.., name=..)` |
| `<arg name="x" default=".."/>` | `DeclareLaunchArgument('x', default_value=..)` |
| `$(arg x)` | `LaunchConfiguration('x')` |
| `<remap from=".." to=".."/>` | `remappings=[('/topic', LaunchConfiguration('x'))]` |
| `<param name=".." value=".."/>` | `parameters=[{'k': v}]`（内联） |
| `<rosparam file=".."/>` | `parameters=[yaml路径]` |
| `<include file="$(find pkg)/launch/.."/>` | `IncludeLaunchDescription(PythonLaunchDescriptionSource(..))` |
| `<group ns="..">` | `GroupAction([PushRosNamespace('..'), ...])` |
| `<group if="$(arg x)">` | `condition=IfCondition(LaunchConfiguration('x'))` |
| `$(find pkg)` | `FindPackageShare('pkg')` |
| `$(eval ...)` | `PythonExpression('...')` |
| `<machine>`（多机） | 已删除（用 Docker/多主机 DDS 配置替代） |
| —（无对应物） | `TimerAction`、`OnProcessExit` 等事件系统 |
| `<node respawn="true"/>` | 用 `OnProcessExit` 事件 + `ExecuteProcess` 重新拉起（练习 4） |

## 4. 参数传递的三种方式（p07 的 03 演示了前两种）

```python
# ① YAML 文件（推荐，节点多时最清晰）
parameters=[PathJoinSubstitution([FindPackageShare('p07_launch'), 'config', 'p07_params.yaml'])]
# YAML 顶层键必须是节点名：
#   param_demo_node:
#     ros__parameters:
#       robot_name: launch_bot

# ② 内联字典（少而临时时用）
parameters=[{'robot_name': 'inline_bot', 'max_speed': 3.0}]

# ③ 命令行 --ros-args -p k:=v（单个实验用）
```

## 5. 排错

| 症状 | 原因 | 解法 |
|---|---|---|
| `file not found` | CMakeLists 漏 `install(DIRECTORY launch ...)` | 补上重编 |
| `Package 'xxx' not found` | launch 用的包没有 exec_depend | package.xml 补 |
| 参数没生效 | YAML 顶层键 ≠ 节点名 | 对齐节点名 |
| include 的文件报警告 | 被包含文件声明的参数没传值 | `launch_arguments=[('name', 'value')]` |
| 节点秒退 | stdout 在 log 文件里 | 先 `output='screen'` 再看 `~/.ros/log` |

## 6. 练习题线索

- 练习 1：内联字典的值可以是 `LaunchConfiguration('name')`。
- 练习 4（respawn）：`RegisterEventHandler(OnProcessExit(target_action=<Node>, on_exit=[<重新起的 Node>]))`，
  注意 launch 里 Node 是 Action 可复用，但两个"同对象" Node 不能同时在跑——用 `ExecuteProcess` 或再建一个 Node。
