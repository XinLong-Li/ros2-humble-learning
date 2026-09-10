// countdown_server.cpp —— 倒数动作服务器（动作类型 p02_interfaces/action/CountDown）
//
// 动作 = 目标(goal) + 过程反馈(feedback) + 最终结果(result)，而且随时可取消。
// 和 ROS 1 actionlib 对照：
//   SimpleActionServer  + executeCB   <->  handle_accepted 拿到的 GoalHandle
//   as_.setSucceeded()/setAborted()   <->  goal_handle->succeed()/abort()
//   as_.publishFeedback()             <->  goal_handle->publish_feedback()
//   as_.isPreemptRequested()          <->  goal_handle->is_canceling()
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "p02_interfaces/action/count_down.hpp"

using namespace std::chrono_literals;  // 1s / 100ms 字面量
using CountDown = p02_interfaces::action::CountDown;
// 服务器侧的 GoalHandle 类型：动作类型不同，类型也不同（ROS 1 里是同一个模板参数）
using GoalHandleCountDown = rclcpp_action::ServerGoalHandle<CountDown>;

class CountdownServer : public rclcpp::Node
{
public:
  CountdownServer()
  : Node("countdown_server")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // 三个回调对应动作的三个关键时机，缺一不可：
    //   handle_goal     —— 收到目标，决定"收不收"（合法性校验只有这一次机会）
    //   handle_cancel   —— 收到取消请求，决定"取不取消"
    //   handle_accepted —— 已接受，拿到 GoalHandle，开始干活
    server_ = rclcpp_action::create_server<CountDown>(
      this, "countdown",
      std::bind(&CountdownServer::handle_goal, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&CountdownServer::handle_cancel, this, std::placeholders::_1),
      std::bind(&CountdownServer::handle_accepted, this, std::placeholders::_1));

    RCLCPP_INFO(
      this->get_logger(),
      "countdown_server 已启动：动作名 /countdown，类型 p02_interfaces/action/CountDown");
  }

private:
  /// 收到新目标：这里返回 REJECT / ACCEPT_AND_EXECUTE / ACCEPT_AND_DEFER
  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID & uuid, std::shared_ptr<const CountDown::Goal> goal)
  {
    // 为什么校验要放在这里：这是动作唯一能"拒绝"目标的时机。
    // 换成话题（发布者管不了订阅者）或服务（只能返回错误码）都做不到这么自然。
    if (goal->step == 0) {
      // step==0 会让倒数永远不前进（死循环），必须拒绝
      RCLCPP_WARN(
        this->get_logger(), "拒绝目标 %s：step 不能为 0（start=%d）",
        rclcpp_action::to_string(uuid).c_str(), goal->start);
      return rclcpp_action::GoalResponse::REJECT;
    }

    // 教学简化：不支持抢占。已有目标在执行时直接拒绝新目标（抢占见 README 练习题）
    if (busy_.load()) {
      RCLCPP_WARN(this->get_logger(), "拒绝目标：已有目标在执行（本示例不支持抢占）");
      return rclcpp_action::GoalResponse::REJECT;
    }

    // 注意字段叫 start 而不是 from：from 是 Python 保留字，
    // 用它当字段名 rosidl 会拒绝生成 Python 绑定（ros2 action send_goal 直接报错）
    RCLCPP_INFO(this->get_logger(), "接受目标：start=%d step=%d", goal->start, goal->step);
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  /// 收到取消请求：返回 ACCEPT 表示"同意尝试取消"
  rclcpp_action::CancelResponse handle_cancel(const std::shared_ptr<GoalHandleCountDown> goal_handle)
  {
    // 注意：此处只是同意，真正的终态要等执行线程调用 goal_handle->canceled() 才发出。
    // 返回 ACCEPT 后 is_canceling() 立刻变 true，执行线程据此决定何时收尾。
    RCLCPP_INFO(
      this->get_logger(), "收到取消请求（goal %s），同意取消",
      rclcpp_action::to_string(goal_handle->get_goal_id()).c_str());
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  /// 目标被接受：开线程真正执行
  void handle_accepted(const std::shared_ptr<GoalHandleCountDown> goal_handle)
  {
    // 本回调必须立刻返回：它跑在执行器线程里，一旦阻塞，
    // 其它回调（新目标、取消请求、结果查询）全都进不来。
    // 所以把执行体丢到独立线程 —— ROS 1 actionlib 的 executeCB 也是这么做的。
    busy_.store(true);
    std::thread(&CountdownServer::execute, this, goal_handle).detach();
  }

  /// 执行体（独立线程）：从 start 开始每 1 秒走 step，直到 <= 0
  void execute(const std::shared_ptr<GoalHandleCountDown> goal_handle)
  {
    const auto goal = goal_handle->get_goal();  // 注意是 const：目标在执行期间不应被修改
    auto feedback = std::make_shared<CountDown::Feedback>();
    auto result = std::make_shared<CountDown::Result>();

    int32_t current = goal->start;  // 目标字段是 start（不能叫 from：Python 保留字）
    int32_t steps = 0;              // 已走步数

    RCLCPP_INFO(this->get_logger(), "开始倒数：%d → 0（步长 %d）", current, goal->step);

    while (rclcpp::ok() && current > 0) {
      // 每个循环先查取消：handle_cancel 返回 ACCEPT 后 is_canceling() 变 true
      if (goal_handle->is_canceling()) {
        result->total_steps = steps;
        result->message = "已取消";
        goal_handle->canceled(result);  // 终态 CANCELED
        RCLCPP_INFO(this->get_logger(), "目标已取消：共走了 %d 步", steps);
        busy_.store(false);
        return;
      }

      // 发布反馈：客户端 feedback_callback 会被逐次调用
      feedback->current = current;
      goal_handle->publish_feedback(feedback);
      RCLCPP_INFO(this->get_logger(), "反馈：current=%d", current);

      // 把 1 秒拆成 10 个 100ms 小睡：收到 Ctrl-C 时线程能立刻退出，
      // 不会出现"节点都析构了，detach 出去的线程还在睡"的悬空访问
      for (int i = 0; i < 10 && rclcpp::ok(); ++i) {
        std::this_thread::sleep_for(100ms);
      }

      current -= goal->step;
      ++steps;
    }

    if (!rclcpp::ok()) {
      // 节点正在关闭：这里不再触碰任何成员（节点随时可能析构），直接收工
      return;
    }

    result->total_steps = steps;
    result->message = "已完成";
    goal_handle->succeed(result);  // 终态 SUCCEEDED
    RCLCPP_INFO(this->get_logger(), "目标完成：total_steps=%d", steps);
    busy_.store(false);
  }

  rclcpp_action::Server<CountDown>::SharedPtr server_;
  // 是否有目标在执行。handle_accepted 在执行器线程写、execute 在工作线程写，
  // 两个线程都会读写，所以用 atomic（普通 bool 是数据竞争）
  std::atomic<bool> busy_{false};
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CountdownServer>());
  rclcpp::shutdown();
  return 0;
}
