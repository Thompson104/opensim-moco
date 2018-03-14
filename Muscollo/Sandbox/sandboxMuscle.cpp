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
#include <DeGrooteFregly2016Muscle.h>

#include <OpenSim/Simulation/SimbodyEngine/SliderJoint.h>
#include <OpenSim/Actuators/Millard2012EquilibriumMuscle.h>

using namespace OpenSim;

// TODO move into the actual test case.
void testDeGrooteFregly2016Muscle() {

    Model model;
    auto* musclePtr = new DeGrooteFregly2016Muscle();
    musclePtr->setName("muscle");
    musclePtr->addNewPathPoint("origin", model.updGround(), SimTK::Vec3(0));
    musclePtr->addNewPathPoint("insertion", model.updGround(),
            SimTK::Vec3(1.0, 0, 0));
    model.addComponent(musclePtr);
    auto& muscle = model.getComponent<DeGrooteFregly2016Muscle>("muscle");

    // Property value bounds
    // ---------------------
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_optimal_force(1.5);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(), Exception);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_default_norm_fiber_length(0.1999);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_default_norm_fiber_length(1.800001);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_activation_time_constant(0);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_deactivation_time_constant(0);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_default_activation(-0.0001);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_fiber_damping(-0.0001);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }
    {
        DeGrooteFregly2016Muscle musc = muscle;
        musc.set_tendon_strain_at_one_norm_force(0);
        SimTK_TEST_MUST_THROW_EXC(musc.finalizeFromProperties(),
                SimTK::Exception::ErrorCheck);
    }

    // printCurvesToSTOFiles()
    // -----------------------
    muscle.printCurvesToSTOFiles();

    printMessage("%f %f %f %f %f %f\n",
            muscle.calcTendonForceMultiplier(1),
            muscle.calcPassiveForceMultiplier(1),
            muscle.calcActiveForceLengthMultiplier(1),
            muscle.calcForceVelocityMultiplier(-1),
            muscle.calcForceVelocityMultiplier(0),
            muscle.calcForceVelocityMultiplier(1));


    // Test that the force-velocity curve inverse is correct.
    // ------------------------------------------------------
    const auto normFiberVelocity = createVectorLinspace(100, -1, 1);
    for (int i = 0; i < normFiberVelocity.nrow(); ++i) {
        const SimTK::Real& vMTilde = normFiberVelocity[i];
        SimTK_TEST_EQ(muscle.calcForceVelocityInverseCurve(
                muscle.calcForceVelocityMultiplier(vMTilde)), vMTilde);
    }

    // solveBisection().
    // -----------------
    {
        auto calcResidual = [](const SimTK::Real& x) {
            return x - 3.78;
        };
        {
            const auto root =
                    muscle.solveBisection(calcResidual, -5, 5, 1e-6, 1e-12);
            SimTK_TEST_EQ_TOL(root, 3.78, 1e-6);
            // Make sure the x tolerance has an effect.
            SimTK_TEST_NOTEQ_TOL(root, 3.78, 1e-10);
        }
        {
            const auto root =
                    muscle.solveBisection(calcResidual, -5, 5, 1e-10, 1e-12);
            SimTK_TEST_EQ_TOL(root, 3.78, 1e-10);
        }
        // Make sure the y tolerance has an effect.
        {
            const auto root =
                    muscle.solveBisection(calcResidual, -5, 5, 1e-12, 1e-4);
            const auto residual = calcResidual(root);
            SimTK_TEST_EQ_TOL(residual, 0, 1e-4);
            // Make sure the x tolerance has an effect.
            SimTK_TEST_NOTEQ_TOL(residual, 0, 1e-10);
        }
        {
            const auto root =
                    muscle.solveBisection(calcResidual, -5, 5, 1e-12, 1e-10);
            const auto residual = calcResidual(root);
            SimTK_TEST_EQ_TOL(residual, 0, 1e-10);
        }
    }
    {
        auto parabola = [](const SimTK::Real& x) {
            return SimTK::square(x - 2.5);
        };
        SimTK_TEST_MUST_THROW_EXC(muscle.solveBisection(parabola, -5, 5),
                Exception);
    }

    SimTK::State state = model.initSystem();

    // getActivation(), setActivation()
    // --------------------------------
    SimTK_TEST_EQ(muscle.getActivation(state), muscle.get_default_activation());
    muscle.setActivation(state, 0.451);
    SimTK_TEST_EQ(muscle.getActivation(state), 0.451);
    // This model only has the muscle states, so activation is index 0.
    SimTK_TEST_EQ(state.getY()[0], 0.451);



}

