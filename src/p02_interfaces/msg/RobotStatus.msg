# 机器人状态（演示：嵌套标准消息 + Header，语法与 ROS 1 几乎一致）
std_msgs/Header header          # 时间戳与 frame_id
string robot_name               # 机器人名字
float64 battery_percentage      # 电量百分比 0~100
geometry_msgs/Pose pose         # 位姿（嵌套 geometry_msgs 类型）
bool is_charging                # 是否充电中
