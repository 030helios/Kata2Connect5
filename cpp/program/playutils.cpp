#include "../program/playutils.h"
#include "../core/timer.h"

#include <sstream>

using namespace std;

static int getDefaultMaxExtraBlack(double sqrtBoardArea)
{
  if (sqrtBoardArea <= 10.00001)
    return 0;
  if (sqrtBoardArea <= 14.00001)
    return 1;
  if (sqrtBoardArea <= 16.00001)
    return 2;
  if (sqrtBoardArea <= 17.00001)
    return 3;
  if (sqrtBoardArea <= 18.00001)
    return 4;
  return 5;
}

ExtraBlackAndKomi PlayUtils::chooseExtraBlackAndKomi(
    float base, float stdev, double allowIntegerProb,
    double handicapProb, int numExtraBlackFixed,
    double bigStdevProb, float bigStdev, double sqrtBoardArea, Rand &rand)
{
  int extraBlack = 0;
  float komi = base;

  float stdevToUse = 0.0f;
  if (stdev > 0.0f)
    stdevToUse = stdev;
  if (bigStdev > 0.0f && rand.nextBool(bigStdevProb))
    stdevToUse = bigStdev;
  //Adjust for board size, so that we don't give the same massive komis on smaller boards
  stdevToUse = stdevToUse * (float)(sqrtBoardArea / 6.0);

  //Add handicap stones
  int defaultMaxExtraBlack = getDefaultMaxExtraBlack(sqrtBoardArea);
  if ((numExtraBlackFixed > 0 || defaultMaxExtraBlack > 0) && rand.nextBool(handicapProb))
  {
    if (numExtraBlackFixed > 0)
      extraBlack = numExtraBlackFixed;
    else
      extraBlack += 1 + rand.nextUInt(defaultMaxExtraBlack);
  }

  bool allowInteger = rand.nextBool(allowIntegerProb);

  ExtraBlackAndKomi ret;
  ret.extraBlack = extraBlack;
  ret.komiMean = komi;
  ret.komiStdev = stdevToUse;
  //These two are set later
  ret.makeGameFair = false;
  ret.makeGameFairForEmptyBoard = false;
  //This is recorded for application later, since other things may adjust the komi in between.
  ret.allowInteger = allowInteger;
  return ret;
}

static float roundKomiWithLinearProb(float komi, Rand &rand)
{
  //Discretize komi
  float lower = floor(komi * 2.0f) / 2.0f;
  float upper = ceil(komi * 2.0f) / 2.0f;

  if (lower == upper)
    komi = lower;
  else
  {
    assert(upper > lower);
    if (rand.nextDouble() < (komi - lower) / (upper - lower))
      komi = upper;
    else
      komi = lower;
  }
  return komi;
}

//Also ignores allowInteger
void PlayUtils::setKomiWithoutNoise(const ExtraBlackAndKomi &extraBlackAndKomi, BoardHistory &hist)
{
}

void PlayUtils::setKomiWithNoise(const ExtraBlackAndKomi &extraBlackAndKomi, BoardHistory &hist, Rand &rand)
{
}

Move PlayUtils::chooseRandomLegalMove(const Board &board, const BoardHistory &hist, Player pla, Rand &gameRand, Move banMove)
{
  int numLegalMoves = 0;
  Move moves[Board::MAX_ARR_SIZE * Board::MAX_ARR_SIZE];
  for (int y = 0; y < board.y_size; y++)
  {
    for (int x = 0; x < board.x_size; x++)
    {
      for (int i = 0; i < board.x_size; i++)
      {
        for (int j = 0; j < board.y_size; j++)
        {
          int from = Location::getLoc(x, y, board.x_size);
          int to = Location::getLoc(i, j, board.x_size);
          if (from == banMove.fromLoc && to == banMove.toLoc)
            continue;
          if (hist.isLegal(board, from, to, pla))
          {
            moves[numLegalMoves] = Move(from, to, pla);
            numLegalMoves += 1;
          }
        }
      }
    }
  }
  if (numLegalMoves > 0)
  {
    int n = gameRand.nextUInt(numLegalMoves);
    return moves[n];
  }
  return Move(0, 0, 0);
}

int PlayUtils::chooseRandomLegalMoves(const Board &board, const BoardHistory &hist, Player pla, Rand &gameRand, Move *buf, int len)
{
  int numLegalMoves = 0;
  Move moves[Board::MAX_ARR_SIZE * Board::MAX_ARR_SIZE];
  for (int y = 0; y < board.y_size; y++)
  {
    for (int x = 0; x < board.x_size; x++)
    {
      for (int i = 0; i < board.x_size; i++)
      {
        for (int j = 0; j < board.y_size; j++)
        {
          int from = Location::getLoc(x, y, board.x_size);
          int to = Location::getLoc(i, j, board.x_size);
          if (hist.isLegal(board, from, to, pla))
          {
            moves[numLegalMoves] = Move(from, to, pla);
            numLegalMoves += 1;
          }
        }
      }
    }
  }
  if (numLegalMoves > 0)
  {
    for (int i = 0; i < len; i++)
    {
      int n = gameRand.nextUInt(numLegalMoves);
      buf[i] = moves[n];
    }
    return len;
  }
  return 0;
}

