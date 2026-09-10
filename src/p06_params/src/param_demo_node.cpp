// param_demo_node.cpp —— 参数演示节点：声明、描述符、校验回调、参数事件
//
// 与 ROS 1 的关键差异（详见 docs/07-parameters.md）：
//   ROS 1:  全局参数服务器，rosparam set/get 改的是"全局"的值，节点通常不必声明
//   ROS 2:  参数属于"每个节点"，必须先 declare_parameter 才能用；
//           改参数 = 调用 /<节点名>/set_parameters 服务，
//           变化以 /parameter_events 话题广播（ros2 param / rqt_reconfigure 都基于它）
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "rcl_interfaces/msg/floating_point_range.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"
#include "rcl_interfaces/msg/parameter_event.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"

using namespace std::chrono_literals;

class ParamDemoNode : public rclcpp::Node
{
public:
  ParamDemoNode()
  : Node("param_demo_node")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // ------------------------------------------------------------------
    // 0) 先订阅 /parameter_events：这是"参数已生效"的通知来源
    // ------------------------------------------------------------------
    // 为什么不用 add_post_set_parameters_callback？
    //   那个 API 是 ROS 2 Iron 之后才加进 rclcpp 的，Humble(16.x) 里没有；
    //   Humble 下的等价做法就是订阅 /parameter_events —— 参数事件在值真正写入
    //   之后才发出，所以拿到的必然是"已生效"的值，语义上就等于 post-set 回调。
    // 两个要点：
    //   * /parameter_events 是所有节点共用的全局话题，必须按 event->node 过滤；
    //   * 先订阅、后声明参数，这样 declare 阶段的覆盖（--params-file / -p）
    //     产生的参数事件也能被收到。
    parameter_event_sub_ =
      this->create_subscription<rcl_interfaces::msg::ParameterEvent>(
        "/parameter_events", rclcpp::ParameterEventsQoS(),
        std::bind(&ParamDemoNode::on_parameter_event, this, std::placeholders::_1));

    // ------------------------------------------------------------------
    // 1) 逐个声明参数（ROS 2 的硬性要求：不声明就用不了）
    // ------------------------------------------------------------------
    // 描述符（ParameterDescriptor）是"给工具链看的元数据"，本身不改变数值：
    //   * ros2 param describe 会显示 description / range / read_only；
    //   * rqt_reconfigure 用它生成滑块，并按 range 限制输入；
    //   * rclcpp 自己也会拿 floating_point_range 再校验一次（见下面的 catch）。
    rcl_interfaces::msg::ParameterDescriptor max_speed_desc;
    max_speed_desc.description = "最大线速度 (m/s)";
    rcl_interfaces::msg::FloatingPointRange speed_range;
    speed_range.from_value = 0.0;
    speed_range.to_value = 5.0;
    speed_range.step = 0.1;
    // floating_point_range 是"长度最多为 1"的数组（BoundedVector<_, 1>），用 push_back 追加
    max_speed_desc.floating_point_range.push_back(speed_range);

    try {
      max_speed_ = this->declare_parameter<double>("max_speed", 2.5, max_speed_desc);
    } catch (const rclcpp::exceptions::InvalidParameterValueException & e) {
      // 什么情况会走到这里？
      //   参数文件 / 命令行的覆盖值被拒绝时（config/param_demo.yaml 里故意写了
      //   max_speed: 9.9），declare_parameter 会抛异常。不接住的话异常会冲出
      //   构造函数，节点直接启动失败——这就是 ROS 2 的真实行为。
      // 这里演示"优雅降级"：打印原因，再用 ignore_override=true 重新声明，
      //   忽略非法覆盖、回退到默认值 2.5。
      RCLCPP_ERROR(this->get_logger(), "参数覆盖值被拒绝: %s", e.what());
      RCLCPP_WARN(
        this->get_logger(),
        "已回退到默认值 max_speed = 2.5（把 YAML 改成 0.0~5.0 之间的值即可生效）");
      max_speed_ = this->declare_parameter<double>("max_speed", 2.5, max_speed_desc, true);
    }

