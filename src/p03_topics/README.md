# p03_topics —— 话题与 QoS

配套文档：[docs/04-topics-and-qos.md](../../docs/04-topics-and-qos.md)

## 学习目标

1. 用自定义消息（`p02_interfaces/msg/RobotStatus`）做发布/订阅，理解 `Header`（`stamp` / `frame_id`）为什么必须填
2. 理解 ROS 2 的 QoS 不是"一个队列长度"，而是可靠性、持久性、历史与深度等**一组策略**，并且**在创建端点时就固化**；
   学会用参数 + YAML 让同一份代码跑出不同 QoS（`--ros-args -p` / `--params-file`）
3. 亲手复现并解释 **best_effort 发布 + reliable 订阅的静默不匹配**：一条也收不到，也没有任何报错/异常
4. 理解 `transient_local` 如何取代 ROS 1 的 `latch=true`，让"迟到的订阅者"也能拿到历史样本

## ROS 1 → ROS 2 对照

| ROS 1 (Noetic) | ROS 2 (Humble) | 说明 |
|---|---|---|
| `advertise<T>(topic, queue_size)` 的 `queue_size` | `create_publisher<T>(topic, depth)` 的 `depth` | ROS 1 里它只是发送队列长度；ROS 2 里 depth 只是 QoS 中 history 的一部分 |
| `advertise<T>(topic, 1, latch=true)` | `qos.durability(TransientLocal)` | 语义等价：为晚加入的订阅者保留历史样本 |
| `nh.subscribe<T>(topic, queue_size, cb)` | `create_subscription<T>(topic, qos, cb)` | 订阅端也要选 QoS，**兼容性由两端共同决定** |
| TCPROS / UDPROS | DDS（Humble 默认 `rmw_fastrtps_cpp`） | 传输层换成 DDS，QoS 就是 DDS 的 QoS |
| `rostopic echo / list / info / hz` | `ros2 topic echo / list / info / hz` | 命令一一对应，ROS 2 多了 `--qos-*` 参数 |
| 连不上时 `rostopic hz` 无输出、无报错 | 同样不出错，消息安静地不来 | ROS 2 没有 master，QoS 不兼容既不抛异常也不让节点退出 |
| 发布者/订阅者靠引用计数决定连接 | DDS 端点自动发现 + QoS 兼容性判定 | 只有 QoS 兼容的端点之间才会真正建立连接 |

### QoS 五要素

| 策略 | 常用取值 | 作用 | ROS 1 对应 |
|---|---|---|---|
| Reliability 可靠性 | `reliable` / `best_effort` | reliable 丢包重传（TCP 味）；best_effort 尽力而为、延迟更低（UDP 味） | TCPROS / UDPROS |
| Durability 持久性 | `volatile` / `transient_local` | transient_local 为晚加入的订阅者保留历史样本 | `latch=true` |
| History 历史 | `keep_last` / `keep_all` | 只保留最近若干条，还是一条不丢全留 | `queue_size` 的语义 |
| Depth 深度 | 整数，如 5 / 10 | `keep_last` 时到底保留几条（本包的 `depth` 参数） | `queue_size` |
| Deadline 截止时间 | 时长，如 `100ms` | 期望的消息间隔；超时可触发 deadline 事件 | 无 |

> 完整策略还有 `lifespan`（消息有效期）和 `liveliness`（发布者存活判定），实际工程里前五条用得最多。

### 兼容性规则（最关键的一条）

**订阅端请求的强度不能高于发布端提供的强度**，否则 DDS 不会建立连接：

| 发布端 | 订阅端 | 能否收到 |
|---|---|---|
| reliable | reliable | 能 |
| reliable | best_effort | 能（订阅端只是享受不到重传） |
| best_effort | best_effort | 能 |
| best_effort | **reliable** | **不能，且两端都不报错** |
| transient_local | transient_local / volatile | 能 |
| volatile | **transient_local** | **不能，且两端都不报错** |

## 文件说明

