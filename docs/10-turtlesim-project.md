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

```cpp
v = clamp(kp_linear * dist, 0, max_speed);            // 线速度∝剩余距离：远快近慢
w = clamp(kp_angular * yaw_error, -2, 2);             // 角速度∝航向误差：转向目标
```
- `kp_linear` 太大 → 到点刹不住来回震荡；太小 → 龟速逼近
- `yaw_error` 必须归一化到 ±π（`while (yaw_error > M_PI) ...`），否则乌龟会绕远路
- 调参实验：`ros2 param set /navigate_to_server kp_linear 0.3` 观察收敛变慢

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
