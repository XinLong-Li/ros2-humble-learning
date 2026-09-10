# 自定义接口 msg / srv / action（p02）

> 对应代码：`src/p02_interfaces/`。这是 ROS 1→2 变化最大的一环之一，值得细读。

## 1. 好消息：接口语法几乎没变

`.msg` / `.srv` / `.action` 文件的写法与 ROS 1 **几乎完全一致**：

```
# RobotStatus.msg —— 字段类型 字段名，支持嵌套与注释
std_msgs/Header header
string robot_name
geometry_msgs/Pose pose

# ComputeSum.srv —— 请求与响应用 --- 分隔
int64[] values
---
int64 sum

# CountDown.action —— 三段顺序：目标 / 结果 / 反馈（顺序别记反！）
# （字段名不能用 Python 保留字如 from/import，rosidl 会拒绝生成）
int32 start
---
int32 current
---
int32 total_steps
```

## 2. 坏消息：生成机制全变了

| | ROS 1 | ROS 2 |
|---|---|---|
| 声明生成 | `add_message_files()` + `generate_messages(DEPENDENCIES ...)` | `rosidl_generate_interfaces(...)` 一个宏收 msg/srv/action |
| 依赖标签 | `<build_depend>message_generation</build_depend>` + `<exec_depend>message_runtime</exec_depend>` | `<buildtool_depend>rosidl_default_generators</buildtool_depend>` + `<exec_depend>rosidl_default_runtime</exec_depend>` |
| 组声明 | 无 | `<member_of_group>rosidl_interface_packages</member_of_group>` |
| 生成头文件位置 | `devel/include/` | `build/<pkg>/rosidl_generator_cpp/`（编译期）+ `install/include/`（安装后） |
| 类型全名 | 两段式 `pkg/Msg` | **三段式** `pkg/msg/Msg`（CLI 与 YAML 里） |
| C++ 头文件 | `pkg/Msg.h` | `pkg/msg/<snake_case>.hpp`（RobotStatus → robot_status.hpp） |

## 3. 在其他包使用自定义接口（三处必改）

假设你的包要用 `p02_interfaces`：

**① package.xml** —— 加一行：
```xml
<depend>p02_interfaces</depend>
```

**② CMakeLists.txt** —— 两处：
```cmake
find_package(p02_interfaces REQUIRED)
target_link_libraries(你的节点 ... p02_interfaces::p02_interfaces)
```

**③ 源码** —— include + 类型：
```cpp
#include "p02_interfaces/msg/robot_status.hpp"     // 文件名是 snake_case
p02_interfaces::msg::RobotStatus msg;              // 类型名保持 CamelCase
```

> 若链接报"找不到 p02_interfaces::p02_interfaces 目标"，退回官方写法：
> ```cmake
> rosidl_get_typesupport_target(cpp_typesupport_target
>   p02_interfaces "rosidl_typesupport_cpp")
> target_link_libraries(你的节点 "${cpp_typesupport_target}")
> ```

## 4. 嵌套与同包引用

- 引用**别的包**的类型：`geometry_msgs/Pose pose`（带包名）
- 引用**同包**的类型：直接写裸类型名 `Waypoint target`（见 NavigateTo.action）
- 引用**标准头**：`std_msgs/Header header`（timestamp 与 frame_id 的标准姿势）

## 5. 验证命令

```bash
ros2 interface list | grep p02               # 全部接口
ros2 interface show p02_interfaces/msg/RobotStatus
ros2 interface show p02_interfaces/action/NavigateTo
ros2 interface package p02_interfaces        # 按包过滤
ros2 interface list -m                       # 接口→包 反查
```

## 6. 必记的坑：改接口要重编下游

`.msg` 改了只重编接口包，下游包手里的还是旧头文件 → 运行期诡异错误。
```bash
colcon build --symlink-install --packages-up-to <用到它的包>
```
本仓库最省事：`colcon build --symlink-install --packages-up-to p09_turtle_control`。
