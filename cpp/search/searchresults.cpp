
//-------------------------------------------------------------------------------------
// This file contains various functions for extracting stats and results from the search, choosing a move, etc
//-------------------------------------------------------------------------------------

#include "../search/search.h"

#include <inttypes.h>

#include "../core/fancymath.h"
#include "../program/playutils.h"

using namespace std;
using nlohmann::json;

static const int64_t MIN_VISITS_FOR_LCB = 3;

bool Search::getPlaySelectionValues(
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs, vector<double> &playSelectionValues, double scaleMaxToAtLeast)
    const
{
  if (rootNode == NULL)
  {
    fromLocs.clear();
    toLocs.clear();
    playSelectionValues.clear();
    return false;
  }
  bool allowDirectPolicyMoves = true;
  return getPlaySelectionValues(*rootNode, fromLocs, toLocs, playSelectionValues, NULL, scaleMaxToAtLeast, allowDirectPolicyMoves);
}

bool Search::getPlaySelectionValues(
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs,
    vector<double> &playSelectionValues,
    vector<double> *retVisitCounts,
    double scaleMaxToAtLeast) const
{
  if (rootNode == NULL)
  {
    fromLocs.clear();
    toLocs.clear();
    playSelectionValues.clear();
    if (retVisitCounts != NULL)
      retVisitCounts->clear();
    return false;
  }
  bool allowDirectPolicyMoves = true;
  return getPlaySelectionValues(
      *rootNode, fromLocs, toLocs, playSelectionValues, retVisitCounts, scaleMaxToAtLeast, allowDirectPolicyMoves);
}

bool Search::getPlaySelectionValues(
    const SearchNode &node,
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs,
    vector<double> &playSelectionValues,
    vector<double> *retVisitCounts,
    double scaleMaxToAtLeast,
    bool allowDirectPolicyMoves) const
{
  std::mutex &mutex = mutexPool->getMutex(node.lockIdx);
  lock_guard<std::mutex> lock(mutex);
  double lcbBuf[NNPos::MAX_NN_POLICY_SIZE];
  double radiusBuf[NNPos::MAX_NN_POLICY_SIZE];
  assert(node.numChildren <= NNPos::MAX_NN_POLICY_SIZE);
  bool result = getPlaySelectionValuesAlreadyLocked(
      node,
      fromLocs,
      toLocs,
      playSelectionValues,
      retVisitCounts,
      scaleMaxToAtLeast,
      allowDirectPolicyMoves,
      false,
      lcbBuf,
      radiusBuf);
  return result;
}

bool Search::getPlaySelectionValuesAlreadyLocked(
    const SearchNode &node,
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs,
    vector<double> &playSelectionValues,
    vector<double> *retVisitCounts,
    double scaleMaxToAtLeast,
    bool allowDirectPolicyMoves,
    bool alwaysComputeLcb,
    // Note: lcbBuf is signed from the player to move's perspective
    double lcbBuf[NNPos::MAX_NN_POLICY_SIZE],
    double radiusBuf[NNPos::MAX_NN_POLICY_SIZE]) const
{
  fromLocs.clear();
  toLocs.clear();
  playSelectionValues.clear();
  if (retVisitCounts != NULL)
    retVisitCounts->clear();

  int numChildren = node.numChildren;
  int64_t totalChildVisits = 0;
  int64_t maxChildVisits = 0;

  // Store up basic visit counts
  for (int i = 0; i < numChildren; i++)
  {
    SearchNode *child = node.children[i];

    while (child->statsLock.test_and_set(std::memory_order_acquire))
      ;
    int64_t childVisits = child->stats.visits;
    child->statsLock.clear(std::memory_order_release);

    fromLocs.push_back(child->prevFromLoc);
    toLocs.push_back(child->prevToLoc);
    totalChildVisits += childVisits;
    if (childVisits > maxChildVisits)
      maxChildVisits = childVisits;
    playSelectionValues.push_back((double)childVisits);
  }

  if (retVisitCounts != NULL)
  {
    *retVisitCounts = playSelectionValues;
  }

  // Find the best child by visits
  int mostVisitedIdx = 0;
  double mostVisitedChildVisits = -1e30;
  for (int i = 0; i < numChildren; i++)
  {
    double value = playSelectionValues[i];
    if (value > mostVisitedChildVisits)
    {
      mostVisitedChildVisits = value;
      mostVisitedIdx = i;
    }
  }

  // Possibly reduce visits on children that we spend too many visits on in retrospect
  if (&node == rootNode && numChildren > 0)
  {
    const SearchNode *bestChild = node.children[mostVisitedIdx];
    double fpuValue = -10.0; // dummy, not actually used since these childs all should actually have visits
    double parentUtility;
    {
      while (node.statsLock.test_and_set(std::memory_order_acquire))
        ;
      double utilitySum = node.stats.utilitySum;
      double weightSum = node.stats.weightSum;
      node.statsLock.clear(std::memory_order_release);
      assert(weightSum > 0.0);
      parentUtility = utilitySum / weightSum;
    }

    bool isDuringSearch = false;

    float *policyProbs = node.nnOutput->getPolicyProbsMaybeNoised();
    double bestChildExploreSelectionValue = getExploreSelectionValue(
        node, policyProbs, bestChild, totalChildVisits, fpuValue, parentUtility, isDuringSearch, maxChildVisits, NULL);

    for (int i = 0; i < numChildren; i++)
    {
      if (i != mostVisitedIdx)
        playSelectionValues[i] = (double)getReducedPlaySelectionVisits(
            node, policyProbs, node.children[i], totalChildVisits, bestChildExploreSelectionValue);
    }
  }

  // Now compute play selection values taking into account LCB
  if (alwaysComputeLcb || (searchParams.useLcbForSelection && numChildren > 0))
  {
    double bestLcb = -1e10;
    int bestLcbIndex = -1;
    for (int i = 0; i < numChildren; i++)
    {
      getSelfUtilityLCBAndRadius(node, node.children[i], lcbBuf[i], radiusBuf[i]);
      // Check if this node is eligible to be considered for best LCB
      double visits = playSelectionValues[i];
      if (visits >= MIN_VISITS_FOR_LCB && visits >= searchParams.minVisitPropForLCB * mostVisitedChildVisits)
      {
        if (lcbBuf[i] > bestLcb)
        {
          bestLcb = lcbBuf[i];
          bestLcbIndex = i;
        }
      }
    }

    if (
        searchParams.useLcbForSelection && numChildren > 0 &&
        (searchParams.useNonBuggyLcb ? (bestLcbIndex >= 0) : (bestLcbIndex > 0)))
    {
      // Best LCB move gets a bonus that ensures it is large enough relative to every other child
      double adjustedVisits = playSelectionValues[bestLcbIndex];
      for (int i = 0; i < numChildren; i++)
      {
        if (i != bestLcbIndex)
        {
          double excessValue = bestLcb - lcbBuf[i];
          // This move is actually worse lcb than some other move, it's just that the other
          // move failed its checks for having enough minimum visits. So don't actually
          // try to compute how much better this one is than that one, because it's not better.
          if (excessValue < 0)
            continue;

          double radius = radiusBuf[i];
          // How many times wider would the radius have to be before the lcb would be worse?
          // Add adjust the denom so that we cannot possibly gain more than a factor of 5, just as a guard
          double radiusFactor = (radius + excessValue) / (radius + 0.20 * excessValue);

          // That factor, squared, is the number of "visits" more that we should pretend we have, for
          // the purpose of selection, since normally stdev is proportional to 1/visits^2.
          double lbound = radiusFactor * radiusFactor * playSelectionValues[i];
          if (lbound > adjustedVisits)
            adjustedVisits = lbound;
        }
      }
      playSelectionValues[bestLcbIndex] = adjustedVisits;
    }
  }

  shared_ptr<NNOutput> nnOutput = node.nnOutput;

  // If we have no children, then use the policy net directly. Only for the root, though, if calling this on any subtree
  // then just require that we have children, for implementation simplicity (since it requires that we have a board and
  // a boardhistory too) (and we also use isAllowedRootMove)
  if (numChildren == 0)
  {
    if (nnOutput == nullptr || &node != rootNode || !allowDirectPolicyMoves)
      return false;

    for (int movePos = 0; movePos < policySize; movePos++)
    {
      Loc fromLoc = NNPos::posToFromLoc(movePos, rootBoard.x_size, rootBoard.y_size, nnXLen, nnYLen);
      Loc toLoc = NNPos::posToToLoc(movePos, rootBoard.x_size, rootBoard.y_size, nnXLen, nnYLen);
      float *policyProbs = nnOutput->getPolicyProbsMaybeNoised();
      double policyProb = policyProbs[movePos];
      if (!rootHistory.isLegal(rootBoard, fromLoc, toLoc, rootPla) || policyProb < 0)
        continue;
      fromLocs.push_back(fromLoc);
      toLocs.push_back(toLoc);
      playSelectionValues.push_back(policyProb);
      numChildren++;
    }
  }

  // Might happen absurdly rarely if we both have no children and don't properly have an nnOutput
  // but have a hash collision or something so we "found" an nnOutput anyways.
  if (numChildren == 0)
    return false;

  double maxValue = 0.0;
  for (int i = 0; i < numChildren; i++)
  {
    if (playSelectionValues[i] > maxValue)
      maxValue = playSelectionValues[i];
  }

  if (maxValue <= 1e-50)
    return false;

  // Sanity check - if somehow we had more than this, something must have overflowed or gone wrong
  assert(maxValue < 1e40);

  double amountToSubtract = std::min(searchParams.chosenMoveSubtract, maxValue / 64.0);
  double amountToPrune = std::min(searchParams.chosenMovePrune, maxValue / 64.0);
  double newMaxValue = maxValue - amountToSubtract;
  for (int i = 0; i < numChildren; i++)
  {
    if (playSelectionValues[i] < amountToPrune)
      playSelectionValues[i] = 0.0;
    else
    {
      playSelectionValues[i] -= amountToSubtract;
      if (playSelectionValues[i] <= 0.0)
        playSelectionValues[i] = 0.0;
    }
  }

  assert(newMaxValue > 0.0);

  if (newMaxValue < scaleMaxToAtLeast)
  {
    for (int i = 0; i < numChildren; i++)
    {
      playSelectionValues[i] *= scaleMaxToAtLeast / newMaxValue;
    }
  }

  return true;
}

