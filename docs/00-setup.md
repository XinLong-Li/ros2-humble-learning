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

### 2.4 国内网络加速（换源 + 代理分工）

> 建议在装任何包**之前**做这一步，否则 `apt install` 会慢到怀疑人生。

#### 为什么要换源

**坑点**：`sudo apt` 默认会**丢弃 shell 里的代理环境变量**（sudo 的安全机制），
所以即使你配了代理，apt 仍然是直连 `packages.ros.org` / `archive.ubuntu.com`（美国），
实测只有几百 KB/s。正确做法是**换国内镜像源**（走 CDN 就近分发，比代理还快还稳），
代理留给 GitHub 这类没有国内镜像的场景。

实测对比（2026-09，公司网络，均为直连）：

| 源 | 换源前（官方） | 换源后（阿里云） | 提升 |
|---|---|---|---|
| Ubuntu 主源 | archive.ubuntu.com 581 KB/s | **4390 KB/s** | 7.5× |
| ROS 2 源 | packages.ros.org 983 KB/s | **6725 KB/s** | 6.8× |

#### 换源操作

```bash
# ① 备份（改坏了能恢复，尤其是 headless 的服务器）
sudo cp /etc/apt/sources.list /etc/apt/sources.list.bak
sudo cp /etc/apt/sources.list.d/ros2.sources /etc/apt/sources.list.d/ros2.sources.bak

# ② Ubuntu 主源 → 阿里云（两条分开跑，避免长命令粘贴时被折行截断）
sudo sed -i 's|http://archive.ubuntu.com/ubuntu|https://mirrors.aliyun.com/ubuntu|g' /etc/apt/sources.list
sudo sed -i 's|http://security.ubuntu.com/ubuntu|https://mirrors.aliyun.com/ubuntu|g' /etc/apt/sources.list

# ③ ROS 2 源 → 阿里云，同时去掉 deb-src（原因见下）
sudo sed -i 's/^Types: deb deb-src/Types: deb/' /etc/apt/sources.list.d/ros2.sources
sudo sed -i 's|http://packages.ros.org/ros2|https://mirrors.aliyun.com/ros2|' /etc/apt/sources.list.d/ros2.sources

# ④ 更新索引（改源后必须执行，否则新源不生效）
sudo apt update
```

**为什么要去掉 `deb-src`**：它下载的是**源码包索引**（`Sources.gz`），学习时完全用不到；
而且镜像站的源码索引同步往往滞后于二进制索引，容易报
`File has unexpected size ... Mirror sync in progress?`。去掉后既快、又少一类报错。

#### 验证

```bash
grep -E "^(Types|URIs):" /etc/apt/sources.list.d/ros2.sources
# 期望：Types: deb  /  URIs: https://mirrors.aliyun.com/ros2/ubuntu

apt-cache policy ros-humble-turtlesim | head -3
# 期望：Installed 与 Candidate 都显示版本号

sudo apt update     # 期望：几秒跑完、没有 Err
```

#### 备选镜像与一键切换

三家都可用，实测速度接近，谁出问题就换谁：

| 镜像 | ROS 2 地址 | 实测速度 | 备注 |
|---|---|---|---|
| **阿里云**（当前） | `https://mirrors.aliyun.com/ros2/ubuntu` | 6.7 MB/s | 商业 CDN，与 Ubuntu 源同一家 |
| 中科大 | `https://mirrors.ustc.edu.cn/ros2/ubuntu` | 5.7 MB/s | 学生社团维护，口碑最好 |
| 清华 | `https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu` | 0.4 MB/s | 同样是社团维护 |

```bash
# 切到中科大
sudo sed -i 's|aliyun\.com/ros2|mirrors.ustc.edu.cn/ros2|' /etc/apt/sources.list.d/ros2.sources && sudo apt update
# 切到清华
sudo sed -i 's|aliyun\.com/ros2|mirrors.tuna.tsinghua.edu.cn/ros2|' /etc/apt/sources.list.d/ros2.sources && sudo apt update
```

