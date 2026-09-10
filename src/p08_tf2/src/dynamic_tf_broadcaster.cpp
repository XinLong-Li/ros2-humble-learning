// dynamic_tf_broadcaster.cpp —— 广播两条动态变换 odom -> base_link -> sensor
//
// 什么是"动态变换"：两个坐标系之间的相对位姿随时间变化，必须按固定频率
// 持续广播。机器人在地图上移动，就是 odom -> base_link 在变。
//
// 本节点一次发布两条变换，形成整棵 TF 树：
//     map --(静态, static_tf_broadcaster)--> odom --(本节点)--> base_link --(本节点)--> sensor
//
// 与 ROS 1 的差异：
//   ROS 1:  tf::TransformBroadcaster br; br.sendTransform(tf::StampedTransform(...));
//   ROS 2:  tf2_ros::TransformBroadcaster，发 geometry_msgs/TransformStamped
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_ros/transform_broadcaster.h"

using namespace std::chrono_literals;

class DynamicTfBroadcaster : public rclcpp::Node
{
public:
  DynamicTfBroadcaster()
  : Node("dynamic_tf_broadcaster")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // 参数必须先 declare_parameter 再 get，否则运行时用 --ros-args -p rate:=50.0 改不了它
    rate_ = this->declare_parameter<double>("rate", 20.0);
    if (rate_ <= 0.0) {
      RCLCPP_WARN(
        this->get_logger(), "参数 rate=%.3f 非法（必须为正数），改用默认值 20.0 Hz", rate_);
      rate_ = 20.0;
    }
    dt_ = 1.0 / rate_;  // 单次回调推进的时间步长，即节点的"内部累计时间"的步长

    // 动态广播器发到 /tf（普通 QoS，不锁存）：只保证"当下"有效，
    // 所以订阅者必须在它运行期间持续接收，这也是要 20 Hz 高频发的原因。
    broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(this);

    // 两个变换共用一个定时器：同一次回调里发出的两条变换，时间戳严格一致，
    // TF 树在同一时刻才是自洽的（否则会看到子帧抖动）。
    const auto period = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(dt_));
    timer_ = this->create_wall_timer(
      period, std::bind(&DynamicTfBroadcaster::timer_callback, this));

    RCLCPP_INFO(
      this->get_logger(), "dynamic_tf_broadcaster 启动：以 %.1f Hz 广播 odom -> base_link -> sensor",
      rate_);
  }

private:
  void timer_callback()
  {
    // 用节点内部累计时间而不是系统时钟：步长恒等于 1/rate，仿真式的推进，
    // 不受定时器抖动影响，数学上更好复现。
    elapsed_ += dt_;

    // 同一个时间戳给两条变换用：TF 要求同一帧的消息时间戳一致
    const rclcpp::Time stamp = this->now();

    // ---- 变换 1：odom -> base_link（机器人本体在里程计系下的位姿）----
    // 模拟机器人一边前进一边自转：
    //   前进：x = 0.5 * t  （0.5 m/s 匀速直线运动）
    //   自转：yaw = 0.5 * t（0.5 rad/s 匀速旋转）
    geometry_msgs::msg::TransformStamped odom_to_base;
    odom_to_base.header.stamp = stamp;
    odom_to_base.header.frame_id = "odom";        // 父坐标系
    odom_to_base.child_frame_id = "base_link";    // 子坐标系

    odom_to_base.transform.translation.x = 0.5 * elapsed_;
    odom_to_base.transform.translation.y = 0.0;
    odom_to_base.transform.translation.z = 0.0;

    tf2::Quaternion q_base;
    q_base.setRPY(0.0, 0.0, 0.5 * elapsed_);  // (roll, pitch, yaw)
    odom_to_base.transform.rotation = tf2::toMsg(q_base);

    broadcaster_->sendTransform(odom_to_base);

    // ---- 变换 2：base_link -> sensor（传感器在机器人本体上的安装位姿）----
    // 安装位置固定 (0.3, 0.0, 0.2)，但安装支架会左右摆动：
    //   yaw = 0.5 * sin(2*pi*t/10)，幅度 0.5 rad，周期 10 s
    // 传感器"随机器人一起动"这个复合运动，就是靠 TF 树逐级相乘算出来的。
    geometry_msgs::msg::TransformStamped base_to_sensor;
    base_to_sensor.header.stamp = stamp;
    base_to_sensor.header.frame_id = "base_link";  // 父坐标系
    base_to_sensor.child_frame_id = "sensor";      // 子坐标系

    base_to_sensor.transform.translation.x = 0.3;
    base_to_sensor.transform.translation.y = 0.0;
    base_to_sensor.transform.translation.z = 0.2;

    tf2::Quaternion q_sensor;
    q_sensor.setRPY(0.0, 0.0, 0.5 * std::sin(2.0 * M_PI * elapsed_ / 10.0));
    base_to_sensor.transform.rotation = tf2::toMsg(q_sensor);

    broadcaster_->sendTransform(base_to_sensor);
  }

  double rate_ = 20.0;     // 广播频率（Hz），可由参数 rate 覆盖
  double dt_ = 0.05;       // 1/rate_
  double elapsed_ = 0.0;   // 节点内部累计时间（秒）
  std::unique_ptr<tf2_ros::TransformBroadcaster> broadcaster_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<DynamicTfBroadcaster>());
  rclcpp::shutdown();
  return 0;
}