void Search::maybeRecomputeNormToTApproxTable()
{
  if (normToTApproxZ <= 0.0 || normToTApproxZ != searchParams.lcbStdevs || normToTApproxTable.size() <= 0)
  {
    normToTApproxZ = searchParams.lcbStdevs;
    normToTApproxTable.clear();
    for (int i = 0; i < 512; i++)
      normToTApproxTable.push_back(FancyMath::normToTApprox(normToTApproxZ, (double)(i + MIN_VISITS_FOR_LCB)));
  }
}

double Search::getNormToTApproxForLCB(int64_t numVisits) const
{
  int64_t idx = numVisits - MIN_VISITS_FOR_LCB;
  assert(idx >= 0);
  if (idx >= normToTApproxTable.size())
    idx = normToTApproxTable.size() - 1;
  return normToTApproxTable[idx];
}

// Parent must be locked
void Search::getSelfUtilityLCBAndRadius(
    const SearchNode &parent,
    const SearchNode *child,
    double &lcbBuf,
    double &radiusBuf) const
{
  while (child->statsLock.test_and_set(std::memory_order_acquire))
    ;
  double utilitySum = child->stats.utilitySum;
  double utilitySqSum = child->stats.utilitySqSum;
  double scoreMeanSum = child->stats.scoreMeanSum;
  double scoreMeanSqSum = child->stats.scoreMeanSqSum;
  double weightSum = child->stats.weightSum;
  double weightSqSum = child->stats.weightSqSum;
  child->statsLock.clear(std::memory_order_release);

  radiusBuf = 2.0 * (searchParams.winLossUtilityFactor + searchParams.staticScoreUtilityFactor +
                     searchParams.dynamicScoreUtilityFactor);
  lcbBuf = -radiusBuf;
  if (weightSum <= 0.0)
    return;

  assert(weightSqSum > 0.0);
  double ess = weightSum * weightSum / weightSqSum;
  int64_t essInt = (int64_t)round(ess);
  if (essInt < MIN_VISITS_FOR_LCB)
    return;

  double utilityNoBonus = utilitySum / weightSum;
  double selfUtility = parent.nextPla == P_WHITE ? utilityNoBonus : -utilityNoBonus;

  double utilityVariance = std::max(1e-8, utilitySqSum / weightSum - utilityNoBonus * utilityNoBonus);
  double estimateStdev = sqrt(utilityVariance / ess);
  double radius = estimateStdev * getNormToTApproxForLCB(essInt);

  lcbBuf = selfUtility - radius;
  radiusBuf = radius;
}

bool Search::getRootValues(ReportedSearchValues &values) const
{
  if (rootNode == NULL)
    return false;
  return getNodeValues(*rootNode, values);
}

ReportedSearchValues Search::getRootValuesRequireSuccess() const
{
  ReportedSearchValues values;
  if (rootNode == NULL)
    throw StringError("Bug? Bot search root was null");
  bool success = getNodeValues(*rootNode, values);
  if (!success)
    throw StringError("Bug? Bot search returned no root values");
  return values;
}

bool Search::getRootRawNNValues(ReportedSearchValues &values) const
{
  if (rootNode == NULL)
    return false;
  return getNodeRawNNValues(*rootNode, values);
}

ReportedSearchValues Search::getRootRawNNValuesRequireSuccess() const
{
  ReportedSearchValues values;
  if (rootNode == NULL)
    throw StringError("Bug? Bot search root was null");
  bool success = getNodeRawNNValues(*rootNode, values);
  if (!success)
    throw StringError("Bug? Bot search returned no root values");
  return values;
}

