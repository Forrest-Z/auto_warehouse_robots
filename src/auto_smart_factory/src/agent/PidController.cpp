#include "agent/PidController.h"

PidController::PidController(ros::Publisher pub, double posTolerance, double angleTolerance, double maxSpeed, double maxAngleSpeed) 
    :
    pubVelocity(pub),
    posTolerance(posTolerance),
    angleTolerance(angleTolerance),
    maxSpeed(maxSpeed),
    maxAngleSpeed(maxAngleSpeed),
    targetDistance(0.0),
    targetAngle(0.0),
    iterations(0),
    sumDistance(0.0),
    sumAngle(0.0),
    start(new Position()),
    last(new Position()) {
}

void PidController::setTarget(double distance, double angle) {
    targetDistance = distance;
    targetAngle = angle;
}

void PidController::publishVelocity(double speed, double angle) {
    geometry_msgs::Twist msg;
    
    msg.linear.x = speed;
    msg.angular.z = angle;

    pubVelocity.publish(msg);
}

void PidController::update(Position* current) {
    double angleCommand = 0;
    double speedCommand = 0;

    if (targetReached(current) == true)
    {
        ROS_INFO("GOAL ACHIEVED");
        publishVelocity(0.0,0.0);
        return;
    }

    if (iterations == 0) {
        start->x = current->x;
        start->y = current->y;
        start->t = current->t;
        start->o = current->o;
        last->x = current->x;
        last->y = current->y;
        last->t = current->t;
        last->o = current->o;
    }
    iterations++;

    //Calculation of action intervention.
    if (fabs(targetDistance) > posTolerance) {
    speedCommand = calculatePSD(current,start->getDistance(current)*copysign(1.0, targetDistance),start->getDistance(last)*copysign(1.0, targetDistance),targetDistance,F_KP,F_KD,F_KI,&sumDistance);
    }

    if (current->o-last->o < -PI) {
        current->o += 2*PI;
    } else if (current->o-last->o > PI) {
        current->o -= 2*PI;
    }

    angleCommand = calculatePSD(current,current->o-start->o, last->o-start->o,targetAngle,R_KP,R_KD,R_KI,&sumAngle);

    //Saving position to last
    last->x = current->x;
    last->y = current->y;
    last->o = current->o;
    last->t = current->t;

    //Invoking method for publishing message
    publishVelocity(fmin(maxAngleSpeed,angleCommand), fmin(maxSpeed,speedCommand));
}

bool PidController::targetReached(Position* current) {
    double distance;
    distance = start->getDistance(current)*copysign(1.0, targetDistance);

    if (fabs(distance-targetDistance) > posTolerance) {
        return false;
    }

    if (fabs(targetAngle - (current->o - start->o)) > angleTolerance &
    fabs(targetAngle - (current->o - start->o) + 2*PI) > angleTolerance &
    fabs(targetAngle - (current->o - start->o) - 2*PI) > angleTolerance) {
        return false;
    }

    return true;
}

double PidController::calculatePSD(Position* current, double currentValue, double lastValue, double referenceValue, double kP, double kD, double kS, double* sum) {
    double speed = 0;
    double error = referenceValue - currentValue;
    double previousError = referenceValue - lastValue;
    double dt = current->t.toSec() - last->t.toSec();
    double derivative = (error - previousError)/dt;
    *sum = *sum + error*dt;
    speed = kP*error + kD*derivative + kS*(*sum);

    return speed;
}

PidController::~PidController() {
}