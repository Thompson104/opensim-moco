/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: MucoMarkerEndpointCost.cpp                               *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Christopher Dembia                                              *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "MucoMarkerEndpointCost.h"

#include <OpenSim/Simulation/Model/Model.h>

using namespace OpenSim;

void MucoMarkerEndpointCost::initializeImpl() const {
    m_point.reset(&getModel().getComponent<Point>(get_point_name()));
}

void MucoMarkerEndpointCost::calcEndpointCostImpl(
        const SimTK::State& finalState, double& cost) const {
    getModel().realizePosition(finalState);
    const auto& actualLocation = m_point->getLocationInGround(finalState);
    cost = (actualLocation - get_reference_location()).normSqr();
}

void MucoMarkerEndpointCost::constructProperties() {
    constructProperty_point_name("");
    constructProperty_reference_location(SimTK::Vec3(0));
}
