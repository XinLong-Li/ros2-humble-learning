// status_publisher.cpp —— 用自定义消息类型发布机器人状态
//
// 与 ROS 1 的对照：
//   ROS 1:  ros::Publisher pub = nh.advertise<p02_interfaces::RobotStatus>("robot_status", 10);
//   ROS 2:  create_publisher<p02_interfaces::msg::RobotStatus>("/robot_status", 10)
//   区别一：消息类型多了 msg 命名空间，头文件从 .h 变成 .hpp（下划线小写文件名）
//   区别二：ROS 1 的 queue_size 只是"发送队列长度"，ROS 2 的 10 是 QoS depth（KeepLast 深度）
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "p02_interfaces/msg/robot_status.hpp"
#include "std_msgs/msg/header.hpp"

using namespace std::chrono_literals;

class StatusPublisher : public rclcpp::Node
{
public:
  StatusPublisher()
  : Node("status_publisher")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // 先 declare_parameter 再 get_parameter：Humble 里取未声明的参数会抛异常。
    // declare 的同时注册了默认值，于是命令行 `-p robot_name:=r2d2` 或 YAML 才能覆盖它
    robot_name_ = this->declare_parameter<std::string>("robot_name", "turtle_x");

    // 用绝对话题名 /robot_status：不受命名空间 remap 影响，方便对照 README 里的命令
    publisher_ = this->create_publisher<p02_interfaces::msg::RobotStatus>("/robot_status", 10);

    // ROS 1 常见写法是 ros::Rate(10) + while 循环；ROS 2 交给定时器，控制权回到执行器，
    // 同一个节点后续还能加订阅/服务而不互相阻塞（执行器在 p04 展开）
    timer_ = this->create_wall_timer(100ms, std::bind(&StatusPublisher::timer_callback, this));

    RCLCPP_INFO(
      this->get_logger(), "status_publisher 启动：10 Hz 发布 /robot_status，robot_name='%s'",
      robot_name_.c_str());
  }

private:
  void timer_callback()
  {
    auto msg = p02_interfaces::msg::RobotStatus();

    // Header 是 ROS 的公共字段：stamp 供下游做时间同步/TF 查询，frame_id 声明坐标在哪套坐标系下。
    // 注意 this->now() 取的是节点时钟（默认系统时钟），不是 ROS 1 的 ros::Time::now() 全局函数
    msg.header.stamp = this->now();
    msg.header.frame_id = "world";

    msg.robot_name = robot_name_;

    // 电量模拟：每 tick 掉 0.1%，跌破 20% 视为进站充电、瞬间充满 ——
    // 故意让循环很快走完（约 80 s 一轮），学习时不用等太久就能看到"充满"那一刻
    battery_ -= 0.1;
    if (battery_ < 20.0) {
      battery_ = 100.0;
    }
    msg.battery_percentage = battery_;
    // 正好 100.0 只出现在重置那一 tick，据此标记"充电中"（下一 tick 就变 99.9 了）
    msg.is_charging = (battery_ >= 100.0);

    // 位置模拟：半径 2 m 的圆上匀速运动，每 tick 转 0.2 rad（10 Hz × 0.2 = 2 rad/s，约 3.1 s 一圈）
    angle_ += 0.2;
    msg.pose.position.x = 2.0 * std::cos(angle_);
    msg.pose.position.y = 2.0 * std::sin(angle_);
    msg.pose.position.z = 0.0;
    // 姿态必须给四元数：单位四元数 (0,0,0,1) 表示"无旋转"。
    // 全 0 的四元数不是合法旋转，下游（如 TF2）会直接报错，所以这里显式赋值
    msg.pose.orientation.w = 1.0;

    publisher_->publish(msg);

    // 10 Hz 下每条都打日志会刷屏（还会拖慢终端渲染），每 10 条打一次足够观察趋势
    if (++published_count_ % 10 == 0) {
      RCLCPP_INFO(
        this->get_logger(), "已发布 %d 条，当前电量 %.1f%%", published_count_, battery_);
    }
  }

  std::string robot_name_;
  double battery_ = 100.0;
  double angle_ = 0.0;
  int published_count_ = 0;
  rclcpp::Publisher<p02_interfaces::msg::RobotStatus>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  // rclcpp::init 取代 ros::init；rclcpp::spin 取代 ros::spin
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StatusPublisher>());
  rclcpp::shutdown();
  return 0;
}