    // 不需要描述符时，第三个参数可以不传
    robot_name_ = this->declare_parameter<std::string>("robot_name", "turtle_x");
    publish_rate_ = this->declare_parameter<double>("publish_rate", 1.0);
    enable_debug_ = this->declare_parameter<bool>("enable_debug", false);

    // 数组参数：YAML 里写成列表；命令行写成 -p waypoints:="['home','kitchen']"
    waypoints_ = this->declare_parameter<std::vector<std::string>>(
      "waypoints", std::vector<std::string>{"home", "kitchen"});

    // serial_no：read_only 参数。
    // read_only 是描述符里的"元信息"，不是权限系统：
    //   * ros2 param set / ros2 param load / rqt_reconfigure 会先 describe，
    //     看到 read_only=true 就拒绝修改；
    //   * Humble 的 rclcpp 自己也会拦：set 会失败并返回
    //     "parameter 'serial_no' cannot be set because it is read-only"；
    //   * 但它拦不住绕过参数 API 的写法（例如程序里直接改自己的成员变量），
    //     所以它只是"规矩的调用方"之间的约定，别当成安全边界。
    rcl_interfaces::msg::ParameterDescriptor serial_desc;
    serial_desc.description = "设备序列号（只读，仅作标识）";
    serial_desc.read_only = true;
    serial_no_ = this->declare_parameter<std::string>("serial_no", "SN-0001", serial_desc);

    // 声明完立刻打印一遍（declare_parameter 的返回值已经是"覆盖后"的生效值，
    // 注意此时 max_speed 既可能是默认的 2.5，也可能是 YAML 里的合法值）
    RCLCPP_INFO(this->get_logger(), "参数初始值:");
    RCLCPP_INFO(this->get_logger(), "  max_speed    = %.2f  (范围 0.0~5.0, step 0.1)", max_speed_);
    RCLCPP_INFO(this->get_logger(), "  robot_name   = %s", robot_name_.c_str());
    RCLCPP_INFO(this->get_logger(), "  publish_rate = %.2f", publish_rate_);
    RCLCPP_INFO(this->get_logger(), "  enable_debug = %s", enable_debug_ ? "true" : "false");
    RCLCPP_INFO(this->get_logger(), "  waypoints    = [%s]", join(waypoints_).c_str());
    RCLCPP_INFO(this->get_logger(), "  serial_no    = %s  (read_only)", serial_no_.c_str());

    // ------------------------------------------------------------------
    // 2) set 前校验回调：任何来源的修改都会先经过它
    // ------------------------------------------------------------------
    // 对比 ROS 1 的 dynamic_reconfigure：那边要写 .cfg 文件 + 重新生成代码，
    // ROS 2 只要注册一个回调，而且 ros2 param set / -p 覆盖 / YAML 加载 /
    // rqt_reconfigure **全部**走同一条路径，规则只写一遍。
    // 注意：返回的 handle 必须保存成成员，否则回调会被立刻注销。
    // 另一个易错点：描述符里的 floating_point_range 由 rclcpp **先**校验，
    // 所以 max_speed:=9.9 会在进入本回调之前就被挡下（报的是
    // "doesn't comply with floating point range"）——本回调是第二道防线，
    // 适合放描述符表达不了的规则（参数间互相约束、字符串不能为空等）。
    on_set_handle_ = this->add_on_set_parameters_callback(
      std::bind(&ParamDemoNode::on_set_parameters, this, std::placeholders::_1));

    // 1 Hz 打印当前全部参数值：改完参数不用猜，看这一行就行
    // （用 create_wall_timer，而不是 ROS 1 的 while + ros::Rate：
    //   单线程执行器里阻塞循环会饿死其他回调）
    timer_ = this->create_wall_timer(1s, std::bind(&ParamDemoNode::timer_callback, this));

    RCLCPP_INFO(this->get_logger(), "param_demo_node 启动完成，等待 ros2 param / rqt_reconfigure");
  }

