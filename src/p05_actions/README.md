# p05_actions —— 动作 action（goal / feedback / result / cancel）

配套文档：[docs/06-actions.md](../../docs/06-actions.md)

## 学习目标

1. 说清动作的三件事：**目标**（一次）、**反馈**（多次）、**结果**（一次），以及贯穿全程的**取消**
2. 会写动作服务器：`create_server` 的 `handle_goal` / `handle_cancel` / `handle_accepted` 三个回调各管什么
3. 会写动作客户端：`async_send_goal` + `SendGoalOptions` 的 feedback/result 回调 + `async_cancel_goal`
4. 知道动作 vs 服务的选择标准：**任务耗时长、要进度、要能中途取消 → 用动作**

## 动作的生命周期

```
      客户端                                              服务器
        │                                                   │
        │  ① async_send_goal(goal)                          │
        ├───────────────────── 目标请求 ────────────────────►│ handle_goal()
        │                                                   │   ├─ 校验目标
        │                                                   │   ├─ REJECT            → 拒绝
        │                                                   │   └─ ACCEPT_AND_EXECUTE → 接受
        │  ② goal 响应（接受 / 拒绝）                        │
        │◄──────────────────────────────────────────────────┤ handle_accepted(GoalHandle)
        │  future.get() → GoalHandle（拒绝时是 nullptr）       │   └─ 开线程执行（回调里不能阻塞！）
        │                                                   │
        │  ③ feedback × N                                   │
        │◄────────────────── 反馈 ───────────────────────────┤ goal_handle->publish_feedback()
        │                                                   │
        │  ④ 取消（可选）                                    │
        ├───────────────────── 取消请求 ────────────────────►│ handle_cancel()
        │◄────────────────── 取消响应 ───────────────────────┤   └─ ACCEPT / REJECT
        │                                                   │ 执行线程 is_canceling() == true
        │  ⑤ result（终态，仅一次）                          │
        │◄────────────────── 结果 ───────────────────────────┤ goal_handle->succeed()   成功
        │                                                   │ goal_handle->canceled()  已取消
        │                                                   │ goal_handle->abort()     中止
```

目标状态机（`ros2 action info` 里的 ActionType 就是这套状态）：
`UNKNOWN → ACCEPTED → EXECUTING → {SUCCEEDED | CANCELED | ABORTED}`，
取消时中间会经过 `CANCELING`。**终态只能有一个，且只能进入一次**。

## 与 ROS 1 actionlib 对照

| ROS 1 (Noetic) | ROS 2 (Humble) |
|---|---|
| `actionlib` 包（单独安装） | `rclcpp_action`（ROS 2 自带） |
| `.action` 文件，两段 `---` 分隔 | **语法完全不变**，只是生成工具换成 rosidl |
| `actionlib_msgs/GoalID` / `GoalStatus` | `action_msgs/msg/GoalInfo` / `GoalStatus`（字段基本对应） |
| `SimpleActionServer` + `executeCB(goal)` | `rclcpp_action::create_server` 的三个回调 + `GoalHandle` |
| `as_.publishFeedback()` | `goal_handle->publish_feedback()` |
| `as_.setSucceeded()` / `setAborted()` | `goal_handle->succeed()` / `abort()` |
| `as_.setPreempted()` | `goal_handle->canceled()`（必须先收到取消请求） |
| `as_.isPreemptRequested()` | `goal_handle->is_canceling()` |
| `SimpleActionClient::sendGoal(g, done_cb, active_cb, feedback_cb)` | `async_send_goal(g, SendGoalOptions)`，三个回调填在 `SendGoalOptions` 里 |
| `ac_.cancelGoal()` | `async_cancel_goal(goal_handle)` |
| `ac_.waitForServer()` | `client->wait_for_action_server()` |
| `rosaction` / `axclient.py` | `ros2 action list / info / send_goal` |
| **抢占（preempt）是 SimpleActionServer 的内建能力** | **没有内建抢占，要自己实现**（见练习题 4） |

> 一句话：接口定义（`.action`）几乎不用改，改的是"怎么写服务器/客户端"。
> ROS 2 把 ROS 1 里藏在 `SimpleActionServer` 里的状态机，摊开成了三个回调和
> 一个你直接操作的 `GoalHandle` —— 代码变长了，但对执行过程完全可控。

## 文件说明