```
p03_topics/
├── package.xml
├── CMakeLists.txt
├── config/
│   └── qos_demo.yaml           # 故意配成不匹配：发布端 best_effort，订阅端 reliable
└── src/
    ├── status_publisher.cpp     # 节点 status_publisher：10 Hz 发布 RobotStatus 到 /robot_status
    ├── status_subscriber.cpp    # 节点 status_subscriber：订阅 /robot_status 并打印
    ├── qos_publisher.cpp        # 节点 qos_publisher：1 Hz 发布 Int32 到 /qos_demo，QoS 由参数决定
    └── qos_subscriber.cpp       # 节点 qos_subscriber：订阅 /qos_demo，QoS 同样由参数决定
```

| 节点 | 话题（角色） | 消息类型 | 频率 | 参数（默认值） |
|---|---|---|---|---|
| `status_publisher` | `/robot_status`（发布） | `p02_interfaces/msg/RobotStatus` | 10 Hz | `robot_name`（`turtle_x`） |
| `status_subscriber` | `/robot_status`（订阅） | 同上 | — | 无 |
| `qos_publisher` | `/qos_demo`（发布） | `std_msgs/msg/Int32` | 1 Hz | `reliability`（`reliable`）、`durability`（`volatile`）、`depth`（`10`） |
| `qos_subscriber` | `/qos_demo`（订阅） | 同上 | — | 同上 |

`RobotStatus` 的模拟规则：电量每 tick 掉 0.1%，跌破 20% 瞬间充满（约 80 s 一轮）；位置在半径 2 m 的圆上每 tick 转 0.2 rad。

## 编译运行

### 0. 构建

```bash
source /opt/ros/humble/setup.bash
cd ~/ros2-humble-learning
colcon build --symlink-install --packages-select p02_interfaces p03_topics
source install/setup.bash
```

> `p03_topics` 依赖 `p02_interfaces`（自定义消息）。首次构建要把两个包一起选上，否则
> `find_package(p02_interfaces)` 直接失败。`--symlink-install` 让 `config/*.yaml` 的改动免重编。

### 1. 正常链路：/robot_status（自定义消息）

```bash
ros2 run p03_topics status_publisher      # 终端 A
ros2 run p03_topics status_subscriber     # 终端 B
```

终端 A 每 10 条（约 1 s）打一次日志，终端 B 每条都打印。另开终端验证：

```bash
ros2 node list                                          # status_publisher / status_subscriber
ros2 topic list
ros2 topic info /robot_status -v                        # 两端 QoS 都是 RELIABLE + VOLATILE（depth 10）
ros2 topic hz /robot_status                             # 应约 10 Hz
ros2 topic echo /robot_status --once                    # 只打一条
ros2 topic echo /robot_status --field battery_percentage # 只看电量，观察它缓慢下降
# （--field 打的是原始 double，会看到 84.80000000000086 这样的浮点误差；
#   节点自己的日志用 %.1f 格式化过，所以显示 84.8）
```

再开一个改了名字的发布者（终端 C），可以看到两个发布者共存，订阅者两条都收：

```bash
ros2 run p03_topics status_publisher --ros-args -p robot_name:=r2d2
```

### 2. 实验一：QoS 静默失败（best_effort 发布 + reliable 订阅）

发布端要 best_effort，订阅端要 reliable —— 这是**不兼容**的组合。

```bash
# 终端 A：发布端 best_effort + volatile（config/qos_demo.yaml 里就是这么配的）
ros2 run p03_topics qos_publisher --ros-args --params-file src/p03_topics/config/qos_demo.yaml

# 终端 B：订阅端 reliable + volatile
ros2 run p03_topics qos_subscriber --ros-args --params-file src/p03_topics/config/qos_demo.yaml
```

现象：A 每秒打印 `发布: N`，B 只打印启动日志，**一条也收不到，也没有任何报错或异常**（节点都活得好好的）。

关于"静默"的准确说法：不兼容不会抛异常、不会让节点退出、订阅端不会打印任何东西。
但 Humble 的 rclcpp 在检测到不兼容时，会在发布端和/或订阅端打一条 WARN：

```
[qos_publisher] New subscription discovered on topic '/qos_demo', requesting incompatible QoS.
                No messages will be sent to it. Last incompatible policy: RELIABILITY_QOS_POLICY
[qos_subscriber] New publisher discovered on topic '/qos_demo', offering incompatible QoS.
                No messages will be sent to it. Last incompatible policy: RELIABILITY_QOS_POLICY
```