Move PlayUtils::chooseRandomPolicyMove(
    const NNOutput *nnOutput, const Board &board, const BoardHistory &hist, Player pla, Rand &gameRand, double temperature, bool allowPass, Move banMove)
{
  const float *policyProbs = nnOutput->policyProbs;
  int nnXLen = nnOutput->nnXLen;
  int nnYLen = nnOutput->nnYLen;
  int numLegalMoves = 0;
  double relProbs[NNPos::MAX_NN_POLICY_SIZE];
  Move moves[NNPos::MAX_NN_POLICY_SIZE];
  for (int pos = 0; pos < NNPos::MAX_NN_POLICY_SIZE; pos++)
  {
    Loc from = NNPos::posToFromLoc(pos, board.x_size, board.y_size, nnXLen, nnYLen);
    Loc to = NNPos::posToToLoc(pos, board.x_size, board.y_size, nnXLen, nnYLen);
    if ((from == Board::PASS_LOC || to == Board::PASS_LOC)) // || loc == banMove)
      continue;
    if (policyProbs[pos] > 0.0 && hist.isLegal(board, from, to, pla))
    {
      double relProb = policyProbs[pos];
      relProbs[numLegalMoves] = relProb;
      moves[numLegalMoves] = Move(from, to, pla);
      numLegalMoves += 1;
    }
  }

  //Just in case the policy map is somehow not consistent with the board position
  if (numLegalMoves > 0)
  {
    uint32_t n = Search::chooseIndexWithTemperature(gameRand, relProbs, numLegalMoves, temperature);
    return moves[n];
  }
  return Move(0, 0, 0);
}

Move PlayUtils::getGameInitializationMove(
    Search *botB, Search *botW, Board &board, const BoardHistory &hist, Player pla, NNResultBuf &buf,
    Rand &gameRand, double temperature)
{
  NNEvaluator *nnEval = (pla == P_BLACK ? botB : botW)->nnEvaluator;
  MiscNNInputParams nnInputParams;
  nnInputParams.drawEquivalentWinsForWhite = (pla == P_BLACK ? botB : botW)->searchParams.drawEquivalentWinsForWhite;
  nnEval->evaluate(board, hist, pla, nnInputParams, buf, false, false);
  std::shared_ptr<NNOutput> nnOutput = std::move(buf.result);

  std::vector<Move> moves;
  std::vector<double> playSelectionValues;
  int nnXLen = nnOutput->nnXLen;
  int nnYLen = nnOutput->nnYLen;
  assert(nnXLen >= board.x_size);
  assert(nnYLen >= board.y_size);
  assert(nnXLen > 0 && nnXLen < 100); //Just a sanity check to make sure no other crazy values have snuck in
  assert(nnYLen > 0 && nnYLen < 100); //Just a sanity check to make sure no other crazy values have snuck in
  int policySize = NNPos::getPolicySize(nnXLen, nnYLen);
  for (int movePos = 0; movePos < policySize; movePos++)
  {
    Loc from = NNPos::posToFromLoc(movePos, board.x_size, board.y_size, nnXLen, nnYLen);
    Loc to = NNPos::posToToLoc(movePos, board.x_size, board.y_size, nnXLen, nnYLen);
    double policyProb = nnOutput->policyProbs[movePos];
    if (!hist.isLegal(board, from, to, pla) || policyProb <= 0)
      continue;
    moves.push_back(Move(from, to, pla));
    playSelectionValues.push_back(pow(policyProb, 1.0 / temperature));
  }

  //In practice, this should never happen, but in theory, a very badly-behaved net that rounds
  //all legal moves to zero could result in this. We still go ahead and fail, since this more likely some sort of bug.
  if (playSelectionValues.size() <= 0)
    throw StringError("getGameInitializationMove: playSelectionValues.size() <= 0");

  //With a tiny probability, choose a uniformly random move instead of a policy move, to also
  //add a bit more outlierish variety
  uint32_t idxChosen;
  if (gameRand.nextBool(0.0002))
    idxChosen = gameRand.nextUInt(playSelectionValues.size());
  else
    idxChosen = gameRand.nextUInt(playSelectionValues.data(), playSelectionValues.size());
  return moves[idxChosen];
}

//Try playing a bunch of pure policy moves instead of playing from the start to initialize the board
//and add entropy
void PlayUtils::initializeGameUsingPolicy(
    Search *botB, Search *botW, Board &board, BoardHistory &hist, Player &pla,
    Rand &gameRand, bool doEndGameIfAllPassAlive,
    double proportionOfBoardArea, double temperature)
{
  NNResultBuf buf;

  //This gives us about 15 moves on average for 19x19.
  int numInitialMovesToPlay = (int)floor(gameRand.nextExponential() * (board.x_size * board.y_size * proportionOfBoardArea));
  assert(numInitialMovesToPlay >= 0);
  for (int i = 0; i < numInitialMovesToPlay; i++)
  {
    Move mov = getGameInitializationMove(botB, botW, board, hist, pla, buf, gameRand, temperature);

    assert(hist.isLegal(board, mov.fromLoc, mov.toLoc, pla));
    //Make the move!
    hist.makeBoardMoveAssumeLegal(board, mov.fromLoc, mov.toLoc, pla, NULL);
    pla = getOpp(pla);

    //Rarely, playing the random moves out this way will end the game
    if (hist.isGameFinished)
      break;
  }
}

