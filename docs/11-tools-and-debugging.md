# 工具链与调试

> 横切文档：这些工具在 S1~S9 全程都会用到，建议按阶段回来查。

## 1. ros2 CLI 全景

```bash
ros2                    # 列出全部子命令
ros2 doctor             # 环境体检（版本/网络/问题清单）
ros2 wtf                # 同上，更口语化
ros2 pkg list / executables <包>
ros2 node list / info <节点>
ros2 topic list -t / echo / hz / bw / info <话题> -v
ros2 service list -t / call <服务> <类型> "{...}" / find <类型>
ros2 action list -t / info / send_goal <动作> <类型> "{...}" --feedback
ros2 param list / get / set / describe / dump / load
ros2 interface list / show / package
ros2 bag record -a / info / play
ros2 launch <包> <文件>
```

**信息收集三件套**（出问题先跑这三条）：
```bash
ros2 node list              # 谁在跑
ros2 topic info <话题> -v   # 端点与 QoS 是否匹配 ★ ROS 2 排错核心
ros2 doctor                 # 环境是否有病
```

## 2. rqt 家族

```bash
ros2 run rqt_graph rqt_graph                    # 节点/话题/服务/动作拓扑图（最常用）
ros2 run rqt_plot rqt_plot /turtle1/pose/x      # 数据曲线（调控制参数必用）
ros2 run rqt_console rqt_console                # 日志聚合查看
ros2 run rqt_publisher rqt_publisher            # 手动发消息（代替 ros2 topic pub）
ros2 run rqt_service_caller rqt_service_caller  # 图形化服务调用
ros2 run rqt_reconfigure rqt_reconfigure        # 图形化参数调节（dynamic_reconfigure 继承者）
ros2 run rqt_bag rqt_bag                        # 图形化 bag 录制
```
对照 ROS 1：`rosrun rqt_graph rqt_graph` → `ros2 run rqt_graph rqt_graph`（包名不变，前缀变化）。

## 3. rviz2

```bash
rviz2
rviz2 -d <配置文件>     # 本仓库预置：p08 的 config/tf2_demo.rviz、p09 的 rviz2/turtle_demo.rviz
```
常用 Display：TF（坐标树）、Grid（地面网格）、Marker/MarkerArray（程序画图）、Image、LaserScan、RobotModel。
**核心概念**：Fixed Frame 决定"你站在哪个坐标系看世界"，必须选 TF 树里存在的帧
（本仓库是 `map`（p08）或 `world`（p09））。

## 4. rosbag2

```bash
ros2 bag record -a                        # 全录（Ctrl-C 停）
ros2 bag record -o bags/demo /turtle1/pose /turtle1/cmd_vel   # 只录指定话题
ros2 bag info bags/demo                   # 时长/话题/大小
ros2 bag play bags/demo                   # 回放
ros2 bag play bags/demo --rate 0.5        # 半速回放
```
与 ROS 1 差异：格式是 **sqlite3（.db3）+ metadata.yaml**（插件式，可选 mcap）；
**不能直接播放旧 .bag**（需 `pip install rosbags` 转换）；
回放时默认**不会重发 /tf_static 外的静态数据**，且话题时间戳保持原样（TF 外推错误的常见来源）。

## 5. 日志与调试

```bash
# 运行时日志级别
ros2 run <包> <节点> --ros-args --log-level debug
# 查看已退出进程的日志
ls ~/.ros/log/    # 每次运行一个目录，取最新的
```
- RCLCPP_INFO / WARN / ERROR / DEBUG / FATAL 五级；`RCLCPP_INFO_THROTTLE` 限频（p09 用过）。
- `RCLCPP_INFO_STREAM` 支持 << 流式输出。

## 6. 官方示例源码（自查 API 的最佳参考）

本机这些目录有大量可读的官方源码：
```bash
/opt/ros/humble/share/demo_nodes_cpp/          # talker/listener 等 demo
/opt/ros/humble/share/turtlesim/               # 含 launch/multisim.launch.py
/opt/ros/humble/share/action_tutorials_cpp/    # action 完整教程源码
/opt/ros/humble/share/examples_rclcpp_minimal_*/  # 最小示例系列
```
搜索源码：`grep -rn "create_wall_timer" /opt/ros/humble/share/demo_nodes_cpp/`。
