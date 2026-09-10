// turtle_pose_monitor.cpp —— 位姿监控：打印位姿 + 累计路程
//
// 演示"订阅 + 定时汇总"的经典组合：
// 回调只做轻量的状态更新（高频），定时器做展示（低频）。
#include <cmath>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "turtlesim/msg/pose.hpp"

class TurtlePoseMonitor : public rclcpp::Node
{
public:
  TurtlePoseMonitor()
  : Node("turtle_pose_monitor")
  {
    // 参数：打印周期（秒）
    this->declare_parameter("print_period", 2.0);
    const double period = this->get_parameter("print_period").as_double();

    subscription_ = this->create_subscription<turtlesim::msg::Pose>(
      "/turtle1/pose", 10,
      std::bind(&TurtlePoseMonitor::pose_callback, this, std::placeholders::_1));

    timer_ = this->create_wall_timer(
      std::chrono::duration<double>(period),
      std::bind(&TurtlePoseMonitor::timer_callback, this));

    RCLCPP_INFO(this->get_logger(), "启动：每 %.1f 秒汇报一次位姿", period);
  }

private:
  void pose_callback(const turtlesim::msg::Pose::SharedPtr msg)
  {
    if (has_pose_) {
      // 欧氏距离累加 = 累计路程
      const double dx = msg->x - last_x_;
      const double dy = msg->y - last_y_;
      total_distance_ += std::hypot(dx, dy);
    }
    has_pose_ = true;
    last_x_ = msg->x;
    last_y_ = msg->y;
    last_theta_ = msg->theta;
  }

  void timer_callback()
  {
    if (!has_pose_) {
      RCLCPP_WARN(this->get_logger(), "还没收到位姿，turtlesim 在跑吗？");
      return;
    }
    RCLCPP_INFO(
      this->get_logger(),
      "位姿: (%.2f, %.2f) yaw=%.2f rad | 累计路程: %.2f m",
      last_x_, last_y_, last_theta_, total_distance_);
  }

  rclcpp::Subscription<turtlesim::msg::Pose>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
  bool has_pose_ = false;
  double last_x_ = 0.0;
  double last_y_ = 0.0;
  double last_theta_ = 0.0;
  double total_distance_ = 0.0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TurtlePoseMonitor>());
  rclcpp::shutdown();
  return 0;
}
