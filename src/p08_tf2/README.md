# p08_tf2 —— TF2 坐标变换

配套文档：[docs/09-tf2.md](../../docs/09-tf2.md)

## 学习目标

1. 搞懂 TF 的三个核心概念：**父子帧**（谁在谁的坐标系下）、**TF 树**（每帧只有一个父）、**时间戳**（变换是随时间变化的，必须带时间）
2. 会写三种节点：静态广播器、动态广播器、监听器（查询 + 点变换）
3. 掌握 `tf2::TimePointZero`、`lookupTransform` 的方向语义、`buffer.transform()` 的用法，以及最常见的**外推（extrapolation）异常**
4. 会用 `tf2_echo` / `view_frames` / rviz2 三件套**验证** TF 树是否正确

## TF 核心概念

### 父子帧与"方向"

一条变换 `A -> B`（`header.frame_id="A"`，`child_frame_id="B"`）表达的意思是：
**B 坐标系的原点，在 A 坐标系下的位姿**。

```
odom -> base_link  表示"机器人在里程计坐标系里的位置和朝向"
```

于是把 `base_link` 系下的点变到 `odom` 系，就是"乘上这条变换"；
反过来要"除以"它 —— 但**不用自己写逆运算**：tf2 会自动沿树逐级求逆再相乘，
所以 `lookupTransform("map", "sensor", ...)` 和 `lookupTransform("sensor", "map", ...)` 都能查。

### TF 树：每个坐标系只能有一个父

TF 维护的是一棵**树**，不是图：**每个 frame 最多只能有一个父**。

同一个 frame 若被两个广播器声明成子帧，TF **不会报错**（这是最坑的地方），
而是只认其中一个父、静默忽略另一条 —— 实测用 `static_transform_publisher --frame-id map
--child-frame-id base_link` 给 `base_link` 造第二个父之后，`view_frames` 里
`base_link` 的 parent 仍然是原来的 `odom`，另一条像是从来没存在过。
所以想改树的结构，**必须连同广播端一起改**，光加一个广播器是没用的。

本包故意让 `odom -> base_link -> sensor` 单向串起来，就是遵守这条规则。

### 时间戳：TF 是"随时间变化的数据库"

`/tf` 上的每条变换都带时间戳，Buffer 默认缓存最近 10 秒。
查询时给时间戳有三种语义：

| 时间参数 | 含义 |
|---|---|
| `tf2::TimePointZero` | **给我最新的那条**（等价于 ROS 1 的 `ros::Time(0)`）。绝大多数场景用它 |
| `this->now()` | 要"此刻"的变换；若此刻还没广播出来会抛外推异常 |
| `this->now() + 10s` | 要"未来"的变换 —— 缓冲区里不可能有，**必然抛异常**（本包 `tf_listener` 第 3 件事就在演示它） |

静态变换（`/tf_static`）的时间戳会被忽略，它在**所有时刻**都有效。

## ROS 1 → ROS 2 对照

| ROS 1 (Noetic) | ROS 2 (Humble) |
|---|---|
| `tf::TransformListener listener;` | `tf2_ros::Buffer`（存数据） + `tf2_ros::TransformListener`（把 `/tf` 灌进 Buffer），**两个对象** |
| `listener.lookupTransform(a, b, t, result)`（引用出参） | `buffer.lookupTransform(a, b, tf2::TimePointZero)`（直接 **return** 结果） |
| `listener.waitForTransform(...)`（返回 bool，阻塞） | Humble 里 `waitForTransform` 已改为**异步**（返回 future、需要回调），没有返回 bool 的阻塞版本。启动等待改用 `canTransform` 轮询或用 `tf2_ros::Buffer` 的异步接口 |
| `tf::TransformBroadcaster br; br.sendTransform(tf::StampedTransform(...));` | `tf2_ros::TransformBroadcaster` / `tf2_ros::StaticTransformBroadcaster`，发 `geometry_msgs/TransformStamped`（`tf::StampedTransform` 已不存在） |
| `ros::Time` / `ros::Duration` | `rclcpp::Time` / `rclcpp::Duration`；TF 内部时间类型是 `tf2::TimePoint`（`std::chrono`），转换用 `tf2_ros::toMsg/fromMsg` |
| `tf::Quaternion q; q.setRPY(...)` | `tf2::Quaternion q; q.setRPY(...)`（**基本不变**），再 `tf2::toMsg(q)` 转成消息 |
| `tf`（tf1）与 `tf2` 并存 | **tf1 彻底退场**，只剩 tf2；ROS 2 里没有 `tf::TransformListener` 这类 API |
| `rosrun tf static_transform_publisher x y z yaw pitch roll frame child`（位置参数） | `ros2 run tf2_ros static_transform_publisher --x 1 --y 0 --z 0 --yaw 0 --frame-id map --child-frame-id odom`（**命名参数**，见下） |

> 老写法（位置参数）在 Humble 里**还能跑**，但会打印
> `Old-style arguments are deprecated; see --help for new-style arguments`。
> 请一律使用新语法，免得升级到新版本时脚本全废。

