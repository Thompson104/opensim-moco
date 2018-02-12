/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: sandboxMuscle.cpp                                        *
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

// Some of this code is based on testSingleMuscle,
// testSingleMuscleDeGrooteFregly2016.

#include <Muscollo/osimMuscollo.h>

#include <MuscolloSandboxShared.h>

#include <OpenSim/Simulation/Model/PathActuator.h>
#include <OpenSim/Simulation/SimbodyEngine/SliderJoint.h>
#include <OpenSim/Actuators/Millard2012EquilibriumMuscle.h>

using namespace OpenSim;

/// TODO prohibit fiber length from going below 0.2.
/// This muscle model was published in De Groote et al. 2016.
/// DGF stands for DeGroote-Fregly. The abbreviation is temporary, and is used
/// to avoid a name conflict with the existing DeGrooteFregly2016Muscle class
/// (which doesn't inherit from OpenSim::Force).
/// Groote, F., Kinney, A. L., Rao, A. V., & Fregly, B. J. (2016). Evaluation of
/// Direct Collocation Optimal Control Problem Formulations for Solving the
/// Muscle Redundancy Problem. Annals of Biomedical Engineering, 44(10), 1–15.
/// http://doi.org/10.1007/s10439-016-1591-9
class /*OSIMMUSCOLLO_API*/DGF2016Muscle : public PathActuator {
    OpenSim_DECLARE_CONCRETE_OBJECT(DGF2016Muscle, PathActuator);
public:

    OpenSim_DECLARE_PROPERTY(max_isometric_force, double,
    "Maximum isometric force that the fibers can generate.");
    OpenSim_DECLARE_PROPERTY(optimal_fiber_length, double,
    "Optimal length of the muscle fibers.");
    OpenSim_DECLARE_PROPERTY(tendon_slack_length, double,
    "Resting length of the tendon");
    OpenSim_DECLARE_PROPERTY(max_contraction_velocity, double,
    "Maximum contraction velocity of the fibers, in "
    "optimal_fiber_length/second");
    OpenSim_DECLARE_PROPERTY(activation_time_constant, double,
    "Smaller value means activation can change more rapidly (units: seconds).");
    OpenSim_DECLARE_PROPERTY(default_activation, double,
    "Value of activation in the default state returned by initSystem().");

    DGF2016Muscle() {
        constructProperties();
    }

    void extendFinalizeFromProperties() override {
        Super::extendFinalizeFromProperties();
        OPENSIM_THROW_IF(!getProperty_optimal_force().getValueIsDefault(),
        Exception, "The optimal_force property is ignored for this Force; "
        "use max_isometric_force instead.");

        // TODO validate properties (nonnegative, etc.).

        m_maxContractionVelocityInMeters =
                get_max_contraction_velocity() * get_optimal_fiber_length();
    }

    void extendAddToSystem(SimTK::MultibodySystem& system) const override {
        Super::extendAddToSystem(system);
        addStateVariable("activation", SimTK::Stage::Dynamics);
    }

    void extendInitStateFromProperties(SimTK::State& s) const override {
        Super::extendInitStateFromProperties(s);
        setStateVariableValue(s, "activation", get_default_activation());
    }

    void extendSetPropertiesFromState(const SimTK::State& s) override {
        Super::extendSetPropertiesFromState(s);
        set_default_activation(getStateVariableValue(s, "activation"));
    }

    // TODO no need to do clamping, etc; CoordinateActuator is bidirectional.
    void computeStateVariableDerivatives(const SimTK::State& s) const override {
        const auto& tau = get_activation_time_constant();
        const auto& u = getControl(s);
        const auto& a = getStateVariableValue(s, "activation");
        const SimTK::Real adot = (u - a) / tau;
        setStateVariableDerivativeValue(s, "activation", adot);
    }

    double computeActuation(const SimTK::State& s) const override {
        const SimTK::Real normFiberLength = calcNormalizedFiberLength(s);
        const SimTK::Real normFiberVelocity = calcNormalizedFiberVelocity(s);

        const SimTK::Real activeForceLengthMult =
                calcActiveForceLengthMultiplier(normFiberLength);
        const SimTK::Real forceVelocityMult =
                calcForceVelocityMultiplier(normFiberVelocity);
        const SimTK::Real& activation = getStateVariableValue(s, "activation");

        const SimTK::Real passiveForceMult =
                calcPassiveForceMultiplier(normFiberLength);

        const SimTK::Real normActiveForce =
                activation * activeForceLengthMult * forceVelocityMult;
        // std::cout << "DEBUG " << s.getTime() << " "
        //         << normFiberLength << " "
        //         << normActiveForce << " "
        //         << passiveForceMult
        //         << std::endl;
        return get_max_isometric_force() * (normActiveForce + passiveForceMult);
    }

