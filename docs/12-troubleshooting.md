# 常见错误与排查手册

> 按场景分类的踩坑手册。每遇到一个新坑就补一条，这是本仓库的"活文档"。

## 0. 装包类（新手最先踩的坑）

| 症状 | 原因 | 解法 |
|---|---|---|
| 照教程装包，结果拉进来一堆 **`ros-rolling-*`**（本仓库实测：误装 `ros-rolling-turtlesim` 带来 98 个包、1~2 GB） | 第三方文档镜像（如 `ros.ncnynl.com`）**URL 里写着 humble，页面内容却是从 rolling 分支生成的**，代码块里的包名是 `ros-rolling-xxx` | 认准官方 `docs.ros.org/en/humble/...`；**照抄命令前先核对包名里的发行版代号**（本机必须是 `ros-humble-*`）；误装后 `sudo apt purge 'ros-rolling-*' && sudo apt autoremove` |
| `apt install` 慢到几十 KB/s | `sudo apt` 会丢弃代理环境变量，直连美国源 | 换国内镜像源，见 [00-setup.md §2.4](00-setup.md) |
| `apt update` 报 `File has unexpected size ... Mirror sync in progress?` | 镜像站正在同步，通常是 `deb-src`（源码索引 Sources.gz）滞后 | 在 `ros2.sources` 里把 `Types: deb deb-src` 改成 `Types: deb`（学习不需要源码包），或临时换一家镜像 |
| 换了源但没变快 / 装的还是旧版本 | 改完源没跑 `sudo apt update` | 索引更新后新源才生效 |

> 判断当前该装哪个发行版的包：`printenv ROS_DISTRO` 输出什么，包名就用什么前缀。

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

### 4.1 GUI 白屏排查（本仓库实测踩坑，2026-09）

症状：窗口能弹出来，但**内容全白**，窗口标题栏带 `[WARN:COPY MODE]` 前缀
（rviz2、turtlesim、甚至最简单的 xmessage 都一样）。

**第一步：区分是"GL 问题"还是"整个 WSLg 挂了"**——跑一个不依赖 OpenGL 的纯 X 客户端：

```bash
xmessage -title TEST "能看到这行字吗"
```

- xmessage **也白屏** → WSLg 合成器整体失效（GPU 直通卡死），见下面第二步
- xmessage 正常、只有 rviz2 白屏 → 只是 GL 问题 → `export LIBGL_ALWAYS_SOFTWARE=1` 后重跑

**第二步：确认 GPU 直通状态**：

```bash
dmesg | grep -i dxg | tail
# 出现 dxgkio_query_adapter_info: Ioctl failed: -22 / dxgkio_reserve_gpu_va failed
# 说明 WSL 拿不到宿主 GPU（本机实测：AMD 780M + RDP 会话下必现）
```

**第三步：修复**——在 **Windows** 的 PowerShell 里重启 WSL：

```powershell
wsl --shutdown
```

等 10 秒重开 WSL 终端即可恢复。原理：GPU 直通失败时 WSLg 本应自动退回软件渲染，
但偶发状态下"卡住不兜底"，重启后软件渲染路径正常工作（此时 `dxg` 报错仍在，
但窗口能画出来，只是没有 GPU 加速、rviz2 略慢）。

若重启后仍白屏，依次尝试：`wsl --update` → 更新显卡驱动 → Windows 侧装 X server
（VcXsrv/X410）并用 `export DISPLAY=<WindowsIP>:0` 绕过 WSLg。

### 4.2 其他 WSL2 问题

| 症状 | 原因 | 解法 |
|---|---|---|
| GUI 窗口完全不出来 | WSLg 没起来 | `wsl --shutdown` 重启 WSL；确认 `echo $DISPLAY` 非空 |
| rviz2 报 GL/EGL 错 | 显卡直通问题 | `export LIBGL_ALWAYS_SOFTWARE=1` |
| Qt 报 xcb 错 | 平台插件问题 | `export QT_QPA_PLATFORM=xcb` |
| xmessage 里中文显示成乱码 | xmessage 自带西文字体无中文字形 | 正常现象，仅影响 xmessage；rviz2/turtlesim 用 Qt 显示中文正常 |
| 时间/日期错乱 | 时钟漂移 | 见上表 |
| 跨 Windows 主机通信不通 | 网络模式限制 | 本机学习场景用 `ROS_LOCALHOST_ONLY=1` |