这条日志出现得很不稳定（实测可能滞后几秒，短时间跑也可能看不到），而且它只说"有个不兼容的端点"，
不告诉你是谁配错了。所以**排查 QoS 问题不要依赖日志，要靠下面这条命令**。

定位问题 —— 用 `-v` 把两端的端点 QoS 打出来对比：

```bash
ros2 topic info /qos_demo -v
```

发布端与订阅端各有一段 `QoS profile:`，重点看 `Reliability:`：

```
Publisher count: 1

Node name: qos_publisher
Endpoint type: PUBLISHER
QoS profile:
  Reliability: BEST_EFFORT        <-- 发布端：尽力而为
  History (Depth): KEEP_LAST (10)
  Durability: VOLATILE
  Deadline: Infinite

Subscription count: 1

Node name: qos_subscriber
Endpoint type: SUBSCRIPTION
QoS profile:
  Reliability: RELIABLE           <-- 订阅端：要求可靠，比发布端"强" → 不兼容，不建立连接
  History (Depth): KEEP_LAST (10)
  Durability: VOLATILE
  Deadline: Infinite
```

> 实测（Humble + Fast DDS）：`History (Depth)` 那行会显示 `UNKNOWN`，换别的 rmw 可能显示 `KEEP_LAST (10)`。
> 这不影响判断 —— 只需盯住 `Reliability` / `Durability` 两行，看它们是否相同、或"订阅端不高于发布端"。

再证明"**数据其实一直在发**"——把订阅端也降成 best_effort，或直接让 CLI 用 best_effort 订阅：

```bash
ros2 topic echo /qos_demo --qos-reliability best_effort   # 立刻收到，说明发布端没问题
ros2 topic echo /qos_demo --qos-reliability reliable      # 什么都收不到，同样静默
ros2 topic hz /qos_demo                                   # hz 用 best_effort 订阅，能收到，约 1 Hz
```

> 陷阱提示：Humble 的 `ros2 topic echo` 在不显式给 `--qos-*` 时会先探测发布端 QoS，再自动匹配
> （发布端都是 reliable 就跟着要 reliable，都 transient_local 就跟着要 transient_local），所以
> 直接 `ros2 topic echo /qos_demo` 反而能收到。**C++ 节点没有这个自适应**：代码里写死什么就是什么，
> 这正是"静默失败"最容易踩的坑 —— 出了问题先查 QoS，别怀疑时钟同步或消息类型。

恢复正常有两种办法（任选其一）：

```bash
ros2 run p03_topics qos_subscriber --ros-args -p reliability:=best_effort   # 订阅端降到 best_effort
ros2 run p03_topics qos_publisher                                            # 或发布端升回默认的 reliable
```

### 3. 实验二：transient_local 取代 latch

```bash
# 终端 A：发布端 durability=transient_local（depth 用默认 10），跑几秒，别停
ros2 run p03_topics qos_publisher --ros-args -p durability:=transient_local

# 终端 B：晚一点再启动订阅端，durability 也设成 transient_local
ros2 run p03_topics qos_subscriber --ros-args -p durability:=transient_local
```

现象：B 一启动就**立刻补收到发布者缓存的历史样本**（值不是从 0 开始），完全不用等 A 的下一条 ——
这就是 ROS 1 `latch=true` 的效果。注意 `keep_last(10)` 意味着最多补 10 条；只想留"最后一条"，
把发布端的 `depth` 设成 1 即可：

```bash
ros2 run p03_topics qos_publisher --ros-args -p durability:=transient_local -p depth:=1
```

对照实验：订阅端用默认的 volatile 启动，它就要等到 A 的下一条消息才会打印第一条 —— volatile 拿不到历史。

```bash
ros2 run p03_topics qos_subscriber --ros-args -p durability:=volatile
```

用 CLI 直接验证"历史样本还在"（发布端要保持运行）：

```bash
ros2 topic echo /qos_demo --qos-reliability reliable --qos-durability transient_local --once
```

> 两个前提：① 历史样本只存在发布者进程的 history 缓存里，发布者一退出就没了（这和 ROS 1 latched 一样）；
> ② durability 之外 reliability 也必须兼容，本例两端都是 reliable。

