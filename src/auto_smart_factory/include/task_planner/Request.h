/*
 * Request.h
 *
 *  Created on: 21.07.2017
 *      Author: jacob
 */

#ifndef AUTO_SMART_FACTORY_SRC_TASK_PLANNER_REQUEST_H_
#define AUTO_SMART_FACTORY_SRC_TASK_PLANNER_REQUEST_H_

#include <ros/ros.h>

#include <auto_smart_factory/Tray.h>
#include <auto_smart_factory/RequestStatus.h>
#include <auto_smart_factory/CalculateETA.h>

#include <task_planner/InputTaskRequirements.h>
#include <task_planner/OutputTaskRequirements.h>
#include <task_planner/TaskData.h>

class TaskPlanner;

/**
 * This class represents a generic request. The properties of this request are
 * defined by the task requirement object specified on construction.
 */
class Request {
public:
	/**
	 * Create a request.
	 * @param tp Pointer to the task planner object
	 * @param taskRequirements The task requirements object defining the properties of this request
	 * @param type String representation of the task type, e.g. 'input' or 'output'
	 */
	Request(TaskPlanner *tp, TaskRequirementsConstPtr taskRequirements, std::string type);

	virtual ~Request();

	/**
	 * Tries to allocate all necessary resources to start a task.
	 *
	 * \throws std::runtime_error with failure description if allocation is not successful
	 *
	 * @return TaskData object that contains all necessary information about allocated resources needed to start a task
	 */
	TaskData allocateResources();

	/**
	 * Check if this request is still pending or can be deleted.
	 * @return True if it is still pending.
	 */
	bool isPending() const;

	/**
	 * Returns id of this request
	 * @return Request id
	 */
	unsigned int getId() const;

	/**
	 * Return status information about this request.
	 * @return Status information
	 */
	auto_smart_factory::RequestStatus getStatus() const;

	/**
	 * Get request's requirements.
	 * @return Requirements object
	 */
	TaskRequirementsConstPtr getRequirements() const;

protected:
	/**
	 * Creates a list of possible source tray candidates for this request.
	 * @param sourceTrayCandidates Output vector for candidates
	 * @return True if candidate list is non-empty
	 */
	bool findSourceCandidates(
			std::vector<auto_smart_factory::Tray> &sourceTrayCandidates) const;

	/**
	 * Creates a list of possible target tray candidates for this request.
	 * @param targetTrayCandidates Output vector for candidates
	 * @return True if candidate list is non-empty
	 */
	bool findTargetCandidates(
			std::vector<auto_smart_factory::Tray> &targetTrayCandidates) const;

	/**
	 * Creates a list of possible robot candidates using lists of source and target tray candidates.
	 * All idle robots are queried using the candidates.
	 * @param robotCandidates Output vector for robot candidates
	 * @param sourceTrayCandidates List of source tray candidates
	 * @param targetTrayCandidates List of output tray candidates
	 * @return True if robot candidate list is non-empty
	 */
	bool getRobotCandidates(std::vector<RobotCandidate> &robotCandidates,
			const std::vector<auto_smart_factory::Tray> &sourceTrayCandidates,
			const std::vector<auto_smart_factory::Tray> &targetTrayCandidates) const;

    /**
	 * Sends requests to ETAServer to get an estimated duration for a task.
	 * @param robotId Id of the robot
	 * @param cand Output of the robot response
	 * @param sourceTrayCandidates Source tray candidates
	 * @param targetTrayCandidates target tray candidates
	 * @return
	 * @todo eta server is to be implemented by the students. Please adjust the necessary
	 * parts mentioned in function in the source (Request.cpp line 229) to successfully call a ROS
	 * service to be implemented under ETA server. 
	 */
    bool getRobotETA(std::string robotId, RobotCandidate &cand,
			const std::vector<auto_smart_factory::Tray> &sourceTrayCandidates,
			const std::vector<auto_smart_factory::Tray> &targetTrayCandidates) const;

	/**
	 * Sends a request to one robot using source and target tray candidates
	 * to get an estimated duration.
	 * @param robotId Id of the robot
	 * @param cand Output of the robot response
	 * @param sourceTrayCandidates Source tray candidates
	 * @param targetTrayCandidates target tray candidates
	 * @return
	 */
	bool sendRobotRequest(std::string robotId, RobotCandidate &cand,
			const std::vector<auto_smart_factory::Tray> &sourceTrayCandidates,
			const std::vector<auto_smart_factory::Tray> &targetTrayCandidates) const;

	/**
	 * Assign request/task to robot for includes the setup of the task in the robot.
	 *
	 * @param robotId Id of the robot
	 * @return True if assigning was successful
	 */
	bool allocateRobot(RobotCandidate candidate) const;

protected:
	/// the request status
	auto_smart_factory::RequestStatus status;

	/// reference to the task planner
	TaskPlanner *taskPlanner;

	/// requirements that need to be fulfilled
	TaskRequirementsConstPtr requirements;

	/// this flag tells whether to use the robot offer with the shortest
	/// estimated duration (true) or just random choice (false)
	bool useBestETA;

protected:
	/// used to generate unique ids
	static unsigned int nextId;

	/// generate new unique id
	static unsigned int getNewId();
};

#endif /* AUTO_SMART_FACTORY_SRC_TASK_PLANNER_REQUEST_H_ */