// compute_sum_client.cpp —— 同步客户端（阻塞式调用）
//
// 与 ROS 1 的对照：
//   ROS 1: my_srv::ComputeSum srv; srv.request.values = ...; client.call(srv);
//          —— 一行到底，阻塞当前线程直到返回，超时要在 launch/参数里配置。
//   ROS 2: 没有 client.call()。官方给的"同步"写法是两步：
//            auto future = client->async_send_request(request);              // 发出去，立刻返回
//            rclcpp::spin_until_future_complete(node, future, 10s);           // 在这里阻塞等待
//          好处是超时直接写在代码里、可以逐个请求控制；
//          代价是等待期间"当前线程被占死"，节点收不到其他回调。
//
// 本文件的核心认知（也是对照实验的重点）：
//   spin_until_future_complete 会在当前线程里"转执行器"，直到 future 就绪或超时。
//   这段时间里，本节点没有任何其他线程在转 —— 心跳定时器、订阅回调全都停摆。
//   想边等结果边干别的，见 compute_sum_client_async.cpp。
//
// 另一个容易误会的点：本客户端是"发一个、等一个"。
//   即使服务端是多线程的 compute_sum_server_mt，两个请求也不会重叠执行，
//   总耗时仍是 2 × delay_ms。要让服务端真正并发，必须同时有多个请求在飞
//   （用异步客户端一次发多个，或开两个客户端进程）。

#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "p02_interfaces/srv/compute_sum.hpp"

using namespace std::chrono_literals;

class ComputeSumClient : public rclcpp::Node
{
public:
  using ComputeSum = p02_interfaces::srv::ComputeSum;

  ComputeSumClient()
  : Node("compute_sum_client")
  {
    this->declare_parameter<int>("num_requests", 2);   // 一共发几个请求
    this->declare_parameter<int>("values_count", 5);   // 每个请求发送 1..N 这个序列
    num_requests_ = static_cast<int>(this->get_parameter("num_requests").as_int());
    values_count_ = static_cast<int>(this->get_parameter("values_count").as_int());

    // 对应 ROS 1 的 nh.serviceClient<my_srv::ComputeSum>("compute_sum")，
    // 服务名必须与服务端完全一致（服务名不做任何"模糊匹配"，对不上就是一直等）
    client_ = this->create_client<ComputeSum>("compute_sum");
  }

  // 返回 false 表示服务一直没出现，main 据此以非 0 退出码结束
  bool wait_for_service(std::chrono::nanoseconds timeout)
  {
    RCLCPP_INFO(
      this->get_logger(), "等待服务 /compute_sum（最多 %lld ms）...",
      static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count()));

    // 对应 ROS 1 的 client.waitForExistence(ros::Duration(5.0))，但返回 bool 便于判断
    if (!client_->wait_for_service(timeout)) {
      RCLCPP_ERROR(
        this->get_logger(),
        "服务 /compute_sum 未在超时时间内出现：请先启动 compute_sum_server 或 compute_sum_server_mt");
      return false;
    }
    RCLCPP_INFO(this->get_logger(), "服务已就绪，开始发送 %d 个请求", num_requests_);
    return true;
  }

  // 返回 true 表示 num_requests_ 个请求全部成功
  bool run()
  {
    if (num_requests_ <= 0) {
      RCLCPP_ERROR(this->get_logger(), "参数 num_requests 必须 >= 1（当前 %d）", num_requests_);
      return false;
    }

    const rclcpp::Time t_start = this->now();
    int ok_count = 0;

    for (int i = 0; i < num_requests_; ++i) {
      // 构造请求：values = [1, 2, ..., values_count]
      auto request = std::make_shared<ComputeSum::Request>();
      for (int v = 1; v <= values_count_; ++v) {
        request->values.push_back(v);
      }

      const rclcpp::Time t0 = this->now();

      // async_send_request 只负责"把请求发出去并返回 future"，它自己不阻塞；
      // 真正阻塞的是下一行的 spin_until_future_complete —— 它在当前线程里转执行器，
      // 直到 future 有结果（SUCCESS）或等满 10 s（TIMEOUT）或被 Ctrl-C 打断（INTERRUPTED）。
      auto future = client_->async_send_request(request);
      const auto ret = rclcpp::spin_until_future_complete(this->get_node_base_interface(), future, 10s);

      // 用节点时钟算耗时：开了 use_sim_time 时它会是仿真时间，语义比 std::chrono 更统一
      const double elapsed = (this->now() - t0).seconds();

      if (ret == rclcpp::FutureReturnCode::SUCCESS) {
        // SUCCESS 只说明"future 完成了"，不代表业务一定成功（本例没有业务错误码）；
        // 取结果用 future.get()，拿到的是 Response::SharedPtr
        auto response = future.get();
        RCLCPP_INFO(
          this->get_logger(),
          "第 %d/%d 次响应：values=1..%d -> sum=%lld, average=%.2f, count=%d（本次耗时 %.3f s）",
          i + 1, num_requests_, values_count_,
          static_cast<long long>(response->sum), response->average,
          static_cast<int>(response->count), elapsed);
        ++ok_count;
      } else if (ret == rclcpp::FutureReturnCode::TIMEOUT) {
        RCLCPP_ERROR(
          this->get_logger(), "第 %d/%d 次请求超时（10 s 内没收到响应）——服务端 delay_ms 是不是太长了？",
          i + 1, num_requests_);
        // 超时后请求仍挂在 client 内部（promise/future 没释放），必须手工清理，
        // 否则每超时一次就多占一块内存 —— 这是 async_send_request + 超时的标准收尾动作
        client_->remove_pending_request(future);
      } else {
        RCLCPP_ERROR(
          this->get_logger(), "第 %d/%d 次请求被中断（Ctrl-C 或 shutdown）", i + 1, num_requests_);
        client_->remove_pending_request(future);
        break;  // 已经要退出了，继续发请求没有意义
      }
    }

    const double total = (this->now() - t_start).seconds();
    RCLCPP_INFO(
      this->get_logger(), "结束：成功 %d/%d 个请求，总耗时 %.3f s", ok_count, num_requests_, total);
    RCLCPP_INFO(
      this->get_logger(),
      "注意：整个同步等待期间本节点只有一个线程，而它正卡在 spin_until_future_complete 里 —— "
      "期间收不到任何其他回调（定时器、订阅都停摆）。想看差异请跑 compute_sum_client_async。");
    return ok_count == num_requests_;
  }

private:
  rclcpp::Client<ComputeSum>::SharedPtr client_;
  int num_requests_ = 2;
  int values_count_ = 5;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // 本节点不需要"常驻 spin"：每次请求用 spin_until_future_complete 临时转一下执行器就够了，
  // 这也是同步客户端的典型形态（对比其他节点的 rclcpp::spin）
  auto node = std::make_shared<ComputeSumClient>();

  bool all_ok = false;
  if (node->wait_for_service(5s)) {
    all_ok = node->run();
  }

  rclcpp::shutdown();
  return all_ok ? 0 : 1;
}