### 4. 一整套可复制命令序列

```bash
# ---------- 0) 构建 ----------
source /opt/ros/humble/setup.bash
cd ~/ros2-humble-learning
colcon build --symlink-install --packages-select p02_interfaces p03_topics
source install/setup.bash

# ---------- 1) 正常链路：/robot_status ----------
# 终端 A
ros2 run p03_topics status_publisher
# 终端 B
ros2 run p03_topics status_subscriber
# 终端 C
ros2 topic hz /robot_status
ros2 topic echo /robot_status --field battery_percentage

# ---------- 2) QoS 静默不匹配：/qos_demo ----------
# 终端 A（发布端 best_effort）
ros2 run p03_topics qos_publisher --ros-args --params-file src/p03_topics/config/qos_demo.yaml
# 终端 B（订阅端 reliable）—— 一条也收不到，不报错（详见实验一）
ros2 run p03_topics qos_subscriber --ros-args --params-file src/p03_topics/config/qos_demo.yaml
# 终端 C（定位 + 证明数据在发）
ros2 topic info /qos_demo -v
ros2 topic echo /qos_demo --qos-reliability best_effort
ros2 topic echo /qos_demo --qos-reliability reliable
# 终端 B 换一种跑法即可恢复正常
ros2 run p03_topics qos_subscriber --ros-args -p reliability:=best_effort

# ---------- 3) transient_local：迟到的订阅者也能收到 ----------
# 终端 A（先跑，等 3 秒再执行终端 B）
ros2 run p03_topics qos_publisher --ros-args -p durability:=transient_local
# 终端 B（后启动，立刻收到历史样本）
ros2 run p03_topics qos_subscriber --ros-args -p durability:=transient_local
# 对照：volatile 订阅端要等新消息（Ctrl+C 停掉终端 B 后重跑）
ros2 run p03_topics qos_subscriber --ros-args -p durability:=volatile

# 收尾：各终端 Ctrl+C 退出
```

## 练习题

**基础**

1. 让 `status_publisher` 把 `frame_id` 也做成参数（默认 `world`）：`declare_parameter<std::string>("frame_id", "world")`，
   用 `ros2 topic echo /robot_status --field header.frame_id` 验证 `-p frame_id:=map` 生效。
2. 用 `ros2 topic pub --once /qos_demo std_msgs/msg/Int32 "{data: 99}"` 手动发一条，再用 `ros2 topic echo /qos_demo` 观察。
   提示：`ros2 topic pub` 默认用 reliable 发布，因此它既能被默认（reliable）的订阅端收到，也能被 best_effort 的订阅端收到；
   反过来（用 best_effort 发布）就没这么宽容了 —— 对照兼容性规则表想一想为什么。
3. 让 `status_subscriber` 不再每条都打印，改成"累计计数 + 每 10 条打印一次平均值"。体会订阅端的自我降频不要靠调 QoS depth 解决。

**进阶**

4. 给 `qos_publisher` / `qos_subscriber` 各加一个 `deadline` 参数（如 `0.5s`），用
   `qos.deadline(rclcpp::Duration::from_seconds(0.5))` 设置，再用 `ros2 topic info /qos_demo -v` 确认端点上的
   `Deadline:` 已生效；然后把发布周期改成 2 s（比 deadline 慢），用 `rclcpp::QOSEventHandler`
   （`#include "rclcpp/qos_event.hpp"`，事件类型 `rclcpp::QOSDeadlineRequestedInfo` / `QOSDeadlineOfferedInfo`）
   捕获 deadline 事件，观察"超时"是怎么被上报的。
5. 装好 `rqt_graph` 后（`sudo apt install ros-humble-rqt-graph`）运行 `rqt_graph`，分别在实验一和实验二运行时观察
   图形差异：不兼容的节点之间**不会**出现连线 —— 把"静默失败"可视化。
6. 解释题：`ros2 topic echo` 默认用 `sensor_data` profile（best_effort + keep_last(5)）。
   为什么用 `ros2 topic echo /qos_demo --qos-reliability reliable` 去 echo 一个 best_effort 发布者会一条都收不到，
   而反过来（reliable 发布者 + best_effort 订阅者）却能正常收到？再用实验一里的发布者验证你的结论。
