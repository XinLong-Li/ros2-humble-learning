# ros2-humble-learning

ROS 2 Humble 系统学习仓库：**C++（rclcpp）优先**的可运行示例 + 中文学习笔记 + 课后练习。

面向有 **ROS 1（Noetic）** 使用经验、希望系统学习 ROS 2 的开发者。每篇文档都会重点标注「**与 ROS 1 相比变了什么**」。

- 环境：WSL2（Ubuntu 22.04）+ ROS 2 Humble（desktop full）
- 语言：C++ 17（rclcpp）；launch 文件使用官方推荐的 Python 写法
- 风格：每个项目都是**完整可运行的示例**，配合 README 讲解 + 课后练习题

## 目录结构

```
ros2-humble-learning/
├── README.md            # 本文件：总览、路线图、快速开始
├── docs/                # 13 篇中文学习文档（见「文档」一节）
└── src/                 # 9 个学习包（自包含 colcon 工作空间）
```

## 学习包总表

| 包 | 学习内容 | 可执行文件（节点） |
|---|---|---|
| [`p01_hello_ros2`](src/p01_hello_ros2/) | 工作空间、colcon、包结构、第一个节点 | `hello_publisher`、`hello_subscriber` |
| [`p02_interfaces`](src/p02_interfaces/) | 自定义 msg / srv / action 接口 | （纯接口包，无节点） |
| [`p03_topics`](src/p03_topics/) | 话题与 QoS（含"静默不匹配"实验） | `status_publisher`、`status_subscriber`、`qos_publisher`、`qos_subscriber` |
| [`p04_services`](src/p04_services/) | 服务、回调组、执行器并发 | `compute_sum_server`、`compute_sum_server_mt`、`compute_sum_client`、`compute_sum_client_async` |
| [`p05_actions`](src/p05_actions/) | 动作：goal/feedback/result/cancel | `countdown_server`、`countdown_client`、`navigate_server`、`navigate_client` |
| [`p06_params`](src/p06_params/) | 参数：声明/校验/事件/YAML | `param_demo_node`、`param_client_node` |
| [`p07_launch`](src/p07_launch/) | launch 系统（Python + XML 对照） | （launch 文件包，无节点） |
| [`p08_tf2`](src/p08_tf2/) | TF2 坐标变换 | `static_tf_broadcaster`、`dynamic_tf_broadcaster`、`tf_listener`、`frame_marker_publisher` |
| [`p09_turtle_control`](src/p09_turtle_control/) | 综合项目：控制 turtlesim 乌龟 | `turtle_tf_broadcaster`、`turtle_pose_monitor`、`turtle_square_drawer`、`turtle_service_client`、`turtle_rotate_client`、`navigate_to_server`、`waypoint_follower` |

> 编号即建议学习顺序。`p02_interfaces` 故意放在第二位：ROS 1 用户习惯"自己包里的类型"，且自定义接口是 ROS 1→2 变化最大的一环（`message_generation` → rosidl），早学会让后续每个包都用上自己的接口。

## 文档

`docs/NN-*` 与 `src/p(NN-01)_*` 一一对应（docs/02 ↔ p01 … docs/10 ↔ p09）：

