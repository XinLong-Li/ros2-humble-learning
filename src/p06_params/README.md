# p06_params —— 参数（parameter）

配套文档：[docs/07-parameters.md](../../docs/07-parameters.md)

## 学习目标

1. 理解 ROS 2 参数模型：**参数属于节点**，必须先 `declare_parameter` 才能读写（ROS 1 的全局参数服务器不存在了）
2. 会用 **描述符**（`ParameterDescriptor`）给参数加上说明、取值范围、只读标记，让 `ros2 param describe` / rqt_reconfigure 认得它
3. 会用 **set 前校验回调**（`add_on_set_parameters_callback`）拒绝非法值——它就是 ROS 2 版的 dynamic_reconfigure
4. 知道参数从哪些途径进来（`--ros-args -p`、YAML 文件、CLI、代码）以及它们**都要过同一套校验**
5. 会用 `AsyncParametersClient` 远程读/写另一个节点的参数，并看懂 `/parameter_events`

## ROS 1 → ROS 2 对照

| ROS 1 (Noetic) | ROS 2 (Humble) |
|---|---|
| 全局参数服务器（master 持有），节点可读任意参数 | **参数属于每个节点**；节点不 `declare_parameter` 就没有这个参数 |
| `rosparam set` / `rosparam get` | `ros2 param set` / `ros2 param get` |
| `rosparam list` / `rosparam dump` | `ros2 param list` / `ros2 param dump` |
| launch 里的 `<param name="x" value="1"/>` | YAML 文件 + `--ros-args --params-file xxx.yaml`（launch 里用 `parameters=[...]`） |
| `nh.getParam("x", v)`（读全局名字） | `declare_parameter<T>("x", 默认值)` 拿返回值，或 `get_parameter("x")` |
| `dynamic_reconfigure`（写 .cfg + 重新生成代码 + rqt_reconfigure） | `add_on_set_parameters_callback` 校验回调 + rqt_reconfigure（**不用写 .cfg**） |
| 参数改动没有统一通知 | `/parameter_events` 话题广播每次变化（rqt_reconfigure 就靠它） |
| 参数随便改，没有类型/范围概念 | 描述符里的 `floating_point_range` / `integer_range` / `read_only` 会被工具链和 rclcpp 检查 |

一句话总结：ROS 1 是"**一个全局字典**"，ROS 2 是"**每个节点自己的、带类型的、可校验的配置项**"。

## 文件说明

```
p06_params/
├── package.xml                     # 包元信息 + 依赖（rclcpp、rcl_interfaces）
├── CMakeLists.txt                  # 构建规则：两个可执行文件 + 安装 config/
├── config/
│   └── param_demo.yaml             # 参数文件：故意写了超范围的 max_speed: 9.9
└── src/
    ├── param_demo_node.cpp         # 节点 param_demo_node：声明/描述符/校验回调/参数事件
    └── param_client_node.cpp       # 节点 param_client_node：远程读写别人的参数（教学序列 6 步）
```

### 两个节点的参数清单

| 节点 | 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|---|
| `param_demo_node` | `max_speed` | double | 2.5 | 描述符带 `floating_point_range` [0.0, 5.0] step 0.1 |
| | `robot_name` | string | `turtle_x` | |
| | `publish_rate` | double | 1.0 | 练习：给它加范围校验 |
| | `enable_debug` | bool | false | |
| | `waypoints` | string[] | `['home','kitchen']` | 数组参数 |
| | `serial_no` | string | `SN-0001` | 描述符带 `read_only = true` |
| `param_client_node` | `remote_node` | string | `param_demo_node` | 要连的目标节点名 |

> `ros2 param list` 里还会看到 `use_sim_time` 和几个 `qos_overrides.*`：那是 rclcpp 自动声明的内建参数，不是本包代码写的。

## 编译运行

```bash
source /opt/ros/humble/setup.bash
cd ~/ros2_ws/ros2-humble-learning
colcon build --symlink-install --packages-select p06_params
source install/setup.bash
```

### 1. 跑起演示节点，用 CLI 读参数

终端 A：

```bash
ros2 run p06_params param_demo_node
```

终端 B：

