#include "MILPSolverGurobiLazy.h"

MILPSolverGurobiLazy::MILPSolverGurobiLazy()
{
	discreteVariablesActivated = true;

	gurobiEnv = new GRBEnv();
	gurobiModel = new GRBModel(*gurobiEnv);

	cachedSolutionHasChanged = true;
	isVariablesFixed = false;

	checkParameters();
}

MILPSolverGurobiLazy::~MILPSolverGurobiLazy()
{
	delete gurobiEnv;
	delete gurobiModel;
}

void MILPSolverGurobiLazy::initializeSolverSettings()
{
	MILPSolverGurobi::initializeSolverSettings();

	try
	{
		gurobiModel->set(GRB_IntParam_LazyConstraints, 1);
	}
	catch (GRBException &e)
	{
		ProcessInfo::getInstance().outputError("Error when initializing parameters for linear solver", e.getMessage());
	}
}

int MILPSolverGurobiLazy::increaseSolutionLimit(int increment)
{
	gurobiModel->getEnv().set(GRB_IntParam_SolutionLimit,
							  gurobiModel->getEnv().get(GRB_IntParam_SolutionLimit) + increment);

	return (gurobiModel->getEnv().get(GRB_IntParam_SolutionLimit));
}

void MILPSolverGurobiLazy::setSolutionLimit(long limit)
{
	if (limit > GRB_MAXINT)
		gurobiModel->getEnv().set(GRB_IntParam_SolutionLimit, GRB_MAXINT);
	else
		gurobiModel->getEnv().set(GRB_IntParam_SolutionLimit, limit);
}

int MILPSolverGurobiLazy::getSolutionLimit()
{
	return (gurobiModel->getEnv().get(GRB_IntParam_SolutionLimit));
}

void MILPSolverGurobiLazy::checkParameters()
{
}

E_ProblemSolutionStatus MILPSolverGurobiLazy::solveProblem()
{
	E_ProblemSolutionStatus MILPSolutionStatus;
	cachedSolutionHasChanged = true;

	try
	{
		GurobiCallback gurobiCallback = GurobiCallback(gurobiModel->getVars());
		gurobiModel->setCallback(&gurobiCallback);
		gurobiModel->optimize();

		MILPSolutionStatus = getSolutionStatus();
	}
	catch (GRBException &e)
	{
		ProcessInfo::getInstance().outputError("Error when solving MILP/LP problem", e.getMessage());
		MILPSolutionStatus = E_ProblemSolutionStatus::Error;
	}

	return (MILPSolutionStatus);
}

