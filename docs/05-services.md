# 服务与执行器（p04）

> 对应代码：`src/p04_services/`。服务 API 变化不大，真正的新知识是**执行器与回调组**。

## 1. 服务端/客户端速查

```cpp
// 服务端（ROS 1: nh.advertiseService）
auto server = this->create_service<ComputeSum>(
  "compute_sum", std::bind(&MyNode::handle, this, _1, _2));
// 回调参数：shared_ptr<Request>, shared_ptr<Response>

// 客户端（ROS 1: client.call(req, res) 阻塞式）
auto client = this->create_client<ComputeSum>("compute_sum");
client->wait_for_service(5s);                        // 先等服务上线
auto future = client->async_send_request(request);   // 永不阻塞，返回 future
rclcpp::spin_until_future_complete(node, future, 10s);  // 边 spin 边等 = "同步"惯用法
auto response = future.get();
```

## 2. 执行器与回调组（本阶段真正的重点）

ROS 1 的 `AsyncSpinner(4)` 是个全局开关；ROS 2 把"谁在哪个线程跑"变成显式对象：

| ROS 1 | ROS 2 |
|---|---|
| ros::spin()（单线程） | SingleThreadedExecutor（默认，`rclcpp::spin` 内部就用它） |
| ros::AsyncSpinner(n) | MultiThreadedExecutor |
| 无 | CallbackGroup（MutuallyExclusive 互斥 / **Reentrant 可重入**） |

**单线程执行器 = 所有回调排队一个线程**：一个服务回调睡 3 秒，期间该节点的所有
订阅回调、定时器、参数服务、shutdown 信号全部排队等待。这是 p04 实验要你亲眼看到的。

## 3. 并发实验（重要：先读再跑）

```bash
# ① 单线程服务端 + 同步客户端：2 请求 × 3 秒 = 约 6 秒（串行）
ros2 run p04_services compute_sum_server --ros-args -p delay_ms:=3000
ros2 run p04_services compute_sum_client    # 默认 num_requests:=2

# ② 多线程服务端（Reentrant 回调组 + MultiThreadedExecutor）+ 异步客户端：约 3 秒（并行）
ros2 run p04_services compute_sum_server_mt --ros-args -p delay_ms:=3000
ros2 run p04_services compute_sum_client_async
```

> ⚠️ 实测结论（Humble）：**"多线程服务端 + 同步客户端"不会变快**——
> 同步客户端"发一个等一个"，同一时刻只有一个请求在飞，服务端再能并行也用不上。
> 真正的并行需要两端配合：客户端一次发出全部请求（async 客户端）+ 服务端多线程处理。

对照表（本包 README 有实测数据）：

| 服务端 | 客户端 | 2×3000ms 总耗时 |
|---|---|---|
| 单线程 | 同步 | ≈6 s |
| 多线程 | 同步 | ≈6 s（客户端瓶颈） |
| 单线程 | 异步 | ≈6 s（服务端瓶颈） |
| 多线程 | 异步 | ≈3 s ✅ |

## 4. 回调组的两个坑

- 默认回调组是 **MutuallyExclusive（互斥）**：同组回调绝不并发，即使多线程执行器。
- 想让同一服务的多个请求并行，服务回调必须用 **Reentrant** 组；否则多线程执行器也白搭。

## 5. CLI 验证

```bash
ros2 service list -t | grep compute_sum
ros2 service call /compute_sum p02_interfaces/srv/ComputeSum "{values: [1,2,3,4]}"
ros2 service find p02_interfaces/srv/ComputeSum
```

## 6. 练习线索

- 基础 3：`ros2 service call` 的 YAML 语法对数组是 `[1,2,3]`；服务响应字段名要写对。
- 进阶 1（每请求一线程）：注意对比"无界线程"与"有界线程池+队列"的取舍（背压）。
- 进阶 3：async 客户端超时用 `future.wait_for()` 或自己计时 + remove_pending_request。