double PlayUtils::getHackedLCBForWinrate(const Search *search, const AnalysisData &data, Player pla)
{
  double winrate = 0.5 * (1.0 + data.winLossValue);
  //Super hacky - in KataGo, lcb is on utility (i.e. also weighting score), not winrate, but if you're using
  //lz-analyze you probably don't know about utility and expect LCB to be about winrate. So we apply the LCB
  //radius to the winrate in order to get something reasonable to display, and also scale it proportionally
  //by how much winrate is supposed to matter relative to score.
  double radiusScaleHackFactor = search->searchParams.winLossUtilityFactor / (search->searchParams.winLossUtilityFactor +
                                                                              search->searchParams.staticScoreUtilityFactor +
                                                                              search->searchParams.dynamicScoreUtilityFactor +
                                                                              1.0e-20 //avoid divide by 0
                                                                             );
  //Also another factor of 0.5 because winrate goes from only 0 to 1 instead of -1 to 1 when it's part of utility
  radiusScaleHackFactor *= 0.5;
  double lcb = pla == P_WHITE ? winrate - data.radius * radiusScaleHackFactor : winrate + data.radius * radiusScaleHackFactor;
  return lcb;
}

float PlayUtils::roundAndClipKomi(double unrounded, const Board &board, bool looseClipping)
{
  //Just in case, make sure komi is reasonable
  float range = looseClipping ? 40.0f + board.x_size * board.y_size : 40.0f + 0.5f * board.x_size * board.y_size;
  if (unrounded < -range)
    unrounded = -range;
  if (unrounded > range)
    unrounded = range;
  return (float)(0.5 * round(2.0 * unrounded));
}

static SearchParams getNoiselessParams(SearchParams oldParams, int64_t numVisits)
{
  SearchParams newParams = oldParams;
  newParams.maxVisits = numVisits;
  newParams.maxPlayouts = numVisits;
  newParams.rootNoiseEnabled = false;
  newParams.rootPolicyTemperature = 1.0;
  newParams.rootPolicyTemperatureEarly = 1.0;
  newParams.rootFpuReductionMax = newParams.fpuReductionMax;
  newParams.rootFpuLossProp = newParams.fpuLossProp;
  newParams.rootDesiredPerChildVisitsCoeff = 0.0;
  newParams.rootNumSymmetriesToSample = 1;
  newParams.searchFactorAfterOnePass = 1.0;
  newParams.searchFactorAfterTwoPass = 1.0;
  if (newParams.numThreads > (numVisits + 7) / 8)
    newParams.numThreads = (numVisits + 7) / 8;
  return newParams;
}

ReportedSearchValues PlayUtils::getWhiteScoreValues(
    Search *bot,
    const Board &board,
    const BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger,
    const OtherGameProperties &otherGameProps)
{
  assert(numVisits > 0);
  SearchParams oldParams = bot->searchParams;
  SearchParams newParams = getNoiselessParams(oldParams, numVisits);

  if (otherGameProps.playoutDoublingAdvantage != 0.0 && otherGameProps.playoutDoublingAdvantagePla != C_EMPTY)
  {
    //Don't actually adjust playouts, but DO tell the bot what it's up against, so that it gives estimates
    //appropriate to the asymmetric game about to be played
    newParams.playoutDoublingAdvantagePla = otherGameProps.playoutDoublingAdvantagePla;
    newParams.playoutDoublingAdvantage = otherGameProps.playoutDoublingAdvantage;
  }

  bot->setParams(newParams);
  bot->setPosition(pla, board, hist);
  bot->runWholeSearch(pla, logger);

  ReportedSearchValues values = bot->getRootValuesRequireSuccess();
  bot->setParams(oldParams);
  return values;
}

static std::pair<double, double> evalKomi(
    map<float, std::pair<double, double>> &scoreWLCache,
    Search *botB,
    Search *botW,
    const Board &board,
    BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger,
    const OtherGameProperties &otherGameProps,
    float roundedClippedKomi)
{
  auto iter = scoreWLCache.find(roundedClippedKomi);
  if (iter != scoreWLCache.end())
    return iter->second;

  float oldKomi = hist.rules.komi;
  hist.rules.komi = (roundedClippedKomi);

  ReportedSearchValues values0 = PlayUtils::getWhiteScoreValues(botB, board, hist, pla, numVisits, logger, otherGameProps);
  double lead = values0.lead;
  double winLoss = values0.winLossValue;

  //If we have a second bot, average the two
  if (botW != NULL && botW != botB)
  {
    ReportedSearchValues values1 = PlayUtils::getWhiteScoreValues(botW, board, hist, pla, numVisits, logger, otherGameProps);
    lead = 0.5 * (values0.lead + values1.lead);
    winLoss = 0.5 * (values0.winLossValue + values1.winLossValue);
  }
  std::pair<double, double> result = std::make_pair(lead, winLoss);
  scoreWLCache[roundedClippedKomi] = result;

  hist.rules.komi = (oldKomi);
  return result;
}