void GurobiCallback::callback()
{
	if (where == GRB_CB_POLLING || where == GRB_CB_PRESOLVE || where == GRB_CB_SIMPLEX || where == GRB_CB_MESSAGE || where == GRB_CB_BARRIER)
		return;

	try
	{
		// Check if better dual bound
		double tmpDualObjBound;

		if (where == GRB_CB_MIP || where == GRB_CB_MIPSOL || where == GRB_CB_MIPNODE)
		{
			switch (where)
			{
			case GRB_CB_MIP:
				tmpDualObjBound = getDoubleInfo(GRB_CB_MIP_OBJBND);
				break;
			case GRB_CB_MIPSOL:
				tmpDualObjBound = getDoubleInfo(GRB_CB_MIPSOL_OBJBND);
				break;
			case GRB_CB_MIPNODE:
				tmpDualObjBound = getDoubleInfo(GRB_CB_MIPNODE_OBJBND);
				break;
			default:
				break;
			}

			if ((isMinimization && tmpDualObjBound > ProcessInfo::getInstance().getDualBound()) || (!isMinimization && tmpDualObjBound < ProcessInfo::getInstance().getDualBound()))
			{
				std::vector<double> doubleSolution; // Empty since we have no point

				DualSolution sol =
					{doubleSolution, E_DualSolutionSource::MILPSolutionFeasible, tmpDualObjBound, ProcessInfo::getInstance().getCurrentIteration()->iterationNumber};
				ProcessInfo::getInstance().addDualSolutionCandidate(sol);
			}
		}

		if (where == GRB_CB_MIPSOL)
		{
			// Check for new primal solution
			double tmpPrimalObjBound = getDoubleInfo(GRB_CB_MIPSOL_OBJ);

			if ((tmpPrimalObjBound < 1e100) && ((isMinimization && tmpPrimalObjBound < ProcessInfo::getInstance().getPrimalBound()) || (!isMinimization && tmpPrimalObjBound > ProcessInfo::getInstance().getPrimalBound())))
			{
				std::vector<double> primalSolution(numVar);

				for (int i = 0; i < numVar; i++)
				{
					primalSolution.at(i) = getSolution(vars[i]);
				}

				SolutionPoint tmpPt;
				tmpPt.iterFound = ProcessInfo::getInstance().getCurrentIteration()->iterationNumber;
				tmpPt.maxDeviation = ProcessInfo::getInstance().originalProblem->getMostDeviatingConstraint(
					primalSolution);
				tmpPt.objectiveValue = ProcessInfo::getInstance().originalProblem->calculateOriginalObjectiveValue(
					primalSolution);
				tmpPt.point = primalSolution;

				ProcessInfo::getInstance().addPrimalSolutionCandidate(tmpPt, E_PrimalSolutionSource::LazyConstraintCallback);
			}
		}

		if (ProcessInfo::getInstance().isAbsoluteObjectiveGapToleranceMet() || ProcessInfo::getInstance().isRelativeObjectiveGapToleranceMet() || checkIterationLimit())
		{
			abort();
			return;
		}

		if (where == GRB_CB_MIPNODE && getIntInfo(GRB_CB_MIPNODE_STATUS) == GRB_OPTIMAL)
		{
			if (Settings::getInstance().getBoolSetting("AddHyperplanesForRelaxedLazySolutions", "Algorithm"))
			{
				std::vector<SolutionPoint> solutionPoints(1);

				std::vector<double> solution(numVar);

				for (int i = 0; i < numVar; i++)
				{
					solution.at(i) = getNodeRel(vars[i]);
				}

				auto mostDevConstr = ProcessInfo::getInstance().originalProblem->getMostDeviatingConstraint(solution);

				SolutionPoint tmpSolPt;

				tmpSolPt.point = solution;
				tmpSolPt.objectiveValue = ProcessInfo::getInstance().originalProblem->calculateOriginalObjectiveValue(
					solution);
				tmpSolPt.iterFound = ProcessInfo::getInstance().getCurrentIteration()->iterationNumber;
				tmpSolPt.maxDeviation = mostDevConstr;

				solutionPoints.at(0) = tmpSolPt;

				if (static_cast<ES_HyperplanePointStrategy>(Settings::getInstance().getIntSetting(
						"HyperplanePointStrategy", "Algorithm")) == ES_HyperplanePointStrategy::ESH)
				{
					if (static_cast<ES_LinesearchConstraintStrategy>(Settings::getInstance().getIntSetting(
							"LinesearchConstraintStrategy", "ESH")) == ES_LinesearchConstraintStrategy::AllAsMaxFunct)
					{
						static_cast<TaskSelectHyperplanePointsLinesearch *>(taskSelectHPPts)->run(solutionPoints);
					}
					else
					{
						static_cast<TaskSelectHyperplanePointsIndividualLinesearch *>(taskSelectHPPts)->run(solutionPoints);
					}
				}
				else
				{
					static_cast<TaskSelectHyperplanePointsSolution *>(taskSelectHPPts)->run(solutionPoints);
				}
			}
		}

		if (where == GRB_CB_MIPSOL)
		{
			ProcessInfo::getInstance().createIteration();
			auto currIter = ProcessInfo::getInstance().getCurrentIteration();

			std::vector<double> solution(numVar);

			for (int i = 0; i < numVar; i++)
			{
				solution.at(i) = getSolution(vars[i]);
			}

			auto mostDevConstr = ProcessInfo::getInstance().originalProblem->getMostDeviatingConstraint(solution);

			//Remove??
			if (mostDevConstr.value <= Settings::getInstance().getDoubleSetting("ConstrTermTolMILP", "Algorithm"))
			{
				return;
			}

			SolutionPoint solutionCandidate;

			solutionCandidate.point = solution;
			solutionCandidate.objectiveValue = getDoubleInfo(GRB_CB_MIPSOL_OBJ);
			solutionCandidate.iterFound = ProcessInfo::getInstance().getCurrentIteration()->iterationNumber;
			solutionCandidate.maxDeviation = mostDevConstr;

			std::vector<SolutionPoint> candidatePoints(1);
			candidatePoints.at(0) = solutionCandidate;

			addLazyConstraint(candidatePoints);

			currIter->maxDeviation = mostDevConstr.value;
			currIter->maxDeviationConstraint = mostDevConstr.idx;
			currIter->solutionStatus = E_ProblemSolutionStatus::Feasible;
			currIter->objectiveValue = getDoubleInfo(GRB_CB_MIPSOL_OBJ);
			
			auto bounds = std::make_pair(ProcessInfo::getInstance().getDualBound(), ProcessInfo::getInstance().getPrimalBound());
			currIter->currentObjectiveBounds = bounds;

			if (Settings::getInstance().getBoolSetting("PrimalStrategyLinesearch", "PrimalBound"))
			{
				taskSelectPrimalSolutionFromLinesearch->run(candidatePoints);
			}

			if (checkFixedNLPStrategy(candidatePoints.at(0)))
			{
				ProcessInfo::getInstance().addPrimalFixedNLPCandidate(candidatePoints.at(0).point,
																	  E_PrimalNLPSource::FirstSolution, getDoubleInfo(GRB_CB_MIPSOL_OBJ), ProcessInfo::getInstance().getCurrentIteration()->iterationNumber,
																	  candidatePoints.at(0).maxDeviation);

				tSelectPrimNLP->run();

				ProcessInfo::getInstance().checkPrimalSolutionCandidates();
			}

			if (Settings::getInstance().getBoolSetting("AddIntegerCuts", "Algorithm"))
			{
				bool addedIntegerCut = false;

				for (auto ic : ProcessInfo::getInstance().integerCutWaitingList)
				{
					this->createIntegerCut(ic);
					addedIntegerCut = true;
				}

				if (addedIntegerCut)
				{
					ProcessInfo::getInstance().outputInfo(
						"     Added " + to_string(ProcessInfo::getInstance().integerCutWaitingList.size()) + " integer cut(s).                                        ");
				}

				ProcessInfo::getInstance().integerCutWaitingList.clear();
			}

			auto bestBound = UtilityFunctions::toStringFormat(getDoubleInfo(GRB_CB_MIPSOL_OBJBND), "%.3f", true);
			auto threadId = "*";
			auto openNodes = "?";

			printIterationReport(candidatePoints.at(0), threadId, bestBound, openNodes);

			if (ProcessInfo::getInstance().isAbsoluteObjectiveGapToleranceMet() || ProcessInfo::getInstance().isRelativeObjectiveGapToleranceMet())
			{
				abort();
				return;
			}
		}

		if (where == GRB_CB_MIPSOL)
		{
			// Add current primal bound as new incumbent candidate
			auto primalBound = ProcessInfo::getInstance().getPrimalBound();

			if (((isMinimization && lastUpdatedPrimal < primalBound) || (!isMinimization && primalBound > primalBound)))
			{
				auto primalSol = ProcessInfo::getInstance().primalSolution;

				std::vector<double> primalSolution(numVar);

				for (int i = 0; i < numVar; i++)
				{
					setSolution(vars[i], primalSol.at(i));
				}

				lastUpdatedPrimal = primalBound;
			}

			// Adds cutoff
			if (isMinimization)
			{
				static_cast<MILPSolverGurobiLazy *>(ProcessInfo::getInstance().MILPSolver)->gurobiModel->set(GRB_DoubleParam_Cutoff, primalBound /*+ 0.0000001*/);

				ProcessInfo::getInstance().outputInfo(
					"     Setting cutoff value to " + UtilityFunctions::toString(primalBound /*+ 0.0000001*/) + " for minimization.");
			}
			else
			{
				static_cast<MILPSolverGurobiLazy *>(ProcessInfo::getInstance().MILPSolver)->gurobiModel->set(GRB_DoubleParam_Cutoff, -primalBound /*- 0.0000001*/);

				ProcessInfo::getInstance().outputInfo(
					"     Setting cutoff value to " + UtilityFunctions::toString(-primalBound /*- 0.0000001*/) + " for minimization.");
			}
		}
	}
	catch (GRBException &e)
	{
		ProcessInfo::getInstance().outputError("Gurobi error when running main callback method", e.getMessage());
	}
}

