// navigate_server.cpp —— 导航动作服务器（动作类型 p02_interfaces/action/NavigateTo）
//
// 和 countdown 的区别：目标不只带标量，而是带一个复合类型
// （p02_interfaces/msg/Waypoint，里面又嵌套 geometry_msgs/Point），
// 反馈是"剩余距离"—— 这正是动作最典型的用法：长时间任务 + 进度反馈。
//
// 本文件里的机器人是虚拟的（成员变量 x_/y_，不走真车）。
// 但 p09 会把这套代码换成"控制真实乌龟"：同一个 action 类型、
// 同一套三个回调，只把"移动一步"的动作从改局部变量换成
// 调用 turtlesim 的 /turtle1/cmd_vel —— 虚拟世界是预演，先在这里把
// 动作的状态机跑通，再换执行体，风险最低。
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "p02_interfaces/action/navigate_to.hpp"
#include "p02_interfaces/msg/waypoint.hpp"

using namespace std::chrono_literals;
using NavigateTo = p02_interfaces::action::NavigateTo;
using GoalHandleNavigateTo = rclcpp_action::ServerGoalHandle<NavigateTo>;

class NavigateServer : public rclcpp::Node
{
public:
  NavigateServer()
  : Node("navigate_server")
  {
    server_ = rclcpp_action::create_server<NavigateTo>(
      this, "navigate_to",
      std::bind(&NavigateServer::handle_goal, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&NavigateServer::handle_cancel, this, std::placeholders::_1),
      std::bind(&NavigateServer::handle_accepted, this, std::placeholders::_1));

    RCLCPP_INFO(
      this->get_logger(),
      "navigate_server 已启动：动作名 /navigate_to，虚拟机器人初始位置 (0.00, 0.00)");
  }

private:
  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID & uuid, std::shared_ptr<const NavigateTo::Goal> goal)
  {
    // 目标校验：这两条都是"启动前就能判定必失败"的参数，趁早拒绝，
    // 别让客户端等半天才收到失败
    if (goal->max_speed <= 0.0) {
      RCLCPP_WARN(
        this->get_logger(), "拒绝目标 %s：max_speed 必须 > 0（收到 %.2f）",
        rclcpp_action::to_string(uuid).c_str(), goal->max_speed);
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (goal->tolerance <= 0.0) {
      RCLCPP_WARN(
        this->get_logger(), "拒绝目标：tolerance 必须 > 0（收到 %.2f），否则永远判不出到达",
        goal->tolerance);
      return rclcpp_action::GoalResponse::REJECT;
    }

    if (busy_.load()) {
      RCLCPP_WARN(this->get_logger(), "拒绝目标：正在导航中（本示例不支持抢占）");
      return rclcpp_action::GoalResponse::REJECT;
    }

    RCLCPP_INFO(
      this->get_logger(), "接受目标：%s (%.2f, %.2f)，tolerance=%.2f max_speed=%.2f",
      goal->target.label.c_str(), goal->target.position.x, goal->target.position.y,
      goal->tolerance, goal->max_speed);
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handle_cancel(
    const std::shared_ptr<GoalHandleNavigateTo> goal_handle)
  {
    // 同意取消：执行线程会在下一轮循环（最多 100ms 后）看到 is_canceling()
    RCLCPP_INFO(
      this->get_logger(), "收到取消请求（goal %s），同意取消，机器人将就地停下",
      rclcpp_action::to_string(goal_handle->get_goal_id()).c_str());
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handle_accepted(const std::shared_ptr<GoalHandleNavigateTo> goal_handle)
  {
    // 回调里不能阻塞（会卡住执行器），执行体丢到独立线程
    busy_.store(true);
    std::thread(&NavigateServer::execute, this, goal_handle).detach();
  }

  /// 虚拟机器人到 (tx, ty) 的直线距离
  double distance_to(double tx, double ty) const
  {
    return std::hypot(tx - x_, ty - y_);
  }

  /// 执行体（独立线程）：每 0.1 秒走一步，速度为 max_speed m/s
  void execute(const std::shared_ptr<GoalHandleNavigateTo> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    const double target_x = goal->target.position.x;
    const double target_y = goal->target.position.y;

    auto feedback = std::make_shared<NavigateTo::Feedback>();
    auto result = std::make_shared<NavigateTo::Result>();
    const rclcpp::Time start = this->now();

    RCLCPP_INFO(
      this->get_logger(), "开始导航：当前位置 (%.2f, %.2f) → %s (%.2f, %.2f)",
      x_, y_, goal->target.label.c_str(), target_x, target_y);

    while (rclcpp::ok()) {
      // 先查取消：客户端取消后 is_canceling() 为 true，这里把终态定为 CANCELED
      if (goal_handle->is_canceling()) {
        result->success = false;
        result->final_distance = distance_to(target_x, target_y);
        result->elapsed_seconds = (this->now() - start).seconds();
        goal_handle->canceled(result);
        RCLCPP_INFO(
          this->get_logger(), "导航被取消：停在 (%.2f, %.2f)，剩余 %.2f m",
          x_, y_, result->final_distance);
        busy_.store(false);
        return;
      }

      const double d = distance_to(target_x, target_y);

      if (d <= goal->tolerance) {
        // 到达判定：动作在此进入终态 SUCCEEDED
        result->success = true;
        result->final_distance = d;
        result->elapsed_seconds = (this->now() - start).seconds();
        goal_handle->succeed(result);
        RCLCPP_INFO(
          this->get_logger(), "已到达 (%.2f, %.2f)：剩余 %.3f m，用时 %.2f s",
          x_, y_, d, result->elapsed_seconds);
        busy_.store(false);
        return;
      }

      // 一步走 max_speed * 0.1 秒的距离（0.1s 一步 → 实际速度就是 max_speed m/s）；
      // min(..., d) 保证不会一步冲过目标来回振荡，也避免 d 很小时方向向量失准
      const double step = std::min(goal->max_speed * 0.1, d);
      x_ += step * (target_x - x_) / d;
      y_ += step * (target_y - y_) / d;

      // 反馈"剩余距离"：客户端据此画进度条 / 判断要不要放弃
      feedback->distance_remaining = d;
      goal_handle->publish_feedback(feedback);
      RCLCPP_INFO(this->get_logger(), "位置 (%.2f, %.2f)：剩余 %.2f m", x_, y_, d);

      std::this_thread::sleep_for(100ms);
    }

    // 走到这里说明 rclcpp::ok() 为 false（Ctrl-C），节点即将关闭：
    // 不再触碰任何成员，直接收工（线程是 detach 出去的，不能比节点活得久）
  }

  rclcpp_action::Server<NavigateTo>::SharedPtr server_;
  std::atomic<bool> busy_{false};

  // 虚拟机器人位置（真实机器人上换成从里程计/位姿话题读）
  double x_ = 0.0;
  double y_ = 0.0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NavigateServer>());
  rclcpp::shutdown();
  return 0;
}