```bash
ros2 param list                              # 看节点有哪些参数
ros2 param describe /param_demo_node max_speed
#   Type: double
#   Description: 最大线速度 (m/s)
#   Constraints:  Min value: 0.0
#                 Max value: 5.0
#                 Step: 0.1
ros2 param get /param_demo_node max_speed     # Double value is: 2.5
ros2 param get /param_demo_node waypoints     # String values are: ['home', 'kitchen']

ros2 param set /param_demo_node max_speed 9.9 # 被拒绝：观察节点侧的日志
#   Setting parameter failed: Parameter {max_speed} doesn't comply with floating point range.

ros2 param set /param_demo_node max_speed 4.0 # 成功
#   Set parameter successful
# 节点 A 侧此刻打印：
#   [on_set] 请求把 max_speed 改成 4.000000
#   [on_set] 校验通过（1 项）
#   参数已更新: max_speed = 4.000000      <- 来自 /parameter_events

ros2 param set /param_demo_node serial_no SN-9999   # read_only 参数
#   Setting parameter failed: parameter 'serial_no' cannot be set because it is read-only
```

### 2. 用 YAML 文件加载参数

```bash
ros2 run p06_params param_demo_node --ros-args \
  --params-file install/p06_params/share/p06_params/config/param_demo.yaml
```

`config/param_demo.yaml` 里 `max_speed: 9.9` 是**故意**写错的，节点会打印：

```
[ERROR] param_demo_node: 参数覆盖值被拒绝: parameter 'max_speed' could not be set:
        Parameter {max_speed} doesn't comply with floating point range.
[WARN]  param_demo_node: 已回退到默认值 max_speed = 2.5（把 YAML 改成 0.0~5.0 之间的值即可生效）
        max_speed    = 2.50          <- 回退到默认值
        robot_name   = demo_bot      <- YAML 里的合法值照样生效
        enable_debug = true
```

> 想验证"YAML 里的合法值会生效"，把 yaml 里的 9.9 改成 3.3 再跑一次即可。
> `--symlink-install` 时 install 里的 yaml 是源码的软链，**直接改源码那份就生效，不用重新编译**。

### 3. 导出当前参数

```bash
ros2 param dump /param_demo_node                              # 打印到屏幕
ros2 param dump /param_demo_node > /tmp/param_demo_dump.yaml  # 存文件（Humble 推荐重定向）
```

dump 出来的顶层 key 是 `/param_demo_node`（**带**斜杠），正好能被 `ros2 param load` 直接读回去——
和第 6 题、下面那张"坑"表里的键名差异是同一件事。

### 4. 跑参数客户端，看完整的远程读写序列

先保证终端 A 的 `param_demo_node` 在跑，然后：

```bash
ros2 run p06_params param_client_node
```

它按 1 秒一步的节奏走完 6 步：等参数服务 → `list_parameters` → `get_parameters` →
`describe_parameters` → `set_parameters(4.0)` 成功 → `set_parameters(9.9)` 被远端拒绝：

```
[5/6] set_parameters(max_speed = 4.0)，期望成功
      successful=true（已写入）
[6/6] set_parameters(max_speed = 9.9)，期望被拒绝
      successful=false, reason='Parameter {max_speed} doesn't comply with floating point range.'
      说明：successful=false 且带回了 reason —— 远端的校验（描述符范围 / on_set 回调）
      在服务端就把非法值挡下了，客户端只拿到结果；target 上的参数值仍是 4.0。
```

### 5. 观察参数事件 `/parameter_events`

终端 B 先监听，终端 C 再改参数：

```bash
# 终端 B
ros2 topic echo /parameter_events
# 终端 C
ros2 param set /param_demo_node publish_rate 3.0
```

终端 B 会收到一条 `node: /param_demo_node`、`changed_parameters: [{name: publish_rate, ...}]` 的事件。
注意：`--once` 常常先收到 `node: /_ros2cli_xxxx`（ros2 CLI 自己临时节点声明 `use_sim_time`）的事件，
那是 CLI 的副产物；`/parameter_events` 是**全局**话题，所有节点的参数变化都在上面。

### 6. rqt_reconfigure 可视化改参数（可选）

```bash
ros2 run rqt_reconfigure rqt_reconfigure
```

左侧选 `/param_demo_node`：`max_speed` 会因为描述符里的范围变成 0.0~5.0 的滑块，
`serial_no` 显示为只读，`waypoints` 是可增删的列表——这就是**不用写 .cfg 的 dynamic_reconfigure**。

