/* -------------------------------------------------------------------------- *
 * OpenSim Muscollo: testConstraints.cpp                                      *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Nicholas Bianco                                                 *
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

#include <Muscollo/osimMuscollo.h>
#include <OpenSim/Common/LinearFunction.h>
#include <simbody/internal/Constraint.h>
#include <simbody/internal/Constraint_Ball.h>
#include <OpenSim/Actuators/CoordinateActuator.h>
#include <OpenSim/Simulation/osimSimulation.h>
#include <OpenSim/Common/GCVSpline.h>

using namespace OpenSim;
using SimTK::Vec3;
using SimTK::UnitVec3;
using SimTK::Vector;
using SimTK::State;

const int NUM_BODIES = 10;
const double BOND_LENGTH = 0.5;

/// Keep constraints satisfied to this tolerance during testing.
static const double ConstraintTol = 1e-10;

/// Compare two quantities that should have been calculated to machine tolerance
/// given the problem size, which we'll characterize by the number of mobilities
/// (borrowed from Simbody's 'testConstraints.cpp').
#define MACHINE_TEST(a,b) SimTK_TEST_EQ_SIZE(a,b, 10*state.getNU())

/// Create a model consisting of a chain of bodies. This model is nearly 
/// identical to the model implemented in Simbody's 'testConstraints.cpp'.
Model createModel() {
    Model model;
    const SimTK::Real mass = 1.23;
    Body* body = new Body("body0", mass, SimTK::Vec3(0.1, 0.2, -0.03),
              mass*SimTK::UnitInertia(1.1, 1.2, 1.3, 0.01, -0.02, 0.07));
    model.addBody(body);

    GimbalJoint* joint = new GimbalJoint("joint0",
        model.getGround(), Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1),
        *body, Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
    model.addJoint(joint);

    for (int i = 1; i < NUM_BODIES; ++i) {
        Body& parent = model.getBodySet().get(model.getNumBodies() - 1);

        std::string bodyName = "body" + std::to_string(i + 1);
        Body* body = new Body(bodyName, mass, SimTK::Vec3(0.1, 0.2, -0.03),
            mass*SimTK::UnitInertia(1.1, 1.2, 1.3, 0.01, -0.02, 0.07));
        model.addBody(body);

        std::string jointName = "joint" + std::to_string(i+1);
        if (i == NUM_BODIES-5) {
            UniversalJoint* joint = new UniversalJoint(jointName,
                parent, Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1), 
                *body, Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
            model.addJoint(joint);
        } else if (i == NUM_BODIES-3) {
            BallJoint* joint = new BallJoint(jointName,
                parent, Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1),
                *body, Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
            model.addJoint(joint);
        } else {
            GimbalJoint* joint = new GimbalJoint(jointName,
                parent, Vec3(-0.1, 0.3, 0.2), Vec3(0.3, -0.2, 0.1),
                *body, Vec3(BOND_LENGTH, 0, 0), Vec3(-0.2, 0.1, -0.3));
            model.addJoint(joint);
        }
    }

    model.finalizeConnections();
    
    return model;
}

/// Create a random state for the model. This implementation mimics the random
/// state creation in Simbody's 'testConstraints.cpp'.
void createState(Model& model, State& state, const Vector& qOverride=Vector()) {
    state = model.initSystem();
    SimTK::Random::Uniform random; 
    for (int i = 0; i < state.getNY(); ++i)
        state.updY()[i] = random.getValue();
    if (qOverride.size())
        state.updQ() = qOverride;
    model.realizeVelocity(state);

    model.updMultibodySystem().project(state, ConstraintTol);
    model.realizeAcceleration(state);
}

/// Get model accelerations given the constraint multipliers. This calculation
/// is necessary for computing constraint defects associated with the system
/// dynamics, represented by the equations 
///
///     M udot + G^T lambda + f_inertial(q,u) = f_applied
///
/// If using an explicit representation of the system dynamics, the derivatives
/// of the generalized speeds for the system need to be computed in order to 
/// construct the defects. Rearranging the equations above (and noting that 
/// Simbody does not actually invert the mass matrix, but rather uses an order-N
/// approach), we obtain
///
///     udot = M_inv (f_applied - f_inertial(q,u) - G^T lambda) 
///          = f(q, u, lambda)
/// 
/// where,
///              q | generalized coordinates
///              u | generalized speeds
///         lambda | Lagrange multipliers
///
/// Since the three quantities required to compute the system accelerations 
/// will eventually become NLP variables in a direct collocation problem, it is 
/// not sufficient to use the internally calculated Lagrange multipliers in 
/// Simbody. An intermediate calculation must be made:
///
///     f_constraint(lambda) = G^T lambda
///
/// Therefore, this method computes the generalized speed derivatives via the
/// equation
///
///     udot = M_inv (f_applied - f_inertial(q,u) - f_constraint(lambda))
///
/// Finally, note that in order for f_constraint to be used like an applied 
/// force (i.e. appear on the RHS), the multipliers are negated in the call to
/// obtain Simbody constraint forces.
void calcAccelerationsFromMultipliers(const Model& model, const State& state, 
        const Vector& multipliers, Vector& udot) {
    const SimTK::MultibodySystem& multibody = model.getMultibodySystem();
    const SimTK::Vector_<SimTK::SpatialVec>& appliedBodyForces = 
        multibody.getRigidBodyForces(state, SimTK::Stage::Dynamics);
    const Vector& appliedMobilityForces = multibody.getMobilityForces(state,
        SimTK::Stage::Dynamics);

    const SimTK::SimbodyMatterSubsystem& matter = model.getMatterSubsystem();
    SimTK::Vector_<SimTK::SpatialVec> constraintBodyForces;
    Vector constraintMobilityForces;
    // Multipliers are negated so constraint forces can be used like applied 
    // forces.
    matter.calcConstraintForcesFromMultipliers(state, -multipliers, 
        constraintBodyForces, constraintMobilityForces);

    SimTK::Vector_<SimTK::SpatialVec> A_GB;
    matter.calcAccelerationIgnoringConstraints(state, 
        appliedMobilityForces + constraintMobilityForces,
        appliedBodyForces + constraintBodyForces, udot, A_GB);
}

/// The following tests add a constraint to a model and check that the method
/// calcAccelerationsFromMultipliers() is implemented correctly. Each test 
/// follows a similar structure:
///     1) Create a model and add a constraint between two bodies
///     2) Create a random state and realize the model to Stage::Acceleration
///     3) Check that state contains at least one Lagrange multiplier
///     4) Compute the model accelerations from Simbody
///     5) Retrieve the Lagrange multiplier values for the current state
///     6) Compute the accelerations from calcAccelerationsFromMultipliers()
///     7) Ensure that the accelerations from step 4 and 6 match

void testWeldConstraint() {
    State state;
    Model model = createModel();
    const std::string& firstBodyName =
            model.getBodySet().get(0).getAbsolutePathString();
    const std::string& lastBodyName =
            model.getBodySet().get(NUM_BODIES-1).getAbsolutePathString();
    WeldConstraint* constraint = new WeldConstraint("weld", firstBodyName, 
        lastBodyName);
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers = 
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers, 
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

void testPointConstraint() {
    State state;
    Model model = createModel();
    const Body& firstBody = model.getBodySet().get(0);
    const Body& lastBody = model.getBodySet().get(NUM_BODIES - 1);
    PointConstraint* constraint = new PointConstraint(firstBody, Vec3(0), 
        lastBody, Vec3(0));
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);
    
    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers, 
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

void testPointOnLineConstraint() {
    State state;
    Model model = createModel();
    const Body& firstBody = model.getBodySet().get(0);
    const Body& lastBody = model.getBodySet().get(NUM_BODIES - 1);
    PointOnLineConstraint* constraint = new PointOnLineConstraint(firstBody,
        Vec3(1,0,0), Vec3(0), lastBody, Vec3(0));
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers, 
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

void testConstantDistanceConstraint() {
    State state;
    Model model = createModel();
    const Body& firstBody = model.getBodySet().get(0);
    const Body& lastBody =
        model.getBodySet().get(NUM_BODIES - 1);
    ConstantDistanceConstraint* constraint = new ConstantDistanceConstraint(
        firstBody, Vec3(0), lastBody, Vec3(0), 4.56);
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers, 
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

void testLockedCoordinate() {
    State state;
    Model model = createModel();
    CoordinateSet& coordSet = model.updCoordinateSet();
    coordSet.getLast()->set_locked(true);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers, 
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

void testCoordinateCouplerConstraint() {
    State state;
    Model model = createModel();
    CoordinateSet& coordSet = model.updCoordinateSet();
    CoordinateCouplerConstraint* constraint = new CoordinateCouplerConstraint();
    Array<std::string> names;
    coordSet.getNames(names);
    constraint->setIndependentCoordinateNames(names.get(0));
    constraint->setDependentCoordinateName(names.getLast());
    LinearFunction func(1.0, 0.0);
    constraint->setFunction(func);
    model.addConstraint(constraint);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers,
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

void testPrescribedMotion() {
    State state;
    Model model = createModel();
    CoordinateSet& coordSet = model.updCoordinateSet();
    LinearFunction func(1.0, 0.0);
    coordSet.getLast()->setPrescribedFunction(func);
    coordSet.getLast()->setDefaultIsPrescribed(true);
    createState(model, state);
    // Check that constraint was added successfully.
    SimTK_TEST(state.getNMultipliers() > 0);

    const Vector& udotSimbody = model.getMatterSubsystem().getUDot(state);
    const Vector& multipliers =
        model.getMatterSubsystem().getConstraintMultipliers(state);
    Vector udotMultipliers;
    calcAccelerationsFromMultipliers(model, state, multipliers, 
        udotMultipliers);
    // Check that accelerations calculated from Lagrange multipliers match
    // Simbody's accelerations.
    MACHINE_TEST(udotSimbody, udotMultipliers);
}

/// Create a torque-actuated double pendulum model. Each subtest will add to the
/// model the relevant constraint(s).
Model createDoublePendulumModel() {
    Model model;
    model.setName("double_pendulum");

    using SimTK::Vec3;
    using SimTK::Inertia;

    // Create two links, each with a mass of 1 kg, center of mass at the body's
    // origin, and moments and products of inertia of zero.
    auto* b0 = new OpenSim::Body("b0", 1, Vec3(0), Inertia(1));
    model.addBody(b0);
    auto* b1 = new OpenSim::Body("b1", 1, Vec3(0), Inertia(1));
    model.addBody(b1);

    // Add station representing the model end-effector.
    auto* endeff = new Station(*b1, Vec3(0));
    endeff->setName("endeff");
    model.addComponent(endeff);

    // Connect the bodies with pin joints. Assume each body is 1 m long.
    auto* j0 = new PinJoint("j0", model.getGround(), Vec3(0), Vec3(0),
        *b0, Vec3(-1, 0, 0), Vec3(0));
    auto& q0 = j0->updCoordinate();
    q0.setName("q0");
    q0.setDefaultValue(0);
    auto* j1 = new PinJoint("j1",
        *b0, Vec3(0), Vec3(0), *b1, Vec3(-1, 0, 0), Vec3(0));
    auto& q1 = j1->updCoordinate();
    q1.setName("q1");
    q1.setDefaultValue(SimTK::Pi);
    model.addJoint(j0);
    model.addJoint(j1);

    // Add coordinate actuators.
    auto* tau0 = new CoordinateActuator();
    tau0->setCoordinate(&j0->updCoordinate());
    tau0->setName("tau0");
    tau0->setOptimalForce(1);
    model.addComponent(tau0);
    auto* tau1 = new CoordinateActuator();
    tau1->setCoordinate(&j1->updCoordinate());
    tau1->setName("tau1");
    tau1->setOptimalForce(1);
    model.addComponent(tau1);
    
    // Add display geometry.
    Ellipsoid bodyGeometry(0.5, 0.1, 0.1);
    SimTK::Transform transform(SimTK::Vec3(-0.5, 0, 0));
    auto* b0Center = new PhysicalOffsetFrame("b0_center", *b0, transform);
    b0->addComponent(b0Center);
    b0Center->attachGeometry(bodyGeometry.clone());
    auto* b1Center = new PhysicalOffsetFrame("b1_center", *b1, transform);
    b1->addComponent(b1Center);
    b1Center->attachGeometry(bodyGeometry.clone());

    model.finalizeConnections();

    return model;
}

/// Run a forward simulation using controls from an OCP solution and compare the
/// state trajectories.
MucoIterate runForwardSimulation(Model model, const MucoSolution& solution, 
    const double& tol) {

    // Get actuator names.
    model.initSystem();
    OpenSim::Array<std::string> actuNames;
    const auto modelPath = model.getAbsolutePath();
    for (const auto& actu : model.getComponentList<Actuator>()) {
        actuNames.append(actu.getAbsolutePathString());
    }

    // Add prescribed controllers to actuators in the model, where the control
    // functions are splined versions of the actuator controls from the OCP 
    // solution.
    const SimTK::Vector& time = solution.getTime();
    auto* controller = new PrescribedController();
    controller->setName("prescribed_controller");
    for (int i = 0; i < actuNames.size(); ++i) {
        const auto control = solution.getControl(actuNames[i]);
        auto* controlFunction = new GCVSpline(5, time.nrow(), &time[0], 
            &control[0]);
        const auto& actu = model.getComponent<Actuator>(actuNames[i]);
        controller->addActuator(actu);
        controller->prescribeControlForActuator(actu.getName(),
                controlFunction);
    }
    model.addController(controller);

    // Add states reporter to the model.
    auto* statesRep = new StatesTrajectoryReporter();
    statesRep->setName("states_reporter");
    statesRep->set_report_time_interval(0.001);
    model.addComponent(statesRep);

    // Add a TableReporter to collect the controls.
    auto* controlsRep = new TableReporter();
    for (int i = 0; i < actuNames.size(); ++i) {
        controlsRep->addToReport(
            model.getComponent(actuNames[i]).getOutput("actuation"), 
            actuNames[i]);
    }
    model.addComponent(controlsRep);
    
    // Simulate!
    SimTK::State state = model.initSystem();
    state.setTime(time[0]);
    Manager manager(model);
    manager.getIntegrator().setAccuracy(1e-9);
    manager.initialize(state);
    state = manager.integrate(time[time.size()-1]);

    // Export results from states reporter to a TimeSeries Table
    TimeSeriesTable states;
    states = statesRep->getStates().exportToTable(model);


    TimeSeriesTable controls;
    controls = controlsRep->getTable();
    
    // Create a MucoIterate to facilitate states trajectory comparison (with
    // dummy data for the multipliers, which we'll ignore).
    const auto& statesTimes = states.getIndependentColumn();
    SimTK::Vector timeVec((int)statesTimes.size(), statesTimes.data(), true);
    auto forwardSolution = MucoIterate(timeVec, states.getColumnLabels(),
        controls.getColumnLabels(), states.getColumnLabels(), {}, 
        states.getMatrix(), controls.getMatrix(), states.getMatrix(),
        SimTK::RowVector(0));
    
    // Compare controls between foward simulation and OCP solution. These
    // should match very closely, since the foward simulation controls are 
    // created from splines of the OCP solution controls
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(forwardSolution,
        {"none"}, {}, {"none"}), 0, 1e-9);

    // Compare states trajectory between forward simulation and OCP solution.
    // The states trajectory may not match as well as the controls.
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(forwardSolution,
        {}, {"none"}, {"none"}), 0, tol);

    return forwardSolution;
}

/// Solve an optimal control problem where a double pendulum must reach a 
/// specified final configuration while subject to a constraint that its
/// end-effector must lie on a vertical line through the origin and minimize
/// control effort.
void testDoublePendulumPointOnLine() {
    MucoTool muco;
    muco.setName("double_pendulum_point_on_line");
    MucoProblem& mp = muco.updProblem();
    // Create double pendulum model and add the point-on-line constraint. The 
    // constraint consists of a vertical line in the y-direction (defined in 
    // ground) and the model end-effector point (the origin of body "b1").
    Model model = createDoublePendulumModel();
    const Body& b1 = model.getBodySet().get("b1");
    const Station& endeff = model.getComponent<Station>("endeff");
    PointOnLineConstraint* constraint = new PointOnLineConstraint(
        model.getGround(), Vec3(0, 1, 0), Vec3(0), b1, endeff.get_location());
    model.addConstraint(constraint);
    model.finalizeConnections();
    mp.setModel(model);

    mp.setTimeBounds(0, 1);
    // Coordinate value state boundary conditions are consistent with the 
    // point-on-line constraint and should require the model to "unfold" itself.
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0, SimTK::Pi / 2);
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50}, 0, 0);
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10}, SimTK::Pi, 0);
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50}, 0, 0);
    mp.setControlInfo("/tau0", {-100, 100});
    mp.setControlInfo("/tau1", {-100, 100});

    MucoControlCost effort;
    mp.addCost(effort);

    MucoTropterSolver& ms = muco.initSolver();
    ms.set_num_mesh_points(15);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    //ms.set_optim_ipopt_print_level(5);
    ms.set_optim_hessian_approximation("exact");
    ms.setGuess("bounds");

    MucoSolution solution = muco.solve();
    solution.write("testConstraints_testDoublePendulumPointOnLine.sto");
    //muco.visualize(solution);

    model.initSystem();
    StatesTrajectory states = solution.exportToStatesTrajectory(mp);
    for (const auto& s : states) {
        model.realizePosition(s);
        const SimTK::Vec3& loc = endeff.getLocationInGround(s);

        // The end-effector should not have moved in the x- or z-directions.
        // TODO: may need to adjust these tolerances
        SimTK_TEST_EQ_TOL(loc[0], 0, 1e-6);
        SimTK_TEST_EQ_TOL(loc[2], 0, 1e-6);
    }

    // Run a forward simulation using the solution controls in prescribed 
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    runForwardSimulation(model, solution, 1e-1);
}

/// Solve an optimal control problem where a double pendulum must reach a 
/// specified final configuration while subject to a constraint that couples
/// its two coordinates together via a linear relationship and minimizing 
/// control effort.
void testDoublePendulumCoordinateCoupler(MucoSolution& solution) {
    MucoTool muco;
    muco.setName("double_pendulum_coordinate_coupler");
    MucoProblem& mp = muco.updProblem();

    // Create double pendulum model and add the coordinate coupler constraint. 
    Model model = createDoublePendulumModel();
    const Coordinate& q0 = model.getCoordinateSet().get("q0");
    const Coordinate& q1 = model.getCoordinateSet().get("q1");
    CoordinateCouplerConstraint* constraint = new CoordinateCouplerConstraint();
    Array<std::string> indepCoordNames;
    indepCoordNames.append("q0");
    constraint->setIndependentCoordinateNames(indepCoordNames);
    constraint->setDependentCoordinateName("q1");
    // Represented by the following equation,
    //      q1 = m*q0 + b
    // this linear function couples the two model coordinates such that given 
    // the boundary conditions for q0 from testDoublePendulumPointOnLine, the
    // same boundary conditions for q1 should be achieved without imposing 
    // bounds for this coordinate.
    const SimTK::Real m = -2;
    const SimTK::Real b = SimTK::Pi;
    LinearFunction linFunc(m, b);
    constraint->setFunction(linFunc);
    model.addConstraint(constraint);
    mp.setModel(model);

    mp.setTimeBounds(0, 1);
    // Boundary conditions are only enforced for the first coordinate, so we can
    // test that the second coordinate is properly coupled.
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0, SimTK::Pi / 2);
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50}, 0, 0);
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10});
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50}, 0, 0);
    mp.setControlInfo("/tau0", {-100, 100});
    mp.setControlInfo("/tau1", {-100, 100});

    MucoControlCost effort;
    mp.addCost(effort);

    MucoTropterSolver& ms = muco.initSolver();
    ms.set_num_mesh_points(50);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    //ms.set_optim_ipopt_print_level(5);
    ms.set_optim_hessian_approximation("limited-memory");
    ms.setGuess("bounds");

    solution = muco.solve();
    solution.write("testConstraints_testDoublePendulumCoordinateCoupler.sto");
    //muco.visualize(solution);

    model.initSystem();
    StatesTrajectory states = solution.exportToStatesTrajectory(mp);
    for (const auto& s : states) {
        model.realizePosition(s);

        // The coordinates should be coupled according to the linear function
        // described above.
        SimTK_TEST_EQ_TOL(q1.getValue(s), m*q0.getValue(s) + b, 1e-6);
    }

    // Run a forward simulation using the solution controls in prescribed 
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    runForwardSimulation(model, solution, 1e-2);
}

/// Solve an optimal control problem where a double pendulum must follow a
/// prescribed motion based on the previous test case (see
/// testDoublePendulumCoordinateCoupler).
void testDoublePendulumPrescribedMotion(MucoSolution& couplerSolution) {
    MucoTool muco;
    muco.setName("double_pendulum_prescribed_motion");
    MucoProblem& mp = muco.updProblem();

    // Create double pendulum model. 
    Model model = createDoublePendulumModel();
    // Create a spline set for the model states from the previous solution. We 
    // need to call initSystem() and set the model here in order to convert the 
    // solution from the previous problem to a StatesTrajectory.
    model.initSystem();
    mp.setModel(model);

    GCVSplineSet statesSpline(
        couplerSolution.exportToStatesTrajectory(mp).exportToTable(model));

    // Apply the prescribed motion constraints.
    Coordinate& q0 = model.updJointSet().get("j0").updCoordinate();
    q0.setPrescribedFunction(statesSpline.get("/jointset/j0/q0/value"));
    q0.setDefaultIsPrescribed(true);
    Coordinate& q1 = model.updJointSet().get("j1").updCoordinate();
    q1.setPrescribedFunction(statesSpline.get("/jointset/j1/q1/value"));
    q1.setDefaultIsPrescribed(true);
    // Set the model again after implementing the constraints.
    mp.setModel(model);

    mp.setTimeBounds(0, 1);
    // No bounds here, since the problem is already highly constrained by the
    // prescribed motion constraints on the coordinates.
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10});
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50});
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10});
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50});
    mp.setControlInfo("/tau0", {-100, 100});
    mp.setControlInfo("/tau1", {-100, 100});

    MucoControlCost effort;
    mp.addCost(effort);

    MucoTropterSolver& ms = muco.initSolver();
    ms.set_num_mesh_points(50); 
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    //ms.set_optim_ipopt_print_level(5);
    ms.set_optim_hessian_approximation("limited-memory");
    ms.setGuess("bounds");

    MucoSolution solution = muco.solve().unseal();
    solution.write("testConstraints_testDoublePendulumPrescribedMotion.sto");
    //muco.visualize(solution);

    // Create a TimeSeriesTable containing the splined state data from 
    // testDoublePendulumCoordinateCoupler. Since this splined data could be 
    // somewhat different from the coordinate coupler OCP solution, we use this 
    // to create a direct comparison between the prescribed motion OCP solution 
    // states and exactly what the PrescribedMotion constraints should be
    // enforcing.
    auto statesTraj = solution.exportToStatesTrajectory(mp);
    // Initialize data structures to use in the TimeSeriesTable
    // convenience constructor.
    std::vector<double> indVec((int)statesTraj.getSize());
    SimTK::Matrix depData((int)statesTraj.getSize(), 
        (int)solution.getStateNames().size());
    Vector timeVec(1);
    for (int i = 0; i < (int)statesTraj.getSize(); ++i) {
        const auto& s = statesTraj.get(i);
        const SimTK::Real& time = s.getTime();
        indVec[i] = time;
        timeVec.updElt(0,0) = time;
        depData.set(i, 0,
                statesSpline.get("/jointset/j0/q0/value").calcValue(timeVec));
        depData.set(i, 1,
                statesSpline.get("/jointset/j1/q1/value").calcValue(timeVec));
        // The values for the speed states are created from the spline 
        // derivative values.
        depData.set(i, 2,
                statesSpline.get("/jointset/j0/q0/value").calcDerivative({0},
            timeVec));
        depData.set(i, 3,
                statesSpline.get("/jointset/j1/q1/value").calcDerivative({0},
            timeVec));
    }
    TimeSeriesTable splineStateValues(indVec, depData, 
        solution.getStateNames());

    // Create a MucoIterate containing the splined state values. The splined
    // state values are also set for the controls and adjuncts as dummy data.
    const auto& statesTimes = splineStateValues.getIndependentColumn();
    SimTK::Vector time((int)statesTimes.size(), statesTimes.data(), true);
    auto mucoIterSpline = MucoIterate(time, splineStateValues.getColumnLabels(),
        splineStateValues.getColumnLabels(), splineStateValues.getColumnLabels(), 
        {}, splineStateValues.getMatrix(), splineStateValues.getMatrix(), 
        splineStateValues.getMatrix(), SimTK::RowVector(0));

    // Only compare the position-level values between the current solution 
    // states and the states from the previous test (original and splined).  
    // These should match well, since position-level values are enforced 
    // directly via a path constraint in the current problem formulation (see 
    // MucoTropterSolver for details).
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(mucoIterSpline, 
        {"/jointset/j0/q0/value", "/jointset/j1/q1/value"}, {"none"}, {"none"}),
                0, 1e-12);
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(couplerSolution,
        {"/jointset/j0/q0/value", "/jointset/j1/q1/value"}, {"none"}, {"none"}),
                0, 1e-12);
    // Only compare the velocity-level values between the current solution 
    // states and the states from the previous test (original and splined).  
    // These won't match as well as the position-level values, since velocity-
    // level errors are not enforced in the current problem formulation.
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(mucoIterSpline,
        {"/jointset/j0/q0/speed", "/jointset/j1/q1/speed"}, {"none"}, {"none"}),
                0, 1e-2);
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(couplerSolution,
        {"/jointset/j0/q0/speed", "/jointset/j1/q1/speed"}, {"none"}, {"none"}),
                0, 1e-2);
    // Compare only the actuator controls. These match worse compared to the
    // velocity-level states. It is currently unclear to what extent this is 
    // related to velocity-level states not matching well or the how the model
    // constraints are enforced in the current formulation.
    SimTK_TEST_EQ_TOL(solution.compareContinuousVariablesRMS(couplerSolution, 
        {"none"}, {"/tau0", "/tau1"}, {"none"}), 0, 1);

    // Run a forward simulation using the solution controls in prescribed 
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    runForwardSimulation(model, solution, 1e-2);
}

class EqualControlConstraint : public MucoPathConstraint {
OpenSim_DECLARE_CONCRETE_OBJECT(EqualControlConstraint, MucoPathConstraint);
protected:
    void initializeImpl() const override {
        // Make sure the model generates a state object with the two controls we 
        // expect, no more and no less.
        const auto state = getModel().getWorkingState();
        getModel().realizeVelocity(state);
        OPENSIM_THROW_IF(getModel().getControls(state).size() != 2, Exception,
            "State has incorrect number of controls (two expected).");

        // There is only constraint equation: match the two model controls.
        setNumEquations(1);
    }
    void calcPathConstraintErrorsImpl(const SimTK::State& state,
            SimTK::Vector& errors) const override {
        getModel().realizeVelocity(state);

        const auto& controls = getModel().getControls(state);
        // In the problem below, the actuators are bilateral and act in 
        // opposite directions, so we use addition to create the residual here.
        errors[0] = controls[1] + controls[0];
    }
};

/// Solve an optimal control problem where a double pendulum must reach a 
/// specified final configuration while subject to a constraint that its
/// actuators must produce an equal control trajectory.
void testDoublePendulumEqualControl() {
    MucoTool muco;
    muco.setName("double_pendulum_equal_control");
    MucoProblem& mp = muco.updProblem();
    Model model = createDoublePendulumModel();
    mp.setModel(model);
    
    MucoConstraintInfo cInfo;
    cInfo.setBounds(std::vector<MucoBounds>(1, {0, 0}));
    EqualControlConstraint equalControlConstraint;
    equalControlConstraint.setConstraintInfo(cInfo);
    mp.addPathConstraint(equalControlConstraint);

    mp.setTimeBounds(0, 1);
    // Coordinate value state boundary conditions are consistent with the 
    // point-on-line constraint and should require the model to "unfold" itself.
    mp.setStateInfo("/jointset/j0/q0/value", {-10, 10}, 0, SimTK::Pi / 2);
    mp.setStateInfo("/jointset/j0/q0/speed", {-50, 50}, 0, 0);
    mp.setStateInfo("/jointset/j1/q1/value", {-10, 10});
    mp.setStateInfo("/jointset/j1/q1/speed", {-50, 50}, 0, 0);
    mp.setControlInfo("/tau0", {-100, 100});
    mp.setControlInfo("/tau1", {-100, 100});

    MucoControlCost effort;
    mp.addCost(effort);

    MucoTropterSolver& ms = muco.initSolver();
    ms.set_num_mesh_points(100);
    ms.set_verbosity(2);
    ms.set_optim_solver("ipopt");
    ms.set_optim_convergence_tolerance(1e-3);
    //ms.set_optim_ipopt_print_level(5);
    ms.set_optim_hessian_approximation("limited-memory");
    ms.setGuess("bounds");

    MucoSolution solution = muco.solve();
    solution.write("testConstraints_testDoublePendulumEqualControl.sto");
    //muco.visualize(solution);

    const auto& control_tau0 = solution.getControl("/tau0");
    const auto& control_tau1 = solution.getControl("/tau1");
    const auto& control_res = control_tau1.abs() - control_tau0.abs();

    SimTK_TEST_EQ_TOL(control_res.normRMS(), 0, 1e-6);
    
    // Run a forward simulation using the solution controls in prescribed 
    // controllers for the model actuators and see if we get the correct states
    // trajectory back.
    // TODO why does the forward solution match so poorly here?
    MucoIterate forwardSolution = runForwardSimulation(model, solution, 2);
    //muco.visualize(forwardSolution);

    // Test de/serialization.
    // ======================
    std::string setup_fname 
        = "testConstraints_testDoublePendulumEqualControl.omuco";
    muco.print(setup_fname);
    MucoSolution solutionDeserialized;
    MucoTool mucoDeserialize(setup_fname);
    solutionDeserialized = mucoDeserialize.solve();
    SimTK_TEST(solution.isNumericallyEqual(solutionDeserialized));
}

int main() {
    OpenSim::Object::registerType(EqualControlConstraint());

    SimTK_START_TEST("testConstraints");
        // DAE calculation subtests.
        SimTK_SUBTEST(testWeldConstraint);
        SimTK_SUBTEST(testPointConstraint);
        SimTK_SUBTEST(testPointOnLineConstraint);
        SimTK_SUBTEST(testConstantDistanceConstraint);
        SimTK_SUBTEST(testLockedCoordinate);
        SimTK_SUBTEST(testCoordinateCouplerConstraint);
        SimTK_SUBTEST(testPrescribedMotion);
        // Direct collocation subtests.
        SimTK_SUBTEST(testDoublePendulumPointOnLine);
        MucoSolution couplerSolution;
        SimTK_SUBTEST1(testDoublePendulumCoordinateCoupler, couplerSolution);
        SimTK_SUBTEST1(testDoublePendulumPrescribedMotion, couplerSolution);
        SimTK_SUBTEST(testDoublePendulumEqualControl);
    SimTK_END_TEST();
}
