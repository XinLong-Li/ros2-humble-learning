// waypoint_follower.cpp —— 航点巡航（结课作品）
//
// 从参数里读航点序列（"label,x,y" 字符串数组），逐个向 /navigate_to 发目标；
// 每个目标的结果回调里链式安排下一个航点；全部走完后停车并退出。
// 至此：话题（pose/cmd_vel）+ 动作（navigate_to）+ 参数（航点 YAML）+ TF 全部串起来了。
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "geometry_msgs/msg/twist.hpp"
#include "p02_interfaces/action/navigate_to.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

using namespace std::chrono_literals;

using NavigateTo = p02_interfaces::action::NavigateTo;
using GoalHandleNavigate = rclcpp_action::ClientGoalHandle<NavigateTo>;

class WaypointFollower : public rclcpp::Node
{
public:
  WaypointFollower()
  : Node("waypoint_follower")
  {
    this->declare_parameter(
      "waypoints",
      std::vector<std::string>{"A,2.0,2.0", "B,8.0,8.0", "C,8.0,2.0", "D,2.0,8.0"});
    this->declare_parameter("tolerance", 0.2);
    this->declare_parameter("max_speed", 2.0);
    this->declare_parameter("wait_between", 0.5);

    client_ = rclcpp_action::create_client<NavigateTo>(this, "navigate_to");
    vel_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("/turtle1/cmd_vel", 10);

    // 轮询动作服务端是否就绪（不能阻塞 spin 的线程）
    timer_ = this->create_wall_timer(500ms, [this]() {
      if (!client_->action_server_is_ready()) {
        RCLCPP_WARN_THROTTLE(
          this->get_logger(), *this->get_clock(), 5000,
          "等待 navigate_to 动作服务端（先启动 navigate_to_server 或直接用 launch 文件）...");
        return;
      }
      timer_->cancel();
      load_waypoints();
      send_next_goal();
    });

    RCLCPP_INFO(this->get_logger(), "航点巡航节点启动，等待动作服务端...");
  }

private:
  struct Waypoint
  {
    std::string label;
    double x;
    double y;
  };

  void load_waypoints()
  {
    for (const auto & text : this->get_parameter("waypoints").as_string_array()) {
      Waypoint wp;
      char label[64];
      // 解析 "label,x,y" 格式；用 C 风格 sscanf 最直白
      if (std::sscanf(text.c_str(), "%63[^,],%lf,%lf", label, &wp.x, &wp.y) == 3) {
        wp.label = label;
        waypoints_.push_back(wp);
      } else {
        RCLCPP_WARN(this->get_logger(), "无法解析航点 '%s'，已跳过", text.c_str());
      }
    }
    RCLCPP_INFO(this->get_logger(), "共加载 %zu 个航点", waypoints_.size());
  }

  void send_next_goal()
  {
    if (index_ >= waypoints_.size()) {
      // 任务完成：停车 + 收尾
      publish_twist(0.0, 0.0);
      RCLCPP_INFO(this->get_logger(), "任务完成：共访问 %zu 个航点！", waypoints_.size());
      rclcpp::shutdown();
      return;
    }

    const auto & wp = waypoints_[index_];
    RCLCPP_INFO(
      this->get_logger(), "前往航点 %zu/%zu: %s (%.1f, %.1f)",
      index_ + 1, waypoints_.size(), wp.label.c_str(), wp.x, wp.y);

    auto goal = NavigateTo::Goal();
    goal.target.label = wp.label;
    goal.target.position.x = wp.x;
    goal.target.position.y = wp.y;
    goal.target.position.z = 0.0;
    goal.tolerance = this->get_parameter("tolerance").as_double();
    goal.max_speed = this->get_parameter("max_speed").as_double();

    auto options = rclcpp_action::Client<NavigateTo>::SendGoalOptions();
    options.feedback_callback =
      [this](GoalHandleNavigate::SharedPtr,
        const std::shared_ptr<const NavigateTo::Feedback> feedback)
      {
        // 20 Hz 的反馈太吵，节流到 1 秒一条
        RCLCPP_INFO_THROTTLE(
          this->get_logger(), *this->get_clock(), 1000,
          "剩余距离: %.2f m", feedback->distance_remaining);
      };
    options.result_callback =
      [this](const GoalHandleNavigate::WrappedResult & result)
      {
        if (result.code == rclcpp_action::ResultCode::SUCCEEDED &&
          result.result && result.result->success)
        {
          RCLCPP_INFO(
            this->get_logger(), "航点 %s 到达（用时 %.1f s）",
            waypoints_[index_].label.c_str(), result.result->elapsed_seconds);
        } else {
          RCLCPP_WARN(
            this->get_logger(), "航点 %s 未完成（code=%d），跳过",
            waypoints_[index_].label.c_str(), static_cast<int>(result.code));
        }
        index_++;
        schedule_next(wait_between_seconds());
      };
    client_->async_send_goal(goal, options);
  }

  double wait_between_seconds()
  {
    return this->get_parameter("wait_between").as_double();
  }

  void schedule_next(double delay_seconds)
  {
    // 航点之间稍作停留：一次性定时器，触发后自我取消
    next_timer_ = this->create_wall_timer(
      std::chrono::duration<double>(delay_seconds),
      [this]() {
        next_timer_->cancel();
        send_next_goal();
      });
  }

  void publish_twist(double linear, double angular)
  {
    auto msg = geometry_msgs::msg::Twist();
    msg.linear.x = linear;
    msg.angular.z = angular;
    vel_pub_->publish(msg);
  }

  rclcpp_action::Client<NavigateTo>::SharedPtr client_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr vel_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::TimerBase::SharedPtr next_timer_;
  std::vector<Waypoint> waypoints_;
  size_t index_ = 0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<WaypointFollower>());
  rclcpp::shutdown();
  return 0;
}
