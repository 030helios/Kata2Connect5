#include "../game/boardhistory.h"

#include <algorithm>

using namespace std;


BoardHistory::BoardHistory()
  :rules(),
   moveHistory(),
   initialBoard(),
   initialPla(P_BLACK),
   presumedNextMovePla(P_BLACK),
   isGameFinished(false),winner(C_EMPTY),finalWhiteMinusBlackScore(0.0f),
   isScored(false),isNoResult(false),isResignation(false)
{
}

BoardHistory::~BoardHistory()
{}

BoardHistory::BoardHistory(const Board& board, Player pla, const Rules& r, int ePhase)
  :rules(r),
   moveHistory(),
   initialBoard(),
   initialPla(),
   presumedNextMovePla(pla),
   isGameFinished(false),winner(C_EMPTY),finalWhiteMinusBlackScore(0.0f),
   isScored(false),isNoResult(false),isResignation(false)
{
  clear(board,pla,rules,ePhase);
}

BoardHistory::BoardHistory(const BoardHistory& other)
  :rules(other.rules),
   moveHistory(other.moveHistory),
   initialBoard(other.initialBoard),
   initialPla(other.initialPla),
   presumedNextMovePla(other.presumedNextMovePla),
   isGameFinished(other.isGameFinished),winner(other.winner),finalWhiteMinusBlackScore(other.finalWhiteMinusBlackScore),
   isScored(other.isScored),isNoResult(other.isNoResult),isResignation(other.isResignation)
{
}


BoardHistory& BoardHistory::operator=(const BoardHistory& other)
{
  if(this == &other)
    return *this;
  rules = other.rules;
  moveHistory = other.moveHistory;
  initialBoard = other.initialBoard;
  initialPla = other.initialPla;
  presumedNextMovePla = other.presumedNextMovePla;
  isGameFinished = other.isGameFinished;
  winner = other.winner;
  finalWhiteMinusBlackScore = other.finalWhiteMinusBlackScore;
  isScored = other.isScored;
  isNoResult = other.isNoResult;
  isResignation = other.isResignation;

  return *this;
}

BoardHistory::BoardHistory(BoardHistory&& other) noexcept
 :rules(other.rules),
  moveHistory(std::move(other.moveHistory)),
  initialBoard(other.initialBoard),
  initialPla(other.initialPla),
  presumedNextMovePla(other.presumedNextMovePla),
  isGameFinished(other.isGameFinished),winner(other.winner),finalWhiteMinusBlackScore(other.finalWhiteMinusBlackScore),
  isScored(other.isScored),isNoResult(other.isNoResult),isResignation(other.isResignation)
{
}

BoardHistory& BoardHistory::operator=(BoardHistory&& other) noexcept
{
  rules = other.rules;
  moveHistory = std::move(other.moveHistory);
  initialBoard = other.initialBoard;
  initialPla = other.initialPla;
  presumedNextMovePla = other.presumedNextMovePla;
  isGameFinished = other.isGameFinished;
  winner = other.winner;
  finalWhiteMinusBlackScore = other.finalWhiteMinusBlackScore;
  isScored = other.isScored;
  isNoResult = other.isNoResult;
  isResignation = other.isResignation;

  return *this;
}

void BoardHistory::clear(const Board& board, Player pla, const Rules& r, int ePhase) {
  rules = r;
  moveHistory.clear();

  initialBoard = board;
  initialPla = pla;

  presumedNextMovePla = pla;

  isGameFinished = false;
  winner = C_EMPTY;
  finalWhiteMinusBlackScore = 0.0f;
  isScored = false;
  isNoResult = false;
  isResignation = false;

}

void BoardHistory::printBasicInfo(ostream& out, const Board& board) const {
  Board::printBoard(out, board, Board::NULL_LOC, &moveHistory);
  out << "Next player: " << PlayerIO::playerToString(presumedNextMovePla) << endl;
  out << "Rules: " << rules.toJsonString() << endl;
  out << "B stones captured: " << board.numBlackCaptures << endl;
  out << "W stones captured: " << board.numWhiteCaptures << endl;
}

