# p04_services —— 服务、回调组与执行器

配套文档：[docs/05-services.md](../../docs/05-services.md)

## 学习目标

1. 会用 `create_service` / `create_client` 写服务端与客户端，接口类型是 p02 定义的 `p02_interfaces/srv/ComputeSum`
2. 理解 ROS 2 的**执行器（Executor）**：`SingleThreadedExecutor`、`MultiThreadedExecutor` 分别意味着什么
3. 理解 **CallbackGroup（回调组）**：`MutuallyExclusive` 与 `Reentrant` 的区别，知道"为什么换了多线程执行器还是串行"
4. 掌握同步调用（`async_send_request` + `spin_until_future_complete`）与异步调用（带响应回调）两种写法，以及各自的阻塞代价

## ROS 1 → ROS 2 快速对照

| ROS 1 (Noetic) | ROS 2 (Humble) |
|---|---|
| `ros::ServiceServer srv = nh.advertiseService("compute_sum", &cb, this);` | `this->create_service<p02_interfaces::srv::ComputeSum>("compute_sum", cb);` |
| 服务回调返回 `bool`，结果写进 `srv.response` | 回调无返回值，往 `std::shared_ptr<Response>` 里填结果 |
| `client.call(srv)`：一行阻塞到底，超时靠 launch 参数 | `auto future = client->async_send_request(req);` + `rclcpp::spin_until_future_complete(node, future, 10s);` |
| `client.waitForExistence(ros::Duration(5.0))` | `client->wait_for_service(5s)`（返回 `bool`，超时可控） |
| 不阻塞只能靠 `ros::AsyncSpinner`（全局线程池，"哪些回调能并发"不可控） | 显式 `Executor`（单/多线程）+ `CallbackGroup`（互斥/可重入），并发粒度精确到回调 |
| 回调并发程度取决于 spin 线程数与运气 | `ReentrantCallbackGroup` + `MultiThreadedExecutor` = 真并发，二者缺一不可 |
| `rosservice call /compute_sum "..."` / `rosservice list` / `rosservice type` | `ros2 service call /compute_sum p02_interfaces/srv/ComputeSum "{values: [1,2,3,4]}"` / `ros2 service list` / `ros2 service type` |
| 服务类型名两段式 `pkg/ComputeSum` | 三段式 `pkg/srv/ComputeSum`（CLI 调用时必须写全） |

> 一句话概括本包：ROS 1 的 `ros::spin()` 把调度藏起来了，ROS 2 把它交还给你 —— 自由度和坑一起给你。

## 文件说明

```
p04_services/
├── package.xml
├── CMakeLists.txt
└── src/
    ├── compute_sum_server.cpp        # 节点 compute_sum_server：单线程执行器（串行，会堵）
    ├── compute_sum_server_mt.cpp     # 节点 compute_sum_server_mt：ReentrantCallbackGroup + MultiThreadedExecutor
    ├── compute_sum_client.cpp        # 节点 compute_sum_client：同步客户端（发一个等一个，等待期间节点卡住）
    └── compute_sum_client_async.cpp  # 节点 compute_sum_client_async：异步客户端（发完就走，500 ms 心跳证明不阻塞）
```

四个节点都用服务名 **`/compute_sum`**，类型 **`p02_interfaces/srv/ComputeSum`**（请求 `int64[] values` → 响应 `sum` / `average` / `count`）。

各节点参数：

| 节点 | 参数 | 默认值 | 含义 |
|---|---|---|---|
| `compute_sum_server` / `compute_sum_server_mt` | `delay_ms` | 1000 | 每个请求模拟的计算耗时（毫秒），用来制造"阻塞" |
| `compute_sum_client` | `num_requests` | 2 | 一共发几个请求（同步：发一个等一个） |
| | `values_count` | 5 | 每个请求的数组是 `1..values_count` |
| `compute_sum_client_async` | `num_requests` | 2 | 同时发出的请求数（异步：全部同时在飞） |
| | `values_count` | 5 | 同上 |

