# p07_launch —— launch 启动系统

配套文档：[docs/08-launch.md](../../docs/08-launch.md)

## 学习目标

1. 用 Python 写 launch 文件：一个函数返回 `LaunchDescription`
2. 掌握参数传递（YAML / 内联）、重映射、命名空间、包含、定时与事件
3. 理解 `ros2 launch` 报错时的排查路径（最常见：install 漏了 launch 目录）

## ROS 1 → ROS 2 快速对照

| ROS 1 (roslaunch XML) | ROS 2 (launch Python) |
|---|---|
| `<node pkg=".." type=".." name=".."/>` | `Node(package=.., executable=.., name=..)` |
| `<arg name="x" default=".."/>` | `DeclareLaunchArgument('x', default_value=..)` |
| `$(arg x)` | `LaunchConfiguration('x')` |
| `<remap from=".." to=".."/>` | `remappings=[('/topic', LaunchConfiguration('x'))]` |
| `<param name=".." value=".."/>` / `<rosparam file=".."/>` | `parameters=[yaml路径或字典]` |
| `<include file="$(find pkg)/launch/.."/>` | `IncludeLaunchDescription(PythonLaunchDescriptionSource(..))` |
| `<group ns="..">` | `GroupAction([PushRosNamespace('..'), ..])` |
| `$(find pkg)` | `FindPackageShare('pkg')` |
| 无对应物（需 launch-prefix hack） | `TimerAction` / `OnProcessExit` 等事件系统 |
| XML 为主 | **Python 为主**，XML 仍支持（语法已变，见 `legacy_style.launch.xml`） |

## 文件说明（按学习顺序递进）

| 文件 | 演示内容 |
|---|---|
| `01_hello_launch.py` | 最小结构：`generate_launch_description()` + 两个 Node |
| `02_args_and_remap.py` | launch 参数、话题重映射 |
| `03_params_from_yaml.py` | YAML 参数文件、内联参数、节点重命名 |
| `04_include_and_group.py` | 包含其他 launch 文件、命名空间分组 |
| `05_timer_and_events.py` | 延迟启动、进程退出事件 |
| `06_full_system.py` | 一键拉起 p03~p06 的 8 个节点 |
| `legacy_style.launch.xml` | ROS 1 风格 XML 对照（注意 `exec` 与 `$(var ...)` 的差异） |

## 编译运行

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select p07_launch
source install/setup.bash

# 01：一键起两个节点
ros2 launch p07_launch 01_hello_launch.py

# 02：命令行覆盖话题名
ros2 launch p07_launch 02_args_and_remap.py topic_name:=my_chatter
# 另一个终端验证：ros2 topic list | grep my_chatter

# 03：YAML 传参（改 install/p07_launch/share/p07_launch/config/p07_params.yaml 即生效，symlink）
ros2 launch p07_launch 03_params_from_yaml.py
# 验证：ros2 param get /param_demo_node robot_name  → launch_bot
#       ros2 param get /param_demo_node_inline robot_name  → inline_bot

# 04：命名空间
ros2 launch p07_launch 04_include_and_group.py
# 验证：ros2 node list | grep group_a  → /group_a/hello_publisher 等

# 05：3 秒后订阅者才启动
ros2 launch p07_launch 05_timer_and_events.py

# 06：一键 8 节点
ros2 launch p07_launch 06_full_system.py
# 验证：ros2 node list | wc -l  → 8 个学习节点（外加 daemon 不算）

# XML 对照
ros2 launch p07_launch legacy_style.launch.xml
```

> 每个 launch 里都可以临时改 `output='screen'` 观察 stdout；不加时日志在 `~/.ros/log/`。

## 常见坑

- **`file not found`**：CMakeLists 漏了 `install(DIRECTORY launch ...)` → 重编。
- **`Package 'xxx' not found`**：launch 里用到的节点所属包没有 `exec_depend` 声明。
- **参数没生效**：YAML 顶层键必须等于**节点名**（含命名空间前缀时也要一致）。
- 被 `IncludeLaunchDescription` 包含的文件，其声明的参数必须显式传值。

## 练习题

**基础**
1. 写一个 `my_launch.py`：用 `ros2 launch p07_launch my_launch.py name:=robot_a` 启动 `status_publisher` 并把 `robot_name` 参数传成 `robot_a`（提示：内联参数字典里也可以用 LaunchConfiguration）。
2. 在 06 基础上给每个节点加命名空间 `sim/`，验证 `ros2 node list` 的变化。
3. 用 XML 写一个和 02 等价的 launch 文件并跑通。

**进阶**
4. 用 `OnProcessExit` 实现：`hello_subscriber` 退出后自动重新拉起它（respawn，需要 `launch.actions.ExecuteProcess` 或再次 Node + 事件）。
5. 读 `/opt/ros/humble/share/turtlesim/launch/multisim.launch.py` 源码，找出它用了哪些你还没见过的 Action。
6. 为什么推荐用 `PathJoinSubstitution` 拼路径而不是 Python 字符串？读 launch 文档里 substitution 一节回答。
