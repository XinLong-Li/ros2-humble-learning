// compute_sum_client_async.cpp —— 异步客户端（发完就走，响应到了再回调）
//
// 与 ROS 1 的对照：
//   ROS 1: 想"不阻塞"得自己上 ros::AsyncSpinner + 自己管线程，稍不注意就踩并发问题；
//          client.call() 本身永远是阻塞的，异步只能靠多线程绕。
//   ROS 2: async_send_request(request, callback) 天生异步 —— 请求发出去立刻返回，
//          响应到达时由执行器调用你的回调，不需要你创建/管理任何线程。
//
// 关键认识：异步 ≠ 多线程。
//   本文件 main 里用的仍是 rclcpp::spin(node)（单线程执行器），
//   但因为没有任何回调"睡大觉"，单线程也能一边等响应、一边每 500 ms 打一次心跳 ——
//   这正是"异步不阻塞"的直接证据（同步版 compute_sum_client 在等待期间打不出心跳）。
//   如果回调本身很耗时，异步也救不了你，那时才需要 compute_sum_server_mt 里的执行器+回调组。
//
// 对照实验（详见 README 实验三）：
//   起 compute_sum_server -p delay_ms:=3000，再跑本节点 -p num_requests:=2，
//   6 秒里心跳日志会一直刷，最后两个响应陆续到达并自动退出进程。

#include <atomic>
#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "p02_interfaces/srv/compute_sum.hpp"

using namespace std::chrono_literals;

class ComputeSumClientAsync : public rclcpp::Node
{
public:
  using ComputeSum = p02_interfaces::srv::ComputeSum;

  // 响应回调的签名必须严格是 void(SharedFuture)，即
  //   std::shared_future<std::shared_ptr<Response>>
  // rclcpp 用 function_traits 精确比对参数类型：写成 const 引用或别的类型会匹配不上，编译不过。
  // 这个别名的写法比手写 std::shared_future<p02_interfaces::srv::ComputeSum::Response::SharedPtr> 省事。
  using SharedFuture = rclcpp::Client<ComputeSum>::SharedFuture;

  ComputeSumClientAsync()
  : Node("compute_sum_client_async")
  {
    this->declare_parameter<int>("num_requests", 2);   // 一共发几个请求（同时在飞）
    this->declare_parameter<int>("values_count", 5);   // 每个请求发送 1..N 这个序列
    num_requests_ = static_cast<int>(this->get_parameter("num_requests").as_int());
    values_count_ = static_cast<int>(this->get_parameter("values_count").as_int());

    client_ = this->create_client<ComputeSum>("compute_sum");

    // 心跳定时器：每 500 ms 打一行。它不是业务的一部分，纯粹是"客户端还活着"的探针：
    // 同步客户端在 spin_until_future_complete 期间不可能打印它。
    heartbeat_timer_ = this->create_wall_timer(500ms, [this]() { this->heartbeat_callback(); });

    RCLCPP_INFO(
      this->get_logger(), "compute_sum_client_async 已启动（心跳 500 ms，用于证明异步等待不阻塞）");
  }

  bool wait_for_service(std::chrono::nanoseconds timeout)
  {
    RCLCPP_INFO(
      this->get_logger(), "等待服务 /compute_sum（最多 %lld ms）...",
      static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count()));

    if (!client_->wait_for_service(timeout)) {
      RCLCPP_ERROR(
        this->get_logger(),
        "服务 /compute_sum 未在超时时间内出现：请先启动 compute_sum_server 或 compute_sum_server_mt");
      return false;
    }
    return true;
  }

  // 把 num_requests_ 个请求一次性全发出去（谁先算完谁先回调）
  // 返回 false 表示参数非法，什么都没发
  bool send_requests()
  {
    if (num_requests_ <= 0) {
      RCLCPP_ERROR(this->get_logger(), "参数 num_requests 必须 >= 1（当前 %d）", num_requests_);
      return false;
    }

    for (int i = 0; i < num_requests_; ++i) {
      auto request = std::make_shared<ComputeSum::Request>();
      for (int v = 1; v <= values_count_; ++v) {
        request->values.push_back(v);
      }

      // 记录"发出时刻"（拷贝进 lambda），回调里用 this->now() 相减就是端到端耗时
      const rclcpp::Time send_time = this->now();

      // 异步：这行立刻返回，不等结果。返回值（future + 请求 id）可以不接 ——
      // client 内部会持有 promise/future，直到响应到达并触发回调；
      // 但要注意：如果响应永远不来，这些内存也不会自动释放，需要 remove_pending_request()
      // 或 prune_pending_requests() 清理（见 README 进阶练习 3）。
      client_->async_send_request(
        request,
        [this, i, send_time](SharedFuture future) {
          this->response_callback(future, i, send_time);
        });

      RCLCPP_INFO(this->get_logger(), "已发出第 %d/%d 个请求（不等待，继续往下走）", i + 1, num_requests_);
    }

    RCLCPP_INFO(
      this->get_logger(), "%d 个请求全部发出 —— 它们此刻都在飞，等响应期间心跳日志照常打印",
      num_requests_);
    return true;
  }

private:
  void heartbeat_callback()
  {
    // 用 load() 读原子量。单线程执行器下其实用普通 int 也安全，
    // 但一旦把 main 换成 MultiThreadedExecutor，普通 int 就会被并发读写 —— 养成好习惯。
    RCLCPP_INFO(
      this->get_logger(), "心跳：客户端仍在响应其他回调（已收到 %d/%d 个响应）",
      received_.load(), num_requests_);
  }

  void response_callback(const SharedFuture & future, int index, const rclcpp::Time & send_time)
  {
    // future.get() 在回调里不会阻塞：能进回调就说明响应已经到了
    auto response = future.get();
    const double elapsed = (this->now() - send_time).seconds();

    RCLCPP_INFO(
      this->get_logger(), "第 %d/%d 个响应：sum=%lld, average=%.2f, count=%d（端到端耗时 %.3f s）",
      index + 1, num_requests_, static_cast<long long>(response->sum), response->average,
      static_cast<int>(response->count), elapsed);

    const int received = ++received_;
    if (received >= num_requests_) {
      RCLCPP_INFO(this->get_logger(), "全部 %d 个响应已收到，调用 rclcpp::shutdown() 退出", received);
      // 在回调里 shutdown 是常见做法：main 里的 spin() 会立刻返回并结束进程。
      // 安全性说明：main 用 shared_ptr 持有节点，而 shutdown() 只是把上下文的 ok() 置为
      // false，并不会析构节点 —— 正在执行的回调里的 this 依然有效。
      // 反过来说：绝不要在回调里 delete 节点或让节点提前析构（本文件靠 main 的 shared_ptr 保活）。
      rclcpp::shutdown();
    }
  }

  rclcpp::Client<ComputeSum>::SharedPtr client_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  std::atomic<int> received_{0};   // 已收到的响应数（shutdown 的触发条件）
  int num_requests_ = 2;
  int values_count_ = 5;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // 用 shared_ptr 持有节点：响应回调可能在任意时刻触发，节点必须活到 spin 返回之后
  auto node = std::make_shared<ComputeSumClientAsync>();

  if (!node->wait_for_service(5s) || !node->send_requests()) {
    rclcpp::shutdown();
    return 1;
  }

  // 单线程执行器足够：心跳定时器和响应回调交替执行，谁都不会把线程睡死。
  // 收到全部响应后，response_callback 里调用 rclcpp::shutdown() 让 spin 返回。
  rclcpp::spin(node);

  rclcpp::shutdown();   // 回调里已经调过一次；shutdown 幂等，重复调用安全
  return 0;
}