static double getNaiveEvenKomiHelper(
    map<float, std::pair<double, double>> &scoreWLCache,
    Search *botB,
    Search *botW,
    const Board &board,
    BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger,
    const OtherGameProperties &otherGameProps,
    bool looseClipping)
{
  float oldKomi = hist.rules.komi;

  //A few times iterate based on expected score a few times to hopefully get a value close to fair
  double lastShift = 0.0;
  double lastWinLoss = 0.0;
  double lastLead = 0.0;
  for (int i = 0; i < 3; i++)
  {
    std::pair<double, double> result = evalKomi(scoreWLCache, botB, botW, board, hist, pla, numVisits, logger, otherGameProps, hist.rules.komi);
    double lead = result.first;
    double winLoss = result.second;

    //If the last shift made stats go the WRONG way, and by a nontrivial amount, then revert half of it and stop immediately.
    if (i > 0)
    {
      if ((lastLead > 0 && lead > lastLead + 5 && winLoss < 0.75) ||
          (lastLead < 0 && lead < lastLead - 5 && winLoss > -0.75) ||
          (lastWinLoss > 0 && winLoss > lastWinLoss + 0.1) ||
          (lastWinLoss < 0 && winLoss < lastWinLoss - 0.1))
      {
        float fairKomi = PlayUtils::roundAndClipKomi(hist.rules.komi - lastShift * 0.5f, board, looseClipping);
        hist.rules.komi = (fairKomi);
        // cout << "STOP" << endl;
        // cout << lastLead << " " << lead << " " << lastWinLoss << " " << winLoss << endl;
        break;
      }
    }
    lastLead = lead;
    lastWinLoss = winLoss;

    // cout << hist.rules.komi << " " << lead << " " << winLoss << endl;

    //Shift by the predicted lead
    double shift = -lead;
    //Under no situations should the shift be bigger in absolute value than the last shift
    if (i > 0 && abs(shift) > abs(lastShift))
    {
      if (shift < 0)
        shift = -abs(lastShift);
      else if (shift > 0)
        shift = abs(lastShift);
    }
    lastShift = shift;

    //If the score and winrate would like to move in opposite directions, quit immediately.
    if ((shift > 0 && winLoss > 0) || (shift < 0 && lead < 0))
      break;

    // cout << "Shifting by " << shift << endl;
    float fairKomi = PlayUtils::roundAndClipKomi(hist.rules.komi + shift, board, looseClipping);
    hist.rules.komi = (fairKomi);

    //After a small shift, break out to the binary search.
    if (abs(shift) < 16.0)
      break;
  }

  //Try a small window and do a binary search
  auto evalWinLoss = [&](double delta)
  {
    double newKomi = hist.rules.komi + delta;
    double winLoss = evalKomi(scoreWLCache, botB, botW, board, hist, pla, numVisits, logger, otherGameProps, PlayUtils::roundAndClipKomi(newKomi, board, looseClipping)).second;
    // cout << "Delta " << delta << " wr " << winLoss << endl;
    return winLoss;
  };

  double lowerDelta;
  double upperDelta;
  double lowerWinLoss;
  double upperWinLoss;

  //Grow window outward
  {
    double winLossZero = evalWinLoss(0);
    if (winLossZero < 0)
    {
      //Losing, so this is the lower bound
      lowerDelta = 0.0;
      lowerWinLoss = winLossZero;
      for (int i = 0; i <= 5; i++)
      {
        upperDelta = round(pow(2.0, i));
        upperWinLoss = evalWinLoss(upperDelta);
        if (upperWinLoss >= 0)
          break;
      }
    }
    else
    {
      //Winning, so this is the upper bound
      upperDelta = 0.0;
      upperWinLoss = winLossZero;
      for (int i = 0; i <= 5; i++)
      {
        lowerDelta = -round(pow(2.0, i));
        lowerWinLoss = evalWinLoss(lowerDelta);
        if (lowerWinLoss <= 0)
          break;
      }
    }
  }

  while (upperDelta - lowerDelta > 0.50001)
  {
    double midDelta = 0.5 * (lowerDelta + upperDelta);
    double midWinLoss = evalWinLoss(midDelta);
    if (midWinLoss < 0)
    {
      lowerDelta = midDelta;
      lowerWinLoss = midWinLoss;
    }
    else
    {
      upperDelta = midDelta;
      upperWinLoss = midWinLoss;
    }
  }
  //Floating point math should be exact to multiples of 0.5 so this should hold *exactly*.
  assert(upperDelta - lowerDelta == 0.5);

  double finalDelta;
  //If the winLoss are crossed, potentially due to noise, then just pick the average
  if (lowerWinLoss >= upperWinLoss - 1e-30)
    finalDelta = 0.5 * (lowerDelta + upperDelta);
  //If 0 is outside of the range, then choose the endpoint of the range.
  else if (upperWinLoss <= 0)
    finalDelta = upperDelta;
  else if (lowerWinLoss >= 0)
    finalDelta = lowerDelta;
  //Interpolate
  else
    finalDelta = lowerDelta + (upperDelta - lowerDelta) * (0 - lowerWinLoss) / (upperWinLoss - lowerWinLoss);

  double newKomi = hist.rules.komi + finalDelta;
  // cout << "Final " << finalDelta << " " << newKomi << endl;

  hist.rules.komi = (oldKomi);
  return newKomi;
}

