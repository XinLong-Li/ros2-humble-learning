// qos_publisher.cpp —— QoS 由参数决定，用来做"两端配置不一致"的实验
//
// 核心知识点：QoS 不是运行时能改的开关，它在 create_publisher 时就固化到 DDS 端点上。
// 所以顺序必须是：declare_parameter → get_parameter → 构造 QoS → create_publisher。
// 建完再改参数对已存在的端点毫无影响（想换 QoS 只能重建发布者）。
//
// 实验提示：当"不兼容的订阅者"连上来时，这个节点会打一条
//   "New subscription discovered on topic '/qos_demo', requesting incompatible QoS.
//    No messages will be sent to it. Last incompatible policy: RELIABILITY_QOS_POLICY"
// 的 WARN。它出现时机不稳定（可能滞后几秒），所以排查 QoS 问题不要靠日志，
// 要靠 `ros2 topic info /qos_demo -v` 对比两端的 QoS profile。
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

using namespace std::chrono_literals;

class QosPublisher : public rclcpp::Node
{
public:
  QosPublisher()
  : Node("qos_publisher")
  {
    // 三个参数都用 declare_parameter 一次完成"声明 + 取默认值"，
    // 于是 ros2 param get /qos_publisher reliability 能查到，命令行/YAML 也能覆盖
    const std::string reliability = this->declare_parameter<std::string>("reliability", "reliable");
    const std::string durability = this->declare_parameter<std::string>("durability", "volatile");
    const int depth = this->declare_parameter<int>("depth", 10);

    // ROS 1 的 queue_size 只表达"队列多长"；ROS 2 的 depth 只是 QoS 的一部分，
    // QoS(depth) 等价于 KeepLast(depth)：保留最近 depth 条，超出丢最旧的
    rclcpp::QoS qos(static_cast<std::size_t>(depth));

    // reliability：reliable = 丢包重传（像 TCPROS），best_effort = 尽力而为（像 UDPROS，延迟更低）
    if (reliability == "best_effort") {
      qos.reliability(rclcpp::ReliabilityPolicy::BestEffort);
    } else {
      qos.reliability(rclcpp::ReliabilityPolicy::Reliable);
    }

    // durability：transient_local = 替"迟到的订阅者"保留最后 depth 条，等价 ROS 1 的 latch=true
    if (durability == "transient_local") {
      qos.durability(rclcpp::DurabilityPolicy::TransientLocal);
    } else {
      qos.durability(rclcpp::DurabilityPolicy::Volatile);
    }

    // QoS 在这里被固化进端点：之后无论怎么改参数都不会影响它
    publisher_ = this->create_publisher<std_msgs::msg::Int32>("/qos_demo", qos);

    timer_ = this->create_wall_timer(1s, std::bind(&QosPublisher::timer_callback, this));

    // 打印实际生效的配置 —— 实验时两端各打一行，一眼就能看出是否匹配
    RCLCPP_INFO(
      this->get_logger(), "qos_publisher 启动：1 Hz 发布 /qos_demo  reliability=%s durability=%s depth=%d",
      reliability.c_str(), durability.c_str(), depth);
  }

private:
  void timer_callback()
  {
    auto msg = std_msgs::msg::Int32();
    msg.data = count_++;  // 自增计数：订阅端收到的值能直接反映"丢了多少条"
    publisher_->publish(msg);

    // 1 Hz 不算吵，每条都打，方便和订阅端的输出对照
    RCLCPP_INFO(this->get_logger(), "发布: %d", msg.data);
  }

  int count_ = 0;
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<QosPublisher>());
  rclcpp::shutdown();
  return 0;
}