void GurobiCallback::createHyperplane(Hyperplane hyperplane)
{
	try
	{
		auto currIter = ProcessInfo::getInstance().getCurrentIteration(); // The unsolved new iteration
		auto optional = ProcessInfo::getInstance().MILPSolver->createHyperplaneTerms(hyperplane);

		if (!optional)
		{
			return;
		}

		auto tmpPair = optional.get();

		bool hyperplaneIsOk = true;

		for (auto E : tmpPair.first)
		{
			if (E.value != E.value) //Check for NaN
			{

				ProcessInfo::getInstance().outputWarning(
					"     Warning: hyperplane not generated, NaN found in linear terms!");
				hyperplaneIsOk = false;
				break;
			}
		}

		if (hyperplaneIsOk)
		{
			GeneratedHyperplane genHyperplane;

			GRBLinExpr expr = 0;

			for (int i = 0; i < tmpPair.first.size(); i++)
			{
				expr += +(tmpPair.first.at(i).value) * (vars[tmpPair.first.at(i).idx]);
			}

			addLazy(expr <= -tmpPair.second);

			int constrIndex = 0;
			genHyperplane.generatedConstraintIndex = constrIndex;
			genHyperplane.sourceConstraintIndex = hyperplane.sourceConstraintIndex;
			genHyperplane.generatedPoint = hyperplane.generatedPoint;
			genHyperplane.source = hyperplane.source;
			genHyperplane.generatedIter = currIter->iterationNumber;
			genHyperplane.isLazy = false;
			genHyperplane.isRemoved = false;

			//ProcessInfo::getInstance().MILPSolver->generatedHyperplanes.push_back(genHyperplane);

			currIter->numHyperplanesAdded++;
			currIter->totNumHyperplanes++;
		}
	}
	catch (GRBException &e)
	{
		ProcessInfo::getInstance().outputError("Gurobi error when creating lazy hyperplane", e.getMessage());
	}
}

