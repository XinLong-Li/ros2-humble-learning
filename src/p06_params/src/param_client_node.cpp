// param_client_node.cpp —— 参数客户端：用 AsyncParametersClient 远程读写另一个节点的参数
//
// 与 ROS 1 的对比：
//   ROS 1: rosparam get/set 直接读写"全局参数服务器"，心里不需要有"哪个节点"的概念
//   ROS 2: 没有全局服务器 —— 想改参数，得先找到那个节点，再调用它的
//          /<节点名>/list_parameters、/get_parameters、/describe_parameters、
//          /set_parameters 四个服务。本节点把这四个服务完整走一遍。
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/parameter_client.hpp"
#include "rcl_interfaces/msg/parameter_type.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"

using namespace std::chrono_literals;

class ParamClientNode : public rclcpp::Node
{
public:
  ParamClientNode()
  : Node("param_client_node")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // 目标节点名做成参数：方便连别的节点，例如
    //   ros2 run p06_params param_client_node --ros-args -p remote_node:=other_node
    remote_node_ = this->declare_parameter<std::string>("remote_node", "param_demo_node");

    // AsyncParametersClient 不是节点：它内部按目标节点名创建 4 个服务客户端，
    // 构造时传入 this，表示"用本节点去连 remote_node_"
    client_ = std::make_shared<rclcpp::AsyncParametersClient>(this, remote_node_);
  }

  /// 顺序执行教学序列，返回 false 表示目标节点的参数服务不可用
  ///
  /// 为什么不用"1 Hz wall timer + 状态机"来分步？
  ///   因为 rclcpp 不允许在"已经被执行器 spin 的回调里"再调用
  ///   spin_until_future_complete —— 会抛 std::runtime_error:
  ///   "Node '/xxx' has already been added to an executor."
  ///   （每个 step 都要在回调里阻塞等结果，两者天生冲突）。
  ///   所以本节点不交给 rclcpp::spin，而是由 main 直接调用本函数顺序推进：
  ///   一次一个请求、单独等待、单独打印，同样避免了"把 6 个请求一次性全发出去"
  ///   导致结果乱序、看不清教学步骤的问题。
  ///   如果确实要"定时器驱动 + 异步结果"，正确写法是给 AsyncParametersClient 的
  ///   每个方法传完成回调，让执行器在结果到达时回调，而不是在回调里阻塞等待。
  bool run_teaching_sequence()
  {
    RCLCPP_INFO(this->get_logger(), "参数教学序列开始，目标节点: %s", remote_node_.c_str());

    // ---- 第 1 步：等参数服务上线（最多 5 s）----
    RCLCPP_INFO(this->get_logger(), "[1/6] 等待 %s 的参数服务（5 s 超时）", remote_node_.c_str());
    if (!client_->wait_for_service(5s)) {
      RCLCPP_ERROR(
        this->get_logger(),
        "目标节点的参数服务不可用，请先在另一个终端启动：ros2 run p06_params param_demo_node");
      return false;
    }
    RCLCPP_INFO(this->get_logger(), "[1/6] 参数服务已就绪");
    step_pause();

    // ---- 第 2 步：list_parameters 列出所有参数名 ----
    RCLCPP_INFO(this->get_logger(), "[2/6] list_parameters({}, 10)");
    auto list_future = client_->list_parameters({}, 10);
    if (!wait(list_future, "list_parameters")) {
      return false;
    }
    const auto listed = list_future.get();
    RCLCPP_INFO(this->get_logger(), "      共 %zu 个参数:", listed.names.size());
    for (const auto & name : listed.names) {
      RCLCPP_INFO(this->get_logger(), "        %s", name.c_str());
    }
    step_pause();

    // ---- 第 3 步：get_parameters 读值 ----
    RCLCPP_INFO(this->get_logger(), "[3/6] get_parameters(max_speed, robot_name, serial_no)");
    const std::vector<std::string> names{"max_speed", "robot_name", "serial_no"};
    auto get_future = client_->get_parameters(names);
    if (!wait(get_future, "get_parameters")) {
      return false;
    }
    for (const auto & parameter : get_future.get()) {
      RCLCPP_INFO(
        this->get_logger(), "      %s = %s",
        parameter.get_name().c_str(), parameter.value_to_string().c_str());
    }
    step_pause();

    // ---- 第 4 步：describe_parameters 看描述符（类型、说明、范围、只读）----
    RCLCPP_INFO(this->get_logger(), "[4/6] describe_parameters(max_speed)");
    auto describe_future = client_->describe_parameters({"max_speed"});
    if (!wait(describe_future, "describe_parameters")) {
      return false;
    }
    for (const auto & descriptor : describe_future.get()) {
      RCLCPP_INFO(
        this->get_logger(), "      name=%s type=%s description='%s' read_only=%s",
        descriptor.name.c_str(), type_name(descriptor.type).c_str(),
        descriptor.description.c_str(), descriptor.read_only ? "true" : "false");
      if (!descriptor.floating_point_range.empty()) {
        const auto & range = descriptor.floating_point_range[0];
        RCLCPP_INFO(
          this->get_logger(), "      取值范围: [%.1f, %.1f] step=%.1f",
          range.from_value, range.to_value, range.step);
      }
    }
    step_pause();

    // ---- 第 5 步：set_parameters 合法值，期望成功 ----
    RCLCPP_INFO(this->get_logger(), "[5/6] set_parameters(max_speed = 4.0)，期望成功");
    auto ok_future = client_->set_parameters({rclcpp::Parameter("max_speed", 4.0)});
    if (!wait(ok_future, "set_parameters(4.0)")) {
      return false;
    }
    print_set_result(ok_future.get());
    step_pause();

    // ---- 第 6 步：set_parameters 非法值，期望被远端的校验回调拒绝 ----
    RCLCPP_INFO(this->get_logger(), "[6/6] set_parameters(max_speed = 9.9)，期望被拒绝");
    auto bad_future = client_->set_parameters({rclcpp::Parameter("max_speed", 9.9)});
    if (!wait(bad_future, "set_parameters(9.9)")) {
      return false;
    }
    print_set_result(bad_future.get());
    RCLCPP_INFO(
      this->get_logger(),
      "      说明：successful=false 且带回了 reason —— 远端的校验（描述符范围 / on_set 回调）"
      "在服务端就把非法值挡下了，客户端只拿到结果；远端节点的参数值仍是 4.0。");

    RCLCPP_INFO(this->get_logger(), "参数教学序列结束（远端参数值保持不变：非法值不会写进去）");
    return true;
  }