bool Search::getNodeRawNNValues(const SearchNode &node, ReportedSearchValues &values) const
{
  std::mutex &mutex = mutexPool->getMutex(node.lockIdx);
  unique_lock<std::mutex> lock(mutex);
  shared_ptr<NNOutput> nnOutput = node.nnOutput;
  lock.unlock();
  if (nnOutput == nullptr)
    return false;

  values.winValue = nnOutput->whiteWinProb;
  values.lossValue = nnOutput->whiteLossProb;
  values.noResultValue = nnOutput->whiteNoResultProb;

  double scoreMean = nnOutput->whiteScoreMean;
  double scoreMeanSq = nnOutput->whiteScoreMeanSq;
  double scoreStdev = getScoreStdev(scoreMean, scoreMeanSq);
  values.staticScoreValue = ScoreValue::expectedWhiteScoreValue(scoreMean, scoreStdev, 0.0, 2.0, rootBoard);
  values.dynamicScoreValue = ScoreValue::expectedWhiteScoreValue(
      scoreMean, scoreStdev, recentScoreCenter, searchParams.dynamicScoreCenterScale, rootBoard);
  values.expectedScore = scoreMean;
  values.expectedScoreStdev = scoreStdev;
  values.lead = nnOutput->whiteLead;

  // Sanity check
  assert(values.winValue >= 0.0);
  assert(values.lossValue >= 0.0);
  assert(values.noResultValue >= 0.0);
  assert(values.winValue + values.lossValue + values.noResultValue < 1.001);

  double winLossValue = values.winValue - values.lossValue;
  if (winLossValue > 1.0)
    winLossValue = 1.0;
  if (winLossValue < -1.0)
    winLossValue = -1.0;
  values.winLossValue = winLossValue;

  values.visits = 1;

  return true;
}

bool Search::getNodeValues(const SearchNode &node, ReportedSearchValues &values) const
{
  std::mutex &mutex = mutexPool->getMutex(node.lockIdx);
  unique_lock<std::mutex> lock(mutex);
  shared_ptr<NNOutput> nnOutput = node.nnOutput;
  lock.unlock();
  if (nnOutput == nullptr)
    return false;

  while (node.statsLock.test_and_set(std::memory_order_acquire))
    ;
  double winValueSum = node.stats.winValueSum;
  double noResultValueSum = node.stats.noResultValueSum;
  double scoreMeanSum = node.stats.scoreMeanSum;
  double scoreMeanSqSum = node.stats.scoreMeanSqSum;
  double leadSum = node.stats.leadSum;
  double weightSum = node.stats.weightSum;
  double utilitySum = node.stats.utilitySum;
  int64_t visits = node.stats.visits;

  node.statsLock.clear(std::memory_order_release);

  if (weightSum <= 0.0)
    return false;

  values.winValue = winValueSum / weightSum;
  values.lossValue = (weightSum - winValueSum - noResultValueSum) / weightSum;
  values.noResultValue = noResultValueSum / weightSum;
  double scoreMean = scoreMeanSum / weightSum;
  double scoreMeanSq = scoreMeanSqSum / weightSum;
  double scoreStdev = getScoreStdev(scoreMean, scoreMeanSq);
  values.staticScoreValue = ScoreValue::expectedWhiteScoreValue(scoreMean, scoreStdev, 0.0, 2.0, rootBoard);
  values.dynamicScoreValue = ScoreValue::expectedWhiteScoreValue(
      scoreMean, scoreStdev, recentScoreCenter, searchParams.dynamicScoreCenterScale, rootBoard);
  values.expectedScore = scoreMean;
  values.expectedScoreStdev = scoreStdev;
  values.lead = leadSum / weightSum;
  values.utility = utilitySum / weightSum;

  // Perform a little normalization - due to tiny floating point errors, winValue and lossValue could be outside [0,1].
  //(particularly lossValue, as it was produced by subtractions from weightSum that could have lost precision).
  if (values.winValue < 0.0)
    values.winValue = 0.0;
  if (values.lossValue < 0.0)
    values.lossValue = 0.0;
  if (values.noResultValue < 0.0)
    values.noResultValue = 0.0;
  double sum = values.winValue + values.lossValue + values.noResultValue;
  assert(sum > 0.9 && sum < 1.1); // If it's wrong by more than this, we have a bigger bug somewhere
  values.winValue /= sum;
  values.lossValue /= sum;
  values.noResultValue /= sum;

  double winLossValue = values.winValue - values.lossValue;
  assert(winLossValue > -1.01 && winLossValue < 1.01); // Sanity check, but allow generously for float imprecision
  if (winLossValue > 1.0)
    winLossValue = 1.0;
  if (winLossValue < -1.0)
    winLossValue = -1.0;
  values.winLossValue = winLossValue;
  values.visits = visits;

  return true;
}

Move Search::getChosenMoveLoc()
{
  if (rootNode == NULL)
    return Move(0, 0, 0);

  vector<Loc> fromLocs;
  vector<Loc> toLocs;
  vector<double> playSelectionValues;
  bool suc = getPlaySelectionValues(fromLocs, toLocs, playSelectionValues, 0.0);
  if (!suc)
    return Move(0, 0, 0);

  assert(fromLocs.size() == playSelectionValues.size());

  double temperature = interpolateEarly(
      searchParams.chosenMoveTemperatureHalflife,
      searchParams.chosenMoveTemperatureEarly,
      searchParams.chosenMoveTemperature);

  uint32_t idxChosen =
      chooseIndexWithTemperature(nonSearchRand, playSelectionValues.data(), playSelectionValues.size(), temperature);
  return Move(fromLocs[idxChosen], toLocs[idxChosen], 0);
}

bool Search::getPolicy(float policyProbs[NNPos::MAX_NN_POLICY_SIZE]) const
{
  if (rootNode == NULL)
    return false;
  std::mutex &mutex = mutexPool->getMutex(rootNode->lockIdx);
  lock_guard<std::mutex> lock(mutex);
  if (rootNode->nnOutput == nullptr)
    return false;

  NNOutput &nnOutput = *(rootNode->nnOutput);
  std::copy(nnOutput.policyProbs, nnOutput.policyProbs + NNPos::MAX_NN_POLICY_SIZE, policyProbs);
  return true;
}

// Safe to call concurrently with search
double Search::getPolicySurprise() const
{
  double surprise = 0.0;
  double searchEntropy = 0.0;
  double policyEntropy = 0.0;
  if (getPolicySurpriseAndEntropy(surprise, searchEntropy, policyEntropy))
    return surprise;
  return 0.0;
}

// Safe to call concurrently with search
bool Search::getPolicySurpriseAndEntropy(double &surpriseRet, double &searchEntropyRet, double &policyEntropyRet)
    const
{
  if (rootNode == NULL)
    return false;
  std::mutex &mutex = mutexPool->getMutex(rootNode->lockIdx);
  unique_lock<std::mutex> lock(mutex);
  if (rootNode->nnOutput == nullptr)
    return false;

  NNOutput &nnOutput = *(rootNode->nnOutput);

  vector<Loc> fromLocs;
  vector<Loc> toLocs;
  vector<double> playSelectionValues;
  bool allowDirectPolicyMoves = true;
  bool alwaysComputeLcb = false;
  double lcbBuf[NNPos::MAX_NN_POLICY_SIZE];
  double radiusBuf[NNPos::MAX_NN_POLICY_SIZE];
  bool suc = getPlaySelectionValuesAlreadyLocked(
      *rootNode, fromLocs, toLocs, playSelectionValues, NULL, 1.0, allowDirectPolicyMoves, alwaysComputeLcb, lcbBuf, radiusBuf);
  if (!suc)
    return false;

  float policyProbsFromNNBuf[NNPos::MAX_NN_POLICY_SIZE];
  {
    float *policyProbsFromNN = nnOutput.getPolicyProbsMaybeNoised();
    std::copy(policyProbsFromNN, policyProbsFromNN + NNPos::MAX_NN_POLICY_SIZE, policyProbsFromNNBuf);
  }
  // Okay we have all the data we need!
  lock.unlock();

  double sumPlaySelectionValues = 0.0;
  for (int i = 0; i < playSelectionValues.size(); i++)
    sumPlaySelectionValues += playSelectionValues[i];

  double surprise = 0.0;
  double searchEntropy = 0.0;
  for (int i = 0; i < playSelectionValues.size(); i++)
  {
    int pos = getDouplePos(fromLocs[i], toLocs[i]);
    double policy = std::max((double)policyProbsFromNNBuf[pos], 1e-100);
    double target = playSelectionValues[i] / sumPlaySelectionValues;
    if (target > 1e-100)
    {
      double logTarget = log(target);
      double logPolicy = log(policy);
      surprise += target * (logTarget - logPolicy);
      searchEntropy += -target * logTarget;
    }
  }

  double policyEntropy = 0.0;
  for (int pos = 0; pos < NNPos::MAX_NN_POLICY_SIZE; pos++)
  {
    double policy = policyProbsFromNNBuf[pos];
    if (policy > 1e-100)
    {
      policyEntropy += -policy * log(policy);
    }
  }

  // Just in case, guard against float imprecision
  if (surprise < 0.0)
    surprise = 0.0;
  if (searchEntropy < 0.0)
    searchEntropy = 0.0;
  if (policyEntropy < 0.0)
    policyEntropy = 0.0;

  surpriseRet = surprise;
  searchEntropyRet = searchEntropy;
  policyEntropyRet = policyEntropy;

  return true;
}

