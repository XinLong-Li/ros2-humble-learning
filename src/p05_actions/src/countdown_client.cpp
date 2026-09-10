// countdown_client.cpp —— 倒数动作客户端
//
// 动作客户端的完整流程（对应 ROS 1 的 SimpleActionClient）：
//   1. wait_for_action_server()  等服务器上线（ROS 1 里是 waitForServer）
//   2. async_send_goal()         发目标，返回 future：被接受 → GoalHandle，被拒绝 → nullptr
//   3. feedback / result 回调     由执行器在 spin 时分发（ROS 1 里是 sendGoal 的
//                                feedback_cb / done_cb）
//   4. async_cancel_goal()       需要时中途取消
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "p02_interfaces/action/count_down.hpp"

using namespace std::chrono_literals;
using CountDown = p02_interfaces::action::CountDown;
// 客户端侧的 GoalHandle：用来追踪/取消一个已发出的目标
using GoalHandleCountDown = rclcpp_action::ClientGoalHandle<CountDown>;

class CountdownClient : public rclcpp::Node
{
public:
  CountdownClient()
  : Node("countdown_client")
  {
    // ROS 2 的参数都要先 declare 再使用（默认值写在这里，运行时用 -p 覆盖）
    // 参数名叫 from 没问题（参数名只是字符串）；但接口字段只能叫 start
    // —— 字段名是 Python 保留字时 rosidl 会拒绝生成，详见 CountDown.action 注释
    from_ = this->declare_parameter("from", 10);
    step_ = this->declare_parameter("step", 1);
    cancel_after_ = this->declare_parameter("cancel_after", 0.0);  // 0 表示不取消

    client_ = rclcpp_action::create_client<CountDown>(this, "countdown");

    // cancel_after > 0 时，用 wall timer 到点后自动取消，用来演示取消路径
    if (cancel_after_ > 0.0) {
      cancel_timer_ = this->create_wall_timer(
        std::chrono::duration<double>(cancel_after_),
        [this]() {cancel_callback();});
    }

    RCLCPP_INFO(
      this->get_logger(), "countdown_client 启动：from=%d step=%d cancel_after=%.2f",
      from_, step_, cancel_after_);
  }

  /// 发送目标并等待"被接受/被拒绝"。
  /// 由 main 在子线程里调用，主线程同时在做 rclcpp::spin（原因见 main 的注释）
  void send_goal_and_wait()
  {
    if (!client_->wait_for_action_server(5s)) {
      RCLCPP_ERROR(
        this->get_logger(), "5 秒内没发现 /countdown 动作服务器，请先启动 countdown_server");
      rclcpp::shutdown();
      return;
    }

    auto goal = CountDown::Goal();
    // 目标的字段名是 start（接口里不能叫 from：Python 保留字，rosidl 会拒绝生成）
    goal.start = from_;  // 本节点的参数仍叫 from，见构造函数里的 declare_parameter
    goal.step = step_;

    rclcpp_action::Client<CountDown>::SendGoalOptions options;

    // 反馈回调：目标执行期间被调用多次
    options.feedback_callback =
      [this](GoalHandleCountDown::SharedPtr, const std::shared_ptr<const CountDown::Feedback> feedback) {
        RCLCPP_INFO(this->get_logger(), "反馈：current=%d", feedback->current);
      };

    // 结果回调：目标进入终态（成功/取消/中止）时调用一次，打印后退出程序
    options.result_callback =
      [this](const GoalHandleCountDown::WrappedResult & result) {
        print_result(result);
        // 目标已终结，客户端使命完成 —— 在回调里关掉 spin（main 里 spin 返回后收尾）
        rclcpp::shutdown();
      };

    // 这两个回调必须在 async_send_goal 之前填好：它们会被绑定到目标上
    goal_handle_future_ = client_->async_send_goal(goal, options);

    // 等目标响应必须"在 spin 环境下"：响应要由执行器处理完才会让 future 变 ready。
    // 本函数跑在子线程、主线程正好在 spin，所以这里 wait_for 是安全的
    // （若在构造函数里直接 wait_for，那时还没人 spin，必然白等到超时）
    if (goal_handle_future_.wait_for(5s) != std::future_status::ready) {
      RCLCPP_ERROR(this->get_logger(), "等待目标响应超时（5 秒）");
      rclcpp::shutdown();
      return;
    }

    auto goal_handle = goal_handle_future_.get();
    if (!goal_handle) {
      // 被拒绝时 future 给出的是 nullptr，这是"服务器主动不收"的信号
      RCLCPP_ERROR(this->get_logger(), "目标被服务器拒绝（检查 step 是否为 0、是否已有目标在执行）");
      rclcpp::shutdown();
      return;
    }

    {
      // 执行器线程里的取消定时器回调也要读这个 handle，加锁保护
      std::lock_guard<std::mutex> lock(mutex_);
      goal_handle_ = goal_handle;
    }
    RCLCPP_INFO(this->get_logger(), "目标已被接受，等待执行…");
  }

private:
  /// 打印终态结果。注意动作有 4 种结局，客户端必须区分处理
  void print_result(const GoalHandleCountDown::WrappedResult & result)
  {
    switch (result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        RCLCPP_INFO(
          this->get_logger(), "结果：成功（total_steps=%d, message='%s'）",
          result.result->total_steps, result.result->message.c_str());
        break;
      case rclcpp_action::ResultCode::CANCELED:
        RCLCPP_INFO(
          this->get_logger(), "结果：已取消（total_steps=%d, message='%s'）",
          result.result->total_steps, result.result->message.c_str());
        break;
      case rclcpp_action::ResultCode::ABORTED:
        RCLCPP_WARN(this->get_logger(), "结果：被中止（total_steps=%d）", result.result->total_steps);
        break;
      default:
        RCLCPP_WARN(this->get_logger(), "结果：未知状态（code=%d）", static_cast<int>(result.code));
        break;
    }
  }

