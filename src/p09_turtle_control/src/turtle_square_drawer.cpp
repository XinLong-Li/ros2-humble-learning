// turtle_square_drawer.cpp —— 画正方形：cmd_vel 定时器状态机
//
// 对照官方示例 `ros2 run turtlesim draw_square`（Python），这里是 C++ 版。
// 状态机：FORWARD（直线走 side_length 米）→ ROTATE（原地转 90°）→ 循环。
// 每个状态的持续时长由路程/角度除以速度算出来，而不是写死秒数。
#include <chrono>
#include <cmath>
#include <memory>

#include "geometry_msgs/msg/twist.hpp"
#include "rclcpp/rclcpp.hpp"

using namespace std::chrono_literals;

class TurtleSquareDrawer : public rclcpp::Node
{
public:
  TurtleSquareDrawer()
  : Node("turtle_square_drawer")
  {
    // 参数：边长与速度
    this->declare_parameter("side_length", 2.0);
    this->declare_parameter("linear_speed", 2.0);
    this->declare_parameter("angular_speed", 1.0);
    side_ = this->get_parameter("side_length").as_double();
    linear_ = this->get_parameter("linear_speed").as_double();
    angular_ = this->get_parameter("angular_speed").as_double();

    publisher_ = this->create_publisher<geometry_msgs::msg::Twist>("/turtle1/cmd_vel", 10);

    // 100 ms 控制周期：每个 tick 决定"继续当前状态还是换状态"
    timer_ = this->create_wall_timer(100ms, std::bind(&TurtleSquareDrawer::control_loop, this));

    RCLCPP_INFO(
      this->get_logger(),
      "启动：画正方形（边长 %.1f m，线速度 %.1f，角速度 %.1f）",
      side_, linear_, angular_);
  }

  ~TurtleSquareDrawer() override
  {
    // 退出前把乌龟刹停，避免遗留速度
    if (publisher_) {
      publisher_->publish(geometry_msgs::msg::Twist());
    }
  }

private:
  enum class State { FORWARD, ROTATE };

  void control_loop()
  {
    const double elapsed = (this->now() - state_start_).seconds();

    if (state_ == State::FORWARD) {
      // 直行：距离 = 速度 × 时间，走满 side_ 米后换旋转
      const double forward_time = side_ / linear_;
      if (elapsed >= forward_time) {
        switch_to(State::ROTATE);
      } else {
        publish_twist(linear_, 0.0);
      }
    } else {
      // 旋转：转 90°（π/2），转满后回到直行
      const double rotate_time = (M_PI / 2.0) / angular_;
      if (elapsed >= rotate_time) {
        switch_to(State::FORWARD);
      } else {
        publish_twist(0.0, angular_);
      }
    }
  }

  void switch_to(State next)
  {
    state_ = next;
    state_start_ = this->now();
    RCLCPP_INFO(
      this->get_logger(),
      "状态切换 -> %s",
      next == State::FORWARD ? "FORWARD(直行)" : "ROTATE(旋转)");
  }

  void publish_twist(double linear, double angular)
  {
    auto msg = geometry_msgs::msg::Twist();
    msg.linear.x = linear;
    msg.angular.z = angular;
    publisher_->publish(msg);
  }

  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;

  State state_ = State::FORWARD;
  rclcpp::Time state_start_ = this->now();   // 构造后立即记起点
  double side_;
  double linear_;
  double angular_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TurtleSquareDrawer>());
  rclcpp::shutdown();
  return 0;
}
