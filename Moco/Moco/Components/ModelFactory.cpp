/* -------------------------------------------------------------------------- *
 * OpenSim Moco: ModelFactory.cpp                                             *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2018 Stanford University and the Authors                     *
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

#include "ModelFactory.h"
#include <OpenSim/Simulation/SimbodyEngine/PinJoint.h>
#include <OpenSim/Actuators/CoordinateActuator.h>

using namespace OpenSim;

Model ModelFactory::createNLinkPendulum(int numLinks) {
    Model model;
    OPENSIM_THROW_IF(numLinks < 0, Exception, "numLinks must be nonnegative.");
    std::string name;
    if (numLinks == 0) {
        name = "empty_model";
    } else if (numLinks == 1) {
        name = "pendulum";
    } else if (numLinks == 2) {
        name = "double_pendulum";
    } else {
        name = std::to_string(numLinks) + "_link_pendulum";
    }
    model.setName(name);
    const auto& ground = model.getGround();

    using SimTK::Vec3;
    using SimTK::Inertia;

    Ellipsoid bodyGeometry(0.5, 0.1, 0.1);
    bodyGeometry.setColor(SimTK::Gray);

    const PhysicalFrame* prevBody = &ground;
    for (int i = 0; i < numLinks; ++i) {
        const std::string istr = std::to_string(i);
        auto* bi = new OpenSim::Body("b" + istr, 1, Vec3(0), Inertia(1));
        model.addBody(bi);

        // Assume each body is 1 m long.
        auto* ji = new PinJoint("j" + istr, *prevBody, Vec3(0), Vec3(0),
                *bi, Vec3(-1, 0, 0), Vec3(0));
        auto& qi = ji->updCoordinate();
        qi.setName("q" + istr);
        model.addJoint(ji);

        auto* taui = new CoordinateActuator();
        taui->setCoordinate(&ji->updCoordinate());
        taui->setName("tau" + istr);
        taui->setOptimalForce(1);
        model.addComponent(taui);

        auto* marker = new Marker("marker" + istr, *bi, Vec3(0));
        model.addMarker(marker);

        // Attach an ellipsoid to a frame located at the center of each body.
        PhysicalOffsetFrame* bicenter = new PhysicalOffsetFrame(
                "b" + istr + "center", *bi, SimTK::Transform(Vec3(-0.5, 0, 0)));
        bi->addComponent(bicenter);
        bicenter->attachGeometry(bodyGeometry.clone());

        prevBody = bi;
    }

    model.finalizeConnections();

    return model;
}