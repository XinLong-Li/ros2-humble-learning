# 话题与 QoS（p03）

> 对应代码：`src/p03_topics/`。QoS 是 ROS 1→2 最大的新概念，本阶段请完整走完实验。

## 1. 为什么 ROS 2 需要 QoS

ROS 1 的话题基于自研 TCPROS/UDPROS：`queue_size` 是唯一的"质量"旋钮，latched 是唯一的"持久"选项。
ROS 2 底层换成 **DDS**（默认 FastDDS），DDS 工业界打磨多年的 **QoS（Quality of Service）**
体系被原样带进来：可靠性、持久性、历史、深度、期限、活跃度……通信两端 QoS **不兼容 = 静默收不到**。

## 2. QoS 五要素（本包演示前三个）

| 策略 | 取值 | 含义 | ROS 1 对照 |
|---|---|---|---|
| Reliability 可靠性 | Reliable / BestEffort | 必达（丢包重传）/ 尽力而为（丢就丢） | 默认 Reliable 类似 TCPROS |
| Durability 持久性 | Volatile / TransientLocal | 不存历史 / 为后到的订阅者保留最后一条 | **latch=true ≈ TransientLocal** |
| History 历史 | KeepLast / KeepAll | 只留最近 N 条 / 全留 | queue_size ≈ KeepLast(depth) |
| Depth 深度 | 整数 | KeepLast 时的 N | queue_size |
| Deadline 期限 | 时长 | 超过该间隔没收到消息即触发回调（本包没演示，练习里有） | 无 |

## 3. 兼容性规则（重点！）

- **Reliable 发布 + BestEffort 订阅**：✅ 兼容（订阅者得到"降级"服务）
- **BestEffort 发布 + Reliable 订阅**：❌ **不兼容——静默收不到**（p03 的核心实验）
- **TransientLocal 发布 + Volatile 订阅**：✅ 兼容
- **Volatile 发布 + TransientLocal 订阅**：✅ 兼容但收不到历史

> 实测（Humble/rclcpp 16.0.19）：不兼容时 rclcpp **可能**在发布端打一条
> `requesting incompatible QoS ... RELIABILITY_QOS_POLICY` 的 WARN，但**出现时机不稳定**，
> 订阅端毫无提示。所以排查不能靠日志，靠：`ros2 topic info /qos_demo -v`
> （看两端 QoS 是否一致）。另外 `ros2 topic echo` 在 Humble 会**自动探测**发布端 QoS，
> 所以直接 echo 反而收得到——别被这个"假象"误导，你的订阅节点是收不到的。

## 4. 三个实验（按序做）

**实验 A：正常链路**（p03 的 status_publisher/subscriber）
```bash
ros2 run p03_topics status_publisher &
ros2 run p03_topics status_subscriber
ros2 topic info /robot_status -v        # 看 RobotStatus 端点与 QoS
ros2 topic hz /robot_status             # ≈10 Hz
```

**实验 B：静默失败与诊断**
```bash
ros2 run p03_topics qos_publisher --ros-args -p reliability:=best_effort
ros2 run p03_topics qos_subscriber  --ros-args -p reliability:=reliable   # 收不到！
ros2 topic info /qos_demo -v        # 两端 QoS 不一致 → 真凶
# 修复：把订阅端也改成 best_effort，或把发布端改回 reliable
```

**实验 C：transient_local（= latched）**
```bash
ros2 run p03_topics qos_publisher --ros-args -p durability:=transient_local
# 等几秒，让它发几条消息，然后：
ros2 run p03_topics qos_subscriber --ros-args -p durability:=transient_local
# 后启动的订阅者立刻收到最后一条消息——ROS 1 的 latch=true 换了个名字
```

## 5. 常用 QoS 预设（ros2 topic 等工具内置）

`sensor_data`（best_effort，传感器默认）、`system_default`、`parameters`（参数服务专用，transient_local）、`services_default`、`action_status_default`。用 `ros2 topic echo --qos-profile sensor_data` 可指定。

## 6. 练习线索

- 基础 2（rqt_graph）：注意 rqt_graph 只显示"话题级"连接，QoS 不匹配时线仍然在——再想想为什么（发现≠可通信）。
- 进阶 deadline：deadline 需要发布端也设 deadline（发布端超时自动"空发"），订阅端注册 deadline 回调，用 QoS 兼容性工具验证。
