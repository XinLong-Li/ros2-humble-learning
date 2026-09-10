// status_subscriber.cpp —— 订阅自定义消息 p02_interfaces/msg/RobotStatus
//
// 与 ROS 1 的对照：
//   ROS 1:  nh.subscribe<p02_interfaces::RobotStatus>("robot_status", 10, cb, this)
//   ROS 2:  create_subscription<p02_interfaces::msg::RobotStatus>("/robot_status", 10, cb)
//   回调参数从 const 引用/裸指针变成 std::shared_ptr<const T>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "p02_interfaces/msg/robot_status.hpp"

class StatusSubscriber : public rclcpp::Node
{
public:
  StatusSubscriber()
  : Node("status_subscriber")
  {
    // depth=10（KeepLast 深度）：回调来不及处理时最多缓存 10 条，再多的丢最旧的。
    // 数值上和 ROS 1 的 queue_size 一样，但 ROS 2 里它只是 QoS 的一部分 ——
    // 这里用的 reliability/durability 都是默认值（reliable + volatile），与发布端一致
    subscription_ = this->create_subscription<p02_interfaces::msg::RobotStatus>(
      "/robot_status", 10,
      std::bind(&StatusSubscriber::topic_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "status_subscriber 启动，等待 /robot_status 消息");
  }

private:
  // 回调参数是 ConstSharedPtr（即 std::shared_ptr<const T>）：消息由中间件分配，
  // 只读地共享给所有订阅者，所以不能改。ROS 1 里消息是拷贝到栈上的，这是本质差异。
  // 注意要写 shared_ptr<const T> 而不是 const shared_ptr<T> —— 后者是"指针本身 const"，
  // 指向的消息仍可写，rclcpp 会走已废弃的重载并给出 deprecated 警告
  void topic_callback(p02_interfaces::msg::RobotStatus::ConstSharedPtr msg)
  {
    RCLCPP_INFO(
      this->get_logger(), "[%s] 电量 %.1f%%  位置 (%.2f, %.2f, %.2f)  充电中=%s",
      msg->robot_name.c_str(),
      msg->battery_percentage,
      msg->pose.position.x, msg->pose.position.y, msg->pose.position.z,
      msg->is_charging ? "是" : "否");
  }

  rclcpp::Subscription<p02_interfaces::msg::RobotStatus>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StatusSubscriber>());
  rclcpp::shutdown();
  return 0;
}
