// frame_marker_publisher.cpp —— 在 rviz2 里把坐标系"画"出来
//
// rviz2 的 TF 显示插件只能画出坐标轴，本节点用 Marker 把机器人本体和
// sensor 坐标系的三根轴画得更醒目，直观看到 TF 树的复合运动。
//
// 关键概念：Marker 的 header.frame_id 决定"这个图形画在哪个坐标系里"。
// 本节点只负责在 sensor 系里画三根固定的轴，它自己不知道 sensor 在哪儿；
// rviz2 会用 TF 把 sensor 系里的图形变换到 Fixed Frame（本配置里是 map），
// 于是三根轴就自动跟着机器人一起动了 —— 这正是 TF 的价值。
//
// 与 ROS 1 的差异：几乎没变。visualization_msgs/Marker 的字段、
// Marker::ADD、lifetime 语义都与 ROS 1 一致，只是消息类型换成 msg 后缀。
#include <chrono>
#include <functional>
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "geometry_msgs/msg/point.hpp"
#include "visualization_msgs/msg/marker.hpp"
#include "visualization_msgs/msg/marker_array.hpp"

using namespace std::chrono_literals;

class FrameMarkerPublisher : public rclcpp::Node
{
public:
  FrameMarkerPublisher()
  : Node("frame_marker_publisher")  // 节点名与可执行文件名一致，是本仓库约定
  {
    // 话题名与 rviz2 里 MarkerArray 显示插件默认订阅的话题一致
    publisher_ = this->create_publisher<visualization_msgs::msg::MarkerArray>(
      "/visualization_marker_array", 10);

    // 5 Hz：Marker 不随时间变化，但要持续发，因为 rviz2 是"订阅到什么画什么"，
    // 而且新启动的 rviz2 需要最新的消息才能画出来。
    timer_ = this->create_wall_timer(
      200ms, std::bind(&FrameMarkerPublisher::timer_callback, this));

    RCLCPP_INFO(
      this->get_logger(),
      "frame_marker_publisher 启动：5 Hz 发布 1 个机器人方块 + 3 根 sensor 坐标轴到 "
      "/visualization_marker_array");
  }

private:
  void timer_callback()
  {
    // 所有 marker 共用同一个时间戳，rviz2 才能在同一时刻把它们拼到一起
    const rclcpp::Time stamp = this->now();

    visualization_msgs::msg::MarkerArray markers;
    markers.markers.push_back(make_robot_marker(stamp));
    // sensor 系的三根轴：X 红、Y 绿、Z 蓝（与 rviz2 自带坐标轴的配色一致）
    markers.markers.push_back(make_axis_marker(stamp, 1, 0.5, 0.0, 0.0, 1.0f, 0.0f, 0.0f));
    markers.markers.push_back(make_axis_marker(stamp, 2, 0.0, 0.5, 0.0, 0.0f, 1.0f, 0.0f));
    markers.markers.push_back(make_axis_marker(stamp, 3, 0.0, 0.0, 0.5, 0.0f, 0.0f, 1.0f));

    publisher_->publish(markers);
  }

  // 机器人本体：一个绿方块，画在 map 系的原地。
  // 它不会动，用来对照 sensor 坐标轴的复合运动。
  visualization_msgs::msg::Marker make_robot_marker(const rclcpp::Time & stamp)
  {
    visualization_msgs::msg::Marker marker;
    marker.header.stamp = stamp;
    marker.header.frame_id = "map";  // 画在 map 系里
    marker.ns = "robot";             // 同一 ns + id 唯一确定一个 marker
    marker.id = 0;
    marker.type = visualization_msgs::msg::Marker::CUBE;
    marker.action = visualization_msgs::msg::Marker::ADD;

    // 注意：geometry_msgs 的四元数默认值是 (0,0,0,0)，不是单位四元数！
    // 不显式写 w=1 的话，rviz2 拿到的姿态是退化的。
    marker.pose.orientation.w = 1.0;

    marker.scale.x = 0.3;  // 边长 0.3 的立方体
    marker.scale.y = 0.3;
    marker.scale.z = 0.3;

    marker.color.r = 0.0f;
    marker.color.g = 1.0f;
    marker.color.b = 0.0f;
    marker.color.a = 1.0f;  // a=0 是完全透明，别忘了设

    // lifetime 全 0 = 永久有效（rviz2 会一直画到收到 DELETE 或节点退出）
    marker.lifetime.sec = 0;
    marker.lifetime.nanosec = 0;

    return marker;
  }

  // 一根坐标轴：ARROW 类型的 marker，用 points 描述起点和终点。
  // 方向向量 (dx, dy, dz) 已经在 sensor 系里给出。
  visualization_msgs::msg::Marker make_axis_marker(
    const rclcpp::Time & stamp, int id, double dx, double dy, double dz,
    float r, float g, float b)
  {
    visualization_msgs::msg::Marker marker;
    marker.header.stamp = stamp;
    // 画在 sensor 系里：本节点不需要知道 sensor 在哪，rviz2 会用 TF 把它搬到 map 下
    marker.header.frame_id = "sensor";
    marker.ns = "sensor_axes";
    marker.id = id;
    marker.type = visualization_msgs::msg::Marker::ARROW;
    marker.action = visualization_msgs::msg::Marker::ADD;
    marker.pose.orientation.w = 1.0;  // 同上：必须显式给单位四元数

    // ARROW 的 scale 含义：x=轴长（有 points 时长度由 points 决定），
    // y=轴径，z=箭头头部直径
    marker.scale.x = 0.5;
    marker.scale.y = 0.02;
    marker.scale.z = 0.05;

    marker.color.r = r;
    marker.color.g = g;
    marker.color.b = b;
    marker.color.a = 1.0f;

    marker.lifetime.sec = 0;
    marker.lifetime.nanosec = 0;

    geometry_msgs::msg::Point start;
    start.x = 0.0;
    start.y = 0.0;
    start.z = 0.0;

    geometry_msgs::msg::Point end;
    end.x = dx;
    end.y = dy;
    end.z = dz;

    marker.points.push_back(start);
    marker.points.push_back(end);
    // 末端点再重复一遍：rviz2 用点的走向决定箭头朝向，最后一段"零长度"的重复点
    // 会让箭头尖收在终点上（这是 ARROW + points 的常用写法）
    marker.points.push_back(end);

    return marker;
  }

  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<FrameMarkerPublisher>());
  rclcpp::shutdown();
  return 0;
}