## 编译运行

```bash
cd ~/ros2-humble-learning
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select p02_interfaces p04_services   # p02 是接口包，必须一起编
source install/setup.bash
```

### 实验一：命令行调用服务（先确认服务本身能用）

```bash
# 终端 A：服务端，每个请求"算" 3 秒
ros2 run p04_services compute_sum_server --ros-args -p delay_ms:=3000

# 终端 B：CLI 调用（注意服务类型必须写三段式全名）
ros2 service call /compute_sum p02_interfaces/srv/ComputeSum "{values: [1,2,3,4]}"
# → 终端 B 会卡住 3 秒，然后打印 sum=10, average=2.5, count=4，终端 A 打印请求/结果日志
```

顺手的排查命令：

```bash
ros2 service list                       # 应有 /compute_sum 和 /compute_sum_server/describe_parameters 等
ros2 service type /compute_sum          # p02_interfaces/srv/ComputeSum
ros2 service find p02_interfaces/srv/ComputeSum   # 按类型反查：谁在提供这个服务
ros2 interface show p02_interfaces/srv/ComputeSum # 看字段定义
ros2 node info /compute_sum_server      # 看节点提供了哪些服务
```

也可以在服务端运行中改参数（下一个请求立即生效，因为回调里每次重新读参数）：

```bash
ros2 param set /compute_sum_server delay_ms 200
```

### 实验二：单线程 vs 多线程服务端

```bash
# ---------- 1) 单线程服务端：两个请求严格串行，总耗时 ≈ 6 s ----------
# 终端 A
ros2 run p04_services compute_sum_server --ros-args -p delay_ms:=3000
# 终端 B
ros2 run p04_services compute_sum_client --ros-args -p num_requests:=2
# 观察：A 里"第 1 个请求"的日志全部打完后，才会出现"第 2 个请求"；B 总耗时 ≈ 6 s

# ---------- 2) 多线程服务端 + 同一个同步客户端：仍然 ≈ 6 s！----------
# 终端 A
ros2 run p04_services compute_sum_server_mt --ros-args -p delay_ms:=3000
# 终端 B
ros2 run p04_services compute_sum_client --ros-args -p num_requests:=2
# 观察：A 打印 "MultiThreadedExecutor 已启动，工作线程数：N"，但总耗时还是 ≈ 6 s
```

> **这里是全包最重要的一课**：多线程服务端没能变快，不是执行器没生效，而是
> 同步客户端"发一个、等一个"—— 任一时刻只有一个请求在飞，服务端再能并发也用不上。
> 另外，即便同时有多个请求到达，若服务端只用 `MultiThreadedExecutor` 而没建
> `ReentrantCallbackGroup`，默认的互斥回调组也会把它们排成一队（可自行验证，见进阶练习 4）。

```bash
# ---------- 3) 想看真并发：用异步客户端一次把两个请求都发出去 ----------
# 终端 A（多线程服务端）
ros2 run p04_services compute_sum_server_mt --ros-args -p delay_ms:=3000
# 终端 B
ros2 run p04_services compute_sum_client_async --ros-args -p num_requests:=2
# 观察：A 里两个请求的线程 id 不同、"并发中 2 个"，总耗时 ≈ 3 s（真并行）

# 换成单线程服务端再跑同样的异步客户端 → 总耗时回到 ≈ 6 s（服务端只能一个个算）
```

对应关系一览（`delay_ms:=3000`，2 个请求）：

| 服务端 | 客户端 | 总耗时 | 说明 |
|---|---|---|---|
| `compute_sum_server` | `compute_sum_client` | ≈ 6 s | 服务端串行 + 客户端串行 |
| `compute_sum_server_mt` | `compute_sum_client` | ≈ 6 s | 服务端能并行，但请求没同时在飞 |
| `compute_sum_server` | `compute_sum_client_async` | ≈ 6 s | 请求同时在飞，但服务端只有一个线程 |
| `compute_sum_server_mt` | `compute_sum_client_async` | ≈ 3 s | 真并发 |