private:
  /// set 前校验回调：返回 false 则这一批参数整体不生效（原值保持不变）
  rcl_interfaces::msg::SetParametersResult on_set_parameters(
    const std::vector<rclcpp::Parameter> & parameters)
  {
    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;

    for (const auto & parameter : parameters) {
      RCLCPP_INFO(
        this->get_logger(), "[on_set] 请求把 %s 改成 %s",
        parameter.get_name().c_str(), parameter.value_to_string().c_str());

      if (parameter.get_name() == "max_speed" &&
        parameter.get_type() == rclcpp::ParameterType::PARAMETER_DOUBLE)
      {
        const double value = parameter.as_double();
        if (value < 0.0 || value > 5.0) {
          result.successful = false;
          result.reason = "max_speed 超出范围 [0.0, 5.0]，已拒绝";
          RCLCPP_WARN(this->get_logger(), "[on_set] 拒绝: %s", result.reason.c_str());
          // 一项不合法就整批拒绝：ROS 2 的语义是"这一批要么全生效、要么全不生效"
          return result;
        }
      }
    }

    RCLCPP_INFO(this->get_logger(), "[on_set] 校验通过（%zu 项）", parameters.size());
    return result;
  }

  /// /parameter_events 回调：参数真正写入之后才会收到
  void on_parameter_event(const rcl_interfaces::msg::ParameterEvent & event)
  {
    // 全局话题，先按节点名过滤出自己
    if (event.node != this->get_fully_qualified_name()) {
      return;
    }
    for (const auto & parameter : event.new_parameters) {
      log_parameter_event(parameter, "新增");
    }
    for (const auto & parameter : event.changed_parameters) {
      log_parameter_event(parameter, "更新");
    }
    for (const auto & parameter : event.deleted_parameters) {
      RCLCPP_INFO(this->get_logger(), "参数已删除: %s", parameter.name.c_str());
    }
  }

  /// 事件里装的是接口消息 rcl_interfaces::msg::Parameter，
  /// 用 rclcpp::Parameter::from_parameter_msg 转成好用的 rclcpp::Parameter 再打印
  void log_parameter_event(const rcl_interfaces::msg::Parameter & msg, const char * kind)
  {
    const rclcpp::Parameter parameter = rclcpp::Parameter::from_parameter_msg(msg);
    RCLCPP_INFO(
      this->get_logger(), "参数已%s: %s = %s", kind,
      parameter.get_name().c_str(), parameter.value_to_string().c_str());
  }

  void timer_callback()
  {
    // get_parameters 一次加锁取一组，比逐个 get_parameter 更整洁
    std::string text;
    for (const auto & parameter : this->get_parameters(param_names_)) {
      text += parameter.get_name() + "=" + parameter.value_to_string() + "  ";
    }
    RCLCPP_INFO(this->get_logger(), "当前参数: %s", text.c_str());
  }

  /// 把字符串数组拼成 "a, b" 以便打印（与 ROS 1 的 vector 打印习惯保持一致）
  static std::string join(const std::vector<std::string> & values)
  {
    std::string text;
    for (size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += values[i];
    }
    return text;
  }

  // 参数名清单：打印、批量读取都以它为准，避免名字写错
  const std::vector<std::string> param_names_{
    "max_speed", "robot_name", "publish_rate", "enable_debug", "waypoints", "serial_no"};

  double max_speed_ = 0.0;
  std::string robot_name_;
  double publish_rate_ = 0.0;
  bool enable_debug_ = false;
  std::vector<std::string> waypoints_;
  std::string serial_no_;

  rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr on_set_handle_;
  rclcpp::Subscription<rcl_interfaces::msg::ParameterEvent>::SharedPtr parameter_event_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  try {
    // 节点构造期间就可能抛异常（参数覆盖被拒绝 / 类型不对），这里统一接住，
    // 否则 stderr 上只有一句 "terminate called after throwing ..."，看不出原因
    auto node = std::make_shared<ParamDemoNode>();
    rclcpp::spin(node);
  } catch (const std::exception & e) {
    RCLCPP_FATAL(rclcpp::get_logger("param_demo_node"), "节点启动/运行失败: %s", e.what());
    RCLCPP_FATAL(
      rclcpp::get_logger("param_demo_node"),
      "提示：检查 config/param_demo.yaml 里参数的类型与取值范围");
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
