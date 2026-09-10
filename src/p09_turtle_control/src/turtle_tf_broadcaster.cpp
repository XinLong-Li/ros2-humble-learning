// turtle_tf_broadcaster.cpp —— 把乌龟位姿广播成 TF 变换
//
// turtlesim 自己不会发 TF，这个节点订阅 /turtle1/pose，
// 把位姿转成 world -> turtle1 的 TransformStamped 广播出去，
// rviz2 与 tf2 工具（tf2_echo / view_frames）就能看到乌龟了。
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "tf2/LinearMath/Quaternion.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_ros/transform_broadcaster.h"
#include "turtlesim/msg/pose.hpp"

class TurtleTfBroadcaster : public rclcpp::Node
{
public:
  TurtleTfBroadcaster()
  : Node("turtle_tf_broadcaster")
  {
    broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(this);

    // turtlesim 的 /turtle1/pose 用默认 QoS（reliable）发布，这里保持一致即可
    subscription_ = this->create_subscription<turtlesim::msg::Pose>(
      "/turtle1/pose", 10,
      std::bind(&TurtleTfBroadcaster::pose_callback, this, std::placeholders::_1));

    RCLCPP_INFO(this->get_logger(), "启动：等待 /turtle1/pose ...");
  }

private:
  void pose_callback(const turtlesim::msg::Pose::SharedPtr msg)
  {
    geometry_msgs::msg::TransformStamped t;

    t.header.stamp = this->now();
    t.header.frame_id = "world";      // 父帧：turtlesim 的世界坐标系
    t.child_frame_id = "turtle1";     // 子帧：乌龟本体

    // 乌龟是 2D 运动：z 恒为 0，姿态只有 yaw
    t.transform.translation.x = msg->x;
    t.transform.translation.y = msg->y;
    t.transform.translation.z = 0.0;

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, msg->theta);
    t.transform.rotation = tf2::toMsg(q);

    broadcaster_->sendTransform(t);
  }

  std::shared_ptr<tf2_ros::TransformBroadcaster> broadcaster_;
  rclcpp::Subscription<turtlesim::msg::Pose>::SharedPtr subscription_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<TurtleTfBroadcaster>());
  rclcpp::shutdown();
  return 0;
}
