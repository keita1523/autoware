#include <decision_maker_node.hpp>

namespace decision_maker
{
void DecisionMakerNode::updateWaitEngageState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::entryDriveState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateDriveState(cstring_t& state_name, int status)
{
  if (isArrivedGoal())
  {
    tryNextState("arrived_goal");
    return;
  }

  if (current_status_.closest_waypoint == -1)
  {
    publishOperatorHelpMessage("The vehicle passed last waypoint or waypoint does not exist near the vehicle.");
    tryNextState("mission_aborted");
    return;
  }

  if (isVehicleOnLaneArea())
  {
    tryNextState("on_lane_area");
  }
  else
  {
    tryNextState("on_free_area");
  }
}

uint8_t DecisionMakerNode::getSteeringStateFromWaypoint(void)
{
  static const double distance_to_target = param_num_of_steer_behind_;
  static const size_t ignore_idx = 0;

  double distance = 0.0;
  geometry_msgs::Pose prev_pose = current_status_.pose;
  uint8_t state = 0;

  if (ignore_idx > current_status_.finalwaypoints.waypoints.size())
  {
    return 0;
  }

  for (unsigned int idx = 0; idx < current_status_.finalwaypoints.waypoints.size() - 1; idx++)
  {
    distance += amathutils::find_distance(prev_pose, current_status_.finalwaypoints.waypoints.at(idx).pose.pose);

    state = current_status_.finalwaypoints.waypoints.at(idx).wpstate.steering_state;

    if (state && (state != autoware_msgs::WaypointState::STR_STRAIGHT || distance >= distance_to_target))
    {
      break;
    }
    prev_pose = current_status_.finalwaypoints.waypoints.at(idx).pose.pose;
  }
  return state;
}
uint8_t DecisionMakerNode::getEventStateFromWaypoint(void)
{
  static const double distance_to_target = param_num_of_steer_behind_;
  static const size_t ignore_idx = 0;

  double distance = 0.0;
  geometry_msgs::Pose prev_pose = current_status_.pose;
  uint8_t state = 0;

  if (ignore_idx > current_status_.finalwaypoints.waypoints.size())
  {
    return 0;
  }

  for (unsigned int idx = 0; idx < current_status_.finalwaypoints.waypoints.size() - 1; idx++)
  {
    distance += amathutils::find_distance(prev_pose, current_status_.finalwaypoints.waypoints.at(idx).pose.pose);

    state = current_status_.finalwaypoints.waypoints.at(idx).wpstate.event_state;

    if (state && distance >= distance_to_target)
    {
      break;
    }
    prev_pose = current_status_.finalwaypoints.waypoints.at(idx).pose.pose;
  }
  return state;
}
std::pair<uint8_t, int> DecisionMakerNode::getStopSignStateFromWaypoint(void)
{
  static const size_t ignore_idx = 0;
  static const double mu = 0.7;  // dry ground/ asphalt/ normal tire
  static const double g = 9.80665;
  static const double margin = 5;
  static const double reaction_time = 0.3 + margin;  // system delay(sec)
  static const size_t reset_count = 20;              //
  const double velocity = amathutils::kmph2mps(current_status_.velocity);

  const double free_running_distance = reaction_time * velocity;
  const double braking_distance = velocity * velocity / (2 * g * mu);
  const double distance_to_target = (free_running_distance + braking_distance) * 2 /* safety margin*/;

  std::pair<uint8_t, int> ret(0, -1);

  double distance = 0.0;
  geometry_msgs::Pose prev_pose = current_status_.pose;
  uint8_t state = 0;

  if (ignore_idx > current_status_.finalwaypoints.waypoints.size() ||
      3 > current_status_.finalwaypoints.waypoints.size())
  {
    return ret;
  }

  // reset previous stop
  if (current_status_.finalwaypoints.waypoints.at(1).gid > current_status_.prev_stopped_wpidx ||
      (unsigned int)(current_status_.prev_stopped_wpidx - current_status_.finalwaypoints.waypoints.at(1).gid) >
          reset_count)
  {
    current_status_.prev_stopped_wpidx = -1;
  }

  for (unsigned int idx = 0; idx < current_status_.finalwaypoints.waypoints.size() - 1; idx++)
  {
    distance += amathutils::find_distance(prev_pose, current_status_.finalwaypoints.waypoints.at(idx).pose.pose);
    state = current_status_.finalwaypoints.waypoints.at(idx).wpstate.stop_state;

    if (state)
    {
      if (current_status_.prev_stopped_wpidx != current_status_.finalwaypoints.waypoints.at(idx).gid)
      {
        ret.first = state;
        ret.second = current_status_.finalwaypoints.waypoints.at(idx).gid;
        break;
      }
    }

    if (distance > distance_to_target)
    {
      break;
    }

    prev_pose = current_status_.finalwaypoints.waypoints.at(idx).pose.pose;
  }
  return ret;
}
void DecisionMakerNode::updateLaneAreaState(cstring_t& state_name, int status)
{
  if (current_status_.finalwaypoints.waypoints.empty())
  {
    ROS_WARN("\"/final_waypoints.\" is not contain waypoints");
    return;
  }

  switch (getEventStateFromWaypoint())
  {
    case autoware_msgs::WaypointState::TYPE_EVENT_BUS_STOP:
      tryNextState("on_bus_stop");
      break;
    default:
      tryNextState("on_cruise");
      break;
  }
}

void DecisionMakerNode::updateCruiseState(cstring_t& state_name, int status)
{
  if (isEventFlagTrue("received_back_state_waypoint"))
  {
    tryNextState("on_back");
    return;
  }

  if (current_status_.change_flag == enumToInteger<E_ChangeFlags>(E_ChangeFlags::STRAIGHT))
  {
    switch (getSteeringStateFromWaypoint())
    {
      case autoware_msgs::WaypointState::STR_LEFT:
        tryNextState("on_left_turn");
        break;
      case autoware_msgs::WaypointState::STR_RIGHT:
        tryNextState("on_right_turn");
        break;
      case autoware_msgs::WaypointState::STR_STRAIGHT:
        tryNextState("on_straight");
        break;
      default:
        break;
    }
  }
  else
  {
    if (current_status_.change_flag == enumToInteger<E_ChangeFlags>(E_ChangeFlags::LEFT))
    {
      tryNextState("lane_change_left");
    }
    else if (current_status_.change_flag == enumToInteger<E_ChangeFlags>(E_ChangeFlags::RIGHT))
    {
      tryNextState("lane_change_right");
    }
  }
}

void DecisionMakerNode::updateFreeAreaState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateBusStopState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updatePullOverState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_LEFT);
}