  /// cancel_after 到点后自动发取消请求
  void cancel_callback()
  {
    // 不在定时器自己的回调里 cancel 它（Humble 下可能段错误），改用标志位只发一次
    if (cancel_sent_) {
      return;
    }
    cancel_sent_ = true;

    GoalHandleCountDown::SharedPtr goal_handle;
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
        [this](const rclcpp_action::Client<CountDown>::CancelResponse::SharedPtr response) {
          // return_code>0 表示服务器接受了取消请求（goals_canceling 里是被取消的目标）
          RCLCPP_INFO(
            this->get_logger(), "取消响应：return_code=%d，正在取消的目标数=%zu",
            response->return_code, response->goals_canceling.size());
        });
    } catch (const rclcpp_action::exceptions::UnknownGoalHandleError & ex) {
      // 目标刚好在这时结束了（客户端已把 handle 忘掉），忽略即可
      RCLCPP_WARN(this->get_logger(), "目标已结束，无需取消：%s", ex.what());
    }
  }

  rclcpp_action::Client<CountDown>::SharedPtr client_;
  rclcpp::TimerBase::SharedPtr cancel_timer_;
  bool cancel_sent_ = false;  // 取消请求只发一次
  std::shared_future<GoalHandleCountDown::SharedPtr> goal_handle_future_;
  // goal_handle_ 由子线程写、执行器线程读（取消定时器），必须加锁
  std::mutex mutex_;
  GoalHandleCountDown::SharedPtr goal_handle_;

  int from_ = 10;
  int step_ = 1;
  double cancel_after_ = 0.0;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<CountdownClient>();

  // 为什么要开子线程发目标：
  //   send_goal_and_wait() 里要 wait_for 目标响应 future，而目标响应必须由执行器
  //   spin 才能送达 —— "等待"和"spin"必须同时进行。
  //   如果主线程先 wait_for 再 spin，就是自己等自己，future 永远不会 ready。
  //   所以：主线程负责 spin，子线程负责发送并等待。
  std::thread sender_thread([node]() {node->send_goal_and_wait();});

  // 目标进入终态时 result 回调里会调用 rclcpp::shutdown()，spin 随之返回
  rclcpp::spin(node);

  sender_thread.join();
  rclcpp::shutdown();
  return 0;
}
