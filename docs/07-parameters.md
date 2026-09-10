# 参数与配置（p06）

> 对应代码：`src/p06_params/`。参数是 ROS 1→2 行为差异最大的部分之一：**从"全局字典"变成"节点私有+必须声明"**。

## 1. 三条核心规则

1. **必须先声明再用**：`declare_parameter("max_speed", 2.5)` → `get_parameter("max_speed")`；
   没声明就 get/set → 抛异常（ROS 1 的 `nh.getParam` 隐式默认值习惯必须改掉）。
2. **参数属于节点**：没有参数服务器。`ros2 param list` 列出的是**每个节点**的参数；
   参数变更通过**参数事件话题** `/parameter_events` 广播（ROS 1 没有对应物）。
3. **YAML 顶层键 = 节点名**：
   ```yaml
   param_demo_node:          # ← 节点名（注意 --params-file 不带斜杠，param load 要带）
     ros__parameters:
       max_speed: 3.0
   ```

## 2. 声明参数 + 描述符（descriptor）

```cpp
// 声明时用 descriptor 描述范围/只读/说明
auto desc = rcl_interfaces::msg::ParameterDescriptor{};
desc.description = "最大线速度 (m/s)";
rcl_interfaces::msg::FloatingPointRange range;
range.from_value = 0.0; range.to_value = 5.0; range.step = 0.1;
desc.floating_point_range = {range};

this->declare_parameter("max_speed", 2.5, desc);
```

> 实测（Humble）：**descriptor 的范围校验先于你的校验回调**——设 9.9 会被 rclcpp
> 以 "doesn't comply with floating point range" 挡下，回调里的自定义 reason 根本轮不到。
> 想看自己回调的 reason？把 descriptor 的 range 去掉（p06 README 有此练习）。

## 3. 校验回调（on_set）与事件流

```cpp
auto cb = [this](const std::vector<rclcpp::Parameter> & params) {
  rcl_interfaces::msg::SetParametersResult result;
  for (const auto & p : params) {
    if (p.get_name() == "max_speed" &&
      (p.as_double() < 0.0 || p.as_double() > 5.0)) {
      result.successful = false;
      result.reason = "max_speed 超出范围 [0.0, 5.0]，已拒绝";
      return result;   // 注意：参数保持原值
    }
  }
  result.successful = true;
  return result;
};
this->add_on_set_parameters_callback(cb);
```

- **Humble 没有 `add_post_set_parameters_callback`**（Iron+ 才有）；"生效后日志"用
  `/parameter_events` 订阅实现（p06 的 param_demo_node 就是这么写的）。
- 监听变更：`ros2 topic echo /parameter_events`，你会看到 `ParameterEvent` 消息（新值/旧值/节点名）。
- `read_only` 参数：Humble 的 rclcpp **真的拒绝** set（"cannot be set because it is read-only"）。
  但它只是协议约束，不是安全边界。

## 4. YAML 加载的三种方式

```bash
# ① 启动时（节点内 declare 后覆盖）—— 最常用
ros2 run p06_params param_demo_node --ros-args \
  --params-file install/p06_params/share/p06_params/config/param_demo.yaml

# ② launch 里（p07 的 03_params_from_yaml.py 演示）
# ③ 运行期加载
ros2 param load /param_demo_node <yaml>    # 注意：顶层键要完整节点名 /param_demo_node
```

> 实测坑：`--params-file` 的 YAML 顶层键写**不带斜杠**的节点名（`param_demo_node`），
> `ros2 param load` 却要求**带斜杠**（`/param_demo_node`）。同一个文件两种要求。

## 5. CLI 全家桶

```bash
ros2 param list                          # 会看到 use_sim_time 等 rclcpp 自动声明的参数
ros2 param describe /param_demo_node max_speed
ros2 param get /param_demo_node max_speed
ros2 param set /param_demo_node max_speed 9.9    # 被拒（观察日志）
ros2 param set /param_demo_node max_speed 4.0    # 成功
ros2 param dump /param_demo_node                 # 导出当前全部参数
ros2 run rqt_reconfigure rqt_reconfigure         # 图形化调参（dynamic_reconfigure 继承者）
```

## 6. 与 ROS 1 的对照总结

| ROS 1 | ROS 2 |
|---|---|
| 全局参数服务器（`rosparam`） | 每节点参数（`ros2 param`） |
| `nh.getParam` 隐式默认值 | 必须先 declare |
| `<param>` 标签 | YAML + `--params-file` / launch / `param load` |
| dynamic_reconfigure | 参数描述符 + on_set 回调 + rqt_reconfigure |
| 无 | `/parameter_events` 事件话题、校验回调、只读参数 |

## 7. 练习线索

- 基础 3（integer_range）：`rcl_interfaces::msg::IntegerRange`，set 时注意类型（`ros2 param set x 3` 传的是 int）。
- 进阶 2：`ros2 param load` 只覆盖文件中出现的键，`--params-file` 会整体覆盖同名键——比较两种方式的日志差异。