void Search::printRootOwnershipMap(ostream &out, Player perspective) const
{
  if (rootNode == NULL)
    return;
  std::mutex &mutex = mutexPool->getMutex(rootNode->lockIdx);
  lock_guard<std::mutex> lock(mutex);
  if (rootNode->nnOutput == nullptr)
    return;

  NNOutput &nnOutput = *(rootNode->nnOutput);
  if (nnOutput.whiteOwnerMap == NULL)
    return;

  Player perspectiveToUse = (perspective != P_BLACK && perspective != P_WHITE) ? rootPla : perspective;
  double perspectiveFactor = perspectiveToUse == P_BLACK ? -1.0 : 1.0;
  /*
  for(int y = 0; y < rootBoard.y_size; y++) {
    for(int x = 0; x < rootBoard.x_size; x++) {
      int pos = NNPos::xyToPos(x, y, nnOutput.nnXLen);
      out << Global::strprintf("%6.1f ", perspectiveFactor * nnOutput.whiteOwnerMap[pos] * 100);
    }
    out << endl;
  }*/
  out << endl;
}

void Search::printRootPolicyMap(ostream &out) const
{
  if (rootNode == NULL)
    return;
  std::mutex &mutex = mutexPool->getMutex(rootNode->lockIdx);
  lock_guard<std::mutex> lock(mutex);
  if (rootNode->nnOutput == nullptr)
    return;

  NNOutput &nnOutput = *(rootNode->nnOutput);

  float *policyProbs = nnOutput.getPolicyProbsMaybeNoised();
  for (int y = 0; y < rootBoard.y_size; y++)
  {
    for (int x = 0; x < rootBoard.x_size; x++)
    {
      for (int i = 0; i < rootBoard.x_size; i++)
      {
        for (int j = 0; j < rootBoard.y_size; j++)
        {
          int from = Location::getLoc(x, y, nnXLen);
          int to = Location::getLoc(i, j, nnXLen);
          int pos = NNPos::locToDoublePos(from, to, rootBoard.x_size, nnXLen, nnYLen);
          out << Global::strprintf("%6.1f ", policyProbs[pos] * 100);
        }
        out << endl;
      }
      out << endl;
    }
    out << endl;
  }
  out << endl;
}

void Search::printRootEndingScoreValueBonus(ostream &out) const
{
  if (rootNode == NULL)
    return;
  std::mutex &mutex = mutexPool->getMutex(rootNode->lockIdx);
  unique_lock<std::mutex> lock(mutex);
  if (rootNode->nnOutput == nullptr)
    return;

  NNOutput &nnOutput = *(rootNode->nnOutput);
  if (nnOutput.whiteOwnerMap == NULL)
    return;

  for (int i = 0; i < rootNode->numChildren; i++)
  {
    const SearchNode *child = rootNode->children[i];

    while (child->statsLock.test_and_set(std::memory_order_acquire))
      ;
    int64_t childVisits = child->stats.visits;
    double utilitySum = child->stats.utilitySum;
    double scoreMeanSum = child->stats.scoreMeanSum;
    double scoreMeanSqSum = child->stats.scoreMeanSqSum;
    double weightSum = child->stats.weightSum;
    child->statsLock.clear(std::memory_order_release);

    double utilityNoBonus = utilitySum / weightSum;

    out << Location::toString(child->prevFromLoc, rootBoard) << " " << Location::toString(child->prevToLoc, rootBoard)
        << " " << Global::strprintf("visits %d utilityNoBonus %.2fc", childVisits, utilityNoBonus * 100);
    out << endl;
  }
}

void Search::appendPV(
    std::vector<Loc> &FromBuf,
    std::vector<Loc> &ToBuf,
    vector<int64_t> &visitsBuf,
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs,
    vector<double> &scratchValues,
    const SearchNode *n,
    int maxDepth) const
{
  appendPVForMove(FromBuf, ToBuf, visitsBuf, fromLocs, toLocs, scratchValues, n, Board::NULL_LOC, Board::NULL_LOC, maxDepth);
}

void Search::appendPVForMove(
    std::vector<Loc> &FromBuf,
    std::vector<Loc> &ToBuf,
    vector<int64_t> &visitsBuf,
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs,
    vector<double> &scratchValues,
    const SearchNode *n,
    Loc fromMove,
    Loc toMove,
    int maxDepth) const
{
  if (n == NULL)
    return;

  for (int depth = 0; depth < maxDepth; depth++)
  {
    bool success = getPlaySelectionValues(*n, fromLocs, toLocs, scratchValues, NULL, 1.0, false);
    if (!success)
      return;

    double maxSelectionValue = POLICY_ILLEGAL_SELECTION_VALUE;
    int bestChildIdx = -1;
    Loc bestFrom = Board::NULL_LOC;
    Loc bestTo = Board::NULL_LOC;

    for (int i = 0; i < scratchValues.size(); i++)
    {
      double selectionValue = scratchValues[i];
      if (depth == 0 && fromLocs[i] == fromMove && toLocs[i] == toMove)
      {
        maxSelectionValue = selectionValue;
        bestChildIdx = i;
        bestFrom = fromLocs[i];
        bestTo = toLocs[i];
        break;
      }

      if (selectionValue > maxSelectionValue)
      {
        maxSelectionValue = selectionValue;
        bestChildIdx = i;
        bestFrom = fromLocs[i];
        bestTo = toLocs[i];
      }
    }

    if (bestChildIdx < 0)
      return;
    if (depth == 0 && fromMove != Board::NULL_LOC && toMove != Board::NULL_LOC && bestFrom != fromMove && bestTo != toMove)
      return;

    const SearchNode &node = *n;
    std::mutex &mutex = mutexPool->getMutex(node.lockIdx);
    unique_lock<std::mutex> lock(mutex);
    assert(node.numChildren >= scratchValues.size());
    // We rely on the fact that children are never reordered - we can access this safely
    // despite dropping the lock in between computing play selection values and now
    n = node.children[bestChildIdx];
    lock.unlock();

    while (n->statsLock.test_and_set(std::memory_order_acquire))
      ;
    int64_t visits = n->stats.visits;
    n->statsLock.clear(std::memory_order_release);

    FromBuf.push_back(bestFrom);
    ToBuf.push_back(bestTo);
    visitsBuf.push_back(visits);
  }
}

