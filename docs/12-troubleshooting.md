# 常见错误与排查手册

> 按场景分类的踩坑手册。每遇到一个新坑就补一条，这是本仓库的"活文档"。

## 1. 编译类

| 症状 | 原因 | 解法 |
|---|---|---|
| `Package 'xxx' not found`（cmake 阶段） | 依赖没装/没声明 | package.xml 加 `<depend>`；系统包用 `sudo apt install ros-humble-<包名>` |
| 找不到接口头文件 | 接口包没编/下游没重编 | `colcon build --packages-up-to <包>` |
| 编译成功但 `ros2 run` 报 `No executable found` | 漏 `install(TARGETS ...)` | CMakeLists 补 install 重编 |
| 改了 .msg 后运行期类型错乱 | 下游包没重编 | `--packages-up-to` 重编下游 |
| 改代码不生效 | 没重编 / 当前终端没重新 source | 重编 + `source install/setup.bash` |
| 链接错误含 `rosidl_typesupport` | 接口链接目标问题 | 用 `rosidl_get_typesupport_target` 官方写法（docs/03） |
| 环境彻底乱了 | install/build 残留脏状态 | `rm -rf build install log` 全量重编 |

## 2. 运行类

| 症状 | 原因 | 解法 |
|---|---|---|
| **订阅端静默收不到数据**（最常见！） | QoS 不匹配（best_effort vs reliable） | `ros2 topic info <话题> -v` 对比两端 QoS；改一致或用 `ros2 topic echo --qos-reliability best_effort` |
| 两个终端互看不到节点 | DDS 多播受限（WSL2/公司网） | `export ROS_LOCALHOST_ONLY=1`；或换 CycloneDDS |
| 节点启动即崩 `parameter 'x' has invalid type / not declared` | 参数没 declare 或类型不对 | 先 `declare_parameter("x", 默认值)` |
| `ros2 param set` 报 unknown parameter | 同上 | 同上（allow_undeclared 是例外，别依赖它） |
| launch 报 `file not found` | 漏 install launch 目录 | CMakeLists 补 `install(DIRECTORY launch ...)` |
| `ros2 run` 找不到**新加**的可执行文件 | 索引缓存 | 重新 `source install/setup.bash` 或重开终端 |
| `--ros-args` 参数没生效 | 位置错了 | `ros2 run <包> <节点> --ros-args -p k:=v`（--ros-args 必须在节点名之后） |
| 节点名冲突 | 同名节点 | 后启动的被改名 `xxx_1`；launch 里用 `name=` 指定 |

## 3. TF 类

| 症状 | 原因 | 解法 |
|---|---|---|
| `Lookup would require extrapolation into the future` | 查询时间戳比最新 TF 还"新" | 用 `tf2::TimePointZero` 查"最新"；或检查时钟 |
| 同上，且刚开机/唤醒后出现 | **WSL2 时钟漂移**（Windows 睡眠后 WSL 时间落后） | `sudo hwclock -s`（需密码）或 Windows 侧 `wsl --shutdown` |
| `Frame X does not exist` | 帧名拼错 / 广播者没在跑 / 查询太早 | 检查拼写；`tf2_echo` 验证；先 `waitForTransform` |
| rviz2 里看不到 TF | Fixed Frame 不对 | 选 TF 树里存在的帧（map/world） |
| 广播了但 view_frames 没有 | 刚启动就生成 | 广播者跑几秒后再 `ros2 run tf2_tools view_frames` |
| bag 回放时 TF 报外推 | 回放时间戳与现网时钟不一致 | `ros2 bag play --clock` + use_sim_time（进阶话题） |

## 4. WSL2 专有

| 症状 | 原因 | 解法 |
|---|---|---|
| GUI 窗口不出来 | WSLg 没起来 | `wsl --shutdown` 重启 WSL；确认 `echo $DISPLAY` 非空 |
| rviz2 报 GL/EGL 错 | 显卡直通问题 | `export LIBGL_ALWAYS_SOFTWARE=1` |
| Qt 报 xcb 错 | 平台插件问题 | `export QT_QPA_PLATFORM=xcb` |
| 时间/日期错乱 | 时钟漂移 | 见上表 |
| 跨 Windows 主机通信不通 | NAT 多播限制 | 本机学习场景用 `ROS_LOCALHOST_ONLY=1` |

## 5. 通杀三板斧

```bash
# ① 看谁在：确认通信双方都在
ros2 node list && ros2 topic list -t

# ② 看连上没：端点 + QoS 匹配情况（ROS 2 没有 master 报错，全靠这个）
ros2 topic info <话题> -v

# ③ 看日志：别只盯屏幕输出
tail -f ~/.ros/log/latest/*.log        # 最近一次运行的日志
```
