# colcon test 假失败排查（ros2/examples）

> 横切文档：排查环境性测试失败的方法论。初学者跑 `colcon test` 看到一堆红字时先读这篇，
> 能省下大量"以为是自己代码写错了"的时间。

- 日期：2026-09-17
- 工作区：`~/ros2_ws`（官方 [ros2/examples](https://github.com/ros2/examples) humble 分支，22 个包）
- 环境：ROS 2 Humble / `rmw_fastrtps_cpp` / WSL2（`networkingMode=mirrored`）/ `ROS_DOMAIN_ID=42`

## 结论

`colcon test` 报出的 3 个包失败**全部是环境性假失败**，与仓库代码无关。单独重跑后：

```text
368 tests, 0 errors, 0 failures, 59 skipped
```

| 现象 | 涉及范围 | 真因 | 处理 |
| --- | --- | --- | --- |
| 一堆 `warnings summary` | 10 个 rclpy / launch 包 | `ament_flake8` 依赖的旧插件用了 `pkg_resources` 老接口 | 无视，用例本身 PASS |
| `xmllint` 失败或卡 60s | `examples_rclcpp_minimal_service`、`examples_rclcpp_cbg_executor` | xmllint 要**联网下载 XSD schema** 才能校验 `package.xml`，网络抖动 | 网络正常时重跑 |
| launch 用例失败 | `launch_testing_examples`（每次挂的用例不同） | 22 个包的测试**并发**跑在同一个 `ROS_DOMAIN_ID` 上抢 DDS | 降并发，或单独重跑该包 |

`59 skipped` 是正常的：`cppcheck 2.7` 有已知性能问题，ament 主动跳过（日志里写明可用 `AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS` 覆盖）。

## 三类失败详解

### 1. flake8 弃用警告 —— 可无视

```text
Warning: SelectableGroups dict interface is deprecated. Use select.
```

出在 `test/test_flake8.py`。这只是 pytest 对插件 importlib 老接口的提示，用例是 PASS 的。它会让该包被计进
`N packages had stderr output` 统计，**不等于测试失败**——看失败要看
`N packages had test failures` 那一行。

### 2. xmllint —— 联网取 XSD 失败/超时

`package.xml` 里声明了：

```xml
<package format="2">
  ...
  <export>...</export>
</package>
<!-- 实际是 xsi:noNamespaceSchemaLocation="http://download.ros.org/schema/package_format2.xsd" -->
```

`ament_xmllint` 调 `xmllint --schema <该 URL>`，**必须把这个 XSD 从网上下下来**才能校验。网络抖动时有两种失败形态：

- `[1]` 立刻失败：`warning: failed to load external entity "http://download.ros.org/schema/package_format2.xsd"`
- `[2]` 卡死：HTTP 请求一直挂着，直到 **ament 默认 60s 超时**被 kill，CTest 报
  `Errors while running CTest`，xunit 里是 `xmllint.xunit.missing_result` / `The test did not generate a result file.`

60s 这个数来自 `ament_add_test.cmake:76` 的 `set(ARG_TIMEOUT 60)`（`/opt/ros/humble/share/ament_cmake_test/cmake/ament_add_test.cmake`）。

实测网络正常时：

```sh
xmllint --noout --schema http://download.ros.org/schema/package_format2.xsd \
        src/examples/rclcpp/services/minimal_service/package.xml
# → 0.45s 通过（走代理 0.45s，不走代理 0.83s）
```

**可选加固（本次未实测）**：用本地 XML catalog 把该 URL 映射到本地文件，彻底摆脱网络依赖：

```sh
mkdir -p ~/.local/share/xml && cd ~/.local/share/xml
curl -O http://download.ros.org/schema/package_format2.xsd
curl -O http://download.ros.org/schema/package_format3.xsd
cat > catalog.xml <<'EOF'
<?xml version="1.0"?>
<catalog xmlns="urn:oasis:names:tc:entity:xmlns:xml:catalog">
  <uri name="http://download.ros.org/schema/package_format2.xsd" uri="package_format2.xsd"/>
  <uri name="http://download.ros.org/schema/package_format3.xsd" uri="package_format3.xsd"/>
</catalog>
EOF
# 之后在 ~/.bashrc 里导出
export XML_CATALOG_FILES=~/.local/share/xml/catalog.xml
```

### 3. launch 测试 —— 并发抢 DDS

`launch_testing_examples` 里失败的用例每次都不一样（`check_msgs` / `set_param` / `check_multiple_nodes`），两种典型现象：

- `WaitForTopics([('chatter', String)], timeout=15.0)` 超时：**15s 一条消息都没收到**
- `assert response.results[0].successful` 挂掉：服务端日志是
  `failed to send response to /demo_node_1/set_parameters (timeout): client will not receive response`

机制：`colcon test` 默认**并行**跑包，22 个包各自的节点、pytest、ctest 进程全挤在同一个
`ROS_DOMAIN_ID=42` 上，DDS 的发现（discovery）与传输被拖慢/丢包。

证据链：

- 单个用例单独跑 pytest：`check_msgs` 1.70s PASS、`set_param` 1.78s PASS
- 清掉干扰进程后整包跑：`colcon test --packages-select launch_testing_examples` → 21.6s 通过
- DDS 通路三向自检全通：C++ talker → `ros2 topic echo --once`、rclpy talker → echo、
  rclpy talker → C++ listener（收到 15 条）

**注意**：诊断用的节点进程一定要 kill 干净。残留的同名节点、或残留的 `/chatter` 发布者，会污染后续跑的 launch 测试（本次排查中就发生过一次）。

## 可复用的排查手法

```sh
# 真实 tally（屏幕上的 Summary 会漏，一定要单独查）
colcon test-result --all
# 只看有问题的
colcon test-result --all | grep -v ', 0 errors, 0 failures'
# 单独重跑去伪
colcon test --packages-select <包名>
# CTest 原始日志
cat build/<包名>/Testing/Temporary/LastTest.log
#   注意：LastTest.log 可能只有几十字节（被后续 ctest 调用覆盖成空壳），
#   真实内容在同目录的 LastTest_<时间戳>.log
# lint 用例的具体报错
cat build/<包名>/test_results/<包名>/xmllint.xunit.xml
```

DDS 通路自检（判断是不是 RMW/网络层的问题）：

```sh
source /opt/ros/humble/setup.bash && source install/setup.bash
ros2 run demo_nodes_cpp talker & sleep 4
timeout 10 ros2 topic echo /chatter std_msgs/msg/String --once   # 收得到 → DDS 正常
```

## 防抖建议

1. 降并发：`colcon test --parallel-workers 2`，最稳但最慢用 `--executor sequential`
2. 只重跑失败的包：`colcon test --packages-select <pkg>`，别整包重来
3. 判定成败以 `colcon test-result --all` 为准，不看屏幕上的 stderr 统计
4. 诊断/手工起的 ROS 节点用完即 kill，避免污染后续测试
