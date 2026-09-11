# 环境搭建与验证（WSL2 + ROS 2 Humble）

> 本文记录本仓库的实测环境快照与配置步骤，也适合作为新机器的搭建备忘录。

## 1. 环境快照（2026-09 实测）

| 项目 | 值 |
|---|---|
| 宿主 | Windows + WSL2（NAT 模式，非 mirrored） |
| 发行版 | Ubuntu 22.04 (jammy)，内核 6.18 WSL2 |
| ROS 2 | Humble（desktop full，283 包）→ `/opt/ros/humble` |
| RMW 实现 | FastDDS（唯一，未装 CycloneDDS） |
| GUI | WSLg 1.0，`DISPLAY=:0`，turtlesim/rviz2/rqt 直接出窗口 |
| 工具链 | gcc 11.4 / cmake 3.22.1 / Python 3.10.12 / colcon 0.3.x |
| 工作空间 | `/home/lxl/ros2_ws/ros2-humble-learning`（本仓库，自包含） |
| 资源 | 8 核，`/home` 余量 900+ GB |

## 2. 一次性配置

### 2.1 安装 colcon（构建工具）

```bash
sudo apt update
sudo apt install -y python3-colcon-common-extensions
colcon --help   # 验证
```

### 2.2 ~/.bashrc 追加（新开终端自动生效）

```bash
# ===== ROS 2 Humble =====
source /opt/ros/humble/setup.bash
# 本学习工作空间的 overlay（存在才 source，避免尚未 build 时报错）
if [ -f /home/lxl/ros2_ws/ros2-humble-learning/install/setup.bash ]; then
  source /home/lxl/ros2_ws/ros2-humble-learning/install/setup.bash
fi
export ROS_DOMAIN_ID=42          # 避免与同网段其他 ROS 2 系统串扰
# export ROS_LOCALHOST_ONLY=1    # 仅在 DDS 发现异常时打开，见 docs/12
```

> 概念：`source /opt/ros/humble/setup.bash` 是底层（underlay），
> `source install/setup.bash` 是本工作空间覆盖层（overlay）——先 source 底层再 source 覆盖层，
> 覆盖层里的包会"遮蔽"底层同名包。这就是 ROS 1 `devel/setup.bash` 的对应物。

### 2.3 构建工作空间

```bash
cd ~/ros2_ws/ros2-humble-learning
colcon build --symlink-install      # 首次 1~3 分钟
source install/setup.bash
```

`--symlink-install`：install 里的文件是源码的软链接，改 launch/yaml/rviz 配置**不用重编**；
改 `.cpp` 或 `.msg` 仍需重编。学习期建议一直开着。

### 2.4 国内网络加速（重要，先做这一步再装任何包）

**坑点**：`sudo apt` 会**丢弃 shell 里的代理环境变量**，所以即使你配了代理，apt 仍是直连
`packages.ros.org` / `archive.ubuntu.com`（美国），实测只有几百 KB/s。正确做法是**换国内镜像源**
（走 CDN，比代理还快），代理留给 GitHub 这类没有国内镜像的场景。

实测对比（2026-09，公司网络）：

| 源 | 换源前 | 换源后 |
|---|---|---|
| Ubuntu 主源 | archive.ubuntu.com 581 KB/s | 阿里云 **4390 KB/s** |
| ROS 2 源 | packages.ros.org 983 KB/s | 中科大 **5075 KB/s** |

```bash
# ① Ubuntu 主源 → 阿里云
sudo cp /etc/apt/sources.list /etc/apt/sources.list.bak
sudo sed -i 's|http://archive.ubuntu.com/ubuntu|https://mirrors.aliyun.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|https://mirrors.aliyun.com/ubuntu|g' /etc/apt/sources.list

# ② ROS 2 源 → 中科大（备选：清华 mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu）
sudo cp /etc/apt/sources.list.d/ros2.sources /etc/apt/sources.list.d/ros2.sources.bak
sudo sed -i 's|http://packages.ros.org/ros2/ubuntu|https://mirrors.ustc.edu.cn/ros2/ubuntu|' /etc/apt/sources.list.d/ros2.sources

# ③ 必须重新更新索引，新源才生效
sudo apt update

# 验证：应显示 Candidate 版本且 update 飞快
apt-cache policy ros-humble-turtlesim | head -3
```

**代理与镜像的分工**（本项目实测结论）：

| 场景 | 用镜像换源 | 用代理 |
|---|---|---|
| `apt install` 装 ROS/系统包 | ✅ 最快 | ❌ sudo 会丢代理变量 |
| `git clone` / `git push` GitHub | 无镜像 | ✅ 必须 |
| pip（可选） | 清华 PyPI 源 | — |

## 3. 目录约定

```
ros2_ws/
└── ros2-humble-learning/     ← git 仓库根 = colcon 工作空间根
    ├── src/                  ← 所有包（colcon 只认这里）
    │   ├── p01_hello_ros2/
    │   └── ...
    ├── build/ install/ log/  ← colcon 产物（.gitignore 已忽略）
    └── docs/                 ← 学习文档
```

## 4. 常用命令速查

### 构建与环境

```bash
colcon build --symlink-install                          # 全量构建
colcon build --symlink-install --packages-select p03    # 只构建指定包
colcon build --symlink-install --packages-up-to p09     # 构建指定包及其依赖（改接口后必用）
rm -rf build install log && colcon build --symlink-install   # 怀疑环境脏了：全量重来
source install/setup.bash                               # 重编后当前终端必须重新 source
ros2 pkg list | grep '^p0'                              # 列出本仓库包
ros2 pkg executables p01_hello_ros2                     # 列出包的节点
```

### 排错

```bash
colcon build 失败 → 看 log/latest_build/<包名>/stdout_stderr.log
ros2 run 找不到可执行文件 → 1) 漏 install(TARGETS)  2) 忘了重新 source
launch 报 file not found → 漏 install(DIRECTORY launch ...)
```

### 运行与检查

```bash
ros2 run <包> <节点> --ros-args -p 参数:=值      # 运行时传参（--ros-args 必须在节点名后）
ros2 node list / info <节点>
ros2 topic list -t / echo / hz / info <话题> -v
ros2 service list -t / call <服务> <类型> "{...}"
ros2 action list -t / send_goal <动作> <类型> "{...}" --feedback
ros2 param list / get / set / describe / dump / load
ros2 interface list / show / package
ros2 bag record -a / info / play
ros2 launch <包> <launch文件>
ros2 run rqt_graph rqt_graph
```

### 进程清理

```bash
pkill -f ros2            # 杀光所有 ros2 进程（含 daemon）
killall turtlesim_node   # 图形程序 Ctrl-C 不灵时
```

## 5. WSL2 注意事项

- **同机多进程通信正常**（走共享内存/回环）；跨 Windows 宿主机的多播发现大概率不通，
  需要时 `export ROS_LOCALHOST_ONLY=1` 强制本机发现（Humble 专属开关）。
- **时钟漂移**：Windows 睡眠唤醒后 WSL 时间可能落后，TF/rosbag 时间戳异常时
  `sudo hwclock -s`（需密码）。
- **rviz2 GL 报错**：`export LIBGL_ALWAYS_SOFTWARE=1` 软渲染兜底。
- 首次开 GUI 窗口较慢（WSLg 冷启动），属正常。