| 文档 | 内容 |
|---|---|
| [plan.md](docs/plan.md) | 学习路线图：10 个阶段的目标、耗时、验收标准 |
| [00-setup.md](docs/00-setup.md) | 环境搭建（WSL2 + Humble）与命令速查 |
| [01-ros1-to-ros2.md](docs/01-ros1-to-ros2.md) | ROS 1 → ROS 2 概念映射表 |
| [02-workspace-and-first-node.md](docs/02-workspace-and-first-node.md) | 工作空间、包与第一个节点（p01） |
| [03-custom-interfaces.md](docs/03-custom-interfaces.md) | 自定义接口 msg/srv/action（p02） |
| [04-topics-and-qos.md](docs/04-topics-and-qos.md) | 话题与 QoS（p03） |
| [05-services.md](docs/05-services.md) | 服务与执行器（p04） |
| [06-actions.md](docs/06-actions.md) | 动作 action（p05） |
| [07-parameters.md](docs/07-parameters.md) | 参数与配置（p06） |
| [08-launch.md](docs/08-launch.md) | launch 启动系统（p07） |
| [09-tf2.md](docs/09-tf2.md) | TF2 坐标变换（p08） |
| [10-turtlesim-project.md](docs/10-turtlesim-project.md) | 综合项目：控制小乌龟（p09） |
| [11-tools-and-debugging.md](docs/11-tools-and-debugging.md) | 工具链与调试（CLI/rqt/rviz2/rosbag） |
| [12-troubleshooting.md](docs/12-troubleshooting.md) | 常见错误与排查手册 |
| [13-turtlesim-teleop-remap.md](docs/13-turtlesim-teleop-remap.md) | 案例复盘：官方教程的键盘 remap 为什么在 Humble 上失效 |
| [14-colcon-test-flaky-failures.md](docs/14-colcon-test-flaky-failures.md) | `colcon test` 环境性假失败排查（xmllint 联网、并发抢 DDS） |
| [15-git-clone-and-shell-notes.md](docs/15-git-clone-and-shell-notes.md) | git clone 与 shell 笔记：代理变量、`-C`、重定向 |

## 快速开始

```bash
# 1. 加载 ROS 2 环境（未写入 ~/.bashrc 时每次新终端都要执行）
source /opt/ros/humble/setup.bash

# 2. 构建全部包（首次 1~3 分钟，此后增量编译只要几秒）
cd ~/ros2-humble-learning
colcon build --symlink-install

# 3. 加载本工作空间的 overlay
source install/setup.bash

# 4. 跑第一个示例（两个终端）
ros2 run p01_hello_ros2 hello_publisher     # 终端 A
ros2 run p01_hello_ros2 hello_subscriber    # 终端 B
```

> `--symlink-install`：launch / yaml / rviz 配置改动无需重新编译，学习期最划算的开关（改 `.cpp` 仍需重编）。

## 学习路线（详细见 [docs/plan.md](docs/plan.md)，总计约 40~50 小时）

| 阶段 | 内容 | 对应包 |
|---|---|---|
| S0 | 环境与工具链（colcon / bashrc / CLI） | — |
| S1 | 第一个节点 | p01 |
| S2 | 自定义接口 | p02 |
| S3 | 话题与 QoS | p03 |
| S4 | 服务与执行器 | p04 |
| S5 | 动作 | p05 |
| S6 | 参数 | p06 |
| S7 | launch | p07 |
| S8 | TF2 | p08 |
| S9 | turtlesim 综合项目 | p09 |

有 Noetic 基础的读者，S0~S2 可以压缩进度；**S3（QoS）、S5（动作）、S8（TF2）新概念密度最高**，建议不要跳过。

## 结课验收清单

- [ ] `colcon build --symlink-install` 全量编译通过，`ros2 pkg list | grep -c '^p0'` 输出 9
- [ ] 话题：talker/listener 互发正常，`ros2 topic hz /chatter` 约 1 Hz
- [ ] QoS：能复现并解释 best_effort 发布者 + reliable 订阅者"静默收不到"现象
- [ ] 服务：`ros2 service call /compute_sum` 返回正确结果
- [ ] 动作：`ros2 action send_goal ... --feedback` 看到逐步反馈，取消路径正常
- [ ] 参数：`ros2 param set` 非法值被校验回调拒绝
- [ ] launch：`ros2 launch p07_launch 06_full_system.py` 一键启动 8+ 节点
- [ ] TF2：`tf2_echo` 输出合理，`tf2_tools view_frames` 生成 PDF，rviz2 显示 TF 树
- [ ] 综合：`turtle_mission.launch.py` 启动后乌龟遍历全部航点
- [ ] rosbag：`ros2 bag record/play` 录制回放正常

## 课后练习约定

每个包的 README 末尾都有**练习题**小节，分「基础」和「进阶」两档。建议按顺序完成，答案不提供——卡住超过 30 分钟再去翻官方文档或参考 `/opt/ros/humble/share/` 下的官方示例源码。

## License

[MIT](LICENSE) © 2026 Xinlong-Li

## 致谢

参考了 ROS 2 官方教程与安装包自带的示例源码（`demo_nodes_cpp`、`turtlesim`、`action_tutorials_cpp`、`examples_rclcpp_minimal_*`）。
