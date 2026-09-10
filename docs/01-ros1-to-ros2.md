# ROS 1 → ROS 2 概念映射

> 面向有 ROS 1 经验的读者。核心结论先行：**你会的概念 80% 没变**（话题/服务/动作/参数/TF/launch 都还在），
> 变的是机制、工具链与 API 拼写。本文按"变化程度"从大到小排列。

## 1. 总览对照表

| 概念 | ROS 1 (Noetic) | ROS 2 (Humble) | 变化程度 |
|---|---|---|---|
| 通信中枢 | roscore / master（**必须启动**） | 无。DDS 自动发现（`ros2 daemon` 只是 CLI 缓存，挂了不影响通信） | ★★★ |
| 传输层 | TCPROS/UDPROS（自研） | DDS（默认 FastDDS），含共享内存 | ★★★ |
| 构建系统 | catkin_make / catkin build | colcon build | ★★ |
| 产物目录 | devel/ + build/ | install/ + build/ + log/ | ★★ |
| C++ 库 | roscpp | rclcpp | ★★（API 大量重命名） |
| Python 库 | rospy | rclpy | ★★ |
| 消息定义 | message_generation + add_message_files | rosidl + rosidl_generate_interfaces | ★★ |
| 类型名 | 两段式 `pkg/Msg` | **三段式** `pkg/msg/Msg` | ★ |
| launch | roslaunch XML | **launch Python**（XML 仍可用，语法变） | ★★★ |
| 参数系统 | 全局参数服务器（隐式） | **每节点参数，必须先 declare** | ★★★ |
| QoS | 仅 queue_size + latch | 完整 QoS 体系（可靠性/持久性/历史/深度/期限） | ★★★ |
| 动作库 | actionlib | rclcpp_action / rclpy.action（内置） | ★★ |
| TF | tf / tf2（共存） | **只有 tf2** | ★★ |
| bag | rosbag（.bag） | rosbag2（sqlite3/mcap，插件式存储） | ★★ |
| 日志 | ROS_INFO 等宏 | RCLCPP_INFO(**get_logger()**, ...) / rclpy 的 get_logger | ★ |
| 时间 | ros::Time::now() | this->now()（节点钟） | ★ |
| 命令行 | rostopic/rosservice/... | `ros2 topic/service/action/param/...` 子命令 | ★ |

## 2. 思维转变三条

### ① 没有 master：静默失败是常态
ROS 1 里话题连不上会报错（master 知道两端存在）。ROS 2 里**话题名拼错、QoS 不匹配、领域号不同，
全部静默**——没有中央节点替你发现。排错工具从"看 master 报错"变成：
`ros2 topic info <话题> -v`（看端点与 QoS）、`ros2 topic list`（看谁在）、rqt_graph。

### ② 参数从"全局字典"变成"节点私有 + 必须声明"
ROS 1 里 `nh.getParam("x", v)` 读不到就用默认值；ROS 2 里 `get_parameter` 之前
**必须先 `declare_parameter`**，否则运行时异常。参数文件（YAML）顶层键是节点名。
这带来了好处：参数校验回调、只读参数、参数事件话题 `/parameter_events`。

### ③ 执行模型显式化：执行器与回调组
ROS 1 的 `AsyncSpinner(4)` 是一个全局开关；ROS 2 把"谁在哪个线程上跑"变成显式对象：
SingleThreadedExecutor / MultiThreadedExecutor + 互斥/重入回调组。
单线程执行器里一个回调睡 3 秒，**所有**回调（包括 shutdown 响应）都排队等它——p04 专门做这个实验。

## 3. API 速查（C++）

```cpp
// 初始化与节点
ros::init(argc, argv, "name");        →  rclcpp::init(argc, argv);
ros::NodeHandle nh;                   →  class MyNode : public rclcpp::Node
                                          { public: MyNode() : Node("name") {...} };

// 发布/订阅
nh.advertise<T>("t", 10)              →  create_publisher<T>("t", 10)
nh.subscribe<T>("t", 10, cb)          →  create_subscription<T>("t", 10, cb)
                                          // 回调参数是 std::shared_ptr<const T>

// 服务
nh.advertiseService("s", cb)          →  create_service<T>("s", cb)
client.call(req, res)                 →  async_send_request(req) + spin_until_future_complete

// 参数
nh.getParam("x", v)                   →  declare_parameter("x", 默认值); get_parameter("x")
nh.setParam("x", v)                   →  set_parameter(rclcpp::Parameter("x", v))

// 日志
ROS_INFO("...")                       →  RCLCPP_INFO(this->get_logger(), "...")

// 时间
ros::Time::now()                      →  this->now()          // rclcpp::Time
ros::Duration(1.0)                    →  std::chrono::duration<double>(1.0)
ros::Rate(10)                         →  create_wall_timer(100ms, cb) 或 rclcpp::Rate(10)

// 回旋
ros::spin()                           →  rclcpp::spin(node)
ros::spinOnce()                       →  （很少需要；用 executor->spin_some() 或回调组）
```

## 4. 不再适用的 ROS 1 习惯清单

| ROS 1 习惯 | 为什么不再适用 |
|---|---|
| 先 `roscore` 再跑节点 | 直接跑节点即可 |
| `rostopic echo` 不带参数 | 命令是 `ros2 topic echo`，且注意其默认 QoS profile（sensor_data/best_effort） |
| `nh.param("x", v, default)` 式隐式参数 | 必须先 declare |
| 在回调里长时间阻塞 | 单线程执行器下会卡死整节点（用回调组+多线程执行器） |
| `roslaunch` 的 `<param>` 标签 | YAML 文件 + launch 里 `parameters=[...]` |
| `static_transform_publisher x y z yaw pitch roll parent child` 位置参数 | Humble 改用 `--x --y --z --yaw ... --frame-id --child-frame-id` |
| `rosbag play` 旧 .bag | rosbag2 不兼容旧格式（需 rosbags 工具转换） |
| `catkin_make` 后 `source devel/setup.bash` | colcon + `source install/setup.bash` |
| `ros::Time(0)` 表示"最新" | tf2 查询用 `tf2::TimePointZero` |

## 5. 建议的阅读顺序

本仓库 docs/02~docs/10 按学习顺序每篇都带"与 ROS 1 相比变了什么"小节：
先扫一遍本文，再按 [plan.md](plan.md) 逐阶段走，遇到旧习惯卡壳就回来看第 4 节。
