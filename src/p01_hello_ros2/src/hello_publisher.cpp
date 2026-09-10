// hello_publisher.cpp —— 最小发布者示例
//
// 与 ROS 1 的关键差异（详见 docs/02-workspace-and-first-node.md）：
//   ROS 1:  ros::init + ros::NodeHandle nh; nh.advertise<T>(...)
//   ROS 2:  继承 rclcpp::Node，create_publisher<T>(...) 的模板参数决定消息类型
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class HelloPublisher : public rclcpp::Node
{
public:
  HelloPublisher()
  : Node("hello_publisher")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // depth=10：发送队列深度，相当于 ROS 1 advertise 时的 queue_size
    publisher_ = this->create_publisher<std_msgs::msg::String>("chatter", 10);

    // ROS 1 常用 ros::Rate + while 循环轮询；ROS 2 更推荐定时器回调
    timer_ = this->create_wall_timer(
      1s, std::bind(&HelloPublisher::timer_callback, this));

    RCLCPP_INFO(this->get_logger(), "hello_publisher 启动，1 Hz 发布到 /chatter");
  }

private:
  void timer_callback()
  {
    auto msg = std_msgs::msg::String();
    msg.data = "Hello ROS 2: " + std::to_string(count_++);
    publisher_->publish(msg);
    // ROS 1 的 ROS_INFO(...) -> RCLCPP_INFO(this->get_logger(), ...)
    RCLCPP_INFO(this->get_logger(), "发布: '%s'", msg.data.c_str());
  }

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  int count_ = 0;
};

int main(int argc, char * argv[])
{
  // rclcpp::init 取代 ros::init；rclcpp::spin 取代 ros::spin
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<HelloPublisher>());
  rclcpp::shutdown();
  return 0;
}