void BoardHistory::printDebugInfo(ostream& out, const Board& board) const {
  out << board << endl;
  out << "Initial pla " << PlayerIO::playerToString(initialPla) << endl;
  out << "Rules " << rules << endl;
  out << "Presumed next pla " << PlayerIO::playerToString(presumedNextMovePla) << endl;
  out << "Game result " << isGameFinished << " " << PlayerIO::playerToString(winner) << " "
      << finalWhiteMinusBlackScore << " " << isScored << " " << isNoResult << " " << isResignation << endl;
  out << "Last moves ";
  /*
  for(int i = 0; i<moveHistory.size(); i++)
    out << Location::toString(moveHistory[i].loc,board) << " ";
    */
  out << endl;
}

void BoardHistory::setFinalScoreAndWinner(float score) {
  finalWhiteMinusBlackScore = score;
  if(finalWhiteMinusBlackScore > 0.0f)
    winner = C_WHITE;
  else if(finalWhiteMinusBlackScore < 0.0f)
    winner = C_BLACK;
  else
    winner = C_EMPTY;
}

void BoardHistory::setWinnerByResignation(Player pla) {
  isGameFinished = true;
  isScored = false;
  isNoResult = false;
  isResignation = true;
  winner = pla;
  finalWhiteMinusBlackScore = 0.0f;
}

bool BoardHistory::isLegal(const Board& board, Loc moveLoc, Player movePla) const {
  //Ko-moves in the encore that are recapture blocked are interpreted as pass-for-ko, so they are legal
  if(encorePhase > 0) {
    if(moveLoc >= 0 && moveLoc < Board::MAX_ARR_SIZE && moveLoc != Board::PASS_LOC) {
      if(board.colors[moveLoc] == getOpp(movePla) && koRecapBlocked[moveLoc] && board.getChainSize(moveLoc) == 1 && board.getNumLiberties(moveLoc) == 1)
        return true;
      Loc koCaptureLoc = board.getKoCaptureLoc(moveLoc,movePla);
      if(koCaptureLoc != Board::NULL_LOC && koRecapBlocked[koCaptureLoc] && board.colors[koCaptureLoc] == getOpp(movePla))
        return true;
    }
  }
  else {
    //Only check ko bans during normal play.
    //Ko mechanics in the encore are totally different, we ignore simple ko loc.
    if(board.isKoBanned(moveLoc))
      return false;
  }
  if(!board.isLegalIgnoringKo(moveLoc,movePla,rules.multiStoneSuicideLegal))
    return false;
  if(superKoBanned[moveLoc])
    return false;

  return true;
}

