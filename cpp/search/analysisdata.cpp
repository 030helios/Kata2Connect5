#include "../search/analysisdata.h"

AnalysisData::AnalysisData()
    : fromLoc(Board::NULL_LOC),
      toLoc(Board::NULL_LOC),
      numVisits(0),
      playSelectionValue(0.0),
      lcb(0.0),
      radius(0.0),
      utility(0.0),
      resultUtility(0.0),
      scoreUtility(0.0),
      winLossValue(0.0),
      policyPrior(0.0),
      scoreMean(0.0),
      scoreStdev(0.0),
      lead(0.0),
      ess(0.0),
      weightFactor(0.0),
      order(0),
      pvFrom(),
      pvTo(),
      pvVisits(),
      node(NULL)
{
}

AnalysisData::AnalysisData(const AnalysisData &other)
    : fromLoc(other.fromLoc),
      toLoc(other.toLoc),
      numVisits(other.numVisits),
      playSelectionValue(other.playSelectionValue),
      lcb(other.lcb),
      radius(other.radius),
      utility(other.utility),
      resultUtility(other.resultUtility),
      scoreUtility(other.scoreUtility),
      winLossValue(other.winLossValue),
      policyPrior(other.policyPrior),
      scoreMean(other.scoreMean),
      scoreStdev(other.scoreStdev),
      lead(other.lead),
      ess(other.ess),
      weightFactor(other.weightFactor),
      order(other.order),
      pvFrom(other.pvFrom),
      pvTo(other.pvTo),
      pvVisits(other.pvVisits),
      node(other.node)
{
}

AnalysisData::AnalysisData(AnalysisData &&other) noexcept
    : fromLoc(other.fromLoc),
      toLoc(other.toLoc),
      numVisits(other.numVisits),
      playSelectionValue(other.playSelectionValue),
      lcb(other.lcb),
      radius(other.radius),
      utility(other.utility),
      resultUtility(other.resultUtility),
      scoreUtility(other.scoreUtility),
      winLossValue(other.winLossValue),
      policyPrior(other.policyPrior),
      scoreMean(other.scoreMean),
      scoreStdev(other.scoreStdev),
      lead(other.lead),
      ess(other.ess),
      weightFactor(other.weightFactor),
      order(other.order),
      pvFrom(std::move(other.pvFrom)),
      pvTo(std::move(other.pvTo)),
      pvVisits(std::move(other.pvVisits)),
      node(other.node)
{
}

AnalysisData::~AnalysisData()
{
}

AnalysisData &AnalysisData::operator=(const AnalysisData &other)
{
  if (this == &other)
    return *this;
  fromLoc = other.fromLoc;
  toLoc = other.toLoc;
  numVisits = other.numVisits;
  playSelectionValue = other.playSelectionValue;
  lcb = other.lcb;
  radius = other.radius;
  utility = other.utility;
  resultUtility = other.resultUtility;
  scoreUtility = other.scoreUtility;
  winLossValue = other.winLossValue;
  policyPrior = other.policyPrior;
  scoreMean = other.scoreMean;
  scoreStdev = other.scoreStdev;
  lead = other.lead;
  ess = other.ess;
  weightFactor = other.weightFactor;
  order = other.order;
  pvFrom = other.pvFrom;
  pvTo = other.pvTo;
  pvVisits = other.pvVisits;
  node = other.node;
  return *this;
}

AnalysisData &AnalysisData::operator=(AnalysisData &&other) noexcept
{
  if (this == &other)
    return *this;
  fromLoc = other.fromLoc;
  toLoc = other.toLoc;
  numVisits = other.numVisits;
  playSelectionValue = other.playSelectionValue;
  lcb = other.lcb;
  radius = other.radius;
  utility = other.utility;
  resultUtility = other.resultUtility;
  scoreUtility = other.scoreUtility;
  winLossValue = other.winLossValue;
  policyPrior = other.policyPrior;
  scoreMean = other.scoreMean;
  scoreStdev = other.scoreStdev;
  lead = other.lead;
  ess = other.ess;
  weightFactor = other.weightFactor;
  order = other.order;
  pvFrom = (std::move(other.pvFrom));
  pvTo = (std::move(other.pvTo));
  pvVisits = std::move(other.pvVisits);
  node = other.node;
  return *this;
}

bool operator<(const AnalysisData &a0, const AnalysisData &a1)
{
  if (a0.playSelectionValue > a1.playSelectionValue)
    return true;
  else if (a0.playSelectionValue < a1.playSelectionValue)
    return false;
  if (a0.numVisits > a1.numVisits)
    return true;
  else if (a0.numVisits < a1.numVisits)
    return false;
  // else if(a0.utility > a1.utility)
  //   return true;
  // else if(a0.utility < a1.utility)
  //   return false;
  else
    return a0.policyPrior > a1.policyPrior;
}

bool AnalysisData::pvContainsPass() const
{
  for (int i = 0; i < pvFrom.size(); i++)
    if (pvFrom[i] == Board::PASS_LOC || pvTo[i] == Board::PASS_LOC)
      return true;
  return false;
}

void AnalysisData::writePV(std::ostream &out, const Board &board) const
{
  for (int j = 0; j < pvFrom.size(); j++)
  {
    if (j > 0)
      out << " ";
    out << Location::toString(pvFrom[j], board);
    out << Location::toString(pvTo[j], board);
  }
}

void AnalysisData::writePVVisits(std::ostream &out) const
{
  for (int j = 0; j < pvVisits.size(); j++)
  {
    if (j > 0)
      out << " ";
    out << pvVisits[j];
  }
}

int AnalysisData::getPVLenUpToPhaseEnd(const Board &initialBoard, const BoardHistory &initialHist, Player initialPla) const
{
  Board board(initialBoard);
  BoardHistory hist(initialHist);
  Player nextPla = initialPla;
  int j;
  for (j = 0; j < pvFrom.size(); j++)
  {
    hist.makeBoardMoveAssumeLegal(board, pvFrom[j], pvTo[j], nextPla, NULL);
    nextPla = getOpp(nextPla);
  }
  return j;
}

void AnalysisData::writePVUpToPhaseEnd(std::ostream &out, const Board &initialBoard, const BoardHistory &initialHist, Player initialPla) const
{
  Board board(initialBoard);
  BoardHistory hist(initialHist);
  Player nextPla = initialPla;
  for (int j = 0; j < pvFrom.size(); j++)
  {
    if (j > 0)
      out << " ";
    out << Location::toString(pvFrom[j], board);
    out << " ";
    out << Location::toString(pvTo[j], board);

    hist.makeBoardMoveAssumeLegal(board, pvFrom[j], pvTo[j], nextPla, NULL);
    nextPla = getOpp(nextPla);
  }
}

void AnalysisData::writePVVisitsUpToPhaseEnd(std::ostream &out, const Board &initialBoard, const BoardHistory &initialHist, Player initialPla) const
{
  Board board(initialBoard);
  BoardHistory hist(initialHist);
  Player nextPla = initialPla;
  assert(pvFrom.size() == pvVisits.size());
  for (int j = 0; j < pvFrom.size(); j ++)
  {
    if (j > 0)
      out << " ";
    out << pvVisits[j];

    hist.makeBoardMoveAssumeLegal(board, pvFrom[j], pvTo[j], nextPla, NULL);
    nextPla = getOpp(nextPla);
  }
}
