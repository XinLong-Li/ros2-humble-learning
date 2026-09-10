// tf_listener.cpp —— 查询 TF 树：反向查询、点变换、以及"外推错误"演示
//
// TF 的查询接口只有两个核心动作：
//   1) lookupTransform(target, source, time)：拿到 target <- source 的变换
//   2) buffer.transform(带 frame_id 的消息, target)：把点/位姿换到另一个坐标系
// 本节点 1 Hz 做三件事，把这两件事和最常见的报错都演示一遍。
//
// 与 ROS 1 的差异（ROS 1 的 tf::TransformListener 在 ROS 2 里被拆成两个对象）：
//   ROS 1:  tf::TransformListener listener;  listener.lookupTransform(...);
//   ROS 2:  tf2_ros::Buffer（存数据） + tf2_ros::TransformListener（把 /tf 灌进 Buffer）
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/point_stamped.hpp"
#include "tf2/utils.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

using namespace std::chrono_literals;

class TfListener : public rclcpp::Node
{
public:
  TfListener()
  : Node("tf_listener"),         // 节点名与可执行文件名一致，是本仓库约定
    // Buffer 需要一个时钟：用节点自己的时钟，时间戳才和消息里的对得上。
    // Buffer 是"TF 数据库"，默认缓存最近 10 秒的变换。
    buffer_(this->get_clock()),
    // TransformListener 负责订阅 /tf 与 /tf_static 并写进 Buffer。
    // 成员声明顺序必须是 buffer_ 在前、listener_ 在后：listener_ 构造时要用 buffer_ 的引用。
    // 这里把节点也传进去，好处是 ros2 node info /tf_listener 里能直接看到 /tf 订阅；
    // 默认 spin_thread=true，listener 用自己的线程处理 /tf，不占用本节点的执行器。
    listener_(buffer_, this)
  {
    wait_for_tf_ready();

    // 1 Hz：TF 查询是"拉"模式，想要多快就查多快，跟广播频率无关
    timer_ = this->create_wall_timer(1s, std::bind(&TfListener::timer_callback, this));

    RCLCPP_INFO(
      this->get_logger(), "tf_listener 启动：1 Hz 查询 sensor <- map，并演示外推（extrapolation）报错");
  }

private:
  // 启动时等待 TF 就绪。
  // 为什么要等：三个节点谁先起来不确定，TF 树要"长齐"（map->odom->base_link->sensor）
  // 才能查询，否则第一次查询必然抛异常。
  // 注意 Humble 的 API 变化：tf2_ros::Buffer::waitForTransform 在 Humble 里已经变成
  // 基于 future 的异步版本（且需要先注册 CreateTimerInterface），没有 ROS 1 那种直接
  // 返回 bool 的阻塞版本，所以这里用 canTransform 轮询实现"最多等 2 秒"的等价语义。
  void wait_for_tf_ready()
  {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    bool ready = false;
    while (std::chrono::steady_clock::now() < deadline) {
      // 只要 TF 树里 sensor 与 map 连通（能互相查到）就算就绪
      if (buffer_.canTransform("sensor", "map", tf2::TimePointZero)) {
        ready = true;
        break;
      }
      std::this_thread::sleep_for(100ms);
    }

    if (ready) {
      RCLCPP_INFO(this->get_logger(), "TF 已就绪：sensor <-> map 连通");
    } else {
      // 只警告、不退出：广播节点可能是稍后才启动的，定时器里的查询会自动恢复
      RCLCPP_WARN(
        this->get_logger(),
        "等待 2 秒仍未收到 sensor <-> map 的 TF，请检查 static_tf_broadcaster 与 "
        "dynamic_tf_broadcaster 是否已启动；本节点继续运行并会不断重试");
    }
  }

  void timer_callback()
  {
    lookup_reverse();
    transform_point();
    lookup_future();
  }

