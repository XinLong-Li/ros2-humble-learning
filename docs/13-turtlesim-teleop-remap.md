# 案例复盘：官方教程的键盘 remap 为什么在 Humble 上失效

> 涉及教程：官方 [Introducing turtlesim and rqt](https://docs.ros.org/en/humble/Tutorials/Beginner-CLI-Tools/Introducing-Turtlesim/Introducing-Turtlesim.html)
> 第 6 步（Control the new turtle）。
> 横切文档，对应概念：话题、动作、remap、ROS 图的底层结构。建议学完 p02（接口）再读，
> 涉及动作的部分可先跳过，做完 p05 再回来看。
> 核心结论：**官方命令没写错，但它在 Humble 上对"动作"部分静默失效——这是 Humble 的实现缺口，
> 官方文档没有注明**。

## 1. 你要做什么

官方教程第 6 步：spawn 出第二只乌龟 turtle2 后，想用**同一个键盘程序** `turtle_teleop_key`
控制它。官方给的命令是：

```bash
ros2 run turtlesim turtle_teleop_key --ros-args --remap turtle1/cmd_vel:=turtle2/cmd_vel --remap turtle1/rotate_absolute:=turtle2/rotate_absolute
```

官方原文声称："Now, you can move `turtle2` when this terminal is active"——言下之意，
**箭头键和字母键都应该控制 turtle2**。

## 2. 实际现象（Humble 实测）

| 按键 | 期望 | 实际 |
|---|---|---|
| 箭头键（移动） | 控制 turtle2 | ✅ 正常 |
| 字母键 G/B/V/C/D/E/R/T（旋转） | 控制 turtle2 | ❌ **仍然控制 turtle1** |

症状极其"安静"：没有任何报错、没有任何警告，字母键照常工作，只是作用在错误的乌龟身上。

## 3. 官方文档错在哪

精确地说，**命令语法本身没错**——两条 remap 在 Jazzy 及以后的发行版上都能正常工作。
官方文档的问题是：

1. **未标注发行版差异**：这条命令依赖"动作名重映射"功能，而该功能 **Humble 没实现**、
   直到 Jazzy 才补齐（见 §5）。文档用同一份内容覆盖所有发行版，在 Humble 语境下就是错的。
2. **声称的效果不成立**："you can move turtle2" 在 Humble 上只对箭头键成立。

> 一个诚实的背景说明：本仓库最初在 docs/06 里写"官方教程只给了 cmd_vel 一条 remap"——
> 那是错的（凭记忆下结论的教训）。核实源码后发现官方确实给了两条。**怀疑"文档怎么写的"时，
> 去查源文件，不要查记忆。**

## 4. 背景知识：为什么箭头和字母是两条独立的通道

`turtle_teleop_key` 内部有两个出口：

```cpp
// 箭头键 → 话题：连续发布速度指令
publisher_ = create_publisher<Twist>("turtle1/cmd_vel", 10);

// 字母键 → 动作：发送"转到绝对朝向"的目标
action_client_ = rclcpp_action::create_client<RotateAbsolute>(this, "turtle1/rotate_absolute");
```

| | 箭头键 | 字母键 |
|---|---|---|
| 通道类型 | **话题**（Topic） | **动作**（Action） |
| 名字 | `/turtle1/cmd_vel` | `/turtle1/rotate_absolute` |
| 语义 | 连续指令流（"以 2 m/s 前进"） | 一次性目标（"转到 90°，完成后告诉我"） |
| remap 时 | **一个名字，直接生效** | **一个动作 = 底层 5 个名字**（见下） |

为什么字母键用动作而不是话题？因为"转到绝对朝向"是**目标型任务**：有开始、有反馈
（还剩多少度）、有完成信号——这正是动作（action）的设计场景，而话题只适合连续数据流。

### 动作的底层结构：1 个动作 = 5 个名字

ROS 2 的动作**不是新的通信原语**，它由服务 + 话题组合而成：

```
/turtle1/rotate_absolute/_action/send_goal      # 服务：提交目标
/turtle1/rotate_absolute/_action/cancel_goal    # 服务：取消目标
/turtle1/rotate_absolute/_action/get_result     # 服务：取结果
/turtle1/rotate_absolute/_action/feedback       # 话题：持续反馈（注意不是 feedback_message！）
/turtle1/rotate_absolute/_action/status         # 话题：目标状态
```

### remap 的匹配规则：精确相等

Humble 的 rcl 里，remap 规则是**逐名字精确字符串匹配**（`rcl/src/rcl/remap.c`）：

```c
matched = (0 == strcmp(expanded_match, name));   // 只有完全相等才算命中
```

所以 `--remap turtle1/rotate_absolute:=turtle2/rotate_absolute` 这条规则：

- 对动作基名 `/turtle1/rotate_absolute` 本身 → 精确相等 ✅
- 但对底层的 5 个名字（如 `/turtle1/rotate_absolute/_action/send_goal`）→ **没有一个是精确相等的** ❌

话题 remap 之所以生效，是因为 `/turtle1/cmd_vel` 是**单个**名字，规则直接命中。

## 5. 根因：Humble 未实现"动作名重映射"

这是上游已知问题：

- [ros2/ros2#1312](https://github.com/ros2/ros2/issues/1312)：*"remapping of action names is not possible"*
- 修复 PR [ros2/rcl#1220](https://github.com/ros2/rcl/pull/1220)（"Added remapping resolution for
  action names"）于 **2025 年 3 月 backport 到 Jazzy**，**未进入 Humble**

修复后的行为：remap 规则先作用于动作基名，再由已重映射的基名派生 5 个底层名字——官方教程的命令就对了。

## 6. 正确做法（Humble 可用）

### 方案 A：逐条 remap 5 个底层名字（本仓库实测通过）

```bash
ros2 run turtlesim turtle_teleop_key --ros-args \
  -r /turtle1/cmd_vel:=/turtle2/cmd_vel \
  -r /turtle1/rotate_absolute/_action/send_goal:=/turtle2/rotate_absolute/_action/send_goal \
  -r /turtle1/rotate_absolute/_action/cancel_goal:=/turtle2/rotate_absolute/_action/cancel_goal \
  -r /turtle1/rotate_absolute/_action/get_result:=/turtle2/rotate_absolute/_action/get_result \
  -r /turtle1/rotate_absolute/_action/feedback:=/turtle2/rotate_absolute/_action/feedback \
  -r /turtle1/rotate_absolute/_action/status:=/turtle2/rotate_absolute/_action/status
```

（`-r` 是 `--remap` 的简写；绝对名字写不写前导 `/` 均可，本仓库实测两者等价。）

### 方案 B：按官方命令跑，接受字母键控制 turtle1

如果你只需要箭头控制第二只龟（大多数演示场景），官方命令的前半条就够了——字母键那半条在
Humble 上是"无害的无效"。

### 方案 C：换 Jazzy 及以上发行版

动作名重映射已在 Jazzy 实现，官方命令可直接使用。学习阶段不建议为此折腾发行版。

## 7. 验证方法（比"拿乌龟试"可靠得多）

```bash
# ① 看动作客户端实际指向（本案例的核心验证）
ros2 node info /teleop_turtle
#     Action Clients:
#       /turtle2/rotate_absolute: turtlesim/action/RotateAbsolute   ← 正确
#       /turtle1/rotate_absolute: ...                               ← remap 没生效

# ② 看动作底层名字的全貌——注意 CLI 会过滤！
#    ros2 service list / topic list 里看不到 /_action/ 名字，
#    用 rclpy 直接查图才能看到（本仓库就是用这招挖出 5 个名字的）：
python3 - << 'EOF'
import time, rclpy
from rclpy.node import Node
rclpy.init()
n = Node('graph_probe'); time.sleep(2)
for name, _ in n.get_service_names_and_types():
    if 'rotate' in name: print(name)
n.destroy_node(); rclpy.shutdown()
EOF
```

## 8. 排查过程复述（方法论）

本次从"现象"到"根因"的完整链条，每一步都是通用的排查手法：

1. **对比两条通道的行为差异** → 发现箭头（话题）正常、字母（动作）失效
2. **`ros2 node info /teleop_turtle`** → 动作客户端仍指向 `/turtle1/rotate_absolute`，
   确认 remap 没生效（而不是"生效了但乌龟没动"）
3. **读 rcl 源码 `remap.c`** → 发现匹配是精确字符串相等 → 假设"底层 5 个名字匹配不上"
4. **用 rclpy 查图** → 拿到 5 个底层名字的真实格式（`/_action/feedback` 不是 `feedback_message`）
5. **实验验证**：基名 remap（相对/绝对）× 2 均失败；5 条底层名字 remap 成功
6. **上游溯源**：ros2/ros2#1312 + rcl#1220，确认是 Humble 的实现缺口

关键原则：**"静默失效"的三个嫌疑犯——QoS 不匹配、名字对不上、remap 没生效——
症状几乎一样，破案全靠 `ros2 node info` / `ros2 topic info -v`，不靠猜。**

## 9. 相关链接

- 官方教程：<https://docs.ros.org/en/humble/Tutorials/Beginner-CLI-Tools/Introducing-Turtlesim/Introducing-Turtlesim.html>
- 上游 issue：[ros2/ros2#1312](https://github.com/ros2/ros2/issues/1312)
- 修复 PR：[ros2/rcl#1220](https://github.com/ros2/rcl/pull/1220)（backport 到 Jazzy）
- 本仓库相关文档：[06-actions.md §4.5](06-actions.md)（动作 remap 的简明版）、[12-troubleshooting.md](12-troubleshooting.md)（静默失败排查）