    // TODO replace with getNormalizedFiberLength from Muscle.
    SimTK::Real calcNormalizedFiberLength(const SimTK::State& s) const {
        const SimTK::Real& fiberLength =
                getLength(s) - get_tendon_slack_length();
        return fiberLength / get_optimal_fiber_length();
    }

    /// This returns the fiber velocity normalized by the max contraction
    /// velocity. The normalized fiber velocity is unitless and should be in
    /// [-1, 1].
    // TODO replace with getNormalizedFiberVelocity from Muscle.
    SimTK::Real calcNormalizedFiberVelocity(const SimTK::State& s) const {
        const SimTK::Real& fiberVelocity = getLengtheningSpeed(s);
        return fiberVelocity / m_maxContractionVelocityInMeters;
    }

    /// @name Calculate multipliers.
    /// @{
    static SimTK::Real calcActiveForceLengthMultiplier(
            const SimTK::Real& normFiberLength) {
        static const double b11 =  0.815;
        static const double b21 =  1.055;
        static const double b31 =  0.162;
        static const double b41 =  0.063;
        static const double b12 =  0.433;
        static const double b22 =  0.717;
        static const double b32 = -0.030;
        static const double b42 =  0.200;
        static const double b13 =  0.100;
        static const double b23 =  1.000;
        static const double b33 =  0.354;
        static const double b43 =  0.000;
        return calcGaussian(normFiberLength, b11, b21, b31, b41) +
                calcGaussian(normFiberLength, b12, b22, b32, b42) +
                calcGaussian(normFiberLength, b13, b23, b33, b43);
    }
    /// Domain: [-1, 1]
    static SimTK::Real calcForceVelocityMultiplier(
            const SimTK::Real& normFiberVelocity) {
        using SimTK::square;
        static const double d1 = -0.318;
        static const double d2 = -8.149;
        static const double d3 = -0.374;
        static const double d4 =  0.886;
        const SimTK::Real tempV = d2 * normFiberVelocity + d3;
        const SimTK::Real tempLogArg = tempV + sqrt(square(tempV) + 1.0);
        return d1 * log(tempLogArg) + d4;
    }

    /// This is the passive force-length curve.
    SimTK::Real calcPassiveForceMultiplier(
            const SimTK::Real& normFiberLength) const {
        // Passive force-length curve.
        // TODO turn some of these into properties:
        static const double kPE = 4.0;
        static const double e0  = 0.6;
        static const double denom = exp(kPE) - 1;
        static const double min_norm_fiber_length = 0.2;
        static const double numer_offset =
                exp(kPE * (min_norm_fiber_length - 1)/e0);
        // The version of this equation in the supplementary materials of De
        // Groote, et al. 2016 has an error. The correct equation passes
        // through y = 0 at x = 0.2, and therefore is never negative within
        // the allowed range of the optimal fiber length. The version in the
        // supplementary materials allows for negative forces.
        return (exp(kPE * (normFiberLength - 1.0) / e0) - numer_offset) / denom;
    }
    /// @}
private:
    void constructProperties() {
        constructProperty_max_isometric_force(1000);
        constructProperty_optimal_fiber_length(0.1);
        constructProperty_tendon_slack_length(0.2);
        constructProperty_max_contraction_velocity(10);
        constructProperty_activation_time_constant(0.010);
        constructProperty_default_activation(0.5);
    }
    /// This is a Gaussian-like function used in the active force-length curve.
    /// A proper Gaussian function does not have the variable in the denominator
    /// of the exponent.
    /// The supplement for De Groote et al., 2016 has a typo:
    /// the denominator should be squared.
    static SimTK::Real calcGaussian(const SimTK::Real& x,const double& b1,
            const double& b2, const double& b3, const double& b4) {
        using SimTK::square;
        return b1 * exp((-0.5 * square(x - b2)) / square(b3 + b4 * x));
    }


    SimTK::Real m_maxContractionVelocityInMeters;
};

