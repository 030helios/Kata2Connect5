#ifndef GAME_BOARDHISTORY_H_
#define GAME_BOARDHISTORY_H_

#include "../core/global.h"
#include "../core/hash.h"
#include "../game/board.h"
#include "../game/rules.h"

//A data structure enabling checking of move legality, including optionally superko,
//and implements scoring and support for various rulesets (see rules.h)
struct BoardHistory {
  Rules rules;
  Player presumedNextMovePla;

  std::map<array<Color, 36>, int> repeat_;
  //Chronological history of moves
  std::vector<Move> moveHistory;

  //The board and player to move as of the very start, before moveHistory.
  Board initialBoard;
  Player initialPla;

  //The "turn number" as of the initial board. Does not affect any rules, but possibly uses may
  //care about this number, for cases where we set up a position from midgame.
  int initialTurnNumber;
  //Is the game supposed to be ended now?
  bool isGameFinished;
  //Winner of the game if the game is supposed to have ended now, C_EMPTY if it is a draw or isNoResult.
  Player winner;
  //Score difference of the game if the game is supposed to have ended now, does NOT take into account whiteKomiAdjustmentForDrawUtility
  //Always an integer or half-integer.
  float finalWhiteMinusBlackScore;
  //True if this game is supposed to be ended with a score
  bool isScored;
  //True if this game is supposed to be ended but there is no result
  bool isNoResult;
  //True if this game is supposed to be ended but it was by resignation rather than an actual end position
  bool isResignation;

  BoardHistory();
  ~BoardHistory();

  BoardHistory(const Board& board, Player pla, const Rules& rules);

  BoardHistory(const BoardHistory& other);
  BoardHistory& operator=(const BoardHistory& other);

  BoardHistory(BoardHistory&& other) noexcept;
  BoardHistory& operator=(BoardHistory&& other) noexcept;

  //Clears all history and status and bonus points, sets encore phase and rules
  void clear(const Board& board, Player pla, const Rules& rules);
  //Set the initial turn number. Affects nothing else.
  void setInitialTurnNumber(int n);
  //Check if a move on the board is legal, taking into account the full game state and superko
  bool isLegal(const Board& board, Loc fromLoc, Loc toLoc, Player movePla) const;

  void makeBoardMoveAssumeLegal(Board& board, Loc fromLoc, Loc toLoc, Player movePla);

  void setWinnerByResignation(Player pla);

  void printBasicInfo(std::ostream& out, const Board& board) const;
  void printDebugInfo(std::ostream& out, const Board& board) const;

private:
  void setFinalScoreAndWinner(float score);
};

#endif  // GAME_BOARDHISTORY_H_
