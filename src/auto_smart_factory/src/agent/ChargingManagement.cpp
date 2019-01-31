#include <agent/ChargingManagement.h>
#include "agent/Agent.h"

bool sortByDuration(const std::pair<Path, uint32_t> &lhs, const std::pair<Path, uint32_t> &rhs) { return lhs.first.getDuration() < rhs.first.getDuration(); }



ChargingManagement::ChargingManagement(Agent* a, auto_smart_factory::WarehouseConfiguration warehouse_configuration, Map* m) {

	agent = a;
	map = m;
	agentID = agent->getAgentID();
	agentBatt = agent->getAgentBattery();
	warehouseConfig = warehouse_configuration;
	ROS_INFO("[ChargingManagement] Started, agent ID: [%s], agent Batt: [%f] ", agentID.c_str(), agentBatt);
	getAllChargingStations();
}

ChargingManagement::~ChargingManagement() {
}


bool ChargingManagement::isEnergyAvailable(double energy){

	agentBatt = agent->getAgentBattery();
	energyAfterTask =  agentBatt - energy;
	if (energyAfterTask > criticalMinimum ){
        return true;
    }
    return false;
}


double ChargingManagement::getDischargedBattery(){
	agentBatt = agent->getAgentBattery();
	return (100-agentBatt);
}


void ChargingManagement::getAllChargingStations(){

	//Iterate through all the trays and separate out charging trays
	for(int i = 0; i < warehouseConfig.trays.size(); i++) {
		if(warehouseConfig.trays[i].type.compare("charging station")==0) {
			charging_trays.push_back(warehouseConfig.trays[i]);
		}
	}

	ROS_INFO("[Charging Management]:Found (%d) Charging Stations !",(unsigned int) charging_trays.size());
}

std::pair<Path, uint32_t> ChargingManagement::getPathToNearestChargingStation(OrientedPoint start, double startingTime){

	//Vector for all paths found
	std::vector<std::pair<Path, uint32_t>> paths;

	//Find paths for all possible charging stations
	for(int i= 0; i<charging_trays.size(); i++){
		paths.push_back(std::make_pair(map->getThetaStarPath(start,charging_trays[i], startingTime), charging_trays[i].id));
	}

    //Sort paths according to their duration, lower is better
	std::sort(paths.begin(), paths.end(), sortByDuration);

    return paths[0];
}


double ChargingManagement::getScoreMultiplier(float cumulatedEnergyConsumption){

	agentBatt = agent->getAgentBattery();
	energyAfterTask =  agentBatt - cumulatedEnergyConsumption; 	//Expected energy in agent after all the tasks

	if(energyAfterTask > upperThreshold){
	    return 1;
	}

	else if (energyAfterTask > lowerThreshold){
		//Calculate factor using the quadratic function : y = (-0.01)x^2+2 * (lowerThreshold/100)* x- lowerThreshold

		double quadraticFraction =  (2 + 0.01*lowerThreshold) * energyAfterTask - pow(energyAfterTask,2) * 0.01 - lowerThreshold;
		return quadraticFraction/100;
	}
	return energyAfterTask/100;
}


bool ChargingManagement::isChargingAppropriate(){
	agentBatt = agent->getAgentBattery();
	return (agentBatt <= upperThreshold);
}

bool ChargingManagement::isCharged(){
	agentBatt = agent->getAgentBattery();
	return (agentBatt <= 100.0f);
}