```
p05_actions/
├── package.xml
├── CMakeLists.txt
└── src/
    ├── countdown_server.cpp   # 节点 countdown_server，动作 /countdown
    │                          #   纯标量目标（start / step），最适合先跑通流程
    ├── countdown_client.cpp   # 节点 countdown_client
    │                          #   参数 from / step / cancel_after（from 填进目标的 start 字段）
    ├── navigate_server.cpp    # 节点 navigate_server，动作 /navigate_to
    │                          #   复合目标（Waypoint + tolerance + max_speed）+ 长任务反馈
    └── navigate_client.cpp    # 节点 navigate_client
                               #   参数 goal_x / goal_y / tolerance / max_speed / cancel_after
```

| 节点 | 动作名 | 动作类型 | 参数（默认值） |
|---|---|---|---|
| `countdown_server` | `/countdown` | `p02_interfaces/action/CountDown` | 无 |
| `countdown_client` | `/countdown` | 同上 | `from`(10) `step`(1) `cancel_after`(0.0) |
| `navigate_server` | `/navigate_to` | `p02_interfaces/action/NavigateTo` | 无 |
| `navigate_client` | `/navigate_to` | 同上 | `goal_x`(5.0) `goal_y`(3.0) `tolerance`(0.2) `max_speed`(1.0) `cancel_after`(0.0) |

`cancel_after`：大于 0 时，客户端会在该秒数后自动发取消请求（0 表示不取消），用来演示取消路径。

> **`from` 和 `start` 不是笔误**：CountDown 的 goal 字段叫 `start` 而不是 `from`，
> 因为 `.action` / `.msg` 的字段名会生成同名 Python 属性，而 `from` 是 Python 保留字
> —— rosidl 会拒绝生成（`ros2 action send_goal` 直接报 `SyntaxError`）。
> 客户端自己的参数名只是字符串，不受这个限制，所以仍然叫 `-p from:=5`。

## 编译运行

```bash
# 在仓库根目录（~/ros2-humble-learning）
source /opt/ros/humble/setup.bash
# p05 依赖 p02_interfaces，--packages-up-to 会把依赖一起编
colcon build --symlink-install --packages-up-to p05_actions
source install/setup.bash
```

> **`.action` 的三段顺序是 目标(goal) → 结果(result) → 反馈(feedback)**
> —— 两处 `---` 分隔，第 2 段是**结果**、第 3 段才是**反馈**，不是"目标/反馈/结果"！
> 这和很多人的直觉相反，而且 `ros2 interface show` 只是原样打印文件、不会纠正你。
> 写反了不会报语法错误，但生成出来的类型会对调：第 2 段变成 `Xxx_Result_`、
> 第 3 段变成 `Xxx_Feedback_`，下游代码就会报
> `..._Feedback_ has no member named 'distance_remaining'` 之类的错。
> p02_interfaces 的两个 `.action` 和本包所有代码都按这个顺序写：
> CountDown = goal(start/step) / result(total_steps,message) / feedback(current)，
> NavigateTo = goal(target,tolerance,max_speed) / result(success,final_distance,elapsed_seconds)
> / feedback(distance_remaining)。

### 1. 先用 CLI 看动作（不写客户端也能测服务器）

```bash
ros2 action list -t                 # -t 同时显示动作类型
ros2 action info /countdown         # 看有几个客户端/服务器
ros2 interface show p02_interfaces/action/CountDown   # 看 goal/result/feedback 字段

ros2 run p05_actions countdown_server                  # 终端 A
# 终端 B：CLI 直接发目标，--feedback 会把每次反馈都打出来
ros2 action send_goal /countdown p02_interfaces/action/CountDown "{start: 5, step: 1}" --feedback
```

预期：终端 B 依次打印 `current: 5 4 3 2 1`，最后 `Result: total_steps: 5, message: 已完成`。

再试一次非法目标，观察"拒绝"发生在服务器日志里、客户端拿到的是拒绝响应：

```bash
ros2 action send_goal /countdown p02_interfaces/action/CountDown "{start: 5, step: 0}"
```

### 2. countdown_client：正常跑通 + 取消

```bash
ros2 run p05_actions countdown_server                    # 终端 A（保持运行）

ros2 run p05_actions countdown_client                    # 终端 B：from=10 默认值
ros2 run p05_actions countdown_client --ros-args -p from:=5 -p step:=1
ros2 run p05_actions countdown_client --ros-args -p cancel_after:=2.0   # 2 秒后自动取消
```

对比两次的输出差别：第三次会看到