Model createHangingMuscleModel() {
    Model model;
    model.setName("isometric_muscle");
    model.set_gravity(SimTK::Vec3(9.81, 0, 0));
    auto* body = new Body("body", 0.5, SimTK::Vec3(0), SimTK::Inertia(0));
    model.addComponent(body);

    // Allows translation along x.
    auto* joint = new SliderJoint("joint", model.getGround(), *body);
    auto& coord = joint->updCoordinate(SliderJoint::Coord::TranslationX);
    coord.setName("height");
    model.addComponent(joint);

    auto* actu = new DGF2016Muscle();
    actu->setName("actuator");
    actu->set_max_isometric_force(30.0);
    actu->set_optimal_fiber_length(0.10);
    actu->set_tendon_slack_length(0.05);
    //actu->set_pennation_angle_at_optimal(0.1);
    //actu->set_max_contraction_velocity(10);
    actu->addNewPathPoint("origin", model.updGround(), SimTK::Vec3(0));
    actu->addNewPathPoint("insertion", *body, SimTK::Vec3(0));
    model.addComponent(actu);


    /*
    auto* actu = new Millard2012EquilibriumMuscle();
    actu->set_fiber_damping(0); // TODO
    actu->setName("actuator");
    actu->set_max_isometric_force(30.0);
    actu->set_optimal_fiber_length(0.10);
    actu->set_ignore_tendon_compliance(true);
    actu->set_tendon_slack_length(0.05);
    actu->set_pennation_angle_at_optimal(0.1);
    actu->set_max_contraction_velocity(10);
    actu->addNewPathPoint("origin", model.updGround(), SimTK::Vec3(0));
    actu->addNewPathPoint("insertion", *body, SimTK::Vec3(0));
    model.addComponent(actu);
    */

    /*
    auto* actu = new ActivationCoordinateActuator();
    actu->setName("actuator");
    actu->setCoordinate(&coord);
    actu->set_activation_time_constant(0.001);
    actu->set_optimal_force(30);
    model.addComponent(actu);

    auto* contr = new PrescribedController();
    contr->setName("controller");
    contr->addActuator(*actu);
    contr->prescribeControlForActuator("actuator", new Constant(1.0));
    model.addComponent(contr);
    */

    body->attachGeometry(new Sphere(0.05));

    return model;
}

int main() {
    MucoTool muco;
    MucoProblem& mp = muco.updProblem();
    Model model = createHangingMuscleModel();
    SimTK::State state = model.initSystem();
    bool hasFiberDynamics = false;
    if (hasFiberDynamics) {
        model.setStateVariableValue(state, "joint/height/value", 0.15);
        model.equilibrateMuscles(state);
    }

    // TODO
    //Manager manager(model, state);
    //manager.integrate(2.0);
    //
    //visualize(model, manager.getStateStorage());
    //std::exit(-1);

    //std::cout << "DEBUG " <<
    //        model.getStateVariableValue(state, "actuator/fiber_length")
    //        << std::endl;
    //model.equilibrateMuscles(state);
    //std::cout << "DEBUG " <<
    //        model.getStateVariableValue(state, "actuator/fiber_length")
    //        << std::endl;
    mp.setModel(model);
    mp.setTimeBounds(0, {0.05, 1.0});
    mp.setStateInfo("joint/height/value", {0, 0.3}, 0.15, 0.14);
    mp.setStateInfo("joint/height/speed", {-10, 10}, 0, 0);
    // TODO initial fiber length?
    // TODO how to enforce initial equilibrium?
    if (hasFiberDynamics) {
        mp.setStateInfo("actuator/fiber_length", {0, 0.3},
                model.getStateVariableValue(state, "actuator/fiber_length"));
    }
    // OpenSim might not allow activations of 0.
    mp.setStateInfo("actuator/activation", {0, 1}, 0);
    mp.setControlInfo("actuator", {0, 1});

    mp.addCost(MucoFinalTimeCost());

    // TODO try ActivationCoordinateActuator first.
    // TODO i feel like the force-velocity effect is much more strict than it
    // should be.

    // MucoTropterSolver& ms = muco.initSolver();
    MucoSolution solution = muco.solve().unseal();
    solution.write("sandboxMuscle_solution.sto");
    std::cout << "DEBUG " << solution.getState("joint/height/value") << std::endl;
    std::cout << "DEBUG " << solution.getState("joint/height/speed") << std::endl;

    // TODO perform forward simulation using optimized controls; see if we
    // end up at the correct final state.
    {

        // Add a controller to the model.
        const SimTK::Vector& time = solution.getTime();
        const auto control = solution.getControl("actuator");
        auto* controlFunction = new GCVSpline(5, time.nrow(), &time[0],
                &control[0]);
        auto* controller = new PrescribedController();
        controller->addActuator(model.getComponent<Actuator>("actuator"));
        controller->prescribeControlForActuator("actuator", controlFunction);
        model.addController(controller);

        // Set the initial state.
        SimTK::State state = model.initSystem();
        model.setStateVariableValue(state, "joint/height/value", 0.15);
        model.setStateVariableValue(state, "actuator/activation", 0);

        // Integrate.
        Manager manager(model, state);
        SimTK::State finalState = manager.integrate(time[time.nrow() - 1]);
        std::cout << "DEBUG "
                << model.getStateVariableValue(finalState, "joint/height/value")
                << std::endl;
        manager.getStateStorage().print("sandboxMuscle_timestepping.sto");
    }


    return EXIT_SUCCESS;
}

