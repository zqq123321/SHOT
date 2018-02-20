#include "RelaxationStrategyStandard.h"

RelaxationStrategyStandard::RelaxationStrategyStandard(IMIPSolver *MIPSolver)
{
	this->MIPSolver = MIPSolver;
}

RelaxationStrategyStandard::~RelaxationStrategyStandard()
{
}

void RelaxationStrategyStandard::setInitial()
{
	LPFinished = false;

	if (Settings::getInstance().getIntSetting("Relaxation.IterationLimit", "Dual") > 0
			&& Settings::getInstance().getDoubleSetting("Relaxation.TimeLimit", "Dual") > 0)
	{
		this->setActive();
	}
	else
	{
		this->setInactive();
	}
}

void RelaxationStrategyStandard::executeStrategy()
{
	int iterInterval = Settings::getInstance().getIntSetting("Relaxation.Frequency", "Dual");
	if (iterInterval != 0 && ProcessInfo::getInstance().getCurrentIteration()->iterationNumber % iterInterval == 0)
	{
		return (this->setActive());
	}

	if (isLPStepFinished() || isCurrentToleranceReached() || isGapReached() || isIterationLimitReached()
			|| isTimeLimitReached() || isRelaxedSolutionEpsilonValid() || isObjectiveStagnant())
	{
		this->setInactive();
	}
	else
	{
		this->setActive();
	}
}

void RelaxationStrategyStandard::setActive()
{
	if (MIPSolver->getDiscreteVariableStatus())
	{
		ProcessInfo::getInstance().stopTimer("MIP");
		ProcessInfo::getInstance().startTimer("LP");
		MIPSolver->activateDiscreteVariables(false);

		ProcessInfo::getInstance().getCurrentIteration()->type = E_IterationProblemType::Relaxed;
	}
}

void RelaxationStrategyStandard::setInactive()
{
	if (!MIPSolver->getDiscreteVariableStatus())
	{
		ProcessInfo::getInstance().stopTimer("LP");
		ProcessInfo::getInstance().startTimer("MIP");
		MIPSolver->activateDiscreteVariables(true);

		ProcessInfo::getInstance().getCurrentIteration()->type = E_IterationProblemType::MIP;

		LPFinished = true;

	}
}

E_IterationProblemType RelaxationStrategyStandard::getProblemType()
{
	if (MIPSolver->getDiscreteVariableStatus())

	return (E_IterationProblemType::MIP);
	else return (E_IterationProblemType::Relaxed);
}

bool RelaxationStrategyStandard::isIterationLimitReached()
{
	auto prevIter = ProcessInfo::getInstance().getPreviousIteration();

	if (prevIter->iterationNumber < Settings::getInstance().getIntSetting("Relaxation.IterationLimit", "Dual"))
	{
		return (false);
	}

	return (true);
}

bool RelaxationStrategyStandard::isTimeLimitReached()
{
	if (ProcessInfo::getInstance().getElapsedTime("LP")
			< Settings::getInstance().getDoubleSetting("Relaxation.TimeLimit", "Dual"))
	{
		return (false);
	}

	return (true);
}

bool RelaxationStrategyStandard::isLPStepFinished()
{
	return (LPFinished);
}

bool RelaxationStrategyStandard::isObjectiveStagnant()
{
	int numSteps = 10;

	auto prevIter = ProcessInfo::getInstance().getPreviousIteration();

	if (prevIter->iterationNumber < numSteps) return (false);

	auto prevIter2 = &ProcessInfo::getInstance().iterations[prevIter->iterationNumber - numSteps];

//TODO: should be substituted with parameter
	if (std::abs((prevIter->objectiveValue - prevIter2->objectiveValue) / prevIter->objectiveValue) < 0.000001) return (true);

	return (false);
}

