# 工作空间、包与第一个节点（p01）

> 对应代码：`src/p01_hello_ros2/`。先跑通再读本文，效果更好。

## 1. colcon 工作空间

```
ros2-humble-learning/          ← 工作空间根
├── src/                       ← 源码（colcon 只认这里）
├── build/                     ← 中间产物（CMake 缓存、生成的头文件）
├── install/                   ← 最终产物（可执行文件、库、share/）★ 这是 overlay 的根
└── log/                       ← 构建日志
```

与 ROS 1 的对照：

| | ROS 1 | ROS 2 |
|---|---|---|
| 构建命令 | `catkin_make` | `colcon build` |
| 环境脚本 | `source devel/setup.bash` | `source install/setup.bash` |
| 概念 | 一个 `devel/` 混着用 | install/ 是"隔离的安装目录"，可被多个工作空间链式 source（underlay/overlay） |
| 常用开关 | `catkin_make -DCATKIN_WHITELIST_PACKAGES=...` | `colcon build --packages-select <pkg>` |
| 符号链接安装 | `catkin build` 默认 | `colcon build --symlink-install` |

**必知**：`colcon build` 会自动按 `package.xml` 里的依赖做拓扑排序，不需要手动指定顺序。

## 2. ament_cmake 包解剖

一个最小的 C++ 包 = 3 个文件：

### package.xml —— 包的"身份证"
```xml
<package format="3">
  <name>p01_hello_ros2</name>
  <version>0.1.0</version>
  <maintainer email="...">...</maintainer>
  <license>MIT</license>

  <buildtool_depend>ament_cmake</buildtool_depend>   <!-- 构建系统本身 -->
  <depend>rclcpp</depend>                            <!-- 编译+运行都要 -->
  <depend>std_msgs</depend>

  <export>
    <build_type>ament_cmake</build_type>             <!-- 必须声明构建类型 -->
  </export>
</package>
```
依赖标签速记：`<depend>`=编译+导出+运行都要（最常用）；`<exec_depend>`=只在运行期要
（如 launch 文件里用到的其他包）；`<buildtool_depend>`=构建工具（ament_cmake）。

### CMakeLists.txt —— 构建规则
```cmake
find_package(ament_cmake REQUIRED)     # 每个依赖都要 find_package
find_package(rclcpp REQUIRED)

add_executable(hello_publisher src/hello_publisher.cpp)
target_link_libraries(hello_publisher rclcpp::rclcpp std_msgs::std_msgs)
                                        # 现代 CMake target 写法

install(TARGETS hello_publisher hello_subscriber
  DESTINATION lib/${PROJECT_NAME})      # ★ 漏了这句：编译成功但 ros2 run 找不到

ament_package()                          # 必须最后一行
```

### 节点源码 —— 三段式
```cpp
rclcpp::init(argc, argv);                          // 取代 ros::init
rclcpp::spin(std::make_shared<HelloPublisher>());  // 取代 ros::spin
rclcpp::shutdown();
```

## 3. 发布者/订阅者逐行要点

- `Node("hello_publisher")`：构造函数里给节点起名。**节点名是全局唯一的**，
  同名后启动的节点会被改名成 `name_1`（ROS 1 则是后启动的抢名字）。
- `create_publisher<T>("chatter", 10)`：模板参数决定消息类型；10 是 QoS depth（≈ queue_size）。
- 回调参数是 `std::shared_ptr<const T>`：消息所有权归订阅者，回调只能读不能改。
- `RCLCPP_INFO(this->get_logger(), ...)`：每个节点有自己的 logger。
- 定时器取代 `ros::Rate` 轮询：`create_wall_timer(1s, cb)` 是 ROS 2 的惯用法
  （ROS 1 的 ros::Rate 循环在 ROS 2 里能写但不再推荐）。

## 4. 验证实验清单

```bash
ros2 node list          # 看节点（注意 daemon 不在里面）
ros2 topic list -t      # 看话题与类型
ros2 topic info /chatter -v   # 端点详情：GID、QoS——这是 ROS 2 的"连接状态检查"
ros2 topic echo /chatter
ros2 topic hz /chatter  # 频率统计 ≈1.0
```

**观察实验**：把 publisher 的话题名改成拼错的 `/chater`，重编重跑——
`ros2 topic echo /chatter` 什么也没有，**没有任何报错**。没有 master 的世界里，
连接失败是静默的。这就是 `ros2 topic info -v` 存在的意义。

## 5. 练习题参考答案线索

（见 p01 README）基础 2 换 Int32 时注意 `std_msgs::msg::Int32` 的字段是 `data`；
进阶 4 需要 `declare_parameter("publish_rate", 1.0)` + `get_parameter` 读值后重设定时器
（Humble 里定时器周期运行期不可改，需要 cancel 旧 timer 再建新的——这是练习的关键点）。