void BoardHistory::makeBoardMoveAssumeLegal(Board& board, Loc fromLoc, Loc toLoc, Player movePla) {
  Hash128 posHashBeforeMove = board.pos_hash;
  Loc moveLoc;
  //If somehow we're making a move after the game was ended, just clear those values and continue
  isGameFinished = false;
  winner = C_EMPTY;
  finalWhiteMinusBlackScore = 0.0f;
  isScored = false;
  isNoResult = false;
  isResignation = false;

  //Update consecutiveEndingPasses and button
  bool isSpightlikeEndingPass = false;
  if(moveLoc != Board::PASS_LOC)
    consecutiveEndingPasses = 0;
  else if(hasButton) {
    assert(encorePhase == 0 && rules.hasButton);
    hasButton = false;
    whiteBonusScore += (movePla == P_WHITE ? 0.5f : -0.5f);
    consecutiveEndingPasses = 0;
    //Taking the button clears all ko hash histories (this is equivalent to not clearing them and treating buttonless
    //state as different than buttonful state)
    hashesBeforeBlackPass.clear();
    hashesBeforeWhitePass.clear();
    koHashHistory.clear();
    //The first turn idx with history will be the one RESULTING from this move.
    firstTurnIdxWithKoHistory = moveHistory.size()+1;
  }
  else {
    //Passes clear ko history in the main phase with spight ko rules and in the encore
    //This lifts bans in spight ko rules and lifts 3-fold-repetition checking in the encore for no-resultifying infinite cycles
    //They also clear in simple ko rules for the purpose of no-resulting long cycles. Long cycles with passes do not no-result.
    if(phaseHasSpightlikeEndingAndPassHistoryClearing()) {
      koHashHistory.clear();
      //The first turn idx with history will be the one RESULTING from this move.
      firstTurnIdxWithKoHistory = moveHistory.size()+1;
      //Does not clear hashesBeforeBlackPass or hashesBeforeWhitePass. Passes lift ko bans, but
      //still repeated positions after pass end the game or phase, which these arrays are used to check.
    }

    Hash128 koHashBeforeThisMove = getKoHash(rules,board,movePla,encorePhase,koRecapBlockHash);
    consecutiveEndingPasses = newConsecutiveEndingPassesAfterPass();
    //Check if we have a game-ending pass BEFORE updating hashesBeforeBlackPass and hashesBeforeWhitePass
    isSpightlikeEndingPass = wouldBeSpightlikeEndingPass(movePla,koHashBeforeThisMove);

    //Update hashesBeforeBlackPass and hashesBeforeWhitePass
    if(movePla == P_BLACK)
      hashesBeforeBlackPass.push_back(koHashBeforeThisMove);
    else if(movePla == P_WHITE)
      hashesBeforeWhitePass.push_back(koHashBeforeThisMove);
    else
      ASSERT_UNREACHABLE;
  }

  //Handle pass-for-ko moves in the encore. Pass for ko lifts a ko recapture block and does nothing else.
  bool wasPassForKo = false;
  if(encorePhase > 0 && moveLoc != Board::PASS_LOC) {
    if(board.colors[moveLoc] == getOpp(movePla) && koRecapBlocked[moveLoc]) {
      setKoRecapBlocked(moveLoc,false);
      wasPassForKo = true;
      //Clear simple ko loc just in case
      //Since we aren't otherwise touching the board, from the board's perspective a player will be moving twice in a row.
      board.clearSimpleKoLoc();
    }
    else {
      Loc koCaptureLoc = board.getKoCaptureLoc(moveLoc,movePla);
      if(koCaptureLoc != Board::NULL_LOC && koRecapBlocked[koCaptureLoc] && board.colors[koCaptureLoc] == getOpp(movePla)) {
        setKoRecapBlocked(koCaptureLoc,false);
        wasPassForKo = true;
        //Clear simple ko loc just in case
        //Since we aren't otherwise touching the board, from the board's perspective a player will be moving twice in a row.
        board.clearSimpleKoLoc();
      }
    }
  }
  //Otherwise handle regular moves
  if(!wasPassForKo) {
    board.playMoveAssumeLegal(moveLoc,movePla);

    if(encorePhase > 0) {
      //Update ko recapture blocks and record that this was a ko capture
      if(board.ko_loc != Board::NULL_LOC) {
        setKoRecapBlocked(moveLoc,true);
        koCapturesInEncore.push_back(EncoreKoCapture(posHashBeforeMove,moveLoc,movePla));
        //Clear simple ko loc now that we've absorbed the ko loc information into the korecap blocks
        //Once we have that, the simple ko loc plays no further role in game state or legality
        board.clearSimpleKoLoc();
      }
      //Unmark all ko recap blocks not on stones
      for(int y = 0; y<board.y_size; y++) {
        for(int x = 0; x<board.x_size; x++) {
          Loc loc = Location::getLoc(x,y,board.x_size);
          if(board.colors[loc] == C_EMPTY && koRecapBlocked[loc])
            setKoRecapBlocked(loc,false);
        }
      }
    }
  }

  //Update recent boards
  currentRecentBoardIdx = (currentRecentBoardIdx + 1) % NUM_RECENT_BOARDS;
  recentBoards[currentRecentBoardIdx] = board;

  Hash128 koHashAfterThisMove = getKoHash(rules,board,getOpp(movePla),encorePhase,koRecapBlockHash);
  koHashHistory.push_back(koHashAfterThisMove);
  moveHistory.push_back(Move(moveLoc,movePla));
  numTurnsThisPhase += 1;
  presumedNextMovePla = getOpp(movePla);

  if(moveLoc != Board::PASS_LOC)
    wasEverOccupiedOrPlayed[moveLoc] = true;

  //Mark all locations that are superko-illegal for the next player, by iterating and testing each point.
  Player nextPla = getOpp(movePla);
  if(encorePhase <= 0 && rules.koRule != Rules::KO_SIMPLE) {
    assert(koRecapBlockHash == Hash128());
    for(int y = 0; y<board.y_size; y++) {
      for(int x = 0; x<board.x_size; x++) {
        Loc loc = Location::getLoc(x,y,board.x_size);
        //Cannot be superko banned if it's not a pseudolegal move in the first place, or we would already ban the move under simple ko.
        if(board.colors[loc] != C_EMPTY || board.isIllegalSuicide(loc,nextPla,rules.multiStoneSuicideLegal) || loc == board.ko_loc)
          superKoBanned[loc] = false;
        //Also cannot be superko banned if a stone was never there or played there before AND the move is not suicide, because that means
        //the move results in a new stone there and if no stone was ever there in the past the it must be a new position.
        else if(!wasEverOccupiedOrPlayed[loc] && !board.isSuicide(loc,nextPla))
          superKoBanned[loc] = false;
        else {
          Hash128 posHashAfterMove = board.getPosHashAfterMove(loc,nextPla);
          Hash128 koHashAfterMove = getKoHashAfterMoveNonEncore(rules, posHashAfterMove, getOpp(nextPla));
          superKoBanned[loc] = koHashOccursInHistory(koHashAfterMove,rootKoHashTable);
        }
      }
    }
  }
  else if(encorePhase > 0) {
    //During the encore, only one capture of each ko in a given position by a given player
    std::fill(superKoBanned, superKoBanned+Board::MAX_ARR_SIZE, false);
    for(size_t i = 0; i<koCapturesInEncore.size(); i++) {
      const EncoreKoCapture& ekc = koCapturesInEncore[i];
      if(ekc.posHashBeforeMove == board.pos_hash && ekc.movePla == nextPla)
        superKoBanned[ekc.moveLoc] = true;
    }
  }

  //Territory scoring - chill 1 point per move in main phase and first encore
  if(rules.scoringRule == Rules::SCORING_TERRITORY && encorePhase <= 1 && moveLoc != Board::PASS_LOC && !wasPassForKo) {
    if(movePla == P_BLACK)
      whiteBonusScore += 1.0f;
    else if(movePla == P_WHITE)
      whiteBonusScore -= 1.0f;
    else
      ASSERT_UNREACHABLE;
  }

  //Handicap bonus score
  if(movePla == P_WHITE && moveLoc != Board::PASS_LOC)
    whiteHasMoved = true;
  if(assumeMultipleStartingBlackMovesAreHandicap && !whiteHasMoved && movePla == P_BLACK && rules.whiteHandicapBonusRule != Rules::WHB_ZERO) {
    whiteHandicapBonusScore = computeWhiteHandicapBonus();
  }

  //Phase transitions and game end
  if(consecutiveEndingPasses >= 2 || isSpightlikeEndingPass) {
    if(rules.scoringRule == Rules::SCORING_AREA) {
      assert(encorePhase <= 0);
      endAndScoreGameNow(board);
    }
    else if(rules.scoringRule == Rules::SCORING_TERRITORY) {
      if(encorePhase >= 2)
        endAndScoreGameNow(board);
      else {
        if(preventEncore) {
          isPastNormalPhaseEnd = true;
        }
        else {
          encorePhase += 1;
          numTurnsThisPhase = 0;
          if(encorePhase == 2)
            std::copy(board.colors, board.colors+Board::MAX_ARR_SIZE, secondEncoreStartColors);

          std::fill(superKoBanned, superKoBanned+Board::MAX_ARR_SIZE, false);
          consecutiveEndingPasses = 0;
          hashesBeforeBlackPass.clear();
          hashesBeforeWhitePass.clear();
          std::fill(koRecapBlocked, koRecapBlocked+Board::MAX_ARR_SIZE, false);
          koRecapBlockHash = Hash128();
          koCapturesInEncore.clear();

          koHashHistory.clear();
          koHashHistory.push_back(getKoHash(rules,board,getOpp(movePla),encorePhase,koRecapBlockHash));
          //The first ko hash history is the one for the move we JUST appended to the move history earlier.
          firstTurnIdxWithKoHistory = moveHistory.size();
        }
      }
    }
    else
      ASSERT_UNREACHABLE;
  }

  //Break long cycles with no-result
  if(moveLoc != Board::PASS_LOC && (encorePhase > 0 || rules.koRule == Rules::KO_SIMPLE)) {
    if(numberOfKoHashOccurrencesInHistory(koHashHistory[koHashHistory.size()-1], rootKoHashTable) >= 3) {
      isNoResult = true;
      isGameFinished = true;
    }
  }

}