> 这些镜像站是谁在维护、可靠性怎么看 → 见 [附录 6.1](#61-这些镜像站是谁在维护)

#### 代理与镜像的分工

| 场景 | 用镜像换源 | 用代理 |
|---|---|---|
| `apt install` 装 ROS/系统包 | ✅ 最快 | ❌ sudo 会丢代理变量 |
| `git clone` / `git push` GitHub | 无国内镜像 | ✅ 必须（本仓库已配 local 代理） |
| pip（可选） | 清华 PyPI：`https://pypi.tuna.tsinghua.edu.cn/simple` | — |
| `gh` CLI（走 api.github.com） | 无 | ⚠️ 经代理会 403，需先 `unset` 代理变量 |

#### 常见报错

| 报错 | 原因 | 处理 |
|---|---|---|
| `File has unexpected size ... Mirror sync in progress?` | 镜像站正在同步（通常是 deb-src 的 `Sources.gz` 滞后） | 去掉 `deb-src`（见上）或临时换一家镜像 |
| `sed: no input files` + `Permission denied` | 长命令粘贴时被终端折行截断，`sed` 没收到文件名 | 拆成短命令一条条跑，详见 [附录 6.2](#62-sed-命令详解) |
| 换了源但没变快 / 装到的还是旧版本 | 没跑 `sudo apt update` | 索引更新后新源才生效 |

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

## 6. 附录

### 6.1 这些镜像站是谁在维护

| 镜像 | 维护方 | 性质 |
|---|---|---|
| **中科大** `mirrors.ustc.edu.cn` | 中科大 **LUG（Linux 用户协会，学生社团）** 实际维护；学校**网络信息中心**提供服务器/机房/带宽 | 非营利，社区公共服务 |
| **清华** `mirrors.tuna.tsinghua.edu.cn` | 清华 **TUNA 协会**（学生社团） | 非营利，社区公共服务 |
| **阿里云** `mirrors.aliyun.com` | 阿里云（商业 CDN） | 商业服务，主要面向自家 ECS 用户 |

**中科大镜像站的来历**：2003 年 LUG 通过 BBS **网上筹款**搭起 `debian.ustc.edu.cn`，后来加了
Ubuntu 镜像；2010 年底在网络信息中心张焕杰老师帮助下获得新服务器，把原有镜像与学校的
CentOS 镜像整合；2011 年 5 月 `mirrors.ustc.edu.cn` 正式对外服务，至今十几年。

**怎么看待"大企业 vs 大学"**——这不是判断可靠性的正确角度：

- 大学镜像站（中科大 LUG、清华 TUNA）是国内开源基础设施的**顶梁柱**，技术热情驱动、
  非营利，同步勤、协议全、文档细致（LUG 还写了不少技术博客）。缺点是**志愿者维护、人力有限**，
  遇到上游仓库大改版时，非核心仓库的同步可能滞后——这正是 `Mirror sync in progress` 报错的成因。
- 商业镜像（阿里云）带宽充足、CDN 就近，对主流发行版同步及时；但对小众仓库的同步优先级
  不一定比大学镜像高。

**结论**：三家都可信，**用实测速度和同步新鲜度选，别用牌子选**。实践建议：配一家主力，
记好一键切换命令（见 §2.4），出问题就换。

> 参考：[USTC LUG 网络服务](https://lug.ustc.edu.cn/wiki/lug/services/mirrors/)、
> [LUG Servers 博客](https://servers.ustclug.org/2016/12/mirrors-obsolete-debian-centos-domain-name/)、
> [中科大镜像源开张报道（OSChina）](https://www.oschina.net/news/17817/ustc-edu-opensource-mirros)

### 6.2 sed 命令详解

换源命令里用到的 `sed` 值得单独讲清楚——它是 Linux 上最常用的文本处理工具之一。

#### sed 是什么

**s**tream **ed**itor，**流编辑器**：逐行读取文本，对每行执行你给的指令，输出结果。
最常用的指令是 `s`（substitute，替换）。

```bash
sed 's/苹果/香蕉/'      # 把每行第一个"苹果"换成"香蕉"
sed 's/苹果/香蕉/g'     # 加 g = global，换成行内所有"苹果"
sed 's/^Types:/类型:/'  # ^ = 行首锚点，只匹配行首的 "Types:"
```

#### 完整拆解

```bash
sudo  sed  -i  's/^Types: deb deb-src/Types: deb/'  文件路径
└─┬─┘ └┬┘ └┬┘  └────────────┬────────────┘         └─┬──┘
  │    │   │                │                        │
  │    │   │                │                        └─ 要改的文件
  │    │   │                └─ sed 脚本：替换指令（多条用 ; 分隔，按顺序执行）
  │    │   └─ in-place：直接修改文件本身
  │    └─ stream editor（流编辑器）
  └─ 以 root 身份运行（/etc/apt/ 下都是 root 的文件，普通用户改不了）
```

#### `-i` 是什么意思

**in-place（就地修改）**。不加它，`sed` 只把结果**打印到屏幕**、不动文件：

```bash
sed 's/abc/xyz/' file      # 只在屏幕上显示替换结果，file 没变 ← 安全的预览
sed -i 's/abc/xyz/' file   # file 真的被改了
```

**好习惯**：改系统文件前先不加 `-i` 跑一遍看输出对不对，确认了再加上 `-i`。

#### 分隔符技巧：`/` 还是 `|`

`s` 指令的格式是 `s/旧内容/新内容/`。当"旧内容"本身含 `/`（比如 URL）时，用 `/` 作分隔符
就得把每个 `/` 转义成 `\/`，非常难看：

```bash
s/https:\/\/mirrors.ustc.edu.cn\/ros2\/ubuntu/https:\/\/mirrors.aliyun.com\/ros2\/ubuntu/
```

改用 `|`、`#`、`@` 这类字符作分隔符就能避免转义：

```bash
s|https://mirrors.ustc.edu.cn/ros2/ubuntu|https://mirrors.aliyun.com/ros2/ubuntu|
```

另一种省事写法是**只匹配能唯一定位的片段**——反正目的只是把它换掉：

```bash
s|packages.ros.org/ros2|mirrors.aliyun.com/ros2|
```

#### 为什么长命令会"被截断"

终端会把长命令**自动折行显示**，但粘贴时如果中间带了真正的换行符，就会被当成"回车"执行，
一条命令变成三条：

```bash
sudo sed -i '...'            # ← sed 没收到文件名 → 报 "sed: no input files"
  /etc/apt/.../ros2.sources  # ← 这行被当成命令执行 → 报 "Permission denied"
  sudo apt update            # ← 这条倒是正常跑了
```

`sed: no input files` 的字面意思就是"你没告诉我改哪个文件"。
**对策**：把长命令拆成多条短命令；或确认粘贴时没有被插入换行。

#### 正则里的小细节

- `.` 在正则中表示**任意字符**，要匹配真正的点号需要转义：`ustc\.edu\.cn`
- `^` 表示**行首**。`^Types:` 只匹配行首的 `Types:`，避免误伤文件里其他位置的相同文字
- 不加 `g` 时，每条命令每行只替换**第一个**匹配；加 `g` 替换全部

#### 验证改动

```bash
grep -E "^(Types|URIs):" /etc/apt/sources.list.d/ros2.sources   # 看结果
# 出错了就用备份恢复：
sudo cp /etc/apt/sources.list.d/ros2.sources.bak /etc/apt/sources.list.d/ros2.sources
```

### 6.3 换个镜像会不会不安全？

**不会**。apt 有完整的 **GPG 签名校验**链路：

1. 仓库的 `Release` 文件由 **ROS 官方私钥签名**，你的系统用导入的公钥（在
   `/etc/apt/sources.list.d/ros2.sources` 的 `Signed-By` 里）验证签名
2. `Packages` 索引文件在 `Release` 里记录 **SHA256 哈希**，逐个校验
3. 每个 `.deb` 包本身也在 `Packages` 里记录了哈希

所以镜像站**只能搬运、不能伪造**——它没有签名私钥，改了任何一个包都会校验失败
（你前面看到的 `File has unexpected size ...` 就是这类校验在起作用，只不过那次的"不一致"
是同步滞后导致的，不是恶意行为）。

镜像选择影响的是**速度**和**可用性**（会不会同步滞后、会不会缺包），**不是安全性**。
最坏情况是装不上或装到旧版本，不会中毒。