* 服务器：`收到取消请求（goal ...），同意取消` → `目标已取消：共走了 N 步`
* 客户端：`结果：已取消（total_steps=N, message='已取消'）`

> 服务器不会因为取消而退出，可以接着再发目标。

### 3. navigate_server + navigate_client：虚拟机器人逼近目标

```bash
ros2 run p05_actions navigate_server                     # 终端 A

ros2 run p05_actions navigate_client                     # 终端 B：目标 (5, 3)
# 反馈会从约 5.83 m 一路递减到 <= tolerance(0.2)，最后打印
#   结果：成功=true，最终距离 0.19 m，耗时 5.80 s
ros2 run p05_actions navigate_client --ros-args -p goal_x:=0.0 -p goal_y:=0.0 \
  -p max_speed:=3.0 -p tolerance:=0.05                   # 换个目标、更快、更精确
ros2 run p05_actions navigate_client --ros-args -p cancel_after:=2.0   # 中途取消，看机器人停在哪
```

也可以用 CLI 发（注意嵌套字段的 YAML 写法）：

```bash
ros2 action send_goal /navigate_to p02_interfaces/action/NavigateTo \
  "{target: {label: 'goal', position: {x: 5.0, y: 3.0, z: 0.0}}, tolerance: 0.2, max_speed: 1.0}" \
  --feedback
```

> `navigate_server` 里的机器人是**虚拟的**（成员变量 `x_`/`y_`）。
> 它不会在两次目标之间回到原点：第二个目标会从上一个终点出发 —— 这正是 p09
> 要延续的语义：p09 把"改局部变量"换成"向 turtlesim 发速度指令"，
> 动作类型和三个回调可以原样搬过去。

## 代码里最容易踩的三个坑

1. **`handle_accepted` 里不能干重活**。它在执行器线程里被调用，阻塞它 = 新目标、
   取消请求、结果查询全部排队。所以真正的执行体要 `std::thread(...).detach()` 出去。
2. **服务器端的 `publish_feedback` / `succeed` 只能在线程里调**，且顺序必须是
   "先判断取消，再发结果"；`canceled()` 要求此前确实收到过取消请求，否则抛异常。
3. **客户端的 `future.wait_for()` 必须在 spin 进行时调用**。目标响应要靠执行器分发，
   没人在 spin 就是自己等自己，必然超时。本示例的写法是：**主线程 `rclcpp::spin`，
   子线程发目标并 `wait_for`**（见两个 client 的 `main`）。

## 练习题

**基础**

1. 用 `ros2 action send_goal /countdown ... "{start: 5, step: 2}" --feedback` 观察反馈序列和
   `total_steps`，说清 `total_steps` 是按什么规则数出来的（提示：数的是"走了几步"，
   不是终点值）。
2. 同时开两个 `countdown_client`（两个终端），观察第二个被服务器以
   "已有目标在执行"拒绝，客户端打印 `目标被服务器拒绝`。想一想：为什么"拒绝"比
   "排队等待"更适合这个场景？
3. 给 `navigate_client` 的 feedback 回调加一句"预计还需 X 秒"（用剩余距离 / max_speed 估算），
   再用 `ros2 action send_goal` 手动发一个 `max_speed:=0.5` 的目标验证估算是否合理。

**进阶**

4. **让 `navigate_server` 支持抢占**：新目标到达时不拒绝，而是先把旧目标 `abort()`
   （或 `canceled()`）再执行新目标。
   提示：把 `busy_` 换成一个"当前 GoalHandle"成员 + `std::mutex`；`handle_goal` 不再直接
   REJECT，而是在 `handle_accepted` 里先对旧 handle 调用终态方法。注意旧 handle 的
   终态方法只能调一次，且执行线程可能正在写它。
5. **给 `countdown_client` 加 `timeout` 参数**：发出目标后超过 N 秒还没收到结果，
   就自动取消并打印"超时"（可以复用 `cancel_after` 的定时器写法，但要在收到结果时
   把定时器 cancel 掉）。顺便思考：客户端超时和服务器自己失败，哪个更容易写对？
6. **让 countdown 支持负数步长（正向计数）**：现在 `while (current > 0)` 的退出条件只
   对递减有意义。给 CountDown 加一个"方向"参数或改用 `std::abs` 判断剩余量，
   让它既能倒数也能正数，同时**保留 `step == 0` 的拒绝逻辑**（提示：`step == 0` 时
   无论什么方向都不前进，必须继续拒绝）。