void Search::printPV(ostream &out, const SearchNode *n, int maxDepth) const
{
  std::vector<Loc> FromBuf;
  std::vector<Loc> ToBuf;
  vector<Loc> fromLocs;
  vector<Loc> toLocs;
  vector<int64_t> visitsBuf;
  vector<double> scratchValues;
  appendPV(FromBuf, ToBuf, visitsBuf, fromLocs, toLocs, scratchValues, n, maxDepth);
  printPV(out, FromBuf, ToBuf);
}

void Search::printPV(ostream &out, const vector<Loc> &FromBuf, const vector<Loc> &ToBuf) const
{
  bool printedAnything = false;
  for (int i = 0; i < FromBuf.size(); i++)
  {
    if (printedAnything)
      out << " ";
    if (FromBuf[i] == Board::NULL_LOC || ToBuf[i] == Board::NULL_LOC)
      continue;
    out << Location::toString(FromBuf[i], rootBoard);
    out << Location::toString(ToBuf[i], rootBoard);
    printedAnything = true;
  }
}

// Child should NOT be locked.
AnalysisData Search::getAnalysisDataOfSingleChild(
    const SearchNode *child,
    vector<Loc> &fromLocs,
    vector<Loc> &toLocs,
    vector<double> &scratchValues,
    Loc fromLoc,
    Loc toLoc,
    double policyProb,
    double fpuValue,
    double parentUtility,
    double parentWinLossValue,
    double parentScoreMean,
    double parentScoreStdev,
    double parentLead,
    int maxPVDepth) const
{
  int64_t numVisits = 0;
  double winValueSum = 0.0;
  double noResultValueSum = 0.0;
  double scoreMeanSum = 0.0;
  double scoreMeanSqSum = 0.0;
  double leadSum = 0.0;
  double weightSum = 0.0;
  double weightSqSum = 0.0;
  double utilitySum = 0.0;

  if (child != NULL)
  {
    while (child->statsLock.test_and_set(std::memory_order_acquire))
      ;
    numVisits = child->stats.visits;
    winValueSum = child->stats.winValueSum;
    noResultValueSum = child->stats.noResultValueSum;
    scoreMeanSum = child->stats.scoreMeanSum;
    scoreMeanSqSum = child->stats.scoreMeanSqSum;
    leadSum = child->stats.leadSum;
    weightSum = child->stats.weightSum;
    weightSqSum = child->stats.weightSqSum;
    utilitySum = child->stats.utilitySum;
    child->statsLock.clear(std::memory_order_release);
  }

  AnalysisData data;
  data.fromLoc = fromLoc;
  data.toLoc = toLoc;
  data.numVisits = numVisits;
  if (weightSum <= 1e-30)
  {
    data.utility = fpuValue;
    data.scoreUtility =
        getScoreUtility(parentScoreMean, parentScoreMean * parentScoreMean + parentScoreStdev * parentScoreStdev, 1.0);
    data.resultUtility = fpuValue - data.scoreUtility;
    data.winLossValue =
        searchParams.winLossUtilityFactor == 1.0 ? parentWinLossValue + (fpuValue - parentUtility) : 0.0;
    data.scoreMean = parentScoreMean;
    data.scoreStdev = parentScoreStdev;
    data.lead = parentLead;
    data.ess = 0.0;
  }
  else
  {
    double winValue = winValueSum / weightSum;
    double lossValue = (weightSum - winValueSum - noResultValueSum) / weightSum;
    double noResultValue = noResultValueSum / weightSum;
    double scoreMean = scoreMeanSum / weightSum;
    double scoreMeanSq = scoreMeanSqSum / weightSum;
    double lead = leadSum / weightSum;

    data.utility = utilitySum / weightSum;
    data.resultUtility = getResultUtility(winValue, noResultValue);
    data.scoreUtility = data.utility - data.resultUtility;
    data.winLossValue = winValue - lossValue;
    data.scoreMean = scoreMean;
    data.scoreStdev = getScoreStdev(scoreMean, scoreMeanSq);
    data.lead = lead;
    data.ess = weightSum * weightSum / weightSqSum;
  }

  data.policyPrior = policyProb;
  data.order = 0;

  data.pvFrom.clear();
  data.pvTo.clear();
  data.pvFrom.push_back(fromLoc);
  data.pvTo.push_back(toLoc);
  data.pvVisits.clear();
  data.pvVisits.push_back(numVisits);
  appendPV(data.pvFrom, data.pvTo, data.pvVisits, fromLocs, toLocs, scratchValues, child, maxPVDepth);

  data.node = child;

  return data;
}

void Search::getAnalysisData(
    vector<AnalysisData> &buf,
    int minMovesToTryToGet,
    bool includeWeightFactors,
    int maxPVDepth) const
{
  buf.clear();
  if (rootNode == NULL)
    return;
  getAnalysisData(*rootNode, buf, minMovesToTryToGet, includeWeightFactors, maxPVDepth);
}