## 本包的 TF 树

```
map
 └── odom            静态变换，平移 (1.0, 0.5, 0.0)，无旋转        ← static_tf_broadcaster
      └── base_link  动态 20 Hz：x = 0.5·t，yaw = 0.5·t          ← dynamic_tf_broadcaster
           └── sensor 动态 20 Hz：平移 (0.3, 0.0, 0.2)，
                                yaw = 0.5·sin(2π·t/10)          ← dynamic_tf_broadcaster
```

`sensor` 在 `map` 下的位姿 = 三条变换依次相乘，所以 rviz2 里会看到
**平移（机器人前进）+ 自转 + 摆动**叠加起来的复合运动。

## 文件说明

```
p08_tf2/
├── package.xml
├── CMakeLists.txt
├── config/
│   └── tf2_demo.rviz              # 预置 rviz2 配置：Fixed Frame = map，已加 TF + MarkerArray
└── src/
    ├── static_tf_broadcaster.cpp  # map -> odom，1 Hz 重复广播静态变换
    ├── dynamic_tf_broadcaster.cpp # odom -> base_link 与 base_link -> sensor，20 Hz
    ├── tf_listener.cpp            # 1 Hz 查询：反向查询 / 点变换 / 外推报错演示
    └── frame_marker_publisher.cpp # 5 Hz 发 MarkerArray：机器人方块 + sensor 三轴箭头
```

## 编译运行

```bash
# 在仓库根目录（~/ros2_ws/ros2-humble-learning）
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select p08_tf2
source install/setup.bash
```

### 1. 三个终端分别启动（顺序不重要，谁先起来都能自愈）

```bash
ros2 run p08_tf2 static_tf_broadcaster    # 终端 A：静态 map -> odom
ros2 run p08_tf2 dynamic_tf_broadcaster   # 终端 B：动态 odom -> base_link -> sensor
ros2 run p08_tf2 tf_listener              # 终端 C：查询并打印（会等你 2 秒直到 TF 就绪）
```

`tf_listener` 的预期输出（每行每秒刷新一次）：

```
[INFO] ... TF 已就绪：sensor <-> map 连通
[INFO] ... [1] sensor <- map: x=0.376 y=2.652 z=-0.200 yaw=-2.015 rad
[INFO] ... [2] map 下的点 (1.000, 1.000, 0.000) 变到 sensor 系: (0.849, 1.319, -0.200)
[WARN] ... [3] 预期中的失败（外推）: Lookup would require extrapolation into the future. ...
```

最后那行 `[3]` 的 WARN 是**故意**的：它在演示"查询未来时刻必然失败"，不是 bug。

### 2. 用 tf2_echo 观察两个坐标系之间的实时变换

```bash
ros2 run tf2_ros tf2_echo map sensor      # Ctrl-C 停
```

会持续打印 `map -> sensor` 的平移/RPY/矩阵。把 `sensor` 换成 `base_link`，
可以看到它沿 x 一直前进、yaw 一直增大（机器人边跑边转圈）。

### 3. 看 /tf 的发布频率

```bash
ros2 topic hz /tf           # 约 40 Hz = 2 条动态变换 × 20 Hz
ros2 topic echo /tf_static --once   # 静态变换走另一个话题：只有一条 map->odom
```

> 注意：**静态变换不在 `/tf` 上**，而是单独走 `/tf_static`（这是很多人的直觉误区）。
> 两者 QoS 也不同：`/tf` 是 volatile（错过就没了，所以要 20 Hz 猛发），
> `/tf_static` 是 transient_local（锁存，后启动的订阅者也能补收到）。

### 4. 生成 TF 树图（需 graphviz，本机已装）

```bash
cd /tmp && ros2 run tf2_tools view_frames      # 监听 5 秒后生成 PDF
# 会在当前目录生成 frames_<日期时间>.pdf / .gv，用 PDF 阅读器打开即可看到上面那棵树
```

### 5. rviz2 可视化

```bash
rviz2 -d $(ros2 pkg prefix p08_tf2)/share/p08_tf2/config/tf2_demo.rviz
```

配置里已经加好两个显示项：**TF**（画坐标轴与帧名）和 **MarkerArray**（订阅 `/visualization_marker_array`），
Fixed Frame 设为 `map`。终端 D 启动 Marker 发布器：

```bash
ros2 run p08_tf2 frame_marker_publisher   # 终端 D：方块 + sensor 三轴箭头
```

应当看到：绿色方块钉在 map 原点，红/绿/蓝三根 arrow 随 sensor 一起"边前进边自转边摆动"。

> **若 .rviz 打不开**（例如 rviz2 版本差异导致配置报错），手动加两个显示项即可：
> 1. rviz2 左侧 Displays 面板左下角 **Add** → 选 **TF** → OK；勾选 Show Names / Show Axes
> 2. 再 **Add** → 选 **MarkerArray** → OK，把 Topic 填 `/visualization_marker_array`
> 3. 左上 **Global Options → Fixed Frame** 改成 `map`
>
> 顺手把 TF 的 **Marker Scale** 调大到 1.5~2，坐标轴看得更清楚。