## 5. C++ 代码类（本仓库实测踩过的坑）

| 症状 | 原因 | 解法 |
|---|---|---|
| 节点随机**段错误**（exit -11） | range-for 遍历 `get_parameter("x").as_string_array()`：引用绑定到临时 Parameter 的内部 vector，临时对象在循环初始化后即销毁 → 悬垂引用 | 先存局部变量再遍历（p09 的 load_waypoints 有完整注释） |
| 字段莫名对调/缺失 | `.action` 三段顺序写反（正确：目标/结果/反馈） | docs/06 第 4 节 |
| 编译报接口字段不存在 | 同上 + Python 保留字字段名 | docs/06 第 4 节 |
| 定时器相关随机崩溃 | 在定时器自己的回调里 `cancel()` 它自己 | 用标志位替代（p05/p09 的取消逻辑有注释） |
| 编译警告 deprecated | 订阅回调用了 `const T::SharedPtr` | 用 `T::ConstSharedPtr` |
| 订阅端收不到任何数据且位置在"服务端后启动"场景 | 个别环境下 DDS 未重新发现后加入的发布者（FastDDS 局限） | 重启订阅节点；或换 CycloneDDS |

## 6. 进程管理坑

### 6.1 `Ctrl-C` vs `Ctrl-Z`（一定要分清）

| 按键 | 信号 | 作用 | 结果 |
|---|---|---|---|
| **`Ctrl-C`** | SIGINT | **结束**程序 | 进程退出，资源释放 ✓ |
| **`Ctrl-Z`** | SIGTSTP | **挂起**程序（暂停但不结束） | 进程状态变成 `T`，仍占着 DDS 端点，`kill`（SIGTERM）也杀不掉它！ |

按了 `Ctrl-Z` 后终端会显示 `[1]+ Stopped ros2 run ...`，此时：

```bash
jobs        # 查看挂起的作业
fg          # 调回前台继续运行
kill %1     # 结束 1 号作业
```

**排查线索**：如果 `ps` 里进程的 STAT 是 `T`（或 `Tl`），就是被挂起了；
用 `kill -9` 强杀，或先 `kill -CONT` 恢复再正常结束。
本仓库实测踩坑：挂起的 `ros2 launch` 还会挡住子进程回收，留下一堆僵尸（`Z`）进程。

### 6.2 多个控制源抢同一个话题

话题**允许多个发布者**，谁都不会报错。多个程序同时往 `/turtle1/cmd_vel` 发指令时，
表现为"乌龟行为诡异"（实测：残留的 `ros2 topic pub` 持续发 `linear.x=2.0`，
叠加导航节点的转向指令 → 乌龟原地画圈，而不是走直线）。

**排查**：看有几个发布者、都是谁：

```bash
ros2 topic info /turtle1/cmd_vel -v | grep -A1 "Publisher count"
ros2 node list | sort | uniq -c | sort -rn     # 找重复的节点名
```

**跑任何 demo 前的自检习惯**：

```bash
ros2 node list                                # 应该只有你预期的节点
ps -eo pid,stat,cmd | grep -E "ros2 launch|ros2 run" | grep -v grep   # 看有无残留（注意 STAT 有无 T）
```

### 6.3 `ros2 run` 包装脚本的残留问题

`ros2 run` 是个包装脚本：`kill $!` 杀掉的是脚本，**节点本体可能继续活着**——
同一个节点名悄悄残留多个实例，会污染通信（本仓库验证巡航时因此翻过车：
残留的 navigate_to_server 抢走了目标，乌龟原地画圈）。清理用：
```bash
pkill -f '<节点可执行文件名>'     # 如 pkill -f waypoint_follower
# 或者按路径批量杀（正则技巧：pkill -f 'install/p0[0-9]_'）
```
验证：`ps aux | grep -E 'install/p0' | grep -v grep` 应为空。

## 7. 通杀三板斧

```bash
# ① 看谁在：确认通信双方都在
ros2 node list && ros2 topic list -t

# ② 看连上没：端点 + QoS 匹配情况（ROS 2 没有 master 报错，全靠这个）
ros2 topic info <话题> -v

# ③ 看日志：别只盯屏幕输出
tail -f ~/.ros/log/latest/*.log        # 最近一次运行的日志
```