GurobiCallback::GurobiCallback(GRBVar *xvars)
{
	vars = xvars;

	isMinimization = ProcessInfo::getInstance().originalProblem->isTypeOfObjectiveMinimize();

	ProcessInfo::getInstance().lastLazyAddedIter = 0;

	cbCalls = 0;

	auto taskInitLinesearch = new TaskInitializeLinesearch();

	if (static_cast<ES_HyperplanePointStrategy>(Settings::getInstance().getIntSetting("HyperplanePointStrategy",
																					  "Algorithm")) == ES_HyperplanePointStrategy::ESH)
	{
		if (static_cast<ES_LinesearchConstraintStrategy>(Settings::getInstance().getIntSetting(
				"LinesearchConstraintStrategy", "ESH")) == ES_LinesearchConstraintStrategy::AllAsMaxFunct)
		{
			taskSelectHPPts = new TaskSelectHyperplanePointsLinesearch();
		}
		else
		{
			taskSelectHPPts = new TaskSelectHyperplanePointsIndividualLinesearch();
		}
	}
	else
	{
		taskSelectHPPts = new TaskSelectHyperplanePointsSolution();
	}

	tSelectPrimNLP = new TaskSelectPrimalCandidatesFromNLP();

	if (ProcessInfo::getInstance().originalProblem->isObjectiveFunctionNonlinear() && Settings::getInstance().getBoolSetting("UseObjectiveLinesearch", "PrimalBound"))
	{
		taskUpdateObjectiveByLinesearch = new TaskUpdateNonlinearObjectiveByLinesearch();
	}

	if (Settings::getInstance().getBoolSetting("PrimalStrategyLinesearch", "PrimalBound"))
	{
		taskSelectPrimalSolutionFromLinesearch = new TaskSelectPrimalCandidatesFromLinesearch();
	}

	lastUpdatedPrimal = ProcessInfo::getInstance().getPrimalBound();

	numVar = (static_cast<MILPSolverGurobiLazy *>(ProcessInfo::getInstance().MILPSolver))->gurobiModel->get(GRB_IntAttr_NumVars);
}

void GurobiCallback::createIntegerCut(std::vector<int> binaryIndexes)
{
	try
	{
		GRBLinExpr expr = 0;

		for (int i = 0; i < binaryIndexes.size(); i++)
		{
			expr += vars[binaryIndexes.at(i)];
		}

		addLazy(expr <= binaryIndexes.size() - 1.0);

		ProcessInfo::getInstance().numIntegerCutsAdded++;
	}
	catch (GRBException &e)
	{
		ProcessInfo::getInstance().outputError("Gurobi error when adding lazy integer cut", e.getMessage());
	}
}

void GurobiCallback::addLazyConstraint(std::vector<SolutionPoint> candidatePoints)
{
	try
	{
		lastNumAddedHyperplanes = 0;
		this->cbCalls++;

		if (static_cast<ES_HyperplanePointStrategy>(Settings::getInstance().getIntSetting("HyperplanePointStrategy",
																						  "Algorithm")) == ES_HyperplanePointStrategy::ESH)
		{
			if (static_cast<ES_LinesearchConstraintStrategy>(Settings::getInstance().getIntSetting(
					"LinesearchConstraintStrategy", "ESH")) == ES_LinesearchConstraintStrategy::AllAsMaxFunct)
			{
				static_cast<TaskSelectHyperplanePointsLinesearch *>(taskSelectHPPts)->run(candidatePoints);
			}
			else
			{
				static_cast<TaskSelectHyperplanePointsIndividualLinesearch *>(taskSelectHPPts)->run(candidatePoints);
			}
		}
		else
		{
			static_cast<TaskSelectHyperplanePointsSolution *>(taskSelectHPPts)->run(candidatePoints);
		}

		for (auto hp : ProcessInfo::getInstance().hyperplaneWaitingList)
		{
			this->createHyperplane(hp);
			this->lastNumAddedHyperplanes++;
		}

		ProcessInfo::getInstance().hyperplaneWaitingList.clear();
	}
	catch (GRBException &e)
	{
		ProcessInfo::getInstance().outputError("Gurobi error when invoking adding lazy constraint", e.getMessage());
	}
}
