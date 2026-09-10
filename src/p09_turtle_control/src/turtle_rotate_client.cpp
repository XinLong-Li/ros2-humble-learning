// turtle_rotate_client.cpp —— 调用 turtlesim 官方动作 /turtle1/rotate_absolute
//
// 演示"调用别人实现的 action"：turtlesim 内置了旋转动作
// （turtlesim/action/RotateAbsolute），这里连续发两个目标：
// 先转到 90°，再转回 0°，结果回调里链式发下一个目标。
#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "turtlesim/action/rotate_absolute.hpp"

using namespace std::chrono_literals;

using RotateAbsolute = turtlesim::action::RotateAbsolute;
using GoalHandleRotate = rclcpp_action::ClientGoalHandle<RotateAbsolute>;

class TurtleRotateClient : public rclcpp::Node
{
public:
  TurtleRotateClient()
  : Node("turtle_rotate_client")
  {
    this->declare_parameter("theta_1", 1.5708);   // 90°
    this->declare_parameter("theta_2", 0.0);      // 转回 0°

    client_ = rclcpp_action::create_client<RotateAbsolute>(
      this, "/turtle1/rotate_absolute");

    // 构造函数里阻塞等待服务端（还没开始 spin，阻塞是安全的）
    if (!client_->wait_for_action_server(5s)) {
      RCLCPP_ERROR(this->get_logger(), "rotate_absolute 动作服务端不可用");
      return;
    }
    RCLCPP_INFO(this->get_logger(), "已连接动作服务端，开始演示");
    send_goal(this->get_parameter("theta_1").as_double());
  }

private:
  void send_goal(double theta)
  {
    auto goal = RotateAbsolute::Goal();
    goal.theta = theta;
    RCLCPP_INFO(this->get_logger(), "发送目标: 旋转到 %.2f rad", theta);

    auto options = rclcpp_action::Client<RotateAbsolute>::SendGoalOptions();
    options.feedback_callback =
      [this](GoalHandleRotate::SharedPtr,
        const std::shared_ptr<const RotateAbsolute::Feedback> feedback)
      {
        RCLCPP_INFO(this->get_logger(), "反馈: 还剩 %.2f rad", feedback->remaining);
      };
    options.result_callback =
      [this, theta](const GoalHandleRotate::WrappedResult & result)
      {
        RCLCPP_INFO(
          this->get_logger(), "旋转完成 (代码=%d, 总转角=%.2f rad)",
          static_cast<int>(result.code),
          result.result ? result.result->delta : -1.0);
        // 第一个目标完成 → 链式发第二个目标；全部完成 → 退出
        if (theta == this->get_parameter("theta_2").as_double()) {
          rclcpp::shutdown();
        } else {
          send_goal(this->get_parameter("theta_2").as_double());
        }
      };
    client_->async_send_goal(goal, options);
  }

  rclcpp_action::Client<RotateAbsolute>::SharedPtr client_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TurtleRotateClient>());
  rclcpp::shutdown();
  return 0;
}
