// qos_subscriber.cpp —— 订阅端 QoS 同样由参数决定，用来复现"静默不匹配"
//
// 最重要的一条规则：订阅端请求的策略强度不能高于发布端提供的强度。
//   发布 reliable + 订阅 best_effort → 兼容（订阅端收得到，只是不重传）
//   发布 best_effort + 订阅 reliable → 不兼容，DDS 不建立连接，消息安静地不来，也不会报错退出
// 实测注意：rclcpp 在检测到不兼容时会在发布端/订阅端各打一条 "incompatible QoS" 的 WARN，
// 但出现时机可能滞后好几秒（短时间跑甚至看不到），所以**别把日志当排查依据** ——
// 可靠的做法永远是 ros2 topic info /qos_demo -v 对比两端的 QoS profile。
// 这与 ROS 1 差别很大：ROS 1 至少还有 master 帮你看看谁在线，ROS 2 全靠 QoS 兼容性判定。
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

class QosSubscriber : public rclcpp::Node
{
public:
  QosSubscriber()
  : Node("qos_subscriber")
  {
    // 与发布端完全相同的三个参数：只有两端配成兼容的组合，才能真正收到消息
    const std::string reliability = this->declare_parameter<std::string>("reliability", "reliable");
    const std::string durability = this->declare_parameter<std::string>("durability", "volatile");
    const int depth = this->declare_parameter<int>("depth", 10);

    // QoS 必须在 create_subscription 之前构建好，创建之后再改无效
    rclcpp::QoS qos(static_cast<std::size_t>(depth));

    if (reliability == "best_effort") {
      qos.reliability(rclcpp::ReliabilityPolicy::BestEffort);
    } else {
      qos.reliability(rclcpp::ReliabilityPolicy::Reliable);
    }

    if (durability == "transient_local") {
      qos.durability(rclcpp::DurabilityPolicy::TransientLocal);
    } else {
      qos.durability(rclcpp::DurabilityPolicy::Volatile);
    }

    subscription_ = this->create_subscription<std_msgs::msg::Int32>(
      "/qos_demo", qos,
      std::bind(&QosSubscriber::topic_callback, this, std::placeholders::_1));

    RCLCPP_INFO(
      this->get_logger(),
      "qos_subscriber 启动：订阅 /qos_demo  reliability=%s durability=%s depth=%d"
      "（与发布端不兼容时会静默收不到消息，排查用 ros2 topic info /qos_demo -v）",
      reliability.c_str(), durability.c_str(), depth);
  }

private:
  // 回调用 ConstSharedPtr（std::shared_ptr<const T>）而不是 const SharedPtr，
  // 否则会命中 rclcpp 的 deprecated 重载（编译警告）
  // 累计计数而不是只看单条值：QoS depth 造成的丢包能立刻从"跳号"看出来
  void topic_callback(std_msgs::msg::Int32::ConstSharedPtr msg)
  {
    ++received_count_;
    RCLCPP_INFO(this->get_logger(), "收到: %d（累计 %d 条）", msg->data, received_count_);
  }

  int received_count_ = 0;
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<QosSubscriber>());
  rclcpp::shutdown();
  return 0;
}
