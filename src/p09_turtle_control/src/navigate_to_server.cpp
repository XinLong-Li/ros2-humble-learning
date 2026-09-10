// navigate_to_server.cpp —— 实现 p02 的 NavigateTo 动作，控制真乌龟
//
// p05 的 navigate_server 是虚拟世界预演版；这里换成真实对象：
// 订阅 /turtle1/pose 拿位姿，发 /turtle1/cmd_vel 驱赶乌龟，
// 用比例控制（线速度∝剩余距离，角速度∝航向误差）逼近目标点。
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>

#include "geometry_msgs/msg/twist.hpp"
#include "p02_interfaces/action/navigate_to.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "turtlesim/msg/pose.hpp"

using NavigateTo = p02_interfaces::action::NavigateTo;
using GoalHandleNavigate = rclcpp_action::ServerGoalHandle<NavigateTo>;

class NavigateToServer : public rclcpp::Node
{
public:
  NavigateToServer()
  : Node("navigate_to_server")
  {
    this->declare_parameter("kp_linear", 1.0);
    this->declare_parameter("kp_angular", 3.0);
    this->declare_parameter("control_rate", 20.0);

    pose_sub_ = this->create_subscription<turtlesim::msg::Pose>(
      "/turtle1/pose", 10,
      std::bind(&NavigateToServer::pose_callback, this, std::placeholders::_1));
    vel_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("/turtle1/cmd_vel", 10);

    action_server_ = rclcpp_action::create_server<NavigateTo>(
      this,
      "navigate_to",
      std::bind(&NavigateToServer::handle_goal, this,
        std::placeholders::_1, std::placeholders::_2),
      std::bind(&NavigateToServer::handle_cancel, this, std::placeholders::_1),
      std::bind(&NavigateToServer::handle_accepted, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "navigate_to 动作服务端启动，等待目标...");
  }

private:
  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID &,
    std::shared_ptr<const NavigateTo::Goal> goal)
  {
    // 目标校验：参数非法直接拒绝，客户端立刻知道，不用等执行阶段
    if (goal->max_speed <= 0.0 || goal->tolerance <= 0.0) {
      RCLCPP_WARN(this->get_logger(), "拒绝目标：max_speed 与 tolerance 必须为正");
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (active_goal_) {
      RCLCPP_WARN(this->get_logger(), "拒绝目标：已有任务在执行（本示例不支持抢占）");
      return rclcpp_action::GoalResponse::REJECT;
    }
    RCLCPP_INFO(
      this->get_logger(), "接受目标: %s (%.1f, %.1f)",
      goal->target.label.c_str(),
      goal->target.position.x, goal->target.position.y);
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handle_cancel(
    const std::shared_ptr<GoalHandleNavigate> goal_handle)
  {
    (void)goal_handle;
    RCLCPP_INFO(this->get_logger(), "收到取消请求，将在下个控制周期响应");
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handle_accepted(const std::shared_ptr<GoalHandleNavigate> goal_handle)
  {
    active_goal_ = goal_handle;
    // 每个目标一条执行线程（对照 p04 讨论过的执行器模型）
    std::thread{std::bind(&NavigateToServer::execute, this, goal_handle)}.detach();
  }

  void execute(const std::shared_ptr<GoalHandleNavigate> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    const double tolerance = goal->tolerance;
    const double max_speed = goal->max_speed;
    const double rate = this->get_parameter("control_rate").as_double();
    const double kp_linear = this->get_parameter("kp_linear").as_double();
    const double kp_angular = this->get_parameter("kp_angular").as_double();
    const auto start = this->now();

    auto result = std::make_shared<NavigateTo::Result>();
    rclcpp::Rate loop_rate(rate);

    while (rclcpp::ok()) {
      // 取消优先于到达判断：用户按下取消就该立刻停
      if (goal_handle->is_canceling()) {
        publish_twist(0.0, 0.0);
        result->success = false;
        result->final_distance = distance_to(goal->target);
        result->elapsed_seconds = (this->now() - start).seconds();
        goal_handle->canceled(result);
        active_goal_.reset();
        RCLCPP_INFO(this->get_logger(), "目标已取消");
        return;
      }

      const double dist = distance_to(goal->target);
      if (dist <= tolerance) {
        publish_twist(0.0, 0.0);
        result->success = true;
        result->final_distance = dist;
        result->elapsed_seconds = (this->now() - start).seconds();
        goal_handle->succeed(result);
        active_goal_.reset();
        RCLCPP_INFO(
          this->get_logger(), "到达 %s！剩余 %.3f m，用时 %.1f s",
          goal->target.label.c_str(), dist, result->elapsed_seconds);
        return;
      }

      // 比例控制：线速度随距离衰减；角速度朝向目标（角度误差归一化到 ±π）
      std::lock_guard<std::mutex> lock(pose_mutex_);
      const double desired_yaw = std::atan2(
        goal->target.position.y - pose_y_,
        goal->target.position.x - pose_x_);
      double yaw_error = desired_yaw - pose_theta_;
      while (yaw_error > M_PI) {yaw_error -= 2.0 * M_PI;}
      while (yaw_error < -M_PI) {yaw_error += 2.0 * M_PI;}

      const double v = std::clamp(kp_linear * dist, 0.0, max_speed);
      const double w = std::clamp(kp_angular * yaw_error, -2.0, 2.0);
      publish_twist(v, w);

      auto feedback = std::make_shared<NavigateTo::Feedback>();
      feedback->distance_remaining = dist;
      goal_handle->publish_feedback(feedback);

      loop_rate.sleep();
    }
    active_goal_.reset();
  }

  double distance_to(const p02_interfaces::msg::Waypoint & target)
  {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    const double dx = target.position.x - pose_x_;
    const double dy = target.position.y - pose_y_;
    return std::hypot(dx, dy);
  }

  void pose_callback(const turtlesim::msg::Pose::SharedPtr msg)
  {
    // 执行线程在另一个线程读位姿，用互斥锁保护
    std::lock_guard<std::mutex> lock(pose_mutex_);
    pose_x_ = msg->x;
    pose_y_ = msg->y;
    pose_theta_ = msg->theta;
  }

  void publish_twist(double linear, double angular)
  {
    auto msg = geometry_msgs::msg::Twist();
    msg.linear.x = linear;
    msg.angular.z = angular;
    vel_pub_->publish(msg);
  }

  rclcpp::Subscription<turtlesim::msg::Pose>::SharedPtr pose_sub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr vel_pub_;
  rclcpp_action::Server<NavigateTo>::SharedPtr action_server_;

  std::shared_ptr<GoalHandleNavigate> active_goal_;
  std::mutex pose_mutex_;
  double pose_x_ = 5.544445;   // turtlesim 初始出生位
  double pose_y_ = 5.544445;
  double pose_theta_ = 0.0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NavigateToServer>());
  rclcpp::shutdown();
  return 0;
}
