// static_tf_broadcaster.cpp —— 广播静态变换 map -> odom
//
// 什么是"静态变换"：两个坐标系之间的相对位姿永远不变。典型场景是把
// 传感器/雷达的安装位置、多传感器之间的固定外参写死在 TF 树里。
// 本包用它来定义 map 与 odom 的固定偏移（真实系统里这个偏移通常来自
// 定位模块对"地图原点"的修正，这里简化为常量）。
//
// 与 ROS 1 的差异：
//   ROS 1:  tf::TransformBroadcaster br;  br.sendTransform(tf::StampedTransform(...));
//   ROS 2:  tf2_ros::StaticTransformBroadcaster / TransformBroadcaster，
//           发的是 geometry_msgs/TransformStamped（不再有 tf::StampedTransform）
#include <chrono>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_ros/static_transform_broadcaster.h"

using namespace std::chrono_literals;

class StaticTfBroadcaster : public rclcpp::Node
{
public:
  StaticTfBroadcaster()
  : Node("static_tf_broadcaster")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // StaticTransformBroadcaster 内部把消息发到 /tf_static，QoS 为 transient_local
    // （相当于 ROS 1 的 latch=true）：即使订阅者在消息发出之后才启动，也能收到这条变换。
    // 这也是 /tf 与 /tf_static 最重要的区别。
    broadcaster_ = std::make_unique<tf2_ros::StaticTransformBroadcaster>(this);

    // 先发一次：让"先启动广播器、后启动 rviz2/tf2_echo"的顺序也能立刻拿到数据
    publish_static_transform();

    // 静态变换的内容永远不变，本来只发一次就够了；这里故意用 1 Hz 重复发布，
    // 是为了演示"静态变换允许重复发布，只要内容相同即可"——
    // 打开 rviz2 可以看到 /tf_static 在持续刷新，而 TF 树不会因此抖动。
    timer_ = this->create_wall_timer(
      1s, std::bind(&StaticTfBroadcaster::publish_static_transform, this));

    RCLCPP_INFO(
      this->get_logger(),
      "static_tf_broadcaster 启动：以 1 Hz 重复广播静态变换 map -> odom（平移 1.0, 0.5, 0.0，无旋转）");
  }

private:
  void publish_static_transform()
  {
    geometry_msgs::msg::TransformStamped tf;

    // 时间戳：静态变换的时间戳在 TF 缓冲里会被忽略（它在所有时刻都有效），
    // 但字段必须填，否则消息不合法。约定俗成填当前时间。
    tf.header.stamp = this->now();

    // 一句话记住方向：header.frame_id 是父坐标系，child_frame_id 是子坐标系。
    // 这条变换表示"odom 的原点，在 map 下的位姿"。
    tf.header.frame_id = "map";
    tf.child_frame_id = "odom";

    tf.transform.translation.x = 1.0;
    tf.transform.translation.y = 0.5;
    tf.transform.translation.z = 0.0;

    // 旋转必须用四元数表达（ROS 里不用欧拉角传变换，避免万向节死锁与旋转顺序歧义）。
    // tf2::Quaternion + setRPY 的写法与 ROS 1 的 tf::Quaternion 完全一致。
    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, 0.0);  // roll, pitch, yaw —— 无旋转
    tf.transform.rotation = tf2::toMsg(q);

    broadcaster_->sendTransform(tf);
  }

  std::unique_ptr<tf2_ros::StaticTransformBroadcaster> broadcaster_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StaticTfBroadcaster>());
  rclcpp::shutdown();
  return 0;
}
