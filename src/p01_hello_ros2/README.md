# p01_hello_ros2 —— 第一个 C++ 包

配套文档：[docs/02-workspace-and-first-node.md](../../docs/02-workspace-and-first-node.md)

## 学习目标

1. 理解 colcon 工作空间的结构（`src/ build/ install/ log/`）与 `install/` 覆盖层（overlay）概念
2. 解剖一个 ament_cmake 包：`package.xml`、`CMakeLists.txt`、`install()` 规则各干什么
3. 写出第一个发布者/订阅者节点，并用 `ros2` CLI 验证

## ROS 1 → ROS 2 快速对照

| ROS 1 (Noetic) | ROS 2 (Humble) |
|---|---|
| `catkin_make` / `catkin build` | `colcon build` |
| `source devel/setup.bash` | `source install/setup.bash` |
| `roscore`（必须先启动 master） | 无 master，DDS 自动发现，直接跑节点 |
| `ros::init(argc, argv, "name")` + `ros::NodeHandle` | `rclcpp::init` + 继承 `rclcpp::Node` |
| `ros::Publisher pub = nh.advertise<T>(...)` | `create_publisher<T>("topic", depth)` |
| `nh.subscribe<T>(..., callback)` | `create_subscription<T>("topic", qos, callback)` |
| `ROS_INFO(...)` | `RCLCPP_INFO(this->get_logger(), ...)` |
| `ros::Rate(1).sleep()` + `ros::spinOnce()` 循环 | `create_wall_timer(1s, cb)` + `rclcpp::spin` |

## 文件说明

```
p01_hello_ros2/
├── package.xml            # 包元信息 + 依赖声明（format="3"）
├── CMakeLists.txt         # 构建规则：可执行文件、链接、安装
└── src/
    ├── hello_publisher.cpp   # 节点 hello_publisher，1 Hz 发布 std_msgs/String 到 /chatter
    └── hello_subscriber.cpp  # 节点 hello_subscriber，订阅 /chatter 并打印
```

## 编译运行

```bash
# 在仓库根目录（~/ros2-humble-learning）
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select p01_hello_ros2
source install/setup.bash

ros2 run p01_hello_ros2 hello_publisher     # 终端 A
ros2 run p01_hello_ros2 hello_subscriber    # 终端 B
```

验证（另开终端）：

```bash
ros2 node list                              # 应看到两个节点
ros2 topic list                             # /chatter /parameter_events ...
ros2 topic info /chatter -v                 # 发布/订阅端点详情（含 QoS）
ros2 topic echo /chatter                    # 实时打印消息
ros2 topic hz /chatter                      # 统计频率，应约 1.0 Hz
```

## 练习题

**基础**
1. 把发布频率改成 5 Hz（改代码），再用 `ros2 topic hz` 验证。
2. 把消息类型换成 `std_msgs/msg/Int32`，让发布者报数 0、1、2、…，订阅者打印平方值。
3. 用 `ros2 topic pub --once /chatter std_msgs/msg/String "{data: '手动发布'}"` 给订阅者发一条消息。

**进阶**
4. 给发布者加一个参数 `publish_rate`（Hz），用 `--ros-args -p publish_rate:=5.0` 运行时改频率。
   （提示：`declare_parameter` + `get_parameter`，p06 会系统讲参数）
5. 让订阅者统计过去 10 秒内收到的消息数，并每秒打印一次。
6. 故意把 publisher 的话题名改成 `/chatter2`，用 `ros2 topic info /chatter` 观察会发生什么——体会"ROS 2 没有 master 报错，连接不上是静默的"。
