# p09_turtle_control —— 综合项目：控制小乌龟

配套文档：[docs/10-turtlesim-project.md](../../docs/10-turtlesim-project.md)

## 学习目标

把前面学到的**话题、服务、动作、参数、TF、launch** 组合成一个完整的小系统：
用动作驱动乌龟走航点，用 TF 把位姿广播给 rviz2，用参数文件配置任务。

## 系统架构

```
                         ┌──────────────────────┐
  config/turtle_params   │  waypoint_follower    │  动作目标（p02 NavigateTo）
  .yaml ────参数────────>│  （任务编排）          │───────────────┐
                         └──────────────────────┘               ▼
                                            ┌──────────────────────────┐
   turtlesim ──/turtle1/pose──> pose 订阅 ──>│   navigate_to_server      │
     ▲                                       │  （比例控制器）           │
     └────── /turtle1/cmd_vel <── Twist 发布 ─┤  publish_feedback 回传   │
                                             └──────────────────────────┘
   turtle_tf_broadcaster：/turtle1/pose → TF(world→turtle1) → rviz2
```

- **话题**：`/turtle1/pose`（状态）、`/turtle1/cmd_vel`（指令）
- **动作**：`/navigate_to`（p02 的 `NavigateTo`，goal/feedback/result/cancel 完整生命周期）
- **服务**：`turtle_service_client` 调 turtlesim 的 `/spawn`、`/turtle1/set_pen`、`/kill`
- **参数**：航点序列、控制增益、速度全部可配置
- **TF**：`world → turtle1`
- **launch**：一键起全系统（含可选 rviz2）

## 文件说明

| 节点（可执行文件） | 职责 |
|---|---|
| `turtle_tf_broadcaster` | 订阅位姿 → 广播 TF（turtlesim 自己不发 TF） |
| `turtle_pose_monitor` | 打印位姿 + 累计路程 |
| `turtle_square_drawer` | 状态机画正方形（对照官方 draw_square 的 C++ 版） |
| `turtle_service_client` | 顺序调用 spawn / set_pen / kill 三个服务 |
| `turtle_rotate_client` | 调用 turtlesim 官方动作 rotate_absolute（连续两个目标） |
| `navigate_to_server` | 实现 `NavigateTo` 动作：比例控制驱赶真乌龟 |
| `waypoint_follower` | 巡航：顺序请求 `navigate_to`，走完 YAML 里的全部航点 |

`launch/turtle_demo.launch.py` 起演示系统；`launch/turtle_mission.launch.py` 起完整巡航（结课验收）。

## 编译运行

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select p09_turtle_control
source install/setup.bash

# ---- 场景一：单节点演示 ----
ros2 run turtlesim turtlesim_node                    # 终端 A（窗口）
ros2 run p09_turtle_control turtle_tf_broadcaster    # 终端 B
ros2 run p09_turtle_control turtle_pose_monitor      # 终端 C
ros2 run p09_turtle_control turtle_square_drawer     # 终端 D：看乌龟画正方形

# ---- 场景二：动作驱动（navigate_to） ----
ros2 launch p09_turtle_control turtle_demo.launch.py show_rviz:=true
# 另开终端手动发目标：
ros2 action send_goal /navigate_to p02_interfaces/action/NavigateTo \
  "{target: {label: 'goal', position: {x: 9.0, y: 2.0, z: 0.0}}, tolerance: 0.2, max_speed: 2.0}" --feedback
# 观察：反馈里 distance_remaining 递减，乌龟先转向再逼近；rviz2 里 world→turtle1 动起来

# 测试取消：目标刚发出就 Ctrl-C（取消 CLI 的 send_goal），
# 或跑 turtle_rotate_client 体验"别人实现的 action"

# ---- 场景三：一键巡航（结课验收） ----
ros2 launch p09_turtle_control turtle_mission.launch.py
# 无需任何操作：乌龟自动依次走完 A(2,2)→B(8,8)→C(8,2)→D(2,8)，
# 走完打日志 "任务完成：共访问 4 个航点！" 并停车。

# ---- 其他 ----
ros2 run p09_turtle_control turtle_service_client   # spawn 乌龟→换红笔→杀掉
ros2 run p09_turtle_control turtle_rotate_client    # 旋转 90° 再转回
ros2 run rqt_graph rqt_graph                        # 看话题/服务/动作连成的图
ros2 bag record -a -o bags/demo                     # 录一段，Ctrl-C 停
ros2 bag info bags/demo && ros2 bag play bags/demo  # 回放（乌龟会重放动作）
```

## 练习题

**基础**
1. 修改 `config/turtle_params.yaml`，设计一条五角星航路（5 个点），跑通巡航。
2. 把 `navigate_to_server` 的 `kp_linear` 调到 0.3，观察收敛变慢；再调到 5.0，观察过冲震荡。
3. 用 `rqt_plot` 画 `/turtle1/pose/x` 与 `/turtle1/pose/y`，验证航点顺序。

**进阶**
4. 给 `waypoint_follower` 加参数 `loop: true`：巡航结束后从第一个航点再来一圈。
5. 用 `turtle_service_client` 的逻辑生成第二只乌龟 `turtle2`，把 `navigate_to_server` 改成可用参数选择控制哪只龟（提示：订阅话题与 cmd_vel 话题名都用参数）。
6. 用 `ros2 action send_goal` 发一个 tolerance 为负的目标，观察 handle_goal 的 REJECT 路径（日志里能看到拒绝原因）。
