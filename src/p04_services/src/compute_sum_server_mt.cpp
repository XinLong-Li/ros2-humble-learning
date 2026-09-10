// compute_sum_server_mt.cpp —— 服务端（多线程执行器 + 可重入回调组）
//
// 与 compute_sum_server.cpp 相比，业务代码（求和）一字不差，**只有调度层变了**：
//   1. 服务放进 ReentrantCallbackGroup（可重入回调组）
//   2. main 改用 MultiThreadedExecutor（默认线程数 = CPU 核数）
//
// 为什么两处都得改？这是 ROS 2 最容易踩的坑之一：
//   ● CallbackGroup 决定"哪些回调允许并发"：
//       MutuallyExclusive（节点默认）：组内回调永远一个一个来，前一个没跑完后一个不开始；
//       Reentrant                  ：组内回调可以同时跑在多个线程上（甚至同一个回调重入）。
//   ● Executor 决定"有几个线程去跑回调"：
//       SingleThreadedExecutor：只有 1 个线程，回调组再开放也没人能并行；
//       MultiThreadedExecutor ：N 个线程，但只会去抢"允许并发"的回调组。
//   结论：Reentrant 组 + MultiThreadedExecutor 才等于真并发。
//   ★ 只把 spin 换成多线程、不管回调组，请求仍会串行 —— 请务必亲手验证一次。
//
// 并且要注意：单线程版里"服务回调阻塞期间参数变更/其他回调也停摆"的现象，
// 在本文件里消失了 —— 只要不是同一个互斥组，睡在服务回调里的线程不影响别的线程干活。
//
// 对照实验（详 README）：
//   起本节点 -p delay_ms:=3000，再跑 compute_sum_client_async -p num_requests:=2
//   → 总耗时约 3 s（两个请求真并行）；换成 compute_sum_server 则约 6 s（串行）。
//   注意别用同步客户端 compute_sum_client 做这个实验：它"发一个等一个"，
//   任一时刻只有一个请求在飞，服务端再能并行也用不上。

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "p02_interfaces/srv/compute_sum.hpp"

class ComputeSumServerMt : public rclcpp::Node
{
public:
  ComputeSumServerMt()
  : Node("compute_sum_server_mt")
  {
    this->declare_parameter<int>("delay_ms", 1000);

    // 创建可重入回调组：允许组内回调（这里是服务回调）被多个线程同时执行。
    // create_service 的参数顺序是 (服务名, 回调, QoS, 回调组)：
    //   第三个参数 rmw_qos_profile_services_default 是服务固定的默认 QoS，
    //   想传回调组就必须把它显式写出来（不能只写前两个参数 + 回调组）。
    callback_group_ = this->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

    service_ = this->create_service<p02_interfaces::srv::ComputeSum>(
      "compute_sum",
      [this](
        std::shared_ptr<p02_interfaces::srv::ComputeSum::Request> request,
        std::shared_ptr<p02_interfaces::srv::ComputeSum::Response> response)
      {
        this->handle_compute_sum(request, response);
      },
      rmw_qos_profile_services_default,
      callback_group_);

    RCLCPP_INFO(
      this->get_logger(),
      "compute_sum_server_mt 已启动：服务 /compute_sum（ReentrantCallbackGroup + MultiThreadedExecutor）");
    RCLCPP_INFO(
      this->get_logger(), "参数 delay_ms 默认 1000 ms；可用 --ros-args -p delay_ms:=3000 改成 3 秒");
  }

private:
  void handle_compute_sum(
    const std::shared_ptr<p02_interfaces::srv::ComputeSum::Request> request,
    std::shared_ptr<p02_interfaces::srv::ComputeSum::Response> response)
  {
    const int64_t delay_ms = this->get_parameter("delay_ms").as_int();

    // 多线程下共享变量必须同步：若用普通 int，++active_requests_ 的"读-改-写"
    // 会被其他线程穿插，计数就错了（这行注释本身就是本文件的教学点之一）。
    const int active = ++active_requests_;
    const int index = ++served_requests_;

    // 打印线程 id 是"确实并发"的最硬证据：两个请求的 id 一定不同
    std::ostringstream thread_id;
    thread_id << std::this_thread::get_id();

    RCLCPP_INFO(
      this->get_logger(), "收到第 %d 个请求：%zu 个元素，开始 %lld ms 计算（线程 %s，并发中 %d 个）",
      index, request->values.size(), static_cast<long long>(delay_ms),
      thread_id.str().c_str(), active);

    // 这里"睡"的只是执行器线程池里的其中一个线程，其他线程照常处理别的请求
    rclcpp::sleep_for(std::chrono::milliseconds(delay_ms));

    int64_t sum = 0;
    for (const int64_t value : request->values) {
      sum += value;
    }
    const int32_t count = static_cast<int32_t>(request->values.size());

    response->sum = sum;
    response->count = count;
    // 空数组时 0/0 = NaN，这里约定 average = 0.0
    response->average = (count == 0) ? 0.0 : static_cast<double>(sum) / static_cast<double>(count);

    const int remaining = --active_requests_;

    RCLCPP_INFO(this->get_logger(), "第 %d 个请求的数据：[%s]", index, format_values(request->values).c_str());
    RCLCPP_INFO(
      this->get_logger(), "第 %d 个请求完成：sum=%lld, average=%.2f, count=%d（线程 %s，还剩 %d 个在算）",
      index, static_cast<long long>(response->sum), response->average,
      static_cast<int>(response->count), thread_id.str().c_str(), remaining);
  }

  // 把数组拼成 "[1, 2, 3]" 便于日志阅读
  static std::string format_values(const std::vector<int64_t> & values)
  {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
      if (i != 0) {
        oss << ", ";
      }
      oss << values[i];
    }
    return oss.str();
  }

  rclcpp::CallbackGroup::SharedPtr callback_group_;
  rclcpp::Service<p02_interfaces::srv::ComputeSum>::SharedPtr service_;

  // 多线程下被并发读写的计数器：必须用 atomic（或加锁）
  std::atomic<int> active_requests_{0};   // 当前正在计算的请求数
  std::atomic<int> served_requests_{0};   // 累计收到过多少个请求
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // 用 shared_ptr 持有节点：Executor 里保存的是节点的弱引用/指针，
  // 节点必须比 executor.spin() 活得久（这里一直活到 main 结束）
  auto node = std::make_shared<ComputeSumServerMt>();

  // MultiThreadedExecutor 的第二个参数是线程数，默认 0 = 用满 CPU 核数
  // （内部取 std::thread::hardware_concurrency()，至少 1）。
  // 想固定线程数就先构造 ExecutorOptions 再传第二个参数：
  //   rclcpp::ExecutorOptions options;                       // 里面可指定 context 等
  //   rclcpp::executors::MultiThreadedExecutor executor(options, 4);
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);

  RCLCPP_INFO(
    node->get_logger(), "MultiThreadedExecutor 已启动，工作线程数：%zu", executor.get_number_of_threads());

  executor.spin();

  rclcpp::shutdown();
  return 0;
}