### 实验三：异步客户端不被阻塞（心跳日志）

```bash
# 终端 A
ros2 run p04_services compute_sum_server --ros-args -p delay_ms:=3000
# 终端 B
ros2 run p04_services compute_sum_client_async --ros-args -p num_requests:=2
```

终端 B 除了请求/响应日志，还会每 500 ms 打印一次：

```
[INFO] [compute_sum_client_async]: 心跳：客户端仍在响应其他回调（已收到 0/2 个响应）
```

这行日志是"异步不阻塞"的直接证据 —— 换成同步客户端 `compute_sum_client`，
在 `spin_until_future_complete` 等待期间整条线程都被占住，同样的心跳根本打不出来。
收到全部响应后，回调里调用 `rclcpp::shutdown()`，进程自动退出（不用手动 Ctrl-C）。

## 原理小结（面试常问）

- **回调组 vs 执行器**：回调组决定"哪些回调允许并发"，执行器决定"有几个线程去跑"。
  `ReentrantCallbackGroup` + `MultiThreadedExecutor` 才是真并发；只改一个都不行。
- **同步客户端的阻塞点**不在 `async_send_request`，而在 `spin_until_future_complete`
  —— 它在当前线程里"转执行器"，转的同时别的回调没机会跑。
- **超时后要收尾**：`spin_until_future_complete` 返回 `TIMEOUT` 时，请求仍挂在 client 内部，
  必须 `client_->remove_pending_request(future)`，否则每超时一次就多占一块内存。
- **多线程下共享数据要同步**：`compute_sum_server_mt` 里的计数器用了 `std::atomic<int>`
  （普通 `int` 的 `++` 会被并发穿插）。

## 练习题

**基础**

1. 起 `compute_sum_server -p delay_ms:=3000`，用 `ros2 service call` 调用一次感受 3 秒阻塞；
   然后 `ros2 param set /compute_sum_server delay_ms 200`，再调用一次 —— 解释为什么第二个请求立刻返回
   （提示：回调里是"每次重新读参数"还是"构造函数读一次"）。
2. 不看本 README，用 `ros2 service list` / `ros2 service type` / `ros2 interface show` 查出
   `/compute_sum` 的类型和字段，自己写出 `ros2 service call` 那一行命令（注意三段式类型名）。
3. 给 `compute_sum_client` 加一个参数 `timeout_ms`（默认 3000），把它传给 `spin_until_future_complete`；
   再对着 `delay_ms:=5000` 的服务端跑，让它走进 `TIMEOUT` 分支并观察 `remove_pending_request` 的效果。

**进阶**

4. 把 `compute_sum_server_mt` 的回调组去掉（`create_service` 只传两个参数，用节点默认的互斥组），
   重跑实验二的第 3 步：总耗时是不是变回 ≈ 6 s 了？用一句话解释你观察到的现象。
5. 用 `ros2 service find p02_interfaces/srv/ComputeSum` 找出当前有哪些节点提供该服务，并思考：
   如果同时启动 `compute_sum_server` 和 `compute_sum_server_mt`（提供同名服务），客户端会调用到谁？
   （提示：ROS 2 不会报错，请求只会落到其中一个服务端，通常是先被发现的那个 ——
   这种"服务重名"的配置属于要避免的错误，实验完记得只留一个。）
6. 给 `compute_sum_client_async` 加超时处理：用 `async_send_request` 的返回值拿到 `request_id`
   （Humble 的类型是 `FutureAndRequestId`，含 `.request_id`），再开一个定时器检查"发出超过 N 秒还没响应"
   的请求，调用 `client_->remove_pending_request(request_id)` 清理并打印告警。
7. （加餐）把服务端改成"每个请求新开一个线程"（`std::thread(...).detach()`）来替代多线程执行器，
   然后讨论为什么 rclcpp 不推荐这么做：线程谁 join、`request`/`response` 的 `shared_ptr` 生命周期
   怎么保证、节点析构时那些线程在干什么。