private:
  /// 步与步之间停 1 s —— 这就是"1 s 步进"的落地方式：
  /// 本节点没有用 wall timer 状态机（原因见 run_teaching_sequence 的注释），
  /// 于是用 rclcpp::sleep_for 拉开节奏：日志按秒刷出，每一步都看得清。
  /// 用 rclcpp::sleep_for 而不是 std::this_thread::sleep_for 的好处：
  /// 它挂在 rclcpp 的 context 上，Ctrl-C / shutdown 时会被立刻唤醒，不会卡住退出。
  void step_pause() const
  {
    rclcpp::sleep_for(1s);
  }

  /// 统一的等待封装：等 future 完成，判返回值是不是 SUCCESS
  template<typename FutureT>
  bool wait(const std::shared_future<FutureT> & future, const char * what)
  {
    // 每次调用内部会新建一个执行器并 spin，直到 future 就绪、超时或被 shutdown 打断
    const auto code =
      rclcpp::spin_until_future_complete(this->get_node_base_interface(), future, 5s);
    if (code != rclcpp::FutureReturnCode::SUCCESS) {
      RCLCPP_ERROR(
        this->get_logger(), "等待 %s 的结果失败（超时或被中断），请确认远端节点还在运行", what);
      return false;
    }
    return true;
  }

  void print_set_result(const std::vector<rcl_interfaces::msg::SetParametersResult> & results)
  {
    for (const auto & result : results) {
      if (result.successful) {
        RCLCPP_INFO(this->get_logger(), "      successful=true（已写入）");
      } else {
        RCLCPP_WARN(
          this->get_logger(), "      successful=false, reason='%s'", result.reason.c_str());
      }
    }
  }

  /// 把 describe 返回的类型编号翻成人能读的名字（取值同 rclcpp::ParameterType）
  static std::string type_name(uint8_t type)
  {
    switch (type) {
      case rcl_interfaces::msg::ParameterType::PARAMETER_BOOL:
        return "bool";
      case rcl_interfaces::msg::ParameterType::PARAMETER_INTEGER:
        return "integer";
      case rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE:
        return "double";
      case rcl_interfaces::msg::ParameterType::PARAMETER_STRING:
        return "string";
      case rcl_interfaces::msg::ParameterType::PARAMETER_BYTE_ARRAY:
        return "byte_array";
      case rcl_interfaces::msg::ParameterType::PARAMETER_BOOL_ARRAY:
        return "bool_array";
      case rcl_interfaces::msg::ParameterType::PARAMETER_INTEGER_ARRAY:
        return "integer_array";
      case rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE_ARRAY:
        return "double_array";
      case rcl_interfaces::msg::ParameterType::PARAMETER_STRING_ARRAY:
        return "string_array";
      default:
        return "not_set";
    }
  }

  std::string remote_node_;
  rclcpp::AsyncParametersClient::SharedPtr client_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // 注意：这里没有 rclcpp::spin —— 序列内部用 spin_until_future_complete 临时
  // 驱动节点（这是官方 minimal client 示例的写法），节点不属于任何长期执行器，
  // 所以每步都能安全地阻塞等待结果。
  auto node = std::make_shared<ParamClientNode>();
  const bool ok = node->run_teaching_sequence();

  rclcpp::shutdown();
  return ok ? 0 : 1;
}
