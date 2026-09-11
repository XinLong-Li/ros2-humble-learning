# 动作 action（p05）

> 对应代码：`src/p05_actions/`。动作 = 带反馈与取消的"长时间服务"，ROS 1 用 actionlib，ROS 2 内置于 rclcpp_action。

## 1. 生命周期

```
客户端 send_goal ──> [GOAL ACCEPTED?] ──否──> REJECT（客户端立刻收到）
                          │是
                          ▼
                   [EXECUTING] ── publish_feedback ──> 客户端（持续）
                      │      │
              succeed │      │ canceled（响应取消请求）
                      ▼      ▼
                  [SUCCEEDED] [CANCELED]（还有 ABORTED = 执行异常中断）
```

- **REJECT** 是同步的：handle_goal 里返回 REJECT，客户端马上知道（本包：step=0 被拒、服务端忙时被拒）。
- **取消**是协作式的：客户端发取消请求 → 服务端 handle_cancel 接受 → 执行线程**主动检查** `is_canceling()` 后收尾。服务端不检查，取消就"不生效"。

## 2. 服务端骨架（rclcpp_action 三回调）

```cpp
action_server_ = rclcpp_action::create_server<CountDown>(
  this, "countdown",
  handle_goal,    // ① 接受/拒绝目标（校验参数、判断忙闲）
  handle_cancel,  // ② 响应取消请求（通常直接 ACCEPT，实际收尾在执行循环里）
  handle_accepted // ③ 目标已接受：在这里开线程/起状态机执行
);
```
对照 actionlib：`SimpleActionServer.execute_cb` 一个回调 → rclcpp_action **三个回调 + GoalHandle**。

## 3. 客户端骨架

```cpp
client_ = rclcpp_action::create_client<CountDown>(this, "countdown");
client_->wait_for_action_server(5s);              // 构造期阻塞等待是安全的

auto options = rclcpp_action::Client<CountDown>::SendGoalOptions();
options.feedback_callback = ...;   // 持续反馈
options.result_callback  = ...;    // 最终结果（SUCCEEDED/CANCELED/ABORTED）
client_->async_send_goal(goal, options);

// 取消：
client_->async_cancel_goal(goal_handle);
```

## 4. .action 文件的两个坑（本仓库实测踩过）

1. **三段顺序是 目标 → 结果 → 反馈**（goal/result/feedback）。写反后 rosidl 不会报错，
   生成代码里 Result 与 Feedback 的字段**对调**，下游编译报
   `NavigateTo_Result_ has no member named 'xxx'`——而且 `ros2 interface show` 显示的是文件原文，不会帮你纠正。
2. **字段名不能用 Python 保留字**（`from`、`import` 等）——rosidl 会拒绝生成（p02 的 CountDown 字段因此叫 `start`）。
   注意区别：**节点参数名只是运行时字符串，叫 `from` 没问题**（p05 的 countdown_client 参数就叫 from，已在注释里说明）。

## 4.5 动作名字的 remap 坑（本仓库实测）

Humble 的 `--remap` 是**逐名字精确匹配**（rcl 源码 `remap.c` 里 `strcmp` 精确相等才算命中，
没有"前缀匹配"）。而一个动作在底层是 **5 个独立的名字**：

```
<turtle>/rotate_absolute/_action/send_goal       # 服务 ×3
<turtle>/rotate_absolute/_action/cancel_goal
<turtle>/rotate_absolute/_action/get_result
<turtle>/rotate_absolute/_action/feedback        # 话题 ×2（注意不是 feedback_message！）
<turtle>/rotate_absolute/_action/status
```

所以 `--remap turtle1/rotate_absolute:=turtle2/rotate_absolute` **不会生效**——
基名和 5 个底层名字没有一个是精确相等的（对比：话题 `cmd_vel` 是单个名字，remap 直接生效。
官方教程的 teleop remap 示例只给了 cmd_vel，所以"箭头控制 turtle2、字母键还控制 turtle1"）。

正确做法是把 5 个底层名字逐一 remap（实测有效）：

```bash
ros2 run turtlesim turtle_teleop_key --ros-args \
  -r /turtle1/cmd_vel:=/turtle2/cmd_vel \
  -r /turtle1/rotate_absolute/_action/send_goal:=/turtle2/rotate_absolute/_action/send_goal \
  -r /turtle1/rotate_absolute/_action/cancel_goal:=/turtle2/rotate_absolute/_action/cancel_goal \
  -r /turtle1/rotate_absolute/_action/get_result:=/turtle2/rotate_absolute/_action/get_result \
  -r /turtle1/rotate_absolute/_action/feedback:=/turtle2/rotate_absolute/_action/feedback \
  -r /turtle1/rotate_absolute/_action/status:=/turtle2/rotate_absolute/_action/status
```

> 底层名字的真实格式怎么确认？CLI 的 `ros2 service list`/`topic list` 会**过滤掉** `/_action/` 名字，
> 用 rclpy 的 `get_service_names_and_types()` 才能看到全貌（本仓库就是这么挖出来的）。

## 5. 验证命令

```bash
ros2 run p05_actions countdown_server
ros2 action list -t
ros2 action send_goal /countdown p02_interfaces/action/CountDown "{start: 5, step: 1}" --feedback
# 观察：反馈 current 5→1，最终 total_steps=5, message='已完成'
ros2 action send_goal /countdown p02_interfaces/action/CountDown "{start: 5, step: 0}"   # 被 REJECT

# 取消路径：CLI 发出后 Ctrl-C（send_goal 进程被杀 = 客户端消失 → 服务端收到取消）
# 或用带参数的客户端： ros2 run p05_actions countdown_client --ros-args -p cancel_after:=2.0

# 导航动作（虚拟世界版，p09 的真龟版是同一类型）：
ros2 run p05_actions navigate_server &
ros2 run p05_actions navigate_client
```

> 实测注意：navigate_server 的虚拟机器人位置**跨目标保留**（p09 的真龟版同语义）——
> 服务端不重启时，第二个目标若已在 tolerance 内会立刻成功。复现取消路径请重启服务端或换远目标。

## 6. 与 actionlib 的迁移对照

| actionlib | rclcpp_action |
|---|---|
| SimpleActionServer | create_server + 3 回调 |
| goal_cb / preempt_cb / execute_cb | handle_goal / handle_cancel / handle_accepted |
| `server.setSucceeded(result)` | `goal_handle->succeed(result)` |
| `server.setPreempted(result)` | `goal_handle->canceled(result)` |
| 隐式抢占（preempt） | 自行实现：忙时 REJECT（本包）或先 ABORT 旧目标再执行 |
| `rosrun actionlib axclient.py` | `ros2 action send_goal --feedback` |

## 7. 练习线索

- 基础 3：`ros2 action info /countdown` 能看出当前有没有目标在执行。
- 进阶 1（抢占）：handle_goal 里先 `active_goal_->abort(结果)` 再接受新目标——注意 active_goal_ 的线程安全。
- 进阶 2（--timeout）：`ros2 action send_goal ... --timeout 10` 会等待反馈超时后主动取消。