void Search::getAnalysisData(
    const SearchNode &node,
    vector<AnalysisData> &buf,
    int minMovesToTryToGet,
    bool includeWeightFactors,
    int maxPVDepth) const
{
  buf.clear();
  vector<SearchNode *> children;
  children.reserve(rootBoard.x_size * rootBoard.y_size + 1);

  int numChildren;
  vector<Loc> fromLocs;
  vector<Loc> toLocs;
  vector<double> scratchValues;
  double lcbBuf[NNPos::MAX_NN_POLICY_SIZE];
  double radiusBuf[NNPos::MAX_NN_POLICY_SIZE];
  float policyProbs[NNPos::MAX_NN_POLICY_SIZE];
  {
    std::mutex &mutex = mutexPool->getMutex(node.lockIdx);
    lock_guard<std::mutex> lock(mutex);
    numChildren = node.numChildren;
    for (int i = 0; i < numChildren; i++)
      children.push_back(node.children[i]);

    if (numChildren <= 0)
      return;

    assert(node.numChildren <= NNPos::MAX_NN_POLICY_SIZE);
    bool alwaysComputeLcb = true;
    bool success = getPlaySelectionValuesAlreadyLocked(
        node, fromLocs, toLocs, scratchValues, NULL, 1.0, false, alwaysComputeLcb, lcbBuf, radiusBuf);
    if (!success)
      return;

    NNOutput &nnOutput = *(node.nnOutput);
    float *policyProbsFromNN = nnOutput.getPolicyProbsMaybeNoised();
    for (int i = 0; i < NNPos::MAX_NN_POLICY_SIZE; i++)
      policyProbs[i] = policyProbsFromNN[i];
  }

  // Copy to make sure we keep these values so we can reuse scratch later for PV
  vector<double> playSelectionValues = scratchValues;

  double policyProbMassVisited = 0.0;
  {
    for (int i = 0; i < numChildren; i++)
    {
      const SearchNode *child = children[i];
      policyProbMassVisited += policyProbs[getDouplePos(child->prevFromLoc, child->prevToLoc)];
    }
    // Probability mass should not sum to more than 1, giving a generous allowance
    // for floating point error.
    assert(policyProbMassVisited <= 1.0001);
  }

  double parentWinLossValue;
  double parentScoreMean;
  double parentScoreStdev;
  double parentLead;
  {
    while (node.statsLock.test_and_set(std::memory_order_acquire))
      ;
    double winValueSum = node.stats.winValueSum;
    double noResultValueSum = node.stats.noResultValueSum;
    double scoreMeanSum = node.stats.scoreMeanSum;
    double scoreMeanSqSum = node.stats.scoreMeanSqSum;
    double leadSum = node.stats.leadSum;
    double weightSum = node.stats.weightSum;
    node.statsLock.clear(std::memory_order_release);
    assert(weightSum > 0.0);

    double winValue = winValueSum / weightSum;
    double lossValue = (weightSum - winValueSum - noResultValueSum) / weightSum;

    parentWinLossValue = winValue - lossValue;
    parentScoreMean = scoreMeanSum / weightSum;
    double scoreMeanSq = scoreMeanSqSum / weightSum;
    parentScoreStdev = getScoreStdev(parentScoreMean, scoreMeanSq);
    parentLead = leadSum / weightSum;
  }

  double parentUtility;
  double fpuValue = getFpuValueForChildrenAssumeVisited(node, node.nextPla, true, policyProbMassVisited, parentUtility);

  for (int i = 0; i < numChildren; i++)
  {
    SearchNode *child = children[i];
    double policyProb = policyProbs[getDouplePos(child->prevFromLoc, child->prevToLoc)];
    AnalysisData data = getAnalysisDataOfSingleChild(
        child,
        fromLocs,
        toLocs,
        scratchValues,
        child->prevFromLoc,
        child->prevToLoc,
        policyProb,
        fpuValue,
        parentUtility,
        parentWinLossValue,
        parentScoreMean,
        parentScoreStdev,
        parentLead,
        maxPVDepth);
    data.playSelectionValue = playSelectionValues[i];
    // Make sure data.lcb is from white's perspective, for consistency with everything else
    // In lcbBuf, it's from self perspective, unlike values at nodes.
    data.lcb = node.nextPla == P_BLACK ? -lcbBuf[i] : lcbBuf[i];
    data.radius = radiusBuf[i];
    buf.push_back(data);
  }

  // Find all children and compute weighting of the children based on their values
  if (includeWeightFactors)
  {
    vector<double> selfUtilityBuf;
    vector<int64_t> visitsBuf;
    int64_t visitsSum = 0;
    for (int i = 0; i < numChildren; i++)
    {
      double childUtility = buf[i].utility;
      selfUtilityBuf.push_back(node.nextPla == P_WHITE ? childUtility : -childUtility);
      visitsBuf.push_back(buf[i].numVisits);
      visitsSum += buf[i].numVisits;
    }

    getValueChildWeights(numChildren, selfUtilityBuf, visitsBuf, scratchValues);

    for (int i = 0; i < numChildren; i++)
      buf[i].weightFactor = visitsBuf[i] * pow(scratchValues[i], searchParams.valueWeightExponent);
    double unnormWeightFactorSum = 0.0;
    for (int i = 0; i < numChildren; i++)
      unnormWeightFactorSum += buf[i].weightFactor;
    if (unnormWeightFactorSum > 0)
      for (int i = 0; i < numChildren; i++)
        buf[i].weightFactor *= (visitsSum / unnormWeightFactorSum);
  }

  // Fill the rest of the moves directly from policy
  if (numChildren < minMovesToTryToGet)
  {
    // A bit inefficient, but no big deal
    for (int i = 0; i < minMovesToTryToGet - numChildren; i++)
    {
      int bestPos = -1;
      double bestPolicy = -1.0;
      for (int pos = 0; pos < NNPos::MAX_NN_POLICY_SIZE; pos++)
      {
        if (policyProbs[pos] < bestPolicy)
          continue;

        bool alreadyUsed = false;
        for (int j = 0; j < buf.size(); j++)
        {
          if (getDouplePos(buf[j].fromLoc, buf[j].toLoc) == pos)
          {
            alreadyUsed = true;
            break;
          }
        }
        if (alreadyUsed)
          continue;

        bestPos = pos;
        bestPolicy = policyProbs[pos];
      }
      if (bestPos < 0 || bestPolicy < 0.0)
        break;

      AnalysisData data = getAnalysisDataOfSingleChild(
          NULL,
          fromLocs,
          toLocs,
          scratchValues,
          NNPos::posToFromLoc(bestPos, rootBoard.x_size, rootBoard.y_size, nnXLen, nnYLen),
          NNPos::posToToLoc(bestPos, rootBoard.x_size, rootBoard.y_size, nnXLen, nnYLen),
          bestPolicy,
          fpuValue,
          parentUtility,
          parentWinLossValue,
          parentScoreMean,
          parentScoreStdev,
          parentLead,
          maxPVDepth);
      buf.push_back(data);
    }
  }
  std::stable_sort(buf.begin(), buf.end());

  for (int i = 0; i < buf.size(); i++)
    buf[i].order = i;
}

void Search::printPVForMove(ostream &out, const SearchNode *n, Loc fromMove, Loc toMove, int maxDepth) const
{
  vector<Loc> fromLocs;
  vector<Loc> toLocs;
  vector<int64_t> visitsBuf;
  vector<Loc> scratchLocsfrom;
  vector<Loc> scratchLocsto;
  vector<double> scratchValues;
  appendPVForMove(fromLocs, toLocs, visitsBuf, scratchLocsfrom, scratchLocsto, scratchValues, n, fromMove, toMove, maxDepth);
  for (int i = 0; i < fromLocs.size(); i++)
  {
    if (i > 0)
      out << " ";
    out << Location::toString(fromLocs[i], rootBoard);
    out << Location::toString(toLocs[i], rootBoard);
  }
}

void Search::printTree(ostream &out, const SearchNode *node, PrintTreeOptions options, Player perspective) const
{
  if (node == NULL)
    return;
  string prefix;
  AnalysisData data;
  {
    vector<Loc> fromLocs;
    vector<Loc> toLocs;
    vector<double> scratchValues;
    // Use dummy values for parent
    double policyProb = NAN;
    double fpuValue = 0;
    double parentUtility = 0;
    double parentWinLossValue = 0;
    double parentScoreMean = 0;
    double parentScoreStdev = 0;
    double parentLead = 0;
    data = getAnalysisDataOfSingleChild(
        node,
        fromLocs,
        toLocs,
        scratchValues,
        (node == rootNode ? Board::NULL_LOC : node->prevFromLoc),
        (node == rootNode ? Board::NULL_LOC : node->prevToLoc),
        policyProb,
        fpuValue,
        parentUtility,
        parentWinLossValue,
        parentScoreMean,
        parentScoreStdev,
        parentLead,
        options.maxPVDepth_);
    data.weightFactor = NAN;
  }
  perspective = (perspective != P_BLACK && perspective != P_WHITE) ? node->nextPla : perspective;
  printTreeHelper(out, node, options, prefix, 0, 0, data, perspective);
}

