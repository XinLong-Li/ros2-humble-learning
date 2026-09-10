// turtle_service_client.cpp —— 顺序调用 turtlesim 的三个服务
//
// /spawn（生成乌龟）→ /turtle1/set_pen（换画笔）→ /kill（杀掉乌龟）。
// 调用"外部节点"的服务是机器人系统常见的分工方式：控制节点负责决策，
// 平台节点（turtlesim）提供能力。对照 ROS 1：rosservice call 的 C++ 版。
#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "turtlesim/srv/kill.hpp"
#include "turtlesim/srv/set_pen.hpp"
#include "turtlesim/srv/spawn.hpp"

using namespace std::chrono_literals;

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("turtle_service_client");

  // ---- 1) /spawn：在 (5,5) 生成第二只乌龟 ----
  auto spawn_client = node->create_client<turtlesim::srv::Spawn>("/spawn");
  if (!spawn_client->wait_for_service(5s)) {
    RCLCPP_ERROR(node->get_logger(), "/spawn 服务不可用——turtlesim_node 在跑吗？");
    rclcpp::shutdown();
    return 1;
  }
  auto spawn_req = std::make_shared<turtlesim::srv::Spawn::Request>();
  spawn_req->x = 5.0;
  spawn_req->y = 5.0;
  spawn_req->theta = 0.0;
  spawn_req->name = "turtle2";
  // async_send_request + spin_until_future_complete = ROS 2 的"同步调用"惯用法
  auto spawn_future = spawn_client->async_send_request(spawn_req);
  if (rclcpp::spin_until_future_complete(node, spawn_future, 5s) ==
    rclcpp::FutureReturnCode::SUCCESS)
  {
    RCLCPP_INFO(node->get_logger(), "生成成功: '%s'", spawn_future.get()->name.c_str());
  } else {
    RCLCPP_ERROR(node->get_logger(), "生成失败或超时");
  }

  // ---- 2) /turtle1/set_pen：给 turtle1 换红色粗画笔 ----
  auto pen_client = node->create_client<turtlesim::srv::SetPen>("/turtle1/set_pen");
  if (pen_client->wait_for_service(5s)) {
    auto pen_req = std::make_shared<turtlesim::srv::SetPen::Request>();
    pen_req->r = 255;
    pen_req->g = 0;
    pen_req->b = 0;
    pen_req->width = 5;
    pen_req->off = 0;
    auto pen_future = pen_client->async_send_request(pen_req);
    rclcpp::spin_until_future_complete(node, pen_future, 5s);
    RCLCPP_INFO(node->get_logger(), "turtle1 画笔已改为红色（宽度 5）");
  }

  // ---- 3) /kill：杀掉 turtle2 ----
  auto kill_client = node->create_client<turtlesim::srv::Kill>("/kill");
  if (kill_client->wait_for_service(5s)) {
    auto kill_req = std::make_shared<turtlesim::srv::Kill::Request>();
    kill_req->name = "turtle2";
    auto kill_future = kill_client->async_send_request(kill_req);
    rclcpp::spin_until_future_complete(node, kill_future, 5s);
    RCLCPP_INFO(node->get_logger(), "turtle2 已被移除");
  }

  RCLCPP_INFO(node->get_logger(), "演示完毕");
  rclcpp::shutdown();
  return 0;
}