void PlayUtils::adjustKomiToEven(
    Search *botB,
    Search *botW,
    const Board &board,
    BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger,
    const OtherGameProperties &otherGameProps,
    Rand &rand)
{
  map<float, std::pair<double, double>> scoreWLCache;
  bool looseClipping = false;
  double newKomi = getNaiveEvenKomiHelper(scoreWLCache, botB, botW, board, hist, pla, numVisits, logger, otherGameProps, looseClipping);
  double lower = floor(newKomi * 2.0) * 0.5;
  double upper = lower + 0.5;
  if (rand.nextBool((newKomi - lower) / (upper - lower)))
    newKomi = upper;
  else
    newKomi = lower;
  hist.rules.komi = (PlayUtils::roundAndClipKomi(newKomi, board, looseClipping));
}

float PlayUtils::computeLead(
    Search *botB,
    Search *botW,
    const Board &board,
    BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger,
    const OtherGameProperties &otherGameProps)
{
  map<float, std::pair<double, double>> scoreWLCache;
  bool looseClipping = true;
  float oldKomi = hist.rules.komi;
  double naiveKomi = getNaiveEvenKomiHelper(scoreWLCache, botB, botW, board, hist, pla, numVisits, logger, otherGameProps, looseClipping);

  bool granularityIsCoarse = hist.rules.scoringRule == Rules::SCORING_AREA;
  if (!granularityIsCoarse)
  {
    assert(hist.rules.komi == oldKomi);
    return (float)(oldKomi - naiveKomi);
  }

  auto evalWinLoss = [&](double newKomi)
  {
    double winLoss = evalKomi(scoreWLCache, botB, botW, board, hist, pla, numVisits, logger, otherGameProps, PlayUtils::roundAndClipKomi(newKomi, board, looseClipping)).second;
    // cout << "Delta " << delta << " wr " << winLoss << endl;
    return winLoss;
  };

  //Smooth over area scoring 2-point granularity

  //If komi is exactly an integer, then we're good.
  if (naiveKomi == round(naiveKomi))
  {
    assert(hist.rules.komi == oldKomi);
    return (float)(oldKomi - naiveKomi);
  }

  double lower = floor(naiveKomi * 2.0) * 0.5;
  double upper = lower + 0.5;

  //Average out the oscillation
  double lowerWinLoss = 0.5 * (evalWinLoss(upper) + evalWinLoss(lower - 0.5));
  double upperWinLoss = 0.5 * (evalWinLoss(upper + 0.5) + evalWinLoss(lower));

  //If the winLoss are crossed, potentially due to noise, then just pick the average
  double result;
  if (lowerWinLoss >= upperWinLoss - 1e-30)
    result = 0.5 * (lower + upper);
  else
  {
    //Interpolate
    result = lower + (upper - lower) * (0 - lowerWinLoss) / (upperWinLoss - lowerWinLoss);
    //Bound the result to be within lower-0.5 and upper+0.5
    if (result < lower - 0.5)
      result = lower - 0.5;
    if (result > upper + 0.5)
      result = upper + 0.5;
  }
  assert(hist.rules.komi == oldKomi);
  return (float)(oldKomi - result);
}

double PlayUtils::getSearchFactor(
    double searchFactorWhenWinningThreshold,
    double searchFactorWhenWinning,
    const SearchParams &params,
    const std::vector<double> &recentWinLossValues,
    Player pla)
{
  double searchFactor = 1.0;
  if (recentWinLossValues.size() >= 3 && params.winLossUtilityFactor - searchFactorWhenWinningThreshold > 1e-10)
  {
    double recentLeastWinning = pla == P_BLACK ? -params.winLossUtilityFactor : params.winLossUtilityFactor;
    for (int i = recentWinLossValues.size() - 3; i < recentWinLossValues.size(); i++)
    {
      if (pla == P_BLACK && recentWinLossValues[i] > recentLeastWinning)
        recentLeastWinning = recentWinLossValues[i];
      if (pla == P_WHITE && recentWinLossValues[i] < recentLeastWinning)
        recentLeastWinning = recentWinLossValues[i];
    }
    double excessWinning = pla == P_BLACK ? -searchFactorWhenWinningThreshold - recentLeastWinning : recentLeastWinning - searchFactorWhenWinningThreshold;
    if (excessWinning > 0)
    {
      double lambda = excessWinning / (params.winLossUtilityFactor - searchFactorWhenWinningThreshold);
      searchFactor = 1.0 + lambda * (searchFactorWhenWinning - 1.0);
    }
  }
  return searchFactor;
}

vector<double> PlayUtils::computeOwnership(
    Search *bot,
    const Board &board,
    const BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger)
{
  assert(numVisits > 0);
  bool oldAlwaysIncludeOwnerMap = bot->alwaysIncludeOwnerMap;
  bot->setAlwaysIncludeOwnerMap(true);

  SearchParams oldParams = bot->searchParams;
  SearchParams newParams = getNoiselessParams(oldParams, numVisits);
  newParams.playoutDoublingAdvantagePla = C_EMPTY;
  newParams.playoutDoublingAdvantage = 0.0;
  //Make sure the search is always from a state where the game isn't believed to end with another pass

  bot->setParams(newParams);
  bot->setPosition(pla, board, hist);
  bot->runWholeSearch(pla, logger);

  int64_t minVisitsForOwnership = 2;
  std::vector<double> ownerships = bot->getAverageTreeOwnership(minVisitsForOwnership);

  bot->setParams(oldParams);
  bot->setAlwaysIncludeOwnerMap(oldAlwaysIncludeOwnerMap);
  bot->clearSearch();

  return ownerships;
}

//Tromp-taylor-like scoring, except recognizes pass-dead stones.
vector<bool> PlayUtils::computeAnticipatedStatusesSimple(
    const Board &board,
    const BoardHistory &hist)
{
  std::vector<bool> isAlive(Board::MAX_ARR_SIZE, false);

  //Treat all stones as alive under a no result
  if (hist.isGameFinished && hist.isNoResult)
  {
    for (int y = 0; y < board.y_size; y++)
    {
      for (int x = 0; x < board.x_size; x++)
      {
        Loc loc = Location::getLoc(x, y, board.x_size);
        if (board.colors[loc] != C_EMPTY)
          isAlive[loc] = true;
      }
    }
  }
  //Else use Tromp-taylorlike scoring, except recognizing pass-dead stones.
  else
  {
    Color area[Board::MAX_ARR_SIZE];
    BoardHistory histCopy = hist;
    histCopy.endAndScoreGameNow(board, area);
    for (int y = 0; y < board.y_size; y++)
    {
      for (int x = 0; x < board.x_size; x++)
      {
        Loc loc = Location::getLoc(x, y, board.x_size);
        if (board.colors[loc] != C_EMPTY)
        {
          isAlive[loc] = board.colors[loc] == area[loc];
        }
      }
    }
  }
  return isAlive;
}

//Always non-tromp-taylorlike in the main phase of the game, this is the ownership that users would want.
vector<bool> PlayUtils::computeAnticipatedStatusesWithOwnership(
    Search *bot,
    const Board &board,
    const BoardHistory &hist,
    Player pla,
    int64_t numVisits,
    Logger &logger,
    std::vector<double> &ownershipsBuf)
{
  std::vector<bool> isAlive(Board::MAX_ARR_SIZE, false);
  bool solved[Board::MAX_ARR_SIZE];
  for (int i = 0; i < Board::MAX_ARR_SIZE; i++)
  {
    isAlive[i] = false;
    solved[i] = false;
  }

  ownershipsBuf = computeOwnership(bot, board, hist, pla, numVisits, logger);
  const std::vector<double> &ownerships = ownershipsBuf;
  int nnXLen = bot->nnXLen;
  int nnYLen = bot->nnYLen;

  //Heuristic:
  //Stones are considered dead if their average ownership is less than 0.2 equity in their own color,
  //or if the worst equity in the chain is less than -0.6 equity in their color.
  const double avgThresholdForLife = 0.2;
  const double worstThresholdForLife = -0.6;

  for (int y = 0; y < board.y_size; y++)
  {
    for (int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, board.x_size);
      if (solved[loc])
        continue;

      if (board.colors[loc] == P_WHITE || board.colors[loc] == P_BLACK)
        isAlive[loc] = true;
    }
  }
  return isAlive;
}

string PlayUtils::BenchmarkResults::toStringNotDone() const
{
  ostringstream out;
  out << "numSearchThreads = " << Global::strprintf("%2d", numThreads) << ":"
      << " " << totalPositionsSearched << " / " << totalPositions << " positions,"
      << " visits/s = " << Global::strprintf("%.2f", totalVisits / totalSeconds)
      << " (" << Global::strprintf("%.1f", totalSeconds) << " secs)";
  return out.str();
}
string PlayUtils::BenchmarkResults::toString() const
{
  ostringstream out;
  out << "numSearchThreads = " << Global::strprintf("%2d", numThreads) << ":"
      << " " << totalPositionsSearched << " / " << totalPositions << " positions,"
      << " visits/s = " << Global::strprintf("%.2f", totalVisits / totalSeconds)
      << " nnEvals/s = " << Global::strprintf("%.2f", numNNEvals / totalSeconds)
      << " nnBatches/s = " << Global::strprintf("%.2f", numNNBatches / totalSeconds)
      << " avgBatchSize = " << Global::strprintf("%.2f", avgBatchSize)
      << " (" << Global::strprintf("%.1f", totalSeconds) << " secs)";
  return out.str();
}
string PlayUtils::BenchmarkResults::toStringWithElo(const BenchmarkResults *baseline, double secondsPerGameMove) const
{
  ostringstream out;
  out << "numSearchThreads = " << Global::strprintf("%2d", numThreads) << ":"
      << " " << totalPositionsSearched << " / " << totalPositions << " positions,"
      << " visits/s = " << Global::strprintf("%.2f", totalVisits / totalSeconds)
      << " nnEvals/s = " << Global::strprintf("%.2f", numNNEvals / totalSeconds)
      << " nnBatches/s = " << Global::strprintf("%.2f", numNNBatches / totalSeconds)
      << " avgBatchSize = " << Global::strprintf("%.2f", avgBatchSize)
      << " (" << Global::strprintf("%.1f", totalSeconds) << " secs)";

  if (baseline == NULL)
    out << " (EloDiff baseline)";
  else
  {
    double diff = computeEloEffect(secondsPerGameMove) - baseline->computeEloEffect(secondsPerGameMove);
    out << " (EloDiff " << Global::strprintf("%+.0f", diff) << ")";
  }
  return out.str();
}