void DecisionMakerNode::updatePullOutState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_RIGHT);
}

void DecisionMakerNode::entryTurnState(cstring_t& state_name, int status)
{
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
    tryNextState("found_stopline");
    return;
  }
  tryNextState("clear");
}

void DecisionMakerNode::updateLeftTurnState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_LEFT);
}

void DecisionMakerNode::updateStraightState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_CLEAR);
}

void DecisionMakerNode::updateRightTurnState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_RIGHT);
}

void DecisionMakerNode::entryLaneChangeState(cstring_t& state_name, int status)
{
  tryNextState("check_target_lane");
}
void DecisionMakerNode::updateLeftLaneChangeState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_LEFT);
}
void DecisionMakerNode::updateRightLaneChangeState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_RIGHT);
}

void DecisionMakerNode::updateCheckLeftLaneState(cstring_t& state_name, int status)
{
  /* need safety check function */
  // static bool is_target_lane_safe = false;
}

void DecisionMakerNode::updateCheckRightLaneState(cstring_t& state_name, int status)
{
  /* need safety check function */
  // static bool is_target_lane_safe = false;
}

void DecisionMakerNode::updateChangeToLeftState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateChangeToRightState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateBackState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_CLEAR);
}

void DecisionMakerNode::updateParkingState(cstring_t& state_name, int status)
{
  publishLampCmd(E_Lamp::LAMP_HAZARD);
}

void DecisionMakerNode::updateGoState(cstring_t& state_name, int status)
{
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
    tryNextState("found_stopline");
  }

  static double stopped_time;
  const double avoiding_timer = 3.0;  // threshold time[s]
  if (current_status_.velocity == 0.0 && current_status_.obstacle_waypoint != -1)
  {
    if (ros::Time::now().toSec() - stopped_time > avoiding_timer)
    {
      tryNextState("completely_stopped");
    }
  }
  else
  {
    stopped_time = ros::Time::now().toSec();
  }
}

void DecisionMakerNode::updateWaitState(cstring_t& state_name, int status)
{
  /* clear,*/
  publishStoplineWaypointIdx(current_status_.closest_waypoint + 1);
}

void DecisionMakerNode::updateStopState(cstring_t& state_name, int status)
{
  publishStoplineWaypointIdx(current_status_.closest_waypoint + 1);
}

void DecisionMakerNode::updateStoplineState(cstring_t& state_name, int status)
{
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
  }

  publishStoplineWaypointIdx(current_status_.found_stopsign_idx);
  /* wait for clearing risk*/

  static bool timerflag = false;
  static ros::Timer stopping_timer;

  if (current_status_.velocity == 0.0 && !timerflag)
  {
    stopping_timer = nh_.createTimer(ros::Duration(0.5),
                                     [&](const ros::TimerEvent&) {
                                       timerflag = false;
                                       current_status_.prev_stopped_wpidx = current_status_.found_stopsign_idx;
                                       tryNextState("clear");
                                       /*if found risk,
                                        * tryNextState("found_risk");*/
                                     },
                                     this, true);
    timerflag = true;
  }
}
void DecisionMakerNode::exitStopState(cstring_t& state_name, int status)
{
  std::pair<uint8_t, int> get_stopsign = getStopSignStateFromWaypoint();
  if (get_stopsign.first != 0)
  {
    current_status_.found_stopsign_idx = get_stopsign.second;
  }
  else
  {
    current_status_.found_stopsign_idx = -1;
    publishStoplineWaypointIdx(current_status_.found_stopsign_idx);
  }
}

void DecisionMakerNode::entryTryAvoidanceState(cstring_t& state_name, int status)
{
}
void DecisionMakerNode::updateTryAvoidanceState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::entryCheckAvoidanceState(cstring_t& state_name, int status)
{
}
void DecisionMakerNode::updateCheckAvoidanceState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::entryAvoidanceState(cstring_t& state_name, int status)
{
}
void DecisionMakerNode::updateAvoidanceState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::entryReturnToLaneState(cstring_t& state_name, int status)
{
}
void DecisionMakerNode::updateReturnToLaneState(cstring_t& state_name, int status)
{
}

void DecisionMakerNode::updateDriveEmergencyState(cstring_t& state_name, int status)
{
}
}