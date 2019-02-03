#include <utility>
#include <iostream>
#include <include/agent/path_planning/Map.h>

#include "ros/ros.h"
#include "Math.h"
#include "agent/path_planning/Rectangle.h"
#include "agent/path_planning/Map.h"
#include "agent/path_planning/Point.h"
#include "agent/path_planning/ThetaStarPathPlanner.h"

int Map::visualisationId = 0;
double Map::infiniteReservationTime = 0;

Map::Map(auto_smart_factory::WarehouseConfiguration warehouseConfig, std::vector<Rectangle>& obstacles, RobotHardwareProfile* hardwareProfile, int ownerId) :
		warehouseConfig(warehouseConfig),
		width(warehouseConfig.map_configuration.width),
		height(warehouseConfig.map_configuration.height),
		margin(warehouseConfig.map_configuration.margin),
		hardwareProfile(hardwareProfile),
		ownerId(ownerId)
{
	if(infiniteReservationTime == 0) {
		infiniteReservationTime = ros::Time::now().toSec() + 1000000.f;
	}
	
	this->obstacles.clear();
	for(const Rectangle& o : obstacles) {
		this->obstacles.emplace_back(o.getPosition(), o.getSize(), o.getRotation());
	}
	
	// Theta star map
	thetaStarMap = ThetaStarMap(this, warehouseConfig.map_configuration.resolutionThetaStar);
	for(const auto& tray : warehouseConfig.trays) {
		OrientedPoint p = getPointInFrontOfTray(tray);
		thetaStarMap.addAdditionalNode(Point(p.x, p.y));
	}
	
	reservations.clear();
	
	// Add idle reservations
	double infiniteReservationStartTime = ros::Time::now().toSec() - 1000;
	for(const auto& idlePosition : warehouseConfig.idle_positions) {
		std::string idStr = idlePosition.id.substr(idlePosition.id.find('_') + 1);
		int id = std::stoi(idStr);
		Point pos = Point(static_cast<float>(idlePosition.pose.x), static_cast<float>(idlePosition.pose.y));
		
		reservations.emplace_back(pos, Point(ROBOT_RADIUS * 2.f, ROBOT_RADIUS * 2.f), 0, infiniteReservationStartTime, infiniteReservationTime, id);
	}
}

bool Map::isInsideAnyInflatedObstacle(const Point& point) const {
	for(const Rectangle& obstacle : obstacles) {
		if(obstacle.isInsideInflated(point)) {
			return true;
		}
	}

	return false;
}

bool Map::isStaticLineOfSightFree(const Point& pos1, const Point& pos2) const {
	for(const Rectangle& obstacle : obstacles) {
		if(Math::doesLineSegmentIntersectRectangle(pos1, pos2, obstacle)) {
			return false;
		}
	}

	return true;
}

bool Map::isTimedLineOfSightFree(const Point& pos1, double startTime, const Point& pos2, double endTime) const {
	if(!isStaticLineOfSightFree(pos1, pos2)) {
		return false;
	}

	for(const Rectangle& reservation : reservations) {
		if(reservation.doesOverlapTimeRange(startTime, endTime, ownerId) && Math::doesLineSegmentIntersectRectangle(pos1, pos2, reservation)) {
			return false;
		}
	}

	return true;
}

TimedLineOfSightResult Map::whenIsTimedLineOfSightFree(const Point& pos1, double startTime, const Point& pos2, double endTime) const {
	TimedLineOfSightResult result;

	if(!isStaticLineOfSightFree(pos1, pos2)) {
		result.blockedByStatic = true;
		return result;
	}

	for(const Rectangle& reservation : reservations) {
		// Directly blocked
		if(reservation.doesOverlapTimeRange(startTime + 0.01f, endTime, ownerId) && Math::doesLineSegmentIntersectRectangle(pos1, pos2, reservation)) {
			result.blockedByTimed = true;
			if(reservation.getFreeAfter() > result.freeAfter) {
				result.freeAfter = reservation.getFreeAfter();
			}
		}

		// Upcoming obstacles			
		if(Math::isPointInRectangle(pos2, reservation) && reservation.getStartTime() > endTime && reservation.getOwnerId() != ownerId) {
			result.hasUpcomingObstacle = true;
			// Todo make adaptive - for now assume that every reservation can be left in x seconds
			double minTimeToLeave = 8;

			double lastValidEntryTime = reservation.getStartTime() - minTimeToLeave;
			if(lastValidEntryTime < result.lastValidEntryTime) {
				result.lastValidEntryTime = lastValidEntryTime;
				result.freeAfterUpcomingObstacle = reservation.getEndTime();
			}
		}
	}
	
	double maxTime = endTime + 10000.f;
	if((result.blockedByTimed && result.freeAfter > maxTime) ||
	   (result.hasUpcomingObstacle && (result.lastValidEntryTime > maxTime || result.freeAfterUpcomingObstacle > maxTime))) {
		result.blockedByStatic = true;
	}

	return result;
}

bool Map::isTimedConnectionFree(const Point& pos1, const Point& pos2, double startTime, double waitingTime, double drivingTime) const {
	// Does not check against static obstacles, this is only used to verify a already planned connection
	
	double endTime = startTime + waitingTime + drivingTime;

	for(const Rectangle& reservation : reservations) {
		// Check if the waiting part is free
		if(reservation.doesOverlapTimeRange(startTime, startTime + waitingTime, ownerId) && Math::isPointInRectangle(pos1, reservation)) {
			//printf("Connection dropped due to waiting: %.1f/%.1f -> %.1f/%.1f : wait: %.1f, drive: %.1f\n", pos1.x, pos1.y, pos2.x, pos2.y, waitingTime, drivingTime);
			return false;
		}

		// Check if the driving part is free
		if(reservation.doesOverlapTimeRange(startTime + waitingTime + 0.001f, endTime, ownerId) && Math::doesLineSegmentIntersectRectangle(pos1, pos2, reservation)) {
			//printf("Connection dropped due to driving: %.1f/%.1f -> %.1f/%.1f : wait: %.1f, drive: %.1f\n", pos1.x, pos1.y, pos2.x, pos2.y, waitingTime, drivingTime);
			return false;
		}
	}

	return true;
}

Point Map::getRandomFreePoint() const {
	Point point;
	bool pointFound = false;
	
	while(!pointFound) {
		point = Point(Math::getRandom(-width/2.f, width/2.f), Math::getRandom(-height/2.f, height/2.f));
		pointFound = !isInsideAnyInflatedObstacle(point);
	}
	
	return point;	
}

float Map::getWidth() const {
	return width;
}

float Map::getHeight() const {
	return height;
}

float Map::getMargin() const {
	return margin;
}

Path Map::getThetaStarPath(const OrientedPoint& start, const OrientedPoint& end, double startingTime) {
	ThetaStarPathPlanner thetaStarPathPlanner(&thetaStarMap, hardwareProfile);
	return thetaStarPathPlanner.findPath(start, end, startingTime);
}

Path Map::getThetaStarPath(const OrientedPoint& start, const auto_smart_factory::Tray& end, double startingTime) {
	ThetaStarPathPlanner thetaStarPathPlanner(&thetaStarMap, hardwareProfile);
	const OrientedPoint endPoint = getPointInFrontOfTray(end);
	//ROS_INFO("Computing path from (%f/%f) to tray of type %s (%f/%f)", start.x, start.y, end.type.c_str(), getPointInFrontOfTray(end).x, getPointInFrontOfTray(end).y);
	return thetaStarPathPlanner.findPath(start, endPoint, startingTime);
}

Path Map::getThetaStarPath(const auto_smart_factory::Tray& start, const OrientedPoint& end, double startingTime) {
	ThetaStarPathPlanner thetaStarPathPlanner(&thetaStarMap, hardwareProfile);
	const OrientedPoint startPoint = getPointInFrontOfTray(start);
	//ROS_INFO("Computing path from tray of type %s (%f/%f) to (%f/%f)", start.type.c_str(), getPointInFrontOfTray(start).x, getPointInFrontOfTray(start).y, end.x, end.y);
	return thetaStarPathPlanner.findPath(startPoint, end, startingTime);
}

Path Map::getThetaStarPath(const auto_smart_factory::Tray& start, const auto_smart_factory::Tray& end, double startingTime) {
	ThetaStarPathPlanner thetaStarPathPlanner(&thetaStarMap, hardwareProfile);
	const OrientedPoint startPoint = getPointInFrontOfTray(start);
	const OrientedPoint endPoint = getPointInFrontOfTray(end);
	//ROS_INFO("Computing path from tray of type %s (%f/%f) to tray of type %s (%f/%f)", start.type.c_str(), getPointInFrontOfTray(start).x, getPointInFrontOfTray(start).y, end.type.c_str(), getPointInFrontOfTray(end).x, getPointInFrontOfTray(end).y);
	return thetaStarPathPlanner.findPath(startPoint, endPoint, startingTime);
}

bool Map::isPointInMap(const Point& pos) const {
	return pos.x >= margin && pos.x <= width - margin && pos.y >= margin && pos.y <= height - margin;
}

void Map::deleteExpiredReservations(double time) {
	auto iter = reservations.begin();

	while(iter != reservations.end()) {
		if((*iter).getEndTime() < time) {
			iter = reservations.erase(iter);
		} else {
			iter++;
		}
	}
}

void Map::addReservations(std::vector<Rectangle> newReservations) {
	for(const auto& r : newReservations) {
		reservations.emplace_back(r.getPosition(), r.getSize(), r.getRotation(), r.getStartTime(), r.getEndTime(), r.getOwnerId());
	}
}

OrientedPoint Map::getPointInFrontOfTray(const auto_smart_factory::Tray& tray) {
	OrientedPoint p;

	// Assume tray.orientation is in degree
	float trayRadius = 0.51f;
	float approachRoutineLength = 0.3f;
	
	double inputDx = std::cos(tray.orientation * PI / 180);
	double inputDy = std::sin(tray.orientation * PI / 180);
	p.x = static_cast<float>(tray.x + (trayRadius + approachRoutineLength + ROBOT_RADIUS) * inputDx);
	p.y = static_cast<float>(tray.y + (trayRadius + approachRoutineLength + ROBOT_RADIUS) * inputDy);
	p.o = static_cast<float>(Math::normalizeRad(Math::toRad(tray.orientation + 180.f)));
	
	return p;
}

visualization_msgs::Marker Map::getObstacleVisualization() {
	visualization_msgs::Marker msg;
	msg.header.frame_id = "map";
	msg.header.stamp = ros::Time::now();
	msg.ns = "Obstacles";
	msg.action = visualization_msgs::Marker::MODIFY;
	msg.pose.orientation.w = 1.0;

	msg.id = 0;
	msg.type = visualization_msgs::Marker::TRIANGLE_LIST;

	msg.scale.x = 1.f;
	msg.scale.y = 1.f;
	msg.scale.z = 1.f;

	msg.color.r = 0.1f;
	msg.color.g = 0.1f;
	msg.color.b = 0.1f;
	msg.color.a = 0.3;

	geometry_msgs::Point p;
	p.z = 0.f;
	// Obstacles
	for(const Rectangle& obstacle : obstacles) {
		const Point* points = obstacle.getPointsInflated();
		// First triangle
		p.x = points[0].x;
		p.y = points[0].y;
		msg.points.push_back(p);

		p.x = points[1].x;
		p.y = points[1].y;
		msg.points.push_back(p);

		p.x = points[2].x;
		p.y = points[2].y;
		msg.points.push_back(p);

		// Second triangle
		p.x = points[2].x;
		p.y = points[2].y;
		msg.points.push_back(p);

		p.x = points[3].x;
		p.y = points[3].y;
		msg.points.push_back(p);

		p.x = points[0].x;
		p.y = points[0].y;
		msg.points.push_back(p);
	}

	return msg;
}

visualization_msgs::Marker Map::getInactiveReservationVisualization(int ownerId, visualization_msgs::Marker::_color_type baseColor) {
	visualization_msgs::Marker msg;
	msg.header.frame_id = "map";
	msg.header.stamp = ros::Time::now();
	msg.ns = "Reservations";
	msg.action = visualization_msgs::Marker::ADD;
	msg.pose.orientation.w = 1.0;
	msg.lifetime = ros::Duration(0.24f);

	msg.id = Map::visualisationId++;
	msg.type = visualization_msgs::Marker::TRIANGLE_LIST;

	msg.scale.x = 1.f;
	msg.scale.y = 1.f;
	msg.scale.z = 1.f;
	
	msg.color = baseColor;
	msg.color.a = 0.2f;

	geometry_msgs::Point p;
	p.z = 0.f;

	double now = ros::Time::now().toSec();
	for(const Rectangle& reservation : reservations) {
		if(reservation.getOwnerId() != ownerId) {
			continue;
		}
		
		if(now >= reservation.getStartTime() && now <= reservation.getEndTime()) {
			continue;
		}
		
		const Point* points = reservation.getPointsInflated();
		// First triangle
		p.x = points[0].x;
		p.y = points[0].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[1].x;
		p.y = points[1].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[2].x;
		p.y = points[2].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		// Second triangle
		p.x = points[2].x;
		p.y = points[2].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[3].x;
		p.y = points[3].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[0].x;
		p.y = points[0].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);
	}

	return msg;
}

visualization_msgs::Marker Map::getActiveReservationVisualization(int ownerId, visualization_msgs::Marker::_color_type baseColor) {
	visualization_msgs::Marker msg;
	msg.header.frame_id = "map";
	msg.header.stamp = ros::Time::now();
	msg.ns = "Reservations";
	msg.action = visualization_msgs::Marker::ADD;
	msg.pose.orientation.w = 1.0;
	msg.lifetime = ros::Duration(0.24f);

	msg.id = Map::visualisationId++;
	msg.type = visualization_msgs::Marker::TRIANGLE_LIST;

	msg.scale.x = 1.f;
	msg.scale.y = 1.f;
	msg.scale.z = 1.f;
	
	msg.color = baseColor;
	msg.color.a = 0.7f;

	geometry_msgs::Point p;
	p.z = 0.f;

	double now = ros::Time::now().toSec();
	for(const Rectangle& reservation : reservations) {
		if(reservation.getOwnerId() != ownerId) {
			continue;
		}

		if(!(now >= reservation.getStartTime() && now <= reservation.getEndTime())) {
			continue;
		}

		const Point* points = reservation.getPointsInflated();
		// First triangle
		p.x = points[0].x;
		p.y = points[0].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[1].x;
		p.y = points[1].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[2].x;
		p.y = points[2].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		// Second triangle
		p.x = points[2].x;
		p.y = points[2].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[3].x;
		p.y = points[3].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);

		p.x = points[0].x;
		p.y = points[0].y;
		msg.points.push_back(p);
		msg.colors.push_back(msg.color);
	}

	return msg;
}

void Map::listAllReservationsIn(Point p) {
	ROS_INFO("Reservations for robot %d at point %f/%f", ownerId, p.x, p.y);
	for(const auto& r : reservations) {
		if(Math::isPointInRectangle(p, r)) {
			ROS_INFO("Reservations At %f/%f | Size %f/%f | Rot: %f | ID: %d, from %f until %f", r.getPosition().x, r.getPosition().y, r.getSize().x, r.getSize().y, r.getRotation(), r.getOwnerId(), r.getStartTime(), r.getEndTime());	
		}		
	}
}

bool Map::isPointTargetOfAnotherRobot(OrientedPoint p) {
	for(const auto& r : reservations) {
		if(Math::isPointInRectangle(Point(p.x, p.y), r) && r.getOwnerId() != ownerId && std::abs(r.getEndTime() - infiniteReservationTime) < 1000) {
			return true;
		}		
	}
	
	return false;
}

bool Map::isPointTargetOfAnotherRobot(const auto_smart_factory::Tray& tray) {
	return isPointTargetOfAnotherRobot(getPointInFrontOfTray(tray));
}