// navigate_client.cpp —— 导航动作客户端
//
// 和 countdown_client 结构完全一样，只是目标换成了复合类型
// （Waypoint + tolerance + max_speed），结果换成三个字段。
// 这说明动作的"骨架"是固定的：发目标 → 收反馈 → 收结果 / 中途取消，
// 变的只是接口里装什么数据。
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "p02_interfaces/action/navigate_to.hpp"
#include "p02_interfaces/msg/waypoint.hpp"  // goal.target 的类型

using namespace std::chrono_literals;
using NavigateTo = p02_interfaces::action::NavigateTo;
using GoalHandleNavigateTo = rclcpp_action::ClientGoalHandle<NavigateTo>;

class NavigateClient : public rclcpp::Node
{
public:
  NavigateClient()
  : Node("navigate_client")
  {
    // 先声明再使用：默认值写在代码里，运行时 -p 覆盖
    goal_x_ = this->declare_parameter("goal_x", 5.0);
    goal_y_ = this->declare_parameter("goal_y", 3.0);
    tolerance_ = this->declare_parameter("tolerance", 0.2);
    max_speed_ = this->declare_parameter("max_speed", 1.0);
    cancel_after_ = this->declare_parameter("cancel_after", 0.0);  // 0 表示不取消

    client_ = rclcpp_action::create_client<NavigateTo>(this, "navigate_to");

    // cancel_after > 0 时到点自动取消，用来演示取消路径
    if (cancel_after_ > 0.0) {
      cancel_timer_ = this->create_wall_timer(
        std::chrono::duration<double>(cancel_after_),
        [this]() {cancel_callback();});
    }

    RCLCPP_INFO(
      this->get_logger(), "navigate_client 启动：目标 (%.2f, %.2f) tolerance=%.2f max_speed=%.2f cancel_after=%.2f",
      goal_x_, goal_y_, tolerance_, max_speed_, cancel_after_);
  }

  /// 发送目标并等待"被接受/被拒绝"。由 main 在子线程里调用（原因见 main）
  void send_goal_and_wait()
  {
    if (!client_->wait_for_action_server(5s)) {
      RCLCPP_ERROR(
        this->get_logger(), "5 秒内没发现 /navigate_to 动作服务器，请先启动 navigate_server");
      rclcpp::shutdown();
      return;
    }

    auto goal = NavigateTo::Goal();
    // 复合字段的赋值：target 是 p02_interfaces/msg/Waypoint，position 又是 geometry_msgs/Point
    goal.target.label = "goal";
    goal.target.position.x = goal_x_;
    goal.target.position.y = goal_y_;
    goal.target.position.z = 0.0;  // 平面移动，z 保持 0
    goal.tolerance = tolerance_;
    goal.max_speed = max_speed_;

    rclcpp_action::Client<NavigateTo>::SendGoalOptions options;

    // 反馈回调：导航过程中每 0.1 秒来一次，打印剩余距离
    options.feedback_callback =
      [this](
        GoalHandleNavigateTo::SharedPtr,
        const std::shared_ptr<const NavigateTo::Feedback> feedback) {
        RCLCPP_INFO(
          this->get_logger(), "反馈：剩余距离 %.2f m", feedback->distance_remaining);
      };

    // 结果回调：到达/取消后触发，打印结果并退出程序
    options.result_callback =
      [this](const GoalHandleNavigateTo::WrappedResult & result) {
        print_result(result);
        rclcpp::shutdown();
      };

    goal_handle_future_ = client_->async_send_goal(goal, options);

    // 等目标响应必须在 spin 中进行（主线程在 spin、本函数在子线程），
    // 否则没人处理响应，future 永远不 ready
    if (goal_handle_future_.wait_for(5s) != std::future_status::ready) {
      RCLCPP_ERROR(this->get_logger(), "等待目标响应超时（5 秒）");
      rclcpp::shutdown();
      return;
    }

    auto goal_handle = goal_handle_future_.get();
    if (!goal_handle) {
      RCLCPP_ERROR(
        this->get_logger(), "目标被拒绝（检查 tolerance/max_speed 是否 <= 0、是否已有目标在执行）");
      rclcpp::shutdown();
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      goal_handle_ = goal_handle;
    }
    RCLCPP_INFO(this->get_logger(), "目标已被接受，开始导航…");
  }

private:
  void print_result(const GoalHandleNavigateTo::WrappedResult & result)
  {
    switch (result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        RCLCPP_INFO(
          this->get_logger(), "结果：成功=%s，最终距离 %.3f m，耗时 %.2f s",
          result.result->success ? "true" : "false",
          result.result->final_distance, result.result->elapsed_seconds);
        break;
      case rclcpp_action::ResultCode::CANCELED:
        RCLCPP_INFO(
          this->get_logger(), "结果：已取消，停在离目标 %.2f m 处，已走 %.2f s",
          result.result->final_distance, result.result->elapsed_seconds);
        break;
      case rclcpp_action::ResultCode::ABORTED:
        RCLCPP_WARN(this->get_logger(), "结果：被中止");
        break;
      default:
        RCLCPP_WARN(this->get_logger(), "结果：未知状态（code=%d）", static_cast<int>(result.code));
        break;
    }
  }

  void cancel_callback()
  {
    cancel_timer_->cancel();  // 一次性定时器

    GoalHandleNavigateTo::SharedPtr goal_handle;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      goal_handle = goal_handle_;
    }
    if (!goal_handle) {
      RCLCPP_WARN(this->get_logger(), "到达取消时间，但目标还没被接受，跳过取消");
      return;
    }

    RCLCPP_INFO(this->get_logger(), "%.2f 秒到，发送取消请求", cancel_after_);
    try {
      client_->async_cancel_goal(
        goal_handle,
        [this](const rclcpp_action::Client<NavigateTo>::CancelResponse::SharedPtr response) {
          RCLCPP_INFO(
            this->get_logger(), "取消响应：return_code=%d，正在取消的目标数=%zu",
            response->return_code, response->goals_canceling.size());
        });
    } catch (const rclcpp_action::exceptions::UnknownGoalHandleError & ex) {
      RCLCPP_WARN(this->get_logger(), "目标已结束，无需取消：%s", ex.what());
    }
  }

  rclcpp_action::Client<NavigateTo>::SharedPtr client_;
  rclcpp::TimerBase::SharedPtr cancel_timer_;
  std::shared_future<GoalHandleNavigateTo::SharedPtr> goal_handle_future_;
  std::mutex mutex_;  // 保护 goal_handle_（子线程写、执行器线程读）
  GoalHandleNavigateTo::SharedPtr goal_handle_;

  double goal_x_ = 5.0;
  double goal_y_ = 3.0;
  double tolerance_ = 0.2;
  double max_speed_ = 1.0;
  double cancel_after_ = 0.0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<NavigateClient>();

  // 主线程 spin、子线程发送并等待：send_goal_and_wait() 里的 future.wait_for()
  // 需要执行器同时在 spin，否则就是自己等自己（详见 countdown_client 的注释）
  std::thread sender_thread([node]() {node->send_goal_and_wait();});

  rclcpp::spin(node);  // 结果回调里 rclcpp::shutdown() 后返回

  sender_thread.join();
  rclcpp::shutdown();
  return 0;
}
