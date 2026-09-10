# p02_interfaces —— 自定义接口包（msg / srv / action）

配套文档：[docs/03-custom-interfaces.md](../../docs/03-custom-interfaces.md)

## 学习目标

1. 会用 `.msg` / `.srv` / `.action` 定义自己的接口——**语法与 ROS 1 几乎一致，但生成机制完全不同**
2. 理解 rosidl：ROS 1 的 `message_generation` 在 ROS 2 变成 `rosidl_default_generators` + `rosidl_generate_interfaces`
3. 学会在其他包中引用本包的接口（三处必改，见下）

## 本包定义的接口

| 文件 | 类型（三段式全名） | 用途 |
|---|---|---|
| `msg/RobotStatus.msg` | `p02_interfaces/msg/RobotStatus` | 机器人状态（嵌套 Header/Pose），p03 使用 |
| `msg/Waypoint.msg` | `p02_interfaces/msg/Waypoint` | 航点，被 NavigateTo.action 复用 |
| `srv/ComputeSum.srv` | `p02_interfaces/srv/ComputeSum` | 数组求和，p04 使用 |
| `action/CountDown.action` | `p02_interfaces/action/CountDown` | 倒数动作，p05 使用 |
| `action/NavigateTo.action` | `p02_interfaces/action/NavigateTo` | 导航动作，p05/p09 使用 |

> 注意类型名的**三段式**：`包名/子目录/类型名`（ROS 1 是两段式 `包名/类型名`）。

## 与 ROS 1 的关键差异

| ROS 1 (Noetic) | ROS 2 (Humble) |
|---|---|
| `add_message_files(...)` + `generate_messages(...)` | `rosidl_generate_interfaces(...)` 一个宏搞定 msg/srv/action |
| `<build_depend>message_generation</build_depend>` | `<buildtool_depend>rosidl_default_generators</buildtool_depend>` |
| `<exec_depend>message_runtime</exec_depend>` | `<exec_depend>rosidl_default_runtime</exec_depend>` |
| 生成头文件在 `devel/include/` | 生成头文件在 `build/<pkg>/rosidl_generator_cpp/` 与 `install/include/` |
| 类型名两段式 `my_pkg/MyMsg` | 类型名三段式 `my_pkg/msg/MyMsg` |

## 编译与查看

```bash
source /opt/ros/humble/setup.bash
colcon build --symlink-install --packages-select p02_interfaces
source install/setup.bash

ros2 interface list | grep p02_interfaces     # 列出本包所有接口
ros2 interface show p02_interfaces/msg/RobotStatus
ros2 interface show p02_interfaces/action/NavigateTo
ros2 interface package p02_interfaces          # 按包过滤
```

## 在其他包中使用本包接口（三处必改）

1. **package.xml**：加 `<depend>p02_interfaces</depend>`
2. **CMakeLists.txt**：`find_package(p02_interfaces REQUIRED)` + `target_link_libraries(你的节点 ... p02_interfaces::p02_interfaces)`
3. **C++ 代码**：
   ```cpp
   #include "p02_interfaces/msg/robot_status.hpp"      // 下划线文件名
   #include "p02_interfaces/srv/compute_sum.hpp"
   #include "p02_interfaces/action/count_down.hpp"
   ```

> 若链接时提示找不到 `p02_interfaces::p02_interfaces` 目标（取决于 ROS 版本），
> 退回官方写法：
> ```cmake
> rosidl_get_typesupport_target(cpp_typesupport_target
>   p02_interfaces "rosidl_typesupport_cpp")
> target_link_libraries(你的节点 "${cpp_typesupport_target}")
> ```

## 重要规则：改接口必须重编下游

改了 `.msg` 只重编本包，下游包仍持有旧的头文件 → 链接/运行期诡异错误。
正确姿势：

```bash
colcon build --symlink-install --packages-up-to p09_turtle_control
```

## 练习题

**基础**
1. 新增 `msg/Vector3Stamped.msg`（字段：`std_msgs/Header header` + `float64 x y z`），编译后用 `ros2 interface show` 查看。
2. 新增 `srv/AddTwoInts.srv`（模仿 ROS 1 经典示例），用 `ros2 service call` 前先 `ros2 interface show` 确认字段。

**进阶**
3. 给 `RobotStatus.msg` 加一个 `string[] tags` 数组字段，重编后用 `ros2 topic pub --once` 手动发布一条试试。
4. 思考：`NavigateTo.action` 里为什么可以直接写 `Waypoint target` 而不用 `p02_interfaces/Waypoint target`？（提示：同包内引用）
5. 用 `ros2 interface list -m` 查看接口属于哪些包，找出 `std_msgs/Header` 的定义文件位置（在 `/opt/ros/humble/share/` 下搜）。