Model createHangingMuscleModel(bool ignoreTendonCompliance) {
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

    auto* actu = new DeGrooteFregly2016Muscle();
    actu->setName("actuator");
    actu->set_max_isometric_force(30.0);
    actu->set_optimal_fiber_length(0.10);
    actu->set_tendon_slack_length(0.05);
    actu->set_tendon_strain_at_one_norm_force(0.10);
    actu->set_ignore_tendon_compliance(ignoreTendonCompliance);
    actu->set_max_contraction_velocity(10);
    actu->addNewPathPoint("origin", model.updGround(), SimTK::Vec3(0));
    actu->addNewPathPoint("insertion", *body, SimTK::Vec3(0));
    model.addForce(actu);


    /*
    auto* actu = new Millard2012EquilibriumMuscle();
    // TODO actu->set_fiber_damping(0);
    actu->setName("actuator");
    actu->set_max_isometric_force(30.0);
    actu->set_optimal_fiber_length(0.10);
    actu->set_ignore_tendon_compliance(ignoreTendonCompliance);
    actu->set_tendon_slack_length(0.05);
    actu->set_pennation_angle_at_optimal(0); // TODO 0.1);
    actu->set_max_contraction_velocity(10);
    actu->addNewPathPoint("origin", model.updGround(), SimTK::Vec3(0));
    actu->addNewPathPoint("insertion", *body, SimTK::Vec3(0));
    model.addForce(actu);
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

void testHangingMuscleMinimumTime(bool ignoreTendonCompliance) {

    std::cout << "testHangingMuscleMinimumTime. "
            << "ignoreTendonCompliance: " << ignoreTendonCompliance
            << "." << std::endl;

    SimTK::Real initActivation = 0.01;
    SimTK::Real initHeight = 0.15;
    SimTK::Real finalHeight = 0.14;

    Model model = createHangingMuscleModel(ignoreTendonCompliance);

    // Passive forward simulation.
    // ---------------------------
    // auto* controller = new PrescribedController();
    // controller->addActuator(model.getComponent<Actuator>("actuator"));
    // controller->prescribeControlForActuator("actuator", new Constant(0.10));
    // model.addController(controller);
    // model.finalizeFromProperties();
    //
    // TableReporter* rep = new TableReporter();
    // rep->addToReport(model.getComponent("actuator/geometrypath").getOutput("length"));
    // rep->addToReport(model.getComponent("actuator").getOutput("normalized_fiber_length"));
    // rep->addToReport(model.getComponent("actuator").getOutput("normalized_fiber_velocity"));
    // // rep->addToReport(model.getComponent("actuator").getOutput("fiber_velocity"));
    // rep->addToReport(model.getComponent("actuator").getOutput("force_velocity_multiplier"));
    // rep->set_report_time_interval(0.001);
    // model.addComponent(rep);

    SimTK::State state = model.initSystem();
    const auto& actuator = model.getComponent("actuator");

    const auto* dgf = dynamic_cast<const DeGrooteFregly2016Muscle*>(&actuator);
    const bool usingDGF = dgf != nullptr;

    if (!ignoreTendonCompliance) {
        model.setStateVariableValue(state, "actuator/activation",
                initActivation);
        model.setStateVariableValue(state, "joint/height/value", initHeight);
        model.equilibrateMuscles(state);
        if (usingDGF) {
            std::cout << "Equilibrium norm fiber length: "
                    << model.getStateVariableValue(state,
                            "actuator/norm_fiber_length")
                    << std::endl;
        } else {
            std::cout << "Equilibrium fiber length: "
                    << model.getStateVariableValue(state,
                            "actuator/fiber_length")
                    << std::endl;
        }
    }
    //
    // Manager manager(model, state);
    // manager.getIntegrator().setAccuracy(1e-10);
    // manager.getIntegrator().setMaximumStepSize(0.001);
    // manager.integrate(2.0);
    // // TODO STOFileAdapter::write(rep->getTable(), "DEBUG_sandboxMuscle_reporter.sto");
    // visualize(model, manager.getStateStorage());


    // Minimum time trajectory optimization.
    // -------------------------------------
    MucoSolution solutionTrajOpt;
    {
        MucoTool muco;
        MucoProblem& problem = muco.updProblem();
        problem.setModel(model);
        problem.setTimeBounds(0, {0.05, 1.0});
        // TODO this might have been the culprit when using the Millard muscle:
        // TODO TODO TODO
        problem.setStateInfo("joint/height/value", {0.10, 0.20},
                initHeight, finalHeight);
        problem.setStateInfo("joint/height/speed", {-10, 10}, 0, 0);
        // TODO initial fiber length?
        // TODO how to enforce initial equilibrium with explicit dynamics?
        if (!ignoreTendonCompliance) {
            if (usingDGF) {
                // We would prefer to use a range of [0.2, 1.8] but then IPOPT
                // tries very small fiber lengths that cause tendon stretch to be
                // HUGE, causing insanely high tendon forces.
                problem.setStateInfo("actuator/norm_fiber_length", {0.8, 1.8},
                        model.getStateVariableValue(state, "actuator/norm_fiber_length"));
            } else {
                problem.setStateInfo("actuator/fiber_length", {0.0, 0.3},
                        model.getStateVariableValue(state, "actuator/fiber_length"));
            }
        }
        // OpenSim might not allow activations of 0.
        problem.setStateInfo("actuator/activation", {0.01, 1}, initActivation);
        problem.setControlInfo("actuator", {0.01, 1});

        problem.addCost(MucoFinalTimeCost());

        MucoTropterSolver& solver = muco.initSolver();
        solver.set_optim_sparsity_detection("initial-guess");
        MucoIterate guessForwardSim = solver.createGuess("time-stepping");
        solver.setGuess(guessForwardSim);
        guessForwardSim.write("sandboxMuscle_guess_forward_sim.sto");
        std::cout << "Guess from forward sim: "
                << guessForwardSim.getStatesTrajectory() << std::endl;
        muco.visualize(guessForwardSim);

        solutionTrajOpt = muco.solve();
        std::string solutionFilename = "sandboxMuscle_solution";
        if (ignoreTendonCompliance)
            solutionFilename += "_rigidtendon";
        solutionFilename += ".sto";
        solutionTrajOpt.write(solutionFilename);
        std::cout << "Solution joint/height/value trajectory: "
                << solutionTrajOpt.getState("joint/height/value") << std::endl;
        std::cout << "Solution joint/height/speed trajectory: "
                << solutionTrajOpt.getState("joint/height/speed") << std::endl;
    }

    // Perform time stepping forward simulation using optimized controls.
    // ------------------------------------------------------------------
    // See if we end up at the correct final state.
    {
        // Add a controller to the model.
        const SimTK::Vector& time = solutionTrajOpt.getTime();
        const auto control = solutionTrajOpt.getControl("actuator");
        auto* controlFunction = new GCVSpline(5, time.nrow(), &time[0],
                &control[0]);
        auto* controller = new PrescribedController();
        controller->addActuator(model.getComponent<Actuator>("actuator"));
        controller->prescribeControlForActuator("actuator", controlFunction);
        model.addController(controller);

        // Set the initial state.
        SimTK::State state = model.initSystem();
        model.setStateVariableValue(state, "joint/height/value", initHeight);
        model.setStateVariableValue(state, "actuator/activation",
                initActivation);

        // Integrate.
        Manager manager(model, state);
        SimTK::State finalState = manager.integrate(time[time.nrow() - 1]);
        std::cout << "Time stepping forward sim joint/height/value history "
                << model.getStateVariableValue(finalState, "joint/height/value")
                << std::endl;
        SimTK_TEST_EQ_TOL(
                model.getStateVariableValue(finalState, "joint/height/value"),
                finalHeight, 1e-4);
        manager.getStateStorage().print("sandboxMuscle_timestepping.sto");
    }

    // Track the kinematics from the trajectory optimization.
    // ------------------------------------------------------
    // We will try to recover muscle activity.
    {
        std::cout << "Tracking the trajectory optimization coordinate solution."
                << std::endl;
        MucoTool muco;
        MucoProblem& problem = muco.updProblem();
        problem.setModel(model);
        // Using an equality constraint for the time bounds was essential for
        // recovering the correct excitation.
        const double finalTime =
                solutionTrajOpt.getTime()[solutionTrajOpt.getNumTimes() - 1];
        problem.setTimeBounds(0, finalTime);
        problem.setStateInfo("joint/height/value", {0.10, 0.20},
                initHeight, finalHeight);
        problem.setStateInfo("joint/height/speed", {-10, 10}, 0, 0);
        if (!ignoreTendonCompliance) {
            if (usingDGF) {
                // We would prefer to use a range of [0.2, 1.8] but then IPOPT
                // tries very small fiber lengths that cause tendon stretch to be
                // HUGE, causing insanely high tendon forces.
                problem.setStateInfo("actuator/norm_fiber_length", {0.8, 1.8},
                        model.getStateVariableValue(state, "actuator/norm_fiber_length"));
            } else {
                problem.setStateInfo("actuator/fiber_length", {0.0, 0.3},
                        model.getStateVariableValue(state, "actuator/fiber_length"));
            }
        }
        // OpenSim might not allow activations of 0.
        problem.setStateInfo("actuator/activation", {0.01, 1}, initActivation);
        problem.setControlInfo("actuator", {0.01, 1});

        MucoStateTrackingCost tracking;

        auto states = solutionTrajOpt.exportToStatesStorage().exportToTable();
        TimeSeriesTable ref(states.getIndependentColumn());
        ref.appendColumn("joint/height/value",
                states.getDependentColumn("joint/height/value"));
        // Tracking joint/height/speed slightly increases the iterations to
        // converge, and tracking activation cuts the iterations in half.
        // TODO try tracking all states, for fun.
        tracking.setReference(ref);
        tracking.setAllowUnusedReferences(true);
        problem.addCost(tracking);

        MucoTropterSolver& solver = muco.initSolver();
        solver.set_optim_sparsity_detection("initial-guess");
        solver.setGuess("time-stepping");
        // Don't need to use the TrajOpt solution as the initial guess; kinda
        // neat. Although, using TrajOpt for the guess improves convergence.
        // TODO solver.setGuess(solutionTrajOpt);

        MucoSolution solutionTrack = muco.solve();
        std::string solutionFilename = "sandboxMuscle_track_solution";
        if (ignoreTendonCompliance)
            solutionFilename += "_rigidtendon";
        solutionFilename += ".sto";
        solutionTrack.write(solutionFilename);
        double error = solutionTrack.compareStatesControlsRMS(solutionTrajOpt);
        std::cout << "RMS error for states and controls: " << error
                << std::endl;
    }

    // TODO perform the tracking with INDYGO.

    // TODO support constraining initial fiber lengths to their equilibrium
    // lengths in Tropter!!!!!!!!!!!!!! (in explicit mode).
}

/// Move a point mass from a fixed starting state to a fixed end
/// position and velocity, in fixed time, with minimum effort. The point mass
/// has 2 DOFs (x and y translation).
///
///                            |< d >|< d >|
///                    ----------------------
///                             \         /
///                              .       .
///                   left muscle \     / right muscle
///                                .   .
///                                 \ /
///                                  O mass
///
/// Here's a sketch of the problem we solve, with activation and fiber dynamics.
/// @verbatim
///   minimize   int_t (aL^2 + aR^2) dt
///   subject to multibody dynamics
///              muscle activation dynamics
///              muscle fiber dynamics
///              x(0) = -0.03
///              y(0) = -d
///              vx(0) = 0
///              vy(0) = 0
///              aL(0) = 0.01
///              aR(0) = 0.01
///              vmL(0) = 0
///              vmR(0) = 0
///              x(0.5) = +0.03
///              y(0.5) = -d + 0.05
///              vx(0.5) = 0
///              vy(0.5) = 0
/// @endverbatim
Model create2DOFs2MusclesModel(const double& width, bool ignoreTendonCompliance)
{

    using SimTK::Vec3;

    Model model;
    model.setName("block2dof2musc");

    // Massless intermediate body.
    auto* intermed = new Body("intermed", 0, Vec3(0), SimTK::Inertia(0));
    model.addComponent(intermed);
    auto* body = new Body("body", 1, Vec3(0), SimTK::Inertia(1));
    model.addComponent(body);

    // TODO Muscollo does not support locked coordinates yet.
    // Allow translation along x and y; disable rotation about z.
    // auto* joint = new PlanarJoint();
    // joint->setName("joint");
    // joint->connectSocket_parent_frame(model.getGround());
    // joint->connectSocket_child_frame(*body);
    // auto& coordTX = joint->updCoordinate(PlanarJoint::Coord::TranslationX);
    // coordTX.setName("tx");
    // auto& coordTY = joint->updCoordinate(PlanarJoint::Coord::TranslationY);
    // coordTY.setName("ty");
    // auto& coordRZ = joint->updCoordinate(PlanarJoint::Coord::RotationZ);
    // coordRZ.setName("rz");
    // coordRZ.setDefaultLocked(true);
    // model.addComponent(joint);
    auto* jointX = new SliderJoint();
    jointX->setName("jtx");
    jointX->connectSocket_parent_frame(model.getGround());
    jointX->connectSocket_child_frame(*intermed);
    auto& coordX = jointX->updCoordinate(SliderJoint::Coord::TranslationX);
    coordX.setName("tx");
    model.addComponent(jointX);

    // The joint's x axis must point in the global "+y" direction.
    auto* jointY = new SliderJoint("jty",
            *intermed, Vec3(0), Vec3(0, 0, 0.5 * SimTK::Pi),
            *body, Vec3(0), Vec3(0, 0, .5 * SimTK::Pi));
    auto& coordY = jointY->updCoordinate(SliderJoint::Coord::TranslationX);
    coordY.setName("ty");
    model.addComponent(jointY);

    {
        auto* actuL = new DeGrooteFregly2016Muscle();
        actuL->setName("left");
        actuL->set_ignore_tendon_compliance(ignoreTendonCompliance);
        actuL->set_max_isometric_force(40);
        actuL->set_optimal_fiber_length(.20);
        actuL->set_tendon_slack_length(0.10);
        actuL->set_pennation_angle_at_optimal(0.0);
        actuL->addNewPathPoint("origin", model.updGround(),
                Vec3(-width, 0, 0));
        actuL->addNewPathPoint("insertion", *body, Vec3(0));
        model.addForce(actuL);
    }
    {
        auto* actuR = new DeGrooteFregly2016Muscle();
        actuR->setName("right");
        actuR->set_ignore_tendon_compliance(ignoreTendonCompliance);
        actuR->set_max_isometric_force(40);
        actuR->set_optimal_fiber_length(.21);
        actuR->set_tendon_slack_length(0.09);
        actuR->set_pennation_angle_at_optimal(0.0);
        actuR->addNewPathPoint("origin", model.updGround(),
                Vec3(+width, 0, 0));
        actuR->addNewPathPoint("insertion", *body, Vec3(0));
        model.addForce(actuR);
    }

    model.print("sandboxMuscle_2dof2musc.osim");
    // model.setUseVisualizer(true);
    // SimTK::State s = model.initSystem();
    // model.equilibrateMuscles(s);
    // model.updMatterSubsystem().setShowDefaultGeometry(true);
    // model.updVisualizer().updSimbodyVisualizer().setBackgroundType(
    //         SimTK::Visualizer::GroundAndSky);
    // model.getVisualizer().show(s);
    // std::cin.get();
    // Manager manager(model, s);
    // manager.integrate(1.0);

    // TODO muscle is generating negative forces!

    return model;
}

void test2DOFs2Muscles(bool ignoreTendonCompliance) {

    const SimTK::Real width = 0.2;
    const SimTK::Real initActivation = 0.01;
    const SimTK::Real initTX = -0.03;
    const SimTK::Real initTY = -width;

    Model model = create2DOFs2MusclesModel(width, ignoreTendonCompliance);

    SimTK::State state = model.initSystem();

    model.setStateVariableValue(state, "jtx/tx/value", initTX);
    model.setStateVariableValue(state, "jty/ty/value", initTY);
    model.setStateVariableValue(state, "left/activation", initActivation);
    model.setStateVariableValue(state, "right/activation", initActivation);
    model.equilibrateMuscles(state);

    // Manager manager(model, state);
    // manager.getIntegrator().setAccuracy(1e-10);
    // manager.getIntegrator().setMaximumStepSize(0.001);
    // manager.integrate(2.0);
    // // TODO STOFileAdapter::write(rep->getTable(), "DEBUG_sandboxMuscle_reporter.sto");
    // visualize(model, manager.getStateStorage());

    // Minimum effort trajectory optimization.
    // ---------------------------------------
    MucoSolution solutionTrajOpt;
    {
        MucoTool muco;
        MucoProblem& problem = muco.updProblem();
        problem.setModel(model);
        problem.setTimeBounds(0, 0.5);
        problem.setStateInfo("jtx/tx/value", {-0.03, 0.03}, initTX, 0.03);
        problem.setStateInfo("jty/ty/value", {-2 * width, 0},
                initTY, -width + 0.05);
        problem.setStateInfo("jtx/tx/speed", {-15, 15}, 0, 0);
        problem.setStateInfo("jty/ty/speed", {-15, 15}, 0, 0);
        if (!ignoreTendonCompliance) {
            problem.setStateInfo("left/norm_fiber_length", {0.6, 1.8},
                    model.getStateVariableValue(state,
                            "left/norm_fiber_length"));
            problem.setStateInfo("right/norm_fiber_length", {0.6, 1.8},
                    model.getStateVariableValue(state,
                            "right/norm_fiber_length"));
        }
        // OpenSim might not allow activations of 0.
        problem.setStateInfo("left/activation", {0.01, 1}, initActivation);
        problem.setStateInfo("right/activation", {0.01, 1}, initActivation);
        problem.setControlInfo("left", {0.01, 1});
        problem.setControlInfo("right", {0.01, 1});

        // TODO change to activation?
        problem.addCost(MucoControlCost());

        MucoTropterSolver& solver = muco.initSolver();
        solver.set_optim_sparsity_detection("initial-guess");
        MucoIterate guessForwardSim = solver.createGuess("time-stepping");
        solver.setGuess(guessForwardSim);
        guessForwardSim.write("sandboxMuscle_guess_forward_sim.sto");
        // std::cout << "Guess from forward sim: "
        //         << guessForwardSim.getStatesTrajectory() << std::endl;
        muco.visualize(guessForwardSim);

        solutionTrajOpt = muco.solve();
        std::string solutionFilename = "sandboxMuscle_2dof2musc_solution.sto";
        solutionTrajOpt.write(solutionFilename);
        muco.visualize(solutionTrajOpt);

        // TODO compare solution to pure tropter problem
        // (test2Muscles2DOFsDeGrooteFregly2016.cpp).
    }

    // TODO look into negative muscle forces!

}

int main() {
    testDeGrooteFregly2016Muscle();

    testHangingMuscleMinimumTime(true);
    testHangingMuscleMinimumTime(false);

    test2DOFs2Muscles(true);
    // TODO test2DOFs2Muscles(false);

    return EXIT_SUCCESS;
}

