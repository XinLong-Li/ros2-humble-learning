# 学习路线图与计划

> 目标读者：有 ROS 1（Noetic）使用经验的开发者。
> 总时长约 **40~50 小时**（每天 2~3 小时 ≈ 3~4 周）。

## 总览

| 阶段 | 学习目标 | 对应代码 | 与 ROS 1 相比最大的变化 | 预估 | 验收标准 |
|---|---|---|---|---|---|
| **S0** 环境与工具链 | colcon/bashrc/CLI 一次配好 | —（[00-setup.md](00-setup.md)） | `catkin_make`→`colcon build`；`devel/`→`install/`；**没有 roscore** | 2 h | `colcon --help`、`ros2 run demo_nodes_cpp talker` 能跑 |
| **S1** 第一个节点 | 包结构、Node 类、pub/sub | p01 | 继承 `rclcpp::Node` 取代 `NodeHandle`；日志宏全换 | 3 h | `ros2 topic hz /chatter` ≈ 1 Hz |
| **S2** 自定义接口 | 会写 .msg/.srv/.action 并在别的包用 | p02 | `message_generation`→rosidl；类型名两段→三段 | 3 h | `ros2 interface show p02_interfaces/msg/RobotStatus` |
| **S3** 话题与 QoS | 理解 QoS，能解释"收不到数据" | p03 | **QoS 全新概念**；latch→transient_local；TCPROS→DDS | 5 h | 复现并解释 QoS 不匹配静默失败 |
| **S4** 服务与执行器 | 同步/异步客户端、回调组、执行器 | p04 | 阻塞 call()→future；AsyncSpinner→显式 Executor+CallbackGroup | 4 h | 并发实验：多线程版约 3 s vs 单线程 6 s |
| **S5** 动作 | goal/feedback/result/cancel 全流程 | p05 | actionlib→rclcpp_action（三回调+GoalHandle） | 5 h | `ros2 action send_goal --feedback` 逐步反馈；取消成功 |
| **S6** 参数 | 声明/校验/文件化 | p06 | 全局参数服务器→**每节点参数且必须声明** | 4 h | 非法值被校验回调拒绝 |
| **S7** Launch | 用 Python 组织多节点 | p07 | roslaunch XML→launch.py | 4 h | `06_full_system.py` 一键起 8 节点 |
| **S8** TF2 | 广播/查询坐标变换 | p08 | tf1 彻底退场；Buffer+Listener；CLI 参数变化 | 5 h | tf2_echo 正常、view_frames 出 PDF、rviz2 显示 TF 树 |
| **S9** 综合项目 | 四类接口+TF+参数+launch 组合 | p09 | 工具全家桶（ros2 bag、rqt、rviz2） | 6–8 h | 乌龟自动走完全部航点 |

> **有 Noetic 基础的可以压缩 S0~S2**；**S3（QoS）、S5（动作）、S8（TF2）新概念密度最高，建议完整走**。
> 每阶段结束都有一个"进阶挑战"，卡住 30 分钟再翻 `docs/12-troubleshooting.md`。

## 最小路径 vs 完整路径

- **最小路径**（~30 h）：跳过各 README 里标"进阶"的节点（p08 的 `frame_marker_publisher`、p09 的 `turtle_square_drawer`/`turtle_pose_monitor` 可选），练习题只做"基础"档。
- **完整路径**（~50 h）：所有节点 + 全部练习题。

## 每日节奏建议

1. **读**：先读该阶段的 docs 文档（30 分钟），重点看"与 ROS 1 相比变了什么"
2. **跑**：按 README 把示例跑通，用 CLI 验证（1 小时）
3. **改**：动手做练习题，改代码-重编-重跑（1 小时）
4. **记**：把踩的坑追加到自己的笔记或 `docs/12-troubleshooting.md`

## 进度自检清单

- [ ] S0：`printenv ROS_DISTRO` 输出 humble；colcon 可用
- [ ] S1：能不看资料写一个最小 pub/sub 包
- [ ] S2：能定义带嵌套字段的 msg 并在别的包引用
- [ ] S3：能解释 best_effort/reliable 匹配规则；知道 latched 的新名字
- [ ] S4：知道单线程执行器里回调阻塞的后果与解法
- [ ] S5：知道 goal handle 三个回调各负责什么；会取消
- [ ] S6：会 declare + 校验 + YAML 三种加载方式
- [ ] S7：能把 8 个节点用 launch 一键拉起
- [ ] S8：会 broadcast + lookup，遇到过并解决外推错误
- [ ] S9：turtle_mission 跑通全航点；rosbag 录制回放正常