KoHashTable::KoHashTable()
  :koHashHistorySortedByLowBits(),
   firstTurnIdxWithKoHistory(0)
{
  idxTable = new uint32_t[TABLE_SIZE];
}
KoHashTable::~KoHashTable() {
  delete[] idxTable;
}

size_t KoHashTable::size() const {
  return koHashHistorySortedByLowBits.size();
}

void KoHashTable::recompute(const BoardHistory& history) {
  koHashHistorySortedByLowBits = history.koHashHistory;
  firstTurnIdxWithKoHistory = history.firstTurnIdxWithKoHistory;

  auto cmpFirstByLowBits = [](const Hash128& a, const Hash128& b) {
    if((a.hash0 & TABLE_MASK) < (b.hash0 & TABLE_MASK))
      return true;
    if((a.hash0 & TABLE_MASK) > (b.hash0 & TABLE_MASK))
      return false;
    return a < b;
  };

  std::sort(koHashHistorySortedByLowBits.begin(),koHashHistorySortedByLowBits.end(),cmpFirstByLowBits);

  //Just in case, since we're using 32 bits for indices.
  if(koHashHistorySortedByLowBits.size() > 1000000000)
    throw StringError("Board history length longer than 1000000000, not supported");
  uint32_t size = (uint32_t)koHashHistorySortedByLowBits.size();

  uint32_t idx = 0;
  for(uint32_t bits = 0; bits<TABLE_SIZE; bits++) {
    while(idx < size && ((koHashHistorySortedByLowBits[idx].hash0 & TABLE_MASK) < bits))
      idx++;
    idxTable[bits] = idx;
  }
}

bool KoHashTable::containsHash(Hash128 hash) const {
  uint32_t bits = hash.hash0 & TABLE_MASK;
  size_t idx = idxTable[bits];
  size_t size = koHashHistorySortedByLowBits.size();
  while(idx < size && ((koHashHistorySortedByLowBits[idx].hash0 & TABLE_MASK) == bits)) {
    if(hash == koHashHistorySortedByLowBits[idx])
      return true;
    idx++;
  }
  return false;
}

int KoHashTable::numberOfOccurrencesOfHash(Hash128 hash) const {
  uint32_t bits = hash.hash0 & TABLE_MASK;
  size_t idx = idxTable[bits];
  size_t size = koHashHistorySortedByLowBits.size();
  int count = 0;
  while(idx < size && ((koHashHistorySortedByLowBits[idx].hash0 & TABLE_MASK) == bits)) {
    if(hash == koHashHistorySortedByLowBits[idx])
      count++;
    idx++;
  }
  return count;
}