//From some test matches by lightvector using g170
static constexpr double eloGainPerDoubling = 250;

double PlayUtils::BenchmarkResults::computeEloEffect(double secondsPerGameMove) const
{
  auto computeEloCost = [&](double baseVisits)
  {
    //Completely ad-hoc formula that approximately fits noisy tests. Probably not very good
    //but then again the recommendation of this benchmark program is very rough anyways, it
    //doesn't need to be all that great.
    return numThreads * 7.0 * pow(1600.0 / (800.0 + baseVisits), 0.85);
  };

  double visitsPerSecond = totalVisits / totalSeconds;
  double gain = eloGainPerDoubling * log(visitsPerSecond) / log(2);
  double visitsPerMove = visitsPerSecond * secondsPerGameMove;
  double cost = computeEloCost(visitsPerMove);
  return gain - cost;
}

void PlayUtils::BenchmarkResults::printEloComparison(const std::vector<BenchmarkResults> &results, double secondsPerGameMove)
{
  int bestIdx = 0;
  for (int i = 1; i < results.size(); i++)
  {
    if (results[i].computeEloEffect(secondsPerGameMove) > results[bestIdx].computeEloEffect(secondsPerGameMove))
      bestIdx = i;
  }

  cout << endl;
  cout << "Based on some test data, each speed doubling gains perhaps ~" << eloGainPerDoubling << " Elo by searching deeper." << endl;
  cout << "Based on some test data, each thread costs perhaps 7 Elo if using 800 visits, and 2 Elo if using 5000 visits (by making MCTS worse)." << endl;
  cout << "So APPROXIMATELY based on this benchmark, if you intend to do a " << secondsPerGameMove << " second search: " << endl;
  for (int i = 0; i < results.size(); i++)
  {
    int numThreads = results[i].numThreads;
    double eloEffect = results[i].computeEloEffect(secondsPerGameMove) - results[0].computeEloEffect(secondsPerGameMove);
    cout << "numSearchThreads = " << Global::strprintf("%2d", numThreads) << ": ";
    if (i == 0)
      cout << "(baseline)" << (i == bestIdx ? " (recommended)" : "") << endl;
    else
      cout << Global::strprintf("%+5.0f", eloEffect) << " Elo" << (i == bestIdx ? " (recommended)" : "") << endl;
  }
  cout << endl;
}

PlayUtils::BenchmarkResults PlayUtils::benchmarkSearchOnPositionsAndPrint(
    const SearchParams &params,
    const CompactSgf *sgf,
    int numPositionsToUse,
    NNEvaluator *nnEval,
    Logger &logger,
    const BenchmarkResults *baseline,
    double secondsPerGameMove,
    bool printElo)
{
  //Pick random positions from the SGF file, but deterministically
  std::vector<Move> moves = sgf->moves;
  string posSeed = "benchmarkPosSeed|";
  for (int i = 0; i < moves.size(); i++)
  { /*
    posSeed += Global::intToString((int)moves[i].loc);
    */
    posSeed += "|";
  }

  std::vector<int> possiblePositionIdxs;
  {
    Rand posRand(posSeed);
    for (int i = 0; i < moves.size(); i++)
    {
      possiblePositionIdxs.push_back(i);
    }
    if (possiblePositionIdxs.size() > 0)
    {
      for (int i = possiblePositionIdxs.size() - 1; i > 1; i--)
      {
        int r = posRand.nextUInt(i);
        int tmp = possiblePositionIdxs[i];
        possiblePositionIdxs[i] = possiblePositionIdxs[r];
        possiblePositionIdxs[r] = tmp;
      }
    }
    if (possiblePositionIdxs.size() > numPositionsToUse)
      possiblePositionIdxs.resize(numPositionsToUse);
  }

  std::sort(possiblePositionIdxs.begin(), possiblePositionIdxs.end());

  BenchmarkResults results;
  results.numThreads = params.numThreads;
  results.totalPositions = possiblePositionIdxs.size();

  nnEval->clearCache();
  nnEval->clearStats();

  Rand seedRand;
  Search *bot = new Search(params, nnEval, Global::uint64ToString(seedRand.nextUInt64()));

  //Ignore the SGF rules, except for komi. Just use Tromp-taylor.
  Rules initialRules = Rules::getTrompTaylorish();
  //Take the komi from the sgf, otherwise ignore the rules in the sgf
  initialRules.komi = sgf->komi;

  Board board;
  Player nextPla;
  BoardHistory hist;
  sgf->setupInitialBoardAndHist(initialRules, board, nextPla, hist);

  int moveNum = 0;

  for (int i = 0; i < possiblePositionIdxs.size(); i++)
  {
    cout << "\r" << results.toStringNotDone() << "      " << std::flush;

    int nextIdx = possiblePositionIdxs[i];
    while (moveNum < moves.size() && moveNum < nextIdx)
    {
      bool suc = hist.isLegal(board, moves[moveNum].fromLoc, moves[moveNum].toLoc, moves[moveNum].pla);
      if (suc)
        hist.makeBoardMoveAssumeLegal(board, moves[moveNum].fromLoc, moves[moveNum].toLoc, moves[moveNum].pla, NULL);
      if (!suc)
      {
        cerr << endl;
        cerr << board << endl;
        cerr << "SGF Illegal move " << (moveNum + 1) << " for " << PlayerIO::colorToChar(moves[moveNum].pla) << ": " << Location::toString(moves[moveNum].fromLoc, board) << " " << Location::toString(moves[moveNum].toLoc, board) << endl;
        throw StringError("Illegal move in SGF");
      }
      nextPla = getOpp(moves[moveNum].pla);
      moveNum += 1;
    }

    bot->clearSearch();
    bot->setPosition(nextPla, board, hist);
    nnEval->clearCache();

    ClockTimer timer;
    bot->runWholeSearch(nextPla, logger);
    double seconds = timer.getSeconds();

    results.totalPositionsSearched += 1;
    results.totalSeconds += seconds;
    results.totalVisits += bot->getRootVisits();
  }

  results.numNNEvals = nnEval->numRowsProcessed();
  results.numNNBatches = nnEval->numBatchesProcessed();
  results.avgBatchSize = nnEval->averageProcessedBatchSize();

  if (printElo)
    cout << "\r" << results.toStringWithElo(baseline, secondsPerGameMove) << std::endl;
  else
    cout << "\r" << results.toString() << std::endl;

  delete bot;

  return results;
}