### 6. 重点提醒：static_transform_publisher 的语法变了

Humble 必须用**命名参数**（ROS 1 的位置参数写法已废弃）：

```bash
ros2 run tf2_ros static_transform_publisher --x 1 --y 0 --z 0 --yaw 0 --frame-id map --child-frame-id odom
```

常用参数：`--x --y --z --roll --pitch --yaw --qx --qy --qz --qw --frame-id --child-frame-id`。
它发布的是 `/tf_static`，与终端 A 的 `static_tf_broadcaster` 功能完全一样 —— 区别是**不用写代码**。

## 本包踩到的 Humble API 变化（值得记一笔）

1. **没有返回 bool 的阻塞式 `waitForTransform`**。Humble 的 `tf2_ros::Buffer::waitForTransform`
   是异步版（返回 `TransformStampedFuture`，要传回调，还必须先注册 `CreateTimerInterface`）。
   `tf_listener.cpp` 里用 `canTransform` 轮询实现了"最多等 2 秒"的等价语义。
2. **`tf2::toMsg(tf2::TimePointZero)` 其实在 `tf2_ros` 命名空间**（`tf2_ros::toMsg`，
   定义在 `tf2_ros/buffer_interface.hpp`，包含 `tf2_ros/buffer.h` 即可用），
   不在 `tf2_geometry_msgs` 里。`tf2_geometry_msgs` 提供的是 `tf2::toMsg(tf2::Quaternion)` 这类几何类型的转换。
3. **`TransformListener` 默认 `spin_thread=true`**：它会给 `/tf`、`/tf_static` 单独建回调组并
   用自己的线程 spin。所以"在构造函数里阻塞等待 TF"才可行（本包就是这么做的）。
4. **`/tf_static` 是 transient_local（锁存）**：后启动的节点也能收到；`/tf` 不是，错过了就没有。
5. `geometry_msgs` 的四元数**默认值是 (0,0,0,0)**，不是单位四元数；写 Marker 时忘了设 `w=1.0` 会出现诡异姿态。

## 练习题

**基础**

1. 让 `dynamic_tf_broadcaster` 的机器人**沿 y 轴倒退**（而不是沿 x 前进），用 `tf2_echo odom base_link` 验证。
2. 把 `sensor` 的安装位置从 `(0.3, 0.0, 0.2)` 改成 `(0.0, 0.0, 0.5)`（正上方），
   观察 `tf_listener` 打印的 `[2]` 点变换结果怎么变，再用 rviz2 看看三根轴的位置。
3. 用命令行给 TF 树加一条静态变换（不用写代码）：
   `ros2 run tf2_ros static_transform_publisher --x 0 --y 0 --z 0.1 --frame-id base_link --child-frame-id laser`
   然后 `ros2 run tf2_ros tf2_echo base_link laser` 验证；最后用 `view_frames` 在 PDF 里确认这条边出现了。

**进阶**

4. 加一个 `base_footprint` 帧（`odom` 的子、`base_link` 的父，REP-105 的标准做法）：
   先想清楚**为什么不能只加一条 `static_transform_publisher --frame-id odom --child-frame-id base_footprint`** ——
   因为 `odom -> base_link` 已经由动态广播器发布了，而 `base_link` 只能有一个父，
   多出来的那条会被静默忽略（见上文"TF 树"一节），`base_footprint` 根本进不了树。
   正确做法二选一：
   - 把 `dynamic_tf_broadcaster.cpp` 里 `odom_to_base.child_frame_id` 从 `base_link` 改成 `base_footprint`，
     再用 `static_transform_publisher --z 0.1 --frame-id base_footprint --child-frame-id base_link`
     把 `base_link` 挂上去（这才是真实机器人的结构：`base_footprint` 是地面投影）；
   - 或者让 `base_footprint` 做 `base_link` 的子帧：
     `static_transform_publisher --z -0.1 --frame-id base_link --child-frame-id base_footprint`
     （注意 z 是 **-0.1**，地面投影点在 base_link 下方）。
   两种都跑一遍 `view_frames`，对比 PDF 里的树形。
5. 把 `sensor` 的**摆动频率**做成参数（当前是写死的 10 秒周期）：
   `declare_parameter<double>("swing_period", 10.0)`，用 `ros2 run p08_tf2 dynamic_tf_broadcaster --ros-args -p swing_period:=2.0` 观察摆动变快。
   （提示：`yaw = 0.5 * sin(2π t / period)`，`rate` 参数已有，照着改。）
6. 解释并验证外推错误的**发生条件**：把 `tf_listener.cpp` 里第 3 件事的时间改成 `this->now() - 20s`
   （过去 20 秒，超出 Buffer 默认 10 秒缓存），看异常变成什么样；再改成 `this->now() - 1s`（缓存内），
   看它是否能成功。总结"什么时刻能查到、什么时刻查不到"。
