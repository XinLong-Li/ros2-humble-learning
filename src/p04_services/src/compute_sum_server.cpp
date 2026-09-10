// compute_sum_server.cpp —— 服务端（单线程执行器版）
//
// 与 ROS 1 的对照：
//   ROS 1: ros::ServiceServer srv = nh.advertiseService("compute_sum", &cb, this);
//   ROS 2: this->create_service<p02_interfaces::srv::ComputeSum>("compute_sum", cb)
//   回调的返回值也不同：ROS 1 用 bool 表示"这次调用成功没有"，结果写进 srv.response；
//   ROS 2 的回调没有 bool 返回值（成败由抛不抛异常/写不写 response 决定），
//   拿到的是 rclcpp 造好的 std::shared_ptr<Response>，往里填就行。
//
// 本文件想讲清 ROS 2 的执行器（Executor）模型：
//   ROS 1 里 ros::spin() 怎么调度回调是"黑盒"（内部 CallbackQueue + 线程池）；
//   ROS 2 把它交还给用户，谁 spin、在哪个线程 spin、哪些回调能并发，全部显式可见。
//   这里的 rclcpp::spin(node) 等价于：
//     rclcpp::executors::SingleThreadedExecutor executor;
//     executor.add_node(node);
//     executor.spin();
//   整个节点只有一个线程，所有回调严格串行 —— 简单、无并发风险，
//   代价是耗时回调会把整节点堵住（见下）。
//
// 对照实验：
//   本节点（-p delay_ms:=3000）跑两个请求要 ≈ 6 s，且第二个请求会一直排队；
//   换成 compute_sum_server_mt.cpp 才能真正并发。

#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "p02_interfaces/srv/compute_sum.hpp"

class ComputeSumServer : public rclcpp::Node
{
public:
  ComputeSumServer()
  : Node("compute_sum_server")
  {
    // ROS 1 的 nh.getParam("delay_ms", v)：取不到就保持 v 的原值（隐式默认值）；
    // ROS 2 必须先 declare_parameter 声明名字、类型、默认值 —— 顺序不能反：
    //   先 declare 后 get 才正常，也才能被命令行 --ros-args -p 或 ros2 param set 修改。
    this->declare_parameter<int>("delay_ms", 1000);

    // 回调签名要能被当作
    //   void(std::shared_ptr<Request>, std::shared_ptr<Response>)
    // 调用 —— rclcpp 内部就是以这个类型（AnyServiceCallback 里的 std::function）保存回调的。
    // 本仓库统一按值传 shared_ptr：与 rclcpp 自己的类型逐字对应，最不容易出岔子
    // （写成 const 引用一般也能编过，但那要多绕一次 std::function 的转换，没必要）。
    service_ = this->create_service<p02_interfaces::srv::ComputeSum>(
      "compute_sum",
      [this](
        std::shared_ptr<p02_interfaces::srv::ComputeSum::Request> request,
        std::shared_ptr<p02_interfaces::srv::ComputeSum::Response> response)
      {
        this->handle_compute_sum(request, response);
      });

    RCLCPP_INFO(
      this->get_logger(), "compute_sum_server 已启动：服务 /compute_sum（SingleThreadedExecutor）");
    RCLCPP_INFO(
      this->get_logger(), "参数 delay_ms 默认 1000 ms；可用 --ros-args -p delay_ms:=3000 改成 3 秒");
  }

private:
  void handle_compute_sum(
    const std::shared_ptr<p02_interfaces::srv::ComputeSum::Request> request,
    std::shared_ptr<p02_interfaces::srv::ComputeSum::Response> response)
  {
    // 每次回调都重新读参数（而不是构造函数里读一次）：
    // 这样 ros2 param set /compute_sum_server delay_ms 200 之后，下一个请求立刻按新值走。
    const int64_t delay_ms = this->get_parameter("delay_ms").as_int();

    RCLCPP_INFO(
      this->get_logger(), "收到请求：%zu 个元素，模拟耗时计算 %lld ms ...",
      request->values.size(), static_cast<long long>(delay_ms));

    // 用睡眠模拟"耗时计算"。这里刻意用 rclcpp::sleep_for 而不是 std::this_thread::sleep_for：
    // rclcpp 版本会被 shutdown()/Ctrl-C 提前唤醒（返回 false），退出时不用干等。
    // 关键认识：睡的是执行器唯一的工作线程 —— 这一觉睡下去，节点什么回调都处理不了：
    // 第二个请求只能在队列里排队，参数服务、日志、其他订阅回调也全部停摆。
    rclcpp::sleep_for(std::chrono::milliseconds(delay_ms));

    int64_t sum = 0;
    for (const int64_t value : request->values) {
      sum += value;
    }
    const int32_t count = static_cast<int32_t>(request->values.size());

    response->sum = sum;
    response->count = count;
    // 空数组时 sum/count 是 0/0 = NaN，客户端打印出来很难解释，这里约定 average = 0.0
    response->average = (count == 0) ? 0.0 : static_cast<double>(sum) / static_cast<double>(count);

    RCLCPP_INFO(this->get_logger(), "请求数据：[%s]", format_values(request->values).c_str());
    RCLCPP_INFO(
      this->get_logger(), "计算结果：sum=%lld, average=%.2f, count=%d",
      static_cast<long long>(response->sum), response->average, static_cast<int>(response->count));
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

  rclcpp::Service<p02_interfaces::srv::ComputeSum>::SharedPtr service_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  // rclcpp::spin(node) ≈ SingleThreadedExecutor + add_node + spin：一个线程串行跑完所有回调。
  // 想换并发调度只改 main 这一处，节点代码一行不用动（对比 compute_sum_server_mt.cpp）。
  rclcpp::spin(std::make_shared<ComputeSumServer>());

  rclcpp::shutdown();
  return 0;
}