void PlayUtils::printGenmoveLog(ostream &out, const AsyncBot *bot, const NNEvaluator *nnEval, Loc from, Loc to, double timeTaken, Player perspective)
{
  const Search *search = bot->getSearch();
  Board::printBoard(out, bot->getRootBoard(), from, &(bot->getRootHist().moveHistory));
  Board::printBoard(out, bot->getRootBoard(), to, &(bot->getRootHist().moveHistory));
  out << bot->getRootHist().rules << "\n";
  if (!std::isnan(timeTaken))
    out << "Time taken: " << timeTaken << "\n";
  out << "Root visits: " << search->getRootVisits() << "\n";
  out << "New playouts: " << search->lastSearchNumPlayouts << "\n";
  out << "NN rows: " << nnEval->numRowsProcessed() << endl;
  out << "NN batches: " << nnEval->numBatchesProcessed() << endl;
  out << "NN avg batch size: " << nnEval->averageProcessedBatchSize() << endl;
  if (search->searchParams.playoutDoublingAdvantage != 0)
    out << "PlayoutDoublingAdvantage: " << (search->getRootPla() == getOpp(search->getPlayoutDoublingAdvantagePla()) ? -search->searchParams.playoutDoublingAdvantage : search->searchParams.playoutDoublingAdvantage) << endl;
  out << "PV: ";
  search->printPV(out, search->rootNode, 25);
  out << "\n";
  out << "Tree:\n";
  search->printTree(out, search->rootNode, PrintTreeOptions().maxDepth(1).maxChildrenToShow(10), perspective);
}

Rules PlayUtils::genRandomRules(Rand &rand)
{
  std::vector<int> allowedKoRules = {Rules::KO_SIMPLE, Rules::KO_POSITIONAL, Rules::KO_SITUATIONAL};
  std::vector<int> allowedScoringRules = {Rules::SCORING_AREA, Rules::SCORING_TERRITORY};
  std::vector<int> allowedTaxRules = {Rules::TAX_NONE, Rules::TAX_SEKI, Rules::TAX_ALL};

  Rules rules;
  rules.koRule = allowedKoRules[rand.nextUInt(allowedKoRules.size())];
  rules.scoringRule = allowedScoringRules[rand.nextUInt(allowedScoringRules.size())];
  rules.taxRule = allowedTaxRules[rand.nextUInt(allowedTaxRules.size())];

  return rules;
}

Loc PlayUtils::maybeCleanupBeforePass(
    enabled_t cleanupBeforePass,
    enabled_t friendlyPass,
    const Player pla,
    Loc moveLoc,
    const AsyncBot *bot)
{
  if (friendlyPass == enabled_t::True)
    return moveLoc;
  const BoardHistory &hist = bot->getRootHist();
  const Rules &rules = hist.rules;
  const bool doCleanupBeforePass =
      cleanupBeforePass == enabled_t::True ? true : cleanupBeforePass == enabled_t::False ? false
                                                                                          : (rules.friendlyPassOk == false && rules.scoringRule == Rules::SCORING_AREA);
  return moveLoc;
}

Loc PlayUtils::maybeFriendlyPass(
    enabled_t cleanupBeforePass,
    enabled_t friendlyPass,
    const Player pla,
    Loc moveLoc,
    Search *bot,
    int64_t numVisits,
    Logger &logger)
{
  return moveLoc;
}