## 本包实测出的几个"坑"（Humble 专属）

| 现象 | 原因 |
|---|---|
| `param set max_speed 9.9` 的报错不是回调里的 reason | **描述符范围由 rclcpp 先校验**，非法值根本进不了 `on_set` 回调；回调是第二道防线 |
| `read_only` 参数在程序里也 set 不动 | Humble 的 rclcpp 会拒绝并回 `parameter 'serial_no' ... is read-only`，`read_only` 不是"仅提示" |
| YAML 里写了超范围的取值，不处理的话节点直接起不来 | 覆盖值被拒 → `declare_parameter` 抛 `InvalidParameterValueException`；本包 try/catch + `ignore_override=true` 优雅降级（类型写错则是 `InvalidParameterTypeException`，由 main 接住并 FATAL 退出） |
| 没有 `add_post_set_parameters_callback` | 那是 Iron 之后才有的 API，Humble(16.x) 用订阅 `/parameter_events` 实现"生效后"通知 |
| `ros2 param set` 报 "Node ... has already been added to an executor" | 不能在已被 spin 的回调里再调 `spin_until_future_complete`（见下面 param_client_node 的设计说明） |
| 运行期 `ros2 param load` 报 "Param file does not contain parameters for /xxx" | `ros2 param load` 按**完整节点名**（带 `/`）去 yaml 里找 key，而 `--params-file` 用的是不带 `/` 的节点名 |

## 设计说明（为什么这么写）

- **param_demo_node** 没有用 `add_post_set_parameters_callback`（Humble 不存在），而是订阅
  `/parameter_events`：事件在值真正写入之后才发出，语义上就是 post-set；订阅还特意放在
  `declare_parameter` **之前**，这样连启动时 YAML 覆盖产生的事件也能收到。
- **param_client_node** 没用"1 Hz wall timer + 状态机"：因为 `spin_until_future_complete`
  不允许在被 spin 的回调里再次调用（会抛 `Node ... has already been added to an executor`）。
  它改为由 `main()` 直接调用顺序函数，每步等结果回来再走下一步，并用 `rclcpp::sleep_for(1s)`
  拉开节奏——同样做到了"一次一个请求、不一次性全发出去"。

## 练习题

**基础**

1. 用 `ros2 param set /param_demo_node publish_rate 5.0` 改频率，再用 `ros2 param get` 确认。
   想一想：为什么改完立即生效，不需要重启节点？（对比 ROS 1 里 `rosparam` 改完还要自己读一遍）
2. 把 `waypoints` 改成 `['home','kitchen','office']`，命令行和 YAML 两种写法都试一遍：
   `-p waypoints:="['home','kitchen','office']"` 与 yaml 里的列表写法，看看 `ros2 param get` 的输出差异。
3. 用 `ros2 param set /param_demo_node serial_no SN-9999` 观察 read_only 的拒绝信息，
   然后讨论："只读"到底能拦住谁？拦不住谁？（提示：直接改自己代码里的成员变量，绕得过参数 API 吗）

**进阶**

4. 给 `publish_rate` 加两层校验：描述符里加范围（比如 0.1~100），再在 `on_set` 回调里加一条
   **描述符表达不了**的规则（例如 `robot_name` 不允许是空字符串）。用 `ros2 param set` 分别验证两层。
5. 把 `max_speed` 描述符里的 `floating_point_range` 注释掉，重新编译后再
   `ros2 param set /param_demo_node max_speed 9.9`——这次你会看到**自己写的**
   `reason="max_speed 超出范围 [0.0, 5.0]，已拒绝"`。对比两次输出，说清 rclcpp 的校验顺序。
   顺便照葫芦画瓢声明一个 `retry_count`（integer，默认 3，`integer_range` 设 [1, 10] step 1），
   用 `ros2 param describe` 看工具链是否认得它。
6. 运行期加载 vs 启动期加载：先跑起节点，再执行
   `ros2 param load /param_demo_node <一个顶层 key 写成 /param_demo_node 的 yaml>`，
   然后回答：为什么 `--params-file` 用的 key 是 `param_demo_node`（不带 `/`），
   而 `ros2 param load` 却要完整节点名？两种方式分别在什么时机把参数写进去？