void Search::printTreeHelper(
    ostream &out,
    const SearchNode *n,
    const PrintTreeOptions &options,
    string &prefix,
    int64_t origVisits,
    int depth,
    const AnalysisData &data,
    Player perspective) const
{
  if (n == NULL)
    return;

  const SearchNode &node = *n;

  Player perspectiveToUse = (perspective != P_BLACK && perspective != P_WHITE) ? n->nextPla : perspective;
  double perspectiveFactor = perspectiveToUse == P_BLACK ? -1.0 : 1.0;

  if (depth == 0)
    origVisits = data.numVisits;

  // Output for this node
  {
    out << prefix;
    char buf[128];

    out << ": ";

    if (data.numVisits > 0)
    {
      sprintf(buf, "T %6.2fc ", (perspectiveFactor * data.utility * 100.0));
      out << buf;
      sprintf(buf, "W %6.2fc ", (perspectiveFactor * data.resultUtility * 100.0));
      out << buf;
      sprintf(
          buf,
          "S %6.2fc (%+5.1f L %+5.1f) ",
          perspectiveFactor * data.scoreUtility * 100.0,
          perspectiveFactor * data.scoreMean,
          perspectiveFactor * data.lead);
      out << buf;
    }

    // bool hasNNValue = false;
    // double nnResultValue;
    // double nnTotalValue;
    // lock.lock();
    // if(node.nnOutput != nullptr) {
    //   nnResultValue = getResultUtilityFromNN(*node.nnOutput);
    //   nnTotalValue = getUtilityFromNN(*node.nnOutput);
    //   hasNNValue = true;
    // }
    // lock.unlock();

    // if(hasNNValue) {
    //   sprintf(buf,"VW %6.2fc VS %6.2fc ", nnResultValue * 100.0, (nnTotalValue - nnResultValue) * 100.0);
    //   out << buf;
    // }
    // else {
    //   sprintf(buf,"VW ---.--c VS ---.--c ");
    //   out << buf;
    // }

    if (depth > 0 && !isnan(data.lcb))
    {
      sprintf(buf, "LCB %7.2fc ", perspectiveFactor * data.lcb * 100.0);
      out << buf;
    }

    if (!isnan(data.policyPrior))
    {
      sprintf(buf, "P %5.2f%% ", data.policyPrior * 100.0);
      out << buf;
    }
    if (!isnan(data.weightFactor))
    {
      sprintf(buf, "WF %5.1f ", data.weightFactor);
      out << buf;
    }
    if (data.playSelectionValue >= 0 && depth > 0)
    {
      sprintf(buf, "PSV %7.0f ", data.playSelectionValue);
      out << buf;
    }

    if (options.printSqs_)
    {
      while (node.statsLock.test_and_set(std::memory_order_acquire))
        ;
      double scoreMeanSqSum = node.stats.scoreMeanSqSum;
      double utilitySqSum = node.stats.utilitySqSum;
      double weightSum = node.stats.weightSum;
      double weightSqSum = node.stats.weightSqSum;
      node.statsLock.clear(std::memory_order_release);
      sprintf(
          buf,
          "SMSQ %5.1f USQ %7.5f W %6.2f WSQ %8.2f ",
          scoreMeanSqSum / weightSum,
          utilitySqSum / weightSum,
          weightSum,
          weightSqSum);
      out << buf;
    }

    sprintf(buf, "N %7" PRIu64 "  --  ", data.numVisits);
    out << buf;

    printPV(out, data.pvFrom,data.pvTo);
    out << endl;
  }

  if (depth >= options.branch_.size())
  {
    if (depth >= options.maxDepth_ + options.branch_.size())
      return;
    if (data.numVisits < options.minVisitsToExpand_)
      return;
    if ((double)data.numVisits < origVisits * options.minVisitsPropToExpand_)
      return;
  }
  if (depth == options.branch_.size())
  {
    out << "---" << PlayerIO::playerToString(node.nextPla) << "(" << (node.nextPla == perspectiveToUse ? "^" : "v")
        << ")---" << endl;
  }

  vector<AnalysisData> analysisData;
  getAnalysisData(node, analysisData, 0, true, options.maxPVDepth_);

  int numChildren = analysisData.size();

  // Apply filtering conditions, but include children that don't match the filtering condition
  // but where there are children afterward that do, in case we ever use something more complex
  // than plain visits as a filter criterion. Do this by finding the last child that we want as the threshold.
  int lastIdxWithEnoughVisits = numChildren - 1;
  while (true)
  {
    if (lastIdxWithEnoughVisits <= 0)
      break;

    int64_t childVisits = analysisData[lastIdxWithEnoughVisits].numVisits;
    bool hasEnoughVisits =
        childVisits >= options.minVisitsToShow_ && (double)childVisits >= origVisits * options.minVisitsPropToShow_;
    if (hasEnoughVisits)
      break;
    lastIdxWithEnoughVisits--;
  }

  int numChildrenToRecurseOn = numChildren;
  if (options.maxChildrenToShow_ < numChildrenToRecurseOn)
    numChildrenToRecurseOn = options.maxChildrenToShow_;
  if (lastIdxWithEnoughVisits + 1 < numChildrenToRecurseOn)
    numChildrenToRecurseOn = lastIdxWithEnoughVisits + 1;

  for (int i = 0; i < numChildren; i++)
  {
    const SearchNode *child = analysisData[i].node;

    if ((depth >= options.branch_.size() && i < numChildrenToRecurseOn) || depth < options.branch_.size())
    {
      size_t oldLen = prefix.length();
      string fromstr = Location::toString(child->prevFromLoc, rootBoard);
      string tostr = Location::toString(child->prevToLoc, rootBoard);
      prefix += fromstr;
      prefix += " ";
      prefix += tostr;
      prefix += " ";
      while (prefix.length() < oldLen + 4)
        prefix += " ";
      printTreeHelper(out, child, options, prefix, origVisits, depth + 1, analysisData[i], perspective);
      prefix.erase(oldLen);
    }
  }
}

vector<double> Search::getAverageTreeOwnership(int64_t minVisits, const SearchNode *node) const
{
  if (node == NULL)
    node = rootNode;
  if (!alwaysIncludeOwnerMap)
    throw StringError("Called Search::getAverageTreeOwnership when alwaysIncludeOwnerMap is false");
  vector<double> vec(nnXLen * nnYLen, 0.0);
  getAverageTreeOwnershipHelper(vec, minVisits, 1.0, node);
  return vec;
}

double Search::getAverageTreeOwnershipHelper(
    vector<double> &accum,
    int64_t minVisits,
    double desiredWeight,
    const SearchNode *node) const
{
  if (node == NULL)
    return 0;

  std::mutex &mutex = mutexPool->getMutex(node->lockIdx);
  unique_lock<std::mutex> lock(mutex);
  if (node->nnOutput == nullptr)
    return 0;

  shared_ptr<NNOutput> nnOutput = node->nnOutput;

  int numChildren = node->numChildren;
  vector<const SearchNode *> children(numChildren);
  for (int i = 0; i < numChildren; i++)
    children[i] = node->children[i];

  // We can unlock now - during a search, children are never deallocated
  lock.unlock();

  vector<int64_t> visitsBuf(numChildren);
  for (int i = 0; i < numChildren; i++)
  {
    const SearchNode *child = children[i];
    while (child->statsLock.test_and_set(std::memory_order_acquire))
      ;
    int64_t childVisits = child->stats.visits;
    child->statsLock.clear(std::memory_order_release);
    visitsBuf[i] = childVisits;
  }

  double relativeChildrenWeightSum = 0.0;
  int64_t usedChildrenVisitSum = 0;
  for (int i = 0; i < numChildren; i++)
  {
    int64_t visits = visitsBuf[i];
    if (visits < minVisits)
      continue;
    relativeChildrenWeightSum += (double)visits * visits;
    usedChildrenVisitSum += visits;
  }

  double desiredWeightFromChildren = desiredWeight * usedChildrenVisitSum / (usedChildrenVisitSum + 1);

  // Recurse
  double actualWeightFromChildren = 0.0;
  for (int i = 0; i < numChildren; i++)
  {
    int64_t visits = visitsBuf[i];
    if (visits < minVisits)
      continue;
    double desiredWeightFromChild = (double)visits * visits / relativeChildrenWeightSum * desiredWeightFromChildren;
    actualWeightFromChildren += getAverageTreeOwnershipHelper(accum, minVisits, desiredWeightFromChild, children[i]);
  }

  double selfWeight = desiredWeight - actualWeightFromChildren;
  float *ownerMap = nnOutput->whiteOwnerMap;
  assert(ownerMap != NULL);
  for (int pos = 0; pos < nnXLen * nnYLen; pos++)
    accum[pos] += selfWeight * ownerMap[pos];

  return desiredWeight;
}

