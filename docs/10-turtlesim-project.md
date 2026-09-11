# 综合项目：控制小乌龟（p09）

> 对应代码：`src/p09_turtle_control/`。这是结课项目：把话题/服务/动作/参数/TF/launch 串成完整系统。

## 1. 系统架构

```
                         ┌──────────────────────┐
  turtle_params.yaml ────>│  waypoint_follower    │  动作目标 NavigateTo（p02 自定义 action）
                         │  （任务编排层）        │───────────────┐
                         └──────────────────────┘               ▼
                                            ┌──────────────────────────┐
   turtlesim ──/turtle1/pose──> pose 订阅 ──>│   navigate_to_server      │
     ▲                                       │  （执行层：比例控制器）   │
     └────── /turtle1/cmd_vel <── Twist 发布 ─┤  feedback: distance_rem. │
                                             └──────────────────────────┘
   turtle_tf_broadcaster：/turtle1/pose → TF(world→turtle1) → rviz2 / tf2_echo
```

分层思想（与真实机器人系统一致）：
- **平台层**：turtlesim 只提供 pose 话题、cmd_vel 话题、spawn/set_pen/kill 服务、rotate_absolute 动作
- **执行层**：`navigate_to_server` 把"去某个点"的动作目标翻译成速度指令
- **编排层**：`waypoint_follower` 只关心"下一个航点是哪"，不碰底层接口

## 2. 逐节点要点

| 节点 | 关键技术点 |
|---|---|
| `turtle_tf_broadcaster` | turtlesim 不发 TF → 我们用位姿话题补上；2D 姿态只需 setRPY(0,0,theta) |
| `turtle_pose_monitor` | 回调做高频轻量更新 + 定时器做低频汇总的经典组合 |
| `turtle_square_drawer` | 状态机 + "路程÷速度=时长"；析构函数刹停 |
| `turtle_service_client` | 调用外部节点服务的标准三步：wait_for_service → async_send_request → spin_until_future_complete |
| `turtle_rotate_client` | 消费"别人实现的 action"；结果回调里链式发下一个目标 |
| `navigate_to_server` | 动作服务端完整实现：目标校验/REJECT、比例控制、角度归一化、取消、每目标一线程 |
| `waypoint_follower` | 航点字符串解析、结果回调链式调度、RCLCPP_INFO_THROTTLE 节流 |

## 3. 比例控制解析（navigate_to_server 的核心）

### 3.1 控制律

```cpp
const double heading_scale = std::max(0.0, std::cos(yaw_error));
const double v = std::clamp(kp_linear * dist * heading_scale, 0.0, max_speed);
const double w = std::clamp(kp_angular * yaw_error, -2.0, 2.0);
```

- **线速度 ∝ 剩余距离**：远快近慢，自然减速到点，不会冲过头
- **线速度还要乘 `cos(航向误差)`**：目标在身后时 (|φ|>90°) 归零 → 先原地转身，对准后再走直线
- **角速度 ∝ 航向误差**，`yaw_error` 必须归一化到 ±π，否则会往反方向绕远路

### 3.2 为什么"朴素比例控制"会把轨迹画成圆圈（实测踩坑）

最早写成 `v = k*dist`、`w = k*φ`（不乘 cos）时，乌龟画出满屏圆圈。原因：

```
线速度与角速度同时饱和（实测 v=2.0 m/s, w=2.0 rad/s）
→ 一边全速前进、一边全速转弯
→ 轨迹是半径 = v/w = 1.0 米的圆弧
```

**排错技巧**：看 `ros2 topic echo /turtle1/cmd_vel` 的两个数值——只要 `linear.x` 和
`angular.z` 长期同时停在各自上限，就一定在画圆。对照画面里圆弧的半径，还能反推 v/w。

### 3.3 更隐蔽的坑：锁的范围太大，把回调"饿死"了（本仓库实测）

改完 cos 之后乌龟反而**彻底不动了**：`v=0.0`、`w=-2.0` 恒定不变，原地转圈。
采样发现它其实已经到了航点（离目标 0.19 米 < 容差 0.2），但控制器**不知道**。

根因在锁的范围：

```cpp
// ❌ 错误写法
std::lock_guard<std::mutex> lock(pose_mutex_);   // 拿锁
...
publish_twist(v, w);
loop_rate.sleep();          // 抱着锁睡 50 ms！
}                           // 出循环才放锁 —— 下一轮开头立刻又抢锁
```

控制线程几乎 100% 时间持有锁（睡觉时也持有），而下一轮循环开头马上重新抢锁，
只留几微秒的空隙——`pose_callback` 常年抢不到锁，**位姿数据一直是几十秒前的旧值**，
控制器于是永远在做错误决策。

正确写法是**只锁"拷贝"这一步**：

```cpp
// ✅ 正确写法：取快照，立刻释放
double x, y, theta;
{
  std::lock_guard<std::mutex> lock(pose_mutex_);
  x = pose_x_; y = pose_y_; theta = pose_theta_;
}                            // 出作用域即释放
... 用局部变量计算 ...
publish_twist(v, w);
loop_rate.sleep();           // 不持锁，回调可以正常更新
```

**通用原则**：多线程共享数据的保护锁，只包住"访问共享数据"的那几行；
绝不要把锁带进 `sleep()`、`publish()`、`wait()` 这类耗时或阻塞调用。

### 3.4 修复前后实测对比

| 航段 | 修复前 | 修复后 |
|---|---|---|
| 起点 → A (2,2) | 38.9 s（绕圈） | **4.5 s** |
| A → B (8,8) | 18.6 s | **6.4 s** |
| B → C (8,2) | 14.5 s | **4.9 s** |
| C → D (2,8) | 29.7 s | **6.1 s** |
| 全程 | 约 102 s | **约 22 s** |

路径效率（直线距离 / 实际路程）= **0.90**（越接近 1 越直）。

### 3.5 调参实验

- `ros2 param set /navigate_to_server kp_linear 0.3` → 收敛变慢
- `ros2 param set /navigate_to_server kp_angular 0.5` → 转向迟钝，拐角画大弧
- 把 `heading_scale` 那行的 `std::cos(yaw_error)` 去掉重编 → 观察乌龟重新开始画圈

## 4. 验收流程（对应 README 场景三）

```bash
ros2 launch p09_turtle_control turtle_mission.launch.py
# 观察：乌龟依次前往 A(2,2)→B(8,8)→C(8,2)→D(2,8)，日志出现"任务完成"
ros2 launch p09_turtle_control turtle_demo.launch.py show_rviz:=true   # rviz2 里看 TF
ros2 run tf2_ros tf2_echo world turtle1        # 数值验证 TF
ros2 run rqt_graph rqt_graph                   # 图验证：话题/服务/动作全连上
ros2 bag record -a -o bags/demo && ros2 bag play bags/demo   # 录放验证
```

## 5. 扩展练习方向（做完基础练习后）

1. **五角星**：改 YAML 航点画五角星（角度 72° 的顶点）
2. **第二只乌龟**：spawn turtle2 + 参数化 navigate_to_server 的话题名（remap）→ 双龟并行巡航
3. **循迹画图**：把 waypoints 密度提高，体会"航点列表 ≈ 离散轨迹"
4. **断点续跑**：bag 回放时观察系统状态恢复的边界（哪些能恢复哪些不能，为什么）