  // ---- 第 1 件事：反向查询 sensor <- map ----
  // TF 树是双向可查的：我们广播的是 map->odom->base_link->sensor（父->子），
  // 但查询时可以反过来问"sensor 在 map 下什么样"，tf2 会自动逐级求逆再相乘。
  void lookup_reverse()
  {
    try {
      // tf2::TimePointZero 的含义是"给我最新的那条，不限定时间"
      // （等价于 ROS 1 的 ros::Time(0)）。
      // 注意参数顺序是 (target_frame, source_frame)，别和广播时的父子顺序搞混。
      const auto tf = buffer_.lookupTransform("sensor", "map", tf2::TimePointZero);

      RCLCPP_INFO(
        this->get_logger(),
        "[1] sensor <- map: x=%.3f y=%.3f z=%.3f yaw=%.3f rad",
        tf.transform.translation.x, tf.transform.translation.y, tf.transform.translation.z,
        tf2::getYaw(tf.transform.rotation));
    } catch (const tf2::TransformException & ex) {
      // TransformException 是所有 TF 异常的基类：
      // LookupException（帧不存在）、ConnectivityException（两帧不连通）、
      // ExtrapolationException（时间超出缓存范围）、InvalidArgumentException
      RCLCPP_WARN(this->get_logger(), "[1] 查询失败: %s", ex.what());
    }
  }

  // ---- 第 2 件事：把 map 系下的一个点变到 sensor 系 ----
  // 这是实际工程里最常用的操作：传感器数据要换到机器人/地图坐标系下方可使用。
  void transform_point()
  {
    try {
      geometry_msgs::msg::PointStamped point_map;
      point_map.header.frame_id = "map";
      // 消息里的时间戳用"零时刻"表示"最新可用"，与上面的 tf2::TimePointZero 对应。
      // 转换函数 tf2_ros::toMsg(tf2::TimePoint) 定义在 tf2_ros/buffer_interface.hpp 里
      // （包含 tf2_ros/buffer.h 即可用），不在 tf2_geometry_msgs 中。
      point_map.header.stamp = tf2_ros::toMsg(tf2::TimePointZero);
      point_map.point.x = 1.0;
      point_map.point.y = 1.0;
      point_map.point.z = 0.0;

      // buffer.transform 内部做的正是 lookupTransform + 应用变换；能对 PointStamped 这么做，
      // 是因为 tf2_geometry_msgs 提供了它的 doTransform 特化（这也是必须依赖它的原因）。
      const auto point_sensor = buffer_.transform(point_map, "sensor");

      RCLCPP_INFO(
        this->get_logger(),
        "[2] map 下的点 (1.000, 1.000, 0.000) 变到 sensor 系: (%.3f, %.3f, %.3f)",
        point_sensor.point.x, point_sensor.point.y, point_sensor.point.z);
    } catch (const tf2::TransformException & ex) {
      RCLCPP_WARN(this->get_logger(), "[2] 点变换失败: %s", ex.what());
    }
  }

  // ---- 第 3 件事：故意查询"未来"的变换，制造经典的外推（extrapolation）错误 ----
  // TF 只能回答"已经发生过的时刻"：缓冲里存的是历史数据，未来的位姿谁也不知道。
  // 查询 now() + 10s 时，tf2 发现请求时刻晚于缓冲区里最新数据的时间戳，
  // 于是抛出 ExtrapolationException。
  void lookup_future()
  {
    try {
      // 这个调用注定失败，只为把异常内容打印出来给读者看
      const auto tf = buffer_.lookupTransform("sensor", "map", this->now() + 10s);
      RCLCPP_INFO(this->get_logger(), "[3] 未预期地成功: x=%.3f", tf.transform.translation.x);
    } catch (const tf2::TransformException & ex) {
      RCLCPP_WARN(
        this->get_logger(),
        "[3] 预期中的失败（外推）: %s。原因是请求时刻在未来，缓冲区里没有（也不可能有）那一刻的数据",
        ex.what());
    }
  }

  // 声明顺序决定初始化顺序：buffer_ 必须先于 listener_ 构造
  tf2_ros::Buffer buffer_;
  tf2_ros::TransformListener listener_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TfListener>());
  rclcpp::shutdown();
  return 0;
}