json Search::getJsonOwnershipMap(
    const Player pla,
    const Player perspective,
    const Board &board,
    const SearchNode *node,
    int ownershipMinVisits) const
{
  vector<double> ownership = getAverageTreeOwnership(ownershipMinVisits, node);
  json ownerships = json::array();
  for (int y = 0; y < board.y_size; y++)
  {
    for (int x = 0; x < board.x_size; x++)
    {
      for (int i = 0; i < board.x_size; i++)
      {
        for (int j = 0; j < board.y_size; j++)
        {
          int from = Location::getLoc(x, y, nnXLen);
          int to = Location::getLoc(i, j, nnXLen);
          int pos = NNPos::locToDoublePos(from, to, board.x_size, nnXLen, nnYLen);
          double o;
          if (perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK))
            o = -ownership[pos];
          else
            o = ownership[pos];
          ownerships.push_back(o);
        }
      }
    }
  }
  return ownerships;
}

bool Search::getAnalysisJson(
    const Player perspective,
    const Board &board,
    const BoardHistory &hist,
    int analysisPVLen,
    int ownershipMinVisits,
    bool preventEncore,
    bool includePolicy,
    bool includeOwnership,
    bool includeMovesOwnership,
    bool includePVVisits,
    json &ret) const
{
  vector<AnalysisData> buf;
  static constexpr int minMoves = 0;

  getAnalysisData(buf, minMoves, false, analysisPVLen);

  // Stats for all the individual moves
  json moveInfos = json::array();
  for (int i = 0; i < buf.size(); i++)
  {
    const AnalysisData &data = buf[i];
    double winrate = 0.5 * (1.0 + data.winLossValue);
    double utility = data.utility;
    double lcb = PlayUtils::getHackedLCBForWinrate(this, data, rootPla);
    double utilityLcb = data.lcb;
    double scoreMean = data.scoreMean;
    double lead = data.lead;
    if (perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && rootPla == P_BLACK))
    {
      winrate = 1.0 - winrate;
      lcb = 1.0 - lcb;
      utility = -utility;
      scoreMean = -scoreMean;
      lead = -lead;
      utilityLcb = -utilityLcb;
    }

    json moveInfo;
    moveInfo["from"] = Location::toString(data.fromLoc, board);
    moveInfo["to"] = Location::toString(data.toLoc, board);
    moveInfo["visits"] = data.numVisits;
    moveInfo["utility"] = utility;
    moveInfo["winrate"] = winrate;
    moveInfo["scoreMean"] = lead;
    moveInfo["scoreSelfplay"] = scoreMean;
    moveInfo["scoreLead"] = lead;
    moveInfo["scoreStdev"] = data.scoreStdev;
    moveInfo["prior"] = data.policyPrior;
    moveInfo["lcb"] = lcb;
    moveInfo["utilityLcb"] = utilityLcb;
    moveInfo["order"] = data.order;

    json pv = json::array();
    int pvLen =
        (preventEncore && data.pvContainsPass()) ? data.getPVLenUpToPhaseEnd(board, hist, rootPla) : (int)data.pvFrom.size();
    for (int j = 0; j < pvLen; j++){
      pv.push_back(Location::toString(data.pvFrom[j], board));
      pv.push_back(Location::toString(data.pvTo[j], board));
    }
    moveInfo["pv"] = pv;

    if (includePVVisits)
    {
      assert(data.pvVisits.size() >= pvLen);
      json pvVisits = json::array();
      for (int j = 0; j < pvLen; j++)
        pvVisits.push_back(data.pvVisits[j]);
      moveInfo["pvVisits"] = pvVisits;
    }

    if (includeMovesOwnership)
      moveInfo["ownership"] = getJsonOwnershipMap(rootPla, perspective, board, data.node, ownershipMinVisits);
    moveInfos.push_back(moveInfo);
  }
  ret["moveInfos"] = moveInfos;

  // Stats for root position
  {
    ReportedSearchValues rootVals;
    bool suc = getRootValues(rootVals);
    if (!suc)
      return false;

    double winrate = 0.5 * (1.0 + rootVals.winLossValue);
    double scoreMean = rootVals.expectedScore;
    double lead = rootVals.lead;
    double utility = rootVals.utility;

    if (perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && rootPla == P_BLACK))
    {
      winrate = 1.0 - winrate;
      scoreMean = -scoreMean;
      lead = -lead;
      utility = -utility;
    }

    json rootInfo;
    rootInfo["visits"] = rootVals.visits;
    rootInfo["winrate"] = winrate;
    rootInfo["scoreSelfplay"] = scoreMean;
    rootInfo["scoreLead"] = lead;
    rootInfo["scoreStdev"] = rootVals.expectedScoreStdev;
    rootInfo["utility"] = utility;

    Hash128 thisHash;
    Hash128 symHash;
    for (int symmetry = 0; symmetry < 8; symmetry++)
    {
      Board symBoard = SymmetryHelpers::getSymBoard(board, symmetry);
      Hash128 hash = symBoard.getSitHashWithSimpleKo(rootPla);
      if (symmetry == 0)
      {
        thisHash = hash;
        symHash = hash;
      }
      else
      {
        if (hash < symHash)
          symHash = hash;
      }
    }
    rootInfo["thisHash"] = Global::uint64ToHexString(thisHash.hash1) + Global::uint64ToHexString(thisHash.hash0);
    rootInfo["symHash"] = Global::uint64ToHexString(symHash.hash1) + Global::uint64ToHexString(symHash.hash0);

    ret["rootInfo"] = rootInfo;
  }
  // Raw policy prior
  if (includePolicy)
  {
    float policyProbs[NNPos::MAX_NN_POLICY_SIZE];
    bool suc = getPolicy(policyProbs);
    if (!suc)
      return false;
    json policy = json::array();
    for (int y = 0; y < board.y_size; y++)
    {
      for (int x = 0; x < board.x_size; x++)
      {
        for (int i = 0; i < board.x_size; i++)
        {
          for (int j = 0; j < board.y_size; j++)
          {
            int from = Location::getLoc(x, y, nnXLen);
            int to = Location::getLoc(i, j, nnXLen);
            int pos = NNPos::locToDoublePos(from, to, board.x_size, nnXLen, nnYLen);
            policy.push_back(policyProbs[pos]);
          }
        }
      }
    }
    ret["policy"] = policy;
  }
  // Average tree ownership
  if (includeOwnership)
    ret["ownership"] = getJsonOwnershipMap(rootPla, perspective, board, rootNode, ownershipMinVisits);
  return true;
}
