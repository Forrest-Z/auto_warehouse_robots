#include <cmath>

#include <utility>

#include "agent/Agent.h"
#include <visualization_msgs/Marker.h>

#include "agent/MotionPlanner.h"
#include "Math.h"
#include "agent/path_planning/Point.h"

#include <ros/console.h>


MotionPlanner::MotionPlanner(Agent* a, auto_smart_factory::RobotConfiguration robot_config, ros::Publisher* motion_pub) {
	robotConfig = std::move(robot_config);
	
	minDrivingSpeed = robotConfig.min_linear_vel;
	maxDrivingSpeed = robotConfig.max_linear_vel;
	maxTurningSpeed = robotConfig.max_angular_vel;
	
	motionPub = motion_pub;
	agent = a;
	agentID = agent->getAgentID();

	ros::NodeHandle n;
	pathPub = n.advertise<visualization_msgs::Marker>("visualization_marker", 10);

	pid = new PidController(motionPub, 0.5, 0.4, 1.0, 2.0);
}

MotionPlanner::~MotionPlanner() = default;

void MotionPlanner::update(geometry_msgs::Point position, double orientation) {
	//driveCurrentPath(Point(position.x, position.y), orientation);
	if(atTarget) {
		if (!isCurrentPointLastPoint())	{
			ROS_WARN("[MotionPlanner] currentTargetIndex: %d | PathObjectSize: %d", currentTargetIndex, pathObject.getPoints().size() - 1);
			advanceToNextPathPoint();
			pid->reset();
			pid->setTargetPoint(currentTarget, Position(position.x, position.y, orientation, ros::Time::now()));
			atTarget = false;
		}
	} else {
		Position* pos = new Position(position.x, position.y, orientation, ros::Time::now());
		atTarget = pid->update(pos);
		delete pos;
	}
}

void MotionPlanner::newPath(geometry_msgs::Point start_position, std::vector<geometry_msgs::Point> new_path, geometry_msgs::Point end_direction_point, bool drive_backwards) {
	std::vector<Point> points;
	
	for(geometry_msgs::Point p : new_path) {
		points.emplace_back(p.x, p.y);
	}
	
	pathObject = Path(points);
	hasFinishedCurrentPath = false;
	currentTarget = pathObject.getPoints().front();
	currentTargetIndex = 0;

	pathPub.publish(pathObject.getVisualizationMsgPoints());
	pathPub.publish(pathObject.getVisualizationMsgLines());

	atTarget = true;
}

void MotionPlanner::enable(bool enable) {
	this->enabled = enable;
}

bool MotionPlanner::isEnabled() {
	return this->enabled;
}

void MotionPlanner::start() {
	standStill = false;
}

void MotionPlanner::stop() {
	standStill = true;
	geometry_msgs::Twist motion;
	motionPub->publish(motion);
}

bool MotionPlanner::isDone() {
	return hasFinishedCurrentPath;
}

bool MotionPlanner::hasPath() {
	return pathObject.getLength() > 0;
}

bool MotionPlanner::driveCurrentPath(Point currentPosition, double orientation) {
	geometry_msgs::Twist motion;

	float distToTarget = Math::getDistance(currentPosition, currentTarget);
	float rotationToTarget = getRotationToTarget(currentPosition, currentTarget, orientation);
	
	float rotationSign = rotationToTarget < 0.f ? -1.f : 1.f;
	rotationToTarget = std::fabs(rotationToTarget);
	
	if(rotationToTarget < allowedRotationDifference) {
		rotationToTarget = 0;
	}
	
	// has reached next point?
	if(distToTarget <= distToReachPoint && !isCurrentPointLastPoint()) {
		// Rerun with new point
		advanceToNextPathPoint();
		return driveCurrentPath(currentPosition, orientation);
	} else if(distToTarget <= distToReachFinalPoint && isCurrentPointLastPoint()) {
		// Todo rotate till final rotation is guaranteed
		stop();
		motionPub->publish(motion);
		hasFinishedCurrentPath = true;
		return true;
	}
	
	// Totally wrong direction
	if(rotationToTarget >= maxRotationDifference) {
		motion.angular.z = Math::clamp(rotationToTarget, 0, maxTurningSpeed) * rotationSign;
		motion.linear.x = 0;
	} else {
		float rotationAlpha = Math::clamp(rotationToTarget/maxRotationDifference, 0, 1);
		motion.angular.z = Math::lerp(minTurningSpeed, maxTurningSpeed, rotationAlpha) * rotationSign;
		
		float speedAlphaThroughRotation = 1.f - rotationAlpha;
		if(speedAlphaThroughRotation < 0.9f) {
			speedAlphaThroughRotation /= 3.f;
		}
		
		float speedAlphaThroughDist = 1.f;
		if(distToTarget <= distToSlowDown) {
			speedAlphaThroughDist = Math::clamp((distToTarget - distToReachPoint) / (distToSlowDown - distToReachPoint), 0, 1.f);
		}
		
		float speedAlpha = std::min(speedAlphaThroughDist, speedAlphaThroughRotation);
		
		motion.linear.x = Math::lerp(minDrivingSpeed, maxDrivingSpeed, speedAlpha);
	}
	
	motionPub->publish(motion);
	return false;
}

/* Returns the angle of the given orientation.
	 * @param orientation: orientation e.g
	 *       0.5
	 *        ||
	 *  1/-1--  --0
	 *        ||
	 *      -0.5
	  @returns an orientation angle e.g.
	 *      270
	 *      ||
	 * 180--  --0/360
 	 *      ||
	 *     90
	 *     */
float MotionPlanner::getRotationFromOrientation(double orientation) {
	if(orientation > 1.f) {
		orientation = -(2.f - orientation);
	} else if(orientation < -1.f) {
		orientation = 2.f + orientation;
	}
	
	if(orientation < 0) {
		return static_cast<float>(std::fabs(orientation) * 180.f);
	} else {
		return static_cast<float>(360.f - orientation * 180.f);
	}
}

float MotionPlanner::getRotationFromOrientationDifference(double orientation) {
	return static_cast<float>(orientation * 180.f);
}

void MotionPlanner::advanceToNextPathPoint() {
	currentTargetIndex++;
	currentTarget = pathObject.getPoints().at(static_cast<unsigned long>(currentTargetIndex));
}

bool MotionPlanner::isCurrentPointLastPoint() {
	return currentTargetIndex == pathObject.getPoints().size() - 1;
}

bool MotionPlanner::isDrivingBackwards() {
	return false;
}

float MotionPlanner::getRotationToTarget(Point currentPosition, Point targetPosition, double orientation) {
	double direction = std::atan2(targetPosition.y - currentPosition.y, targetPosition.x - currentPosition.x);

	return static_cast<float>(Math::getAngleDifferenceInRad(orientation, direction));
}

