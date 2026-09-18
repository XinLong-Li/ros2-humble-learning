# colcon 与 overlay 概念梳理

> 横切文档：构建系统与工作空间的**概念地图**。命令清单见 [00-setup.md §4](00-setup.md)，
> 本文回答"为什么是这样"——colcon 的定位、它和 catkin/CMake/ament 的层级关系、
> 以及 overlay/underlay 这个 ROS 2 特有的分层环境概念。

## 1. colcon 是什么

colcon，读作 /ˈkɑːl.kɑːn/，全称 **COLlective CONstruction**。

**一句话：colcon = ROS 2 的多包构建/编译管理工具（ROS 1 时代是 `catkin_make`）。**
它是**上层调度工具**，自动扫描工作空间里所有 ROS 2 功能包、解析包之间的依赖顺序，
然后调用底层的 CMake / Python setuptools 去真正编译代码。

> 类比：`colcon` 是**包工头**，负责排好各个包的编译先后顺序；`CMake` 是工人，
> 负责真正把 C++ 源码编译成可执行文件。

### 工作空间结构

```
ros2_ws/
├── src/            # 源代码，放各个 ROS 2 功能包（git 仓库放这里）
├── build/          # colcon 生成：编译中间文件（每个包独立目录）
├── install/        # colcon 生成：编译成品，可执行文件、库、环境脚本
└── log/            # colcon 生成：编译日志，报错看这里
```

### `colcon build` 做了什么

1. 扫描 `src` 下所有包，读取每个包的 `package.xml`，解析依赖关系，自动排编译顺序（A 被 B 依赖 → A 先编译）
2. 并行调用 cmake/setuptools 编译各个包
3. 产物输出到 `build`，安装到 `install`
4. 在 install 目录生成 `setup.bash` / `local_setup.bash`——**source 之后 `ros2 run` 才能找到你编译出来的节点**

常用变体：

```bash
colcon build --symlink-install        # launch/py/yaml 用软链接，改这些文件不用重编（开发首选）
colcon build --packages-select my_pkg # 只编译指定包
colcon build --packages-up-to my_pkg  # 编译指定包 + 它依赖的上游（改接口后必用）
colcon test                           # 跑测试
```

### 层级关系（和 RMW/DDS 串起来）

```
你的 C++/Python 源码（src/my_pkg）
        ↓
package.xml + CMakeLists.txt / setup.py 描述包信息
        ↓
colcon（调度） → ament_cmake / ament_python（ROS 2 构建辅助） → CMake/gcc
        ↓
编译出可执行节点，链接 rclcpp / rclpy → RMW → DDS(FastDDS/CycloneDDS)
```

### 四个易混概念

| 名称 | 是什么 |
|---|---|
| **colcon** | 多包调度器，负责排编译顺序，**本身不做编译** |
| **ament_cmake** | ROS 2 的 CMake 扩展，给 CMake 增加 ROS 2 能力（生成 msg、注册节点） |
| **CMake** | 通用 C/C++ 构建系统，把源码编译成二进制 |
| **package.xml** | 每个 ROS 包的描述文件，声明依赖、包名版本；colcon 靠它识别包 |

### 安装与命令补全

```bash
sudo apt install python3-colcon-common-extensions

# 命令补全（推荐）
echo "source /usr/share/colcon_argcomplete/hook/colcon-argcomplete.bash" >> ~/.bashrc
source ~/.bashrc
```

### 开发固定流程

```bash
cd ros2_ws
colcon build --symlink-install
source install/setup.bash
ros2 run 包名 节点名
```

**小坑提醒**

- 必须在**工作空间根目录**执行 `colcon build`，不是 `src` 目录
- 改代码后要重新 build；加了 `--symlink-install` 则 launch/py/yaml 改动免重编
- source 只对当前终端生效，新开终端要重新 source（或写进 `~/.bashrc`）

## 2. catkin → colcon：历史与设计

### catkin（ROS 1）

- **开发主体**：Willow Garage（美国机器人研究公司，ROS 的发源地）
- **诞生**：替代 ROS 初代 `rosbuild`，从 ROS Groovy 版本正式启用
- **命名由来**：catkin = 柳絮，取自 Willow（柳树），呼应公司名 Willow Garage
- **核心开发者**：Tully Foote、Dirk Thomas 等人
- 三个概念要分清：
  1. **catkin**（构建框架）：`CMakeLists.txt` 的扩展、`package.xml` 解析规则
  2. **catkin_make / catkin_make_isolated**：官方构建命令
  3. **catkin_tools（`catkin build`）**：社区第三方增强工具，非官方出品

> Willow Garage 后来缩减解散，catkin 由 OSRF（现在的 Open Robotics）接手维护。

### colcon（ROS 2）

- **核心主导人：Dirk Thomas（同一个人！当年 catkin 的核心开发者）**
- **起源**：ROS 2 早期先用 `ament_tools`，后来觉得局限性大；2017 年启动 colcon 项目，
  **融合 catkin 与 ament 的经验**，统一成一套跨平台元构建工具
- **开发组织**：Open Robotics 维护，代码托管在独立的 [colcon](https://github.com/colcon) GitHub 组织（独立于 ros2 仓库）
- **定位**：**通用元构建工具，不只限于 ROS**，理论上可以编译普通 CMake、Python 包

历史脉络：

```
Willow Garage catkin → OSRF ament_tools → Dirk Thomas 主导开发 colcon
```

### 层级对应（关键）

| ROS 1 | ROS 2 | 角色 |
|---|---|---|
| `catkin_make` | `colcon` | **上层调度工具**（包工头） |
| `catkin` | `ament_cmake` / `ament_python` | **单包构建框架**（工人规范） |

### colcon 相对 catkin_make 的改进

1. `catkin_make` 所有包共享同一个 CMake 上下文，容易出现变量污染
2. colcon **每个包独立隔离编译环境**（每个包单独调用 CMake）
3. 原生支持 Windows、macOS、Linux 三平台
4. 架构插件化：不仅能编译 ament 包，还兼容老 catkin 包、纯 CMake 包

**一句话总结**：catkin 是 Willow Garage 团队开发的 ROS 1 构建体系；
colcon 由当年 catkin 核心开发者 Dirk Thomas 在 Open Robotics 主导开发，
是为 ROS 2 打造的新一代隔离式元编译调度工具。

源码：catkin <https://github.com/ros/catkin> · colcon <https://github.com/colcon> ·
ament_cmake <https://github.com/ament/ament_cmake>

## 3. overlay / underlay：ROS 2 的分层环境

**一句话：overlay = 叠在底层 ROS 之上、你自己的开发工作空间；底层那个叫 underlay。**

```
underlay（底层基础）：/opt/ros/humble           系统装好的 ROS 2 本体
        ↓
overlay（上层叠加层）：~/ros2-humble-learning   你自己的开发工作空间
```

`install/` 目录就是 overlay 的**环境入口**，`source install/setup.bash` 就是把这一层
"叠加"到终端的 ROS 环境里。

### 核心规则：source 顺序决定覆盖关系

**必须先 source underlay，再 source overlay**：

```bash
source /opt/ros/humble/setup.bash                      # 第一步：底层基础
source ~/ros2-humble-learning/install/setup.bash       # 第二步：自己的 overlay
```

ROS 查找包的逻辑：**先去 overlay 里找，找不到才去 underlay 找**。
如果 overlay 里有一个和 underlay 同名的包，**overlay 版本直接覆盖底层**。

> 举例：你自己写了一个 turtlesim 放到 `src/`，编译后 source overlay，
> `ros2 run turtlesim turtlesim_node` 会跑你编译的版本，而不是 `/opt/ros/humble` 自带的原版。

### 目录结构与环境变量

```
ros2-humble-learning/          ← overlay 工作空间根
├── src/                       ← 源码，放你的包
├── build/                     ← colcon 编译中间文件
├── install/                   ← ★ overlay 的环境出口，setup.bash 在这里
└── log/
```

`install/setup.bash` 的作用：修改环境变量（`AMENT_PREFIX_PATH`、`PATH`、`LD_LIBRARY_PATH`），
把这个工作空间注册进 ROS 环境，成为上层 overlay。

查看当前终端加载了哪些层：

```bash
printenv AMENT_PREFIX_PATH | tr ':' '\n'
# 越靠前的优先级越高；underlay 在最末尾
```

实测（只 source underlay → 再 source overlay）：

```text
只 source /opt/ros/humble/setup.bash:
  /opt/ros/humble                        # 就 1 项

再 source ~/ros2-humble-learning/install/setup.bash:
  /home/lxl/ros2-humble-learning/install/p09_turtle_control
  /home/lxl/ros2-humble-learning/install/p08_tf2
  ...                                    # overlay 的包排在最前
  /opt/ros/humble                        # underlay 被挤到最后
```

### 可以多层叠加

```
底层 underlay:  /opt/ros/humble
        ↓
overlay 1:      ~/ros2-humble-learning
        ↓
overlay 2:      ~/robot_project_ws
```

source 顺序：底层 → overlay1 → overlay2，**越后面 source 的优先级越高**。

### 与 ROS 1 的对比

ROS 1 的 catkin 也有 overlay，但入口是 `devel/` 目录；
ROS 2 用 `install/` 作为 overlay 入口，**没有 devel 文件夹**，隔离性更强。

### 三个踩坑点

1. ❌ **顺序不能反**：先 source overlay 再 source `/opt/ros/humble`，你的 overlay 会被底层覆盖失效
2. overlay 编译时会读取 underlay 里所有包作为依赖 → **`colcon build` 前终端必须已 source underlay**
3. overlay 只对**当前终端**生效，新开终端环境清空，需要重新 source

**一句话总结**：underlay 是地基（系统 ROS）；overlay 是盖在地基上的自建房（你的工作空间），
房子里同名的东西优先使用，且不会改动地基文件。
