// hello_subscriber.cpp —— 最小订阅者示例
//
// 与 ROS 1 的关键差异（详见 docs/02-workspace-and-first-node.md）：
//   ROS 1:  nh.subscribe<T>("chatter", 1000, callback, this)
//   ROS 2:  create_subscription<T>("chatter", qos, callback)
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

class HelloSubscriber : public rclcpp::Node
{
public:
  HelloSubscriber()
  : Node("hello_subscriber")
  {
    // 订阅端 depth=10；回调签名必须是 std::shared_ptr<const T>，
    // 消息的所有权归订阅者，不能修改，这就是 shared_ptr 加 const 的原因
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "chatter", 10,
      std::bind(&HelloSubscriber::topic_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "hello_subscriber 启动，等待 /chatter 消息");
  }

private:
  void topic_callback(const std_msgs::msg::String::SharedPtr msg)
  {
    RCLCPP_INFO(this->get_logger(), "收到: '%s'", msg->data.c_str());
  }

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<HelloSubscriber>());
  rclcpp::shutdown();
  return 0;
}
