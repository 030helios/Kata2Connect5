#include "../game/board.h"

/*
 * board.cpp
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modificationss).
 */

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "../core/rand.h"

using namespace std;

//LOCATION--------------------------------------------------------------------------------
Loc Location::getLoc(int x, int y, int x_size)
{
  return (x + 1) + (y + 1) * (x_size + 1);
}
int Location::getX(Loc loc, int x_size)
{
  return (loc % (x_size + 1)) - 1;
}
int Location::getY(Loc loc, int x_size)
{
  return (loc / (x_size + 1)) - 1;
}
void Location::getAdjacentOffsets(short adj_offsets[8], int x_size)
{
  adj_offsets[0] = -(x_size + 1);
  adj_offsets[1] = -1;
  adj_offsets[2] = 1;
  adj_offsets[3] = (x_size + 1);
  adj_offsets[4] = -(x_size + 1) - 1;
  adj_offsets[5] = -(x_size + 1) + 1;
  adj_offsets[6] = (x_size + 1) - 1;
  adj_offsets[7] = (x_size + 1) + 1;
}

bool Location::isAdjacent(Loc loc0, Loc loc1, int x_size)
{
  return loc0 == loc1 - (x_size + 1) || loc0 == loc1 - 1 || loc0 == loc1 + 1 || loc0 == loc1 + (x_size + 1);
}

Loc Location::getMirrorLoc(Loc loc, int x_size, int y_size)
{
  if (loc == Board::NULL_LOC || loc == Board::PASS_LOC)
    return loc;
  return getLoc(x_size - 1 - getX(loc, x_size), y_size - 1 - getY(loc, x_size), x_size);
}

Loc Location::getCenterLoc(int x_size, int y_size)
{
  if (x_size % 2 == 0 || y_size % 2 == 0)
    return Board::NULL_LOC;
  return getLoc(x_size / 2, y_size / 2, x_size);
}

bool Location::isCentral(Loc loc, int x_size, int y_size)
{
  int x = getX(loc, x_size);
  int y = getY(loc, x_size);
  return x >= (x_size - 1) / 2 && x <= x_size / 2 && y >= (y_size - 1) / 2 && y <= y_size / 2;
}

#define FOREACHADJ(BLOCK)          \
  {                                \
    int ADJOFFSET = -(x_size + 1); \
    {BLOCK};                       \
    ADJOFFSET = -1;                \
    {BLOCK};                       \
    ADJOFFSET = 1;                 \
    {BLOCK};                       \
    ADJOFFSET = x_size + 1;        \
    {                              \
      BLOCK                        \
    }                              \
  };
#define ADJ0 (-(x_size + 1))
#define ADJ1 (-1)
#define ADJ2 (1)
#define ADJ3 (x_size + 1)

//CONSTRUCTORS AND INITIALIZATION----------------------------------------------------------

Board::Board()
{
  init(19, 19);
}

Board::Board(int x, int y)
{
  init(x, y);
}

Board::Board(const Board &other)
{
  x_size = other.x_size;
  y_size = other.y_size;

  memcpy(colors, other.colors, sizeof(Color) * MAX_ARR_SIZE);
  memcpy(chain_data, other.chain_data, sizeof(ChainData) * MAX_ARR_SIZE);
  memcpy(chain_head, other.chain_head, sizeof(Loc) * MAX_ARR_SIZE);
  memcpy(next_in_chain, other.next_in_chain, sizeof(Loc) * MAX_ARR_SIZE);

  ko_loc = other.ko_loc;
  // empty_list = other.empty_list;
  pos_hash = other.pos_hash;
  numBlackCaptures = other.numBlackCaptures;
  numWhiteCaptures = other.numWhiteCaptures;

  memcpy(adj_offsets, other.adj_offsets, sizeof(short) * 8);
}

void Board::init(int xS, int yS)
{
  if (xS < 0 || yS < 0 || xS > MAX_LEN || yS > MAX_LEN)
    throw StringError("Board::init - invalid board size");

  x_size = xS;
  y_size = yS;

  for (int i = 0; i < MAX_ARR_SIZE; i++)
    colors[i] = C_WALL;

  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = (x + 1) + (y + 1) * (x_size + 1);
      colors[loc] = C_EMPTY;
      // empty_list.add(loc);
    }
  }

  ko_loc = NULL_LOC;
  numBlackCaptures = 0;
  numWhiteCaptures = 0;

  Location::getAdjacentOffsets(adj_offsets, x_size);
}

void Board::clearSimpleKoLoc()
{
  ko_loc = NULL_LOC;
}
void Board::setSimpleKoLoc(Loc loc)
{
  ko_loc = loc;
}

//Gets the number of stones of the chain at loc. Precondition: location must be black or white.
int Board::getChainSize(Loc loc) const
{
  return chain_data[chain_head[loc]].num_locs;
}

//Gets the number of liberties of the chain at loc. Assertion: location must be black or white.
int Board::getNumLiberties(Loc loc) const
{
  return chain_data[chain_head[loc]].num_liberties;
}

//Check if moving here would be a self-capture
bool Board::isSuicide(Loc loc, Player pla) const
{
  if (loc == PASS_LOC)
    return false;

  Player opp = getOpp(pla);
  FOREACHADJ(
      Loc adj = loc + ADJOFFSET;

      if (colors[adj] == C_EMPTY) return false;
      else if (colors[adj] == pla) {
        if (getNumLiberties(adj) > 1)
          return false;
      } else if (colors[adj] == opp) {
        if (getNumLiberties(adj) == 1)
          return false;
      });

  return true;
}

//Check if moving here is would be an illegal self-capture
bool Board::isIllegalSuicide(Loc loc, Player pla, bool isMultiStoneSuicideLegal) const
{
  Player opp = getOpp(pla);
  FOREACHADJ(
      Loc adj = loc + ADJOFFSET;

      if (colors[adj] == C_EMPTY) return false;
      else if (colors[adj] == pla) {
        if (isMultiStoneSuicideLegal || getNumLiberties(adj) > 1)
          return false;
      } else if (colors[adj] == opp) {
        if (getNumLiberties(adj) == 1)
          return false;
      });

  return true;
}

//Returns a fast lower bound on the number of liberties a new stone placed here would have
void Board::getBoundNumLibertiesAfterPlay(Loc loc, Player pla, int &lowerBound, int &upperBound) const
{
  Player opp = getOpp(pla);

  int numImmediateLibs = 0;      //empty spaces adjacent
  int numCaps = 0;               //number of adjacent directions in which we will capture
  int potentialLibsFromCaps = 0; //Total number of stones we're capturing (possibly with multiplicity)
  int numConnectionLibs = 0;     //Sum over friendly groups connected to of their libs-1
  int maxConnectionLibs = 0;     //Max over friendly groups connected to of their libs-1

  for (int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == C_EMPTY)
    {
      numImmediateLibs++;
    }
    else if (colors[adj] == opp)
    {
      int libs = chain_data[chain_head[adj]].num_liberties;
      if (libs == 1)
      {
        numCaps++;
        potentialLibsFromCaps += chain_data[chain_head[adj]].num_locs;
      }
    }
    else if (colors[adj] == pla)
    {
      int libs = chain_data[chain_head[adj]].num_liberties;
      int connLibs = libs - 1;
      numConnectionLibs += connLibs;
      if (connLibs > maxConnectionLibs)
        maxConnectionLibs = connLibs;
    }
  }

  lowerBound = numCaps + (maxConnectionLibs > numImmediateLibs ? maxConnectionLibs : numImmediateLibs);
  upperBound = numImmediateLibs + potentialLibsFromCaps + numConnectionLibs;
}

//Returns the number of liberties a new stone placed here would have, or max if it would be >= max.
int Board::getNumLibertiesAfterPlay(Loc loc, Player pla, int max) const
{
  Player opp = getOpp(pla);

  int numLibs = 0;
  Loc libs[MAX_PLAY_SIZE];
  int numCapturedGroups = 0;
  Loc capturedGroupHeads[4];

  //First, count immediate liberties and groups that would be captured
  for (int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == C_EMPTY)
    {
      libs[numLibs++] = adj;
      if (numLibs >= max)
        return max;
    }
    else if (colors[adj] == opp && getNumLiberties(adj) == 1)
    {
      libs[numLibs++] = adj;
      if (numLibs >= max)
        return max;

      Loc head = chain_head[adj];
      bool alreadyFound = false;
      for (int j = 0; j < numCapturedGroups; j++)
      {
        if (capturedGroupHeads[j] == head)
        {
          alreadyFound = true;
          break;
        }
      }
      if (!alreadyFound)
        capturedGroupHeads[numCapturedGroups++] = head;
    }
  }

  auto wouldBeEmpty = [numCapturedGroups, &capturedGroupHeads, this, opp](Loc lc) {
    if (this->colors[lc] == C_EMPTY)
      return true;
    if (this->colors[lc] == opp)
    {
      for (int i = 0; i < numCapturedGroups; i++)
        if (capturedGroupHeads[i] == this->chain_head[lc])
          return true;
    }
    return false;
  };

  //Next, walk through all stones of all surrounding groups we would connect with and count liberties, avoiding overlap.
  int numConnectingGroups = 0;
  Loc connectingGroupHeads[4];
  for (int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == pla)
    {
      Loc head = chain_head[adj];
      bool alreadyFound = false;
      for (int j = 0; j < numConnectingGroups; j++)
      {
        if (connectingGroupHeads[j] == head)
        {
          alreadyFound = true;
          break;
        }
      }
      if (!alreadyFound)
      {
        connectingGroupHeads[numConnectingGroups++] = head;

        Loc cur = adj;
        do
        {
          for (int k = 0; k < 4; k++)
          {
            Loc possibleLib = cur + adj_offsets[k];
            if (possibleLib != loc && wouldBeEmpty(possibleLib))
            {
              bool alreadyCounted = false;
              for (int l = 0; l < numLibs; l++)
              {
                if (libs[l] == possibleLib)
                {
                  alreadyCounted = true;
                  break;
                }
              }
              if (!alreadyCounted)
              {
                libs[numLibs++] = possibleLib;
                if (numLibs >= max)
                  return max;
              }
            }
          }

          cur = next_in_chain[cur];
        } while (cur != adj);
      }
    }
  }
  return numLibs;
}

//Check if moving here is illegal due to simple ko
bool Board::isKoBanned(Loc loc) const
{
  return loc == ko_loc;
}

bool Board::isOnBoard(Loc loc) const
{
  return loc >= 0 && loc < MAX_ARR_SIZE && colors[loc] != C_WALL;
}

bool Board::get_is_legal_capture(Player pla, Loc fromLoc, Loc toLoc, const std::array<int, 56> &circle) const
{
  int our_index = -1;
  bool CanCapture = false;

  bool passed_the_loop = false;

  for (int src_index = 0; src_index < 56; src_index++)
  {
    if (circle[src_index] == -1)
      passed_the_loop = true;

    if (!passed_the_loop)
      continue;

    if (circle[src_index] == fromLoc)
    {
      if (our_index != -1)
        return false;
      else
        our_index = src_index;
    }
    if (circle[src_index] == toLoc && our_index != -1 && passed_the_loop)
      return true;
  }
}
bool Board::getIsLegalCapture(Player pla, Loc fromLoc, Loc toLoc) const
{
  if (get_is_legal_capture(pla, fromLoc, toLoc, kOuter))
    return true;
  if (get_is_legal_capture(pla, fromLoc, toLoc, kOuterReverse))
    return true;
  if (get_is_legal_capture(pla, fromLoc, toLoc, kInter))
    return true;
  if (get_is_legal_capture(pla, fromLoc, toLoc, kInterReverse))
    return true;
  return false;
}
//Check if moving here is illegal.
bool Board::isLegal(Loc fromLoc, Loc toLoc, Player pla, bool isMultiStoneSuicideLegal) const
{
  if (colors[toLoc] == C_EMPTY && Location::isAdjacent(toLoc, fromLoc, 6))
  {
    return true;
  }
  return getIsLegalCapture(pla, fromLoc, toLoc);
}

//Check if moving here is illegal, ignoring simple ko
bool Board::isLegalIgnoringKo(Loc fromLoc, Loc toLoc, Player pla, bool isMultiStoneSuicideLegal) const
{
  if (pla != P_BLACK && pla != P_WHITE)
    return false;
  return loc == PASS_LOC || (loc >= 0 &&
                             loc < MAX_ARR_SIZE &&
                             (colors[loc] == C_EMPTY) &&
                             !isIllegalSuicide(loc, pla, isMultiStoneSuicideLegal));
}

bool Board::isAdjacentToPla(Loc loc, Player pla) const
{
  FOREACHADJ(
      Loc adj = loc + ADJOFFSET;
      if (colors[adj] == pla) return true;);
  return false;
}

bool Board::isAdjacentOrDiagonalToPla(Loc loc, Player pla) const
{
  for (int i = 0; i < 8; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == pla)
      return true;
  }
  return false;
}

bool Board::isAdjacentToChain(Loc loc, Loc chain) const
{
  if (colors[chain] == C_EMPTY)
    return false;
  FOREACHADJ(
      Loc adj = loc + ADJOFFSET;
      if (colors[adj] == colors[chain] && chain_head[adj] == chain_head[chain]) return true;);
  return false;
}

//Does this connect two pla distinct groups that are not both pass-alive and not within opponent pass-alive area either?
bool Board::isNonPassAliveSelfConnection(Loc loc, Player pla, Color *passAliveArea) const
{
  if (colors[loc] != C_EMPTY || passAliveArea[loc] == pla)
    return false;

  Loc nonPassAliveAdjHead = NULL_LOC;
  for (int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == pla && passAliveArea[adj] == C_EMPTY)
    {
      nonPassAliveAdjHead = chain_head[adj];
      break;
    }
  }

  if (nonPassAliveAdjHead == NULL_LOC)
    return false;

  for (int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == pla && chain_head[adj] != nonPassAliveAdjHead)
      return true;
  }

  return false;
}

bool Board::isEmpty() const
{
  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (colors[loc] != C_EMPTY)
        return false;
    }
  }
  return true;
}

int Board::numStonesOnBoard() const
{
  int num = 0;
  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (colors[loc] == C_BLACK || colors[loc] == C_WHITE)
        num += 1;
    }
  }
  return num;
}

int Board::numPlaStonesOnBoard(Player pla) const
{
  int num = 0;
  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (colors[loc] == pla)
        num += 1;
    }
  }
  return num;
}

bool Board::setStone(Loc fromLoc, Loc toLoc, Color color)
{
  if (loc < 0 || loc >= MAX_ARR_SIZE || colors[loc] == C_WALL)
    return false;
  if (color != C_BLACK && color != C_WHITE && color != C_EMPTY)
    return false;

  if (colors[loc] == color)
  {
  }
  else if (colors[loc] == C_EMPTY)
    playMoveAssumeLegal(loc, color);
  else if (color == C_EMPTY)
    removeSingleStone(loc);
  else
  {
    removeSingleStone(loc);
    if (!isSuicide(loc, color))
      playMoveAssumeLegal(loc, color);
  }

  ko_loc = NULL_LOC;
  return true;
}

//Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
bool Board::playMove(Loc fromLoc, Loc toLoc, Player pla, bool isMultiStoneSuicideLegal)
{
  if (isLegal(loc, pla, isMultiStoneSuicideLegal))
  {
    playMoveAssumeLegal(loc, pla);
    return true;
  }
  return false;
}

//Plays the specified move, assuming it is legal, and returns a MoveRecord for the move
Board::MoveRecord Board::playMoveRecorded(Loc fromLoc, Loc toLoc, Player pla)
{
  MoveRecord record;
  record.loc = loc;
  record.pla = pla;
  record.ko_loc = ko_loc;
  record.capDirs = 0;

  if (loc != PASS_LOC)
  {
    Player opp = getOpp(pla);

    {
      int adj = loc + ADJ0;
      if (colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 0);
    }
    {
      int adj = loc + ADJ1;
      if (colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 1);
    }
    {
      int adj = loc + ADJ2;
      if (colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 2);
    }
    {
      int adj = loc + ADJ3;
      if (colors[adj] == opp && getNumLiberties(adj) == 1)
        record.capDirs |= (((uint8_t)1) << 3);
    }

    if (record.capDirs == 0 && isSuicide(loc, pla))
      record.capDirs = 0x10;
  }

  playMoveAssumeLegal(loc, pla);
  return record;
}

//Undo the move given by record. Moves MUST be undone in the order they were made.
//Undos will NOT typically restore the precise representation in the board to the way it was. The heads of chains
//might change, the order of the circular lists might change, etc.
void Board::undo(Board::MoveRecord record)
{
  ko_loc = record.ko_loc;

  Loc loc = record.loc;
  if (loc == PASS_LOC)
    return;

  //Re-fill stones in all captured directions
  for (int i = 0; i < 4; i++)
  {
    int adj = loc + adj_offsets[i];
    if (record.capDirs & (1 << i))
    {
      if (colors[adj] == C_EMPTY)
      {
        addChain(adj, getOpp(record.pla));

        int numUncaptured = chain_data[chain_head[adj]].num_locs;
        if (record.pla == P_BLACK)
          numWhiteCaptures -= numUncaptured;
        else
          numBlackCaptures -= numUncaptured;
      }
    }
  }
  //Re-fill suicided stones
  if (record.capDirs == 0x10)
  {
    assert(colors[loc] == C_EMPTY);
    addChain(loc, record.pla);
    int numUncaptured = chain_data[chain_head[loc]].num_locs;
    if (record.pla == P_BLACK)
      numBlackCaptures -= numUncaptured;
    else
      numWhiteCaptures -= numUncaptured;
  }

  //Delete the stone played here.
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
  colors[loc] = C_EMPTY;
  // empty_list.add(loc);

  //Uneat opp liberties
  changeSurroundingLiberties(loc, getOpp(record.pla), +1);

  //If this was not a single stone, we may need to recompute the chain from scratch
  if (chain_data[chain_head[loc]].num_locs > 1)
  {
    int numNeighbors = 0;
    FOREACHADJ(
        int adj = loc + ADJOFFSET;
        if (colors[adj] == record.pla)
            numNeighbors++;);

    //If the move had exactly one neighbor, we know its undoing didn't disconnect the group,
    //so don't need to rebuild the whole chain.
    if (numNeighbors <= 1)
    {
      //If the undone move was the location of the head, we need to move the head.
      Loc head = chain_head[loc];
      if (head == loc)
      {
        Loc newHead = next_in_chain[loc];
        //Run through the whole chain and make their heads point to the new head
        Loc cur = loc;
        do
        {
          chain_head[cur] = newHead;
          cur = next_in_chain[cur];
        } while (cur != loc);

        //Move over the head data
        chain_data[newHead] = chain_data[head];
        head = newHead;
      }

      //Extract this move out of the circlar list of stones. Unfortunately we don't have a prev pointer, so we need to walk the loop.
      {
        //Starting at the head is likely to need to walk less since whenever we merge a single stone into an existing group
        //we put it right after the old head.
        Loc cur = head;
        while (next_in_chain[cur] != loc)
          cur = next_in_chain[cur];
        //Advance the pointer to put loc out of the loop
        next_in_chain[cur] = next_in_chain[loc];
      }

      //Lastly, fix up liberties. Removing this stone removed all liberties next to this stone
      //that weren't already liberties of the group.
      int libertyDelta = 0;
      FOREACHADJ(
          int adj = loc + ADJOFFSET;
          if (colors[adj] == C_EMPTY && !isLibertyOf(adj, head)) libertyDelta--;);
      //Removing this stone itself added a liberty to the group though.
      libertyDelta++;
      chain_data[head].num_liberties += libertyDelta;
      //And update the count of stones in the group
      chain_data[head].num_locs--;
    }
    //More than one neighbor. Removing this stone potentially disconnects the group into two, so we just do a complete rebuild
    //of the resulting group(s).
    else
    {
      //Run through the whole chain and make their heads point to nothing
      Loc cur = loc;
      do
      {
        chain_head[cur] = NULL_LOC;
        cur = next_in_chain[cur];
      } while (cur != loc);

      //Rebuild each chain adjacent now
      for (int i = 0; i < 4; i++)
      {
        int adj = loc + adj_offsets[i];
        if (colors[adj] == record.pla && chain_head[adj] == NULL_LOC)
          rebuildChain(adj, record.pla);
      }
    }
  }
}

//Plays the specified move, assuming it is legal.
void Board::playMoveAssumeLegal(Loc fromLoc, Loc toLoc, Player pla)
{
  Player opp = getOpp(pla);

  swap(colors[fromLoc], colors[toLoc]);
  colors[fromLoc] = C_EMPTY;
  repeat_[colors]++;
  //maybe add history
}

int Board::countHeuristicConnectionLibertiesX2(Loc loc, Player pla) const
{
  int num_libsX2 = 0;
  for (int i = 0; i < 4; i++)
  {
    Loc adj = loc + adj_offsets[i];
    if (colors[adj] == pla)
    {
      int libs = chain_data[chain_head[adj]].num_liberties;
      if (libs > 1)
        num_libsX2 += libs * 2 - 3;
    }
  }
  return num_libsX2;
}

//Loc is a liberty of head's chain if loc is empty and adjacent to a stone of head.
//Assumes loc is empty
bool Board::isLibertyOf(Loc loc, Loc head) const
{
  Loc adj;
  adj = loc + ADJ0;
  if (colors[adj] == colors[head] && chain_head[adj] == head)
    return true;
  adj = loc + ADJ1;
  if (colors[adj] == colors[head] && chain_head[adj] == head)
    return true;
  adj = loc + ADJ2;
  if (colors[adj] == colors[head] && chain_head[adj] == head)
    return true;
  adj = loc + ADJ3;
  if (colors[adj] == colors[head] && chain_head[adj] == head)
    return true;

  return false;
}

//Apply the specified delta to the liberties of all adjacent groups of the specified color
void Board::changeSurroundingLiberties(Loc loc, Player pla, int delta)
{
  Loc adj0 = loc + ADJ0;
  Loc adj1 = loc + ADJ1;
  Loc adj2 = loc + ADJ2;
  Loc adj3 = loc + ADJ3;

  if (colors[adj0] == pla)
    chain_data[chain_head[adj0]].num_liberties += delta;
  if (colors[adj1] == pla && !(colors[adj0] == pla && chain_head[adj0] == chain_head[adj1]))
    chain_data[chain_head[adj1]].num_liberties += delta;
  if (colors[adj2] == pla && !(colors[adj0] == pla && chain_head[adj0] == chain_head[adj2]) && !(colors[adj1] == pla && chain_head[adj1] == chain_head[adj2]))
    chain_data[chain_head[adj2]].num_liberties += delta;
  if (colors[adj3] == pla && !(colors[adj0] == pla && chain_head[adj0] == chain_head[adj3]) && !(colors[adj1] == pla && chain_head[adj1] == chain_head[adj3]) && !(colors[adj2] == pla && chain_head[adj2] == chain_head[adj3]))
    chain_data[chain_head[adj3]].num_liberties += delta;
}

int Location::distance(Loc loc0, Loc loc1, int x_size)
{
  int dx = getX(loc1, x_size) - getX(loc0, x_size);
  int dy = (loc1 - loc0 - dx) / (x_size + 1);
  return (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
}

int Location::euclideanDistanceSquared(Loc loc0, Loc loc1, int x_size)
{
  int dx = getX(loc1, x_size) - getX(loc0, x_size);
  int dy = (loc1 - loc0 - dx) / (x_size + 1);
  return dx * dx + dy * dy;
}

//TACTICAL STUFF--------------------------------------------------------------------

//Helper, find liberties of group at loc. Fills in buf, returns the number of liberties.
//bufStart is where to start checking to avoid duplicates. bufIdx is where to start actually writing.
int Board::findLiberties(Loc loc, vector<Loc> &buf, int bufStart, int bufIdx) const
{
  int numFound = 0;
  Loc cur = loc;
  do
  {
    for (int i = 0; i < 4; i++)
    {
      Loc lib = cur + adj_offsets[i];
      if (colors[lib] == C_EMPTY)
      {
        //Check for dups
        bool foundDup = false;
        for (int j = bufStart; j < bufIdx + numFound; j++)
        {
          if (buf[j] == lib)
          {
            foundDup = true;
            break;
          }
        }
        if (!foundDup)
        {
          if (bufIdx + numFound >= buf.size())
            buf.resize(buf.size() * 3 / 2 + 64);
          buf[bufIdx + numFound] = lib;
          numFound++;
        }
      }
    }

    cur = next_in_chain[cur];
  } while (cur != loc);

  return numFound;
}

//Helper, find captures that gain liberties for the group at loc. Fills in result, returns the number of captures.
//bufStart is where to start checking to avoid duplicates. bufIdx is where to start actually writing.
int Board::findLibertyGainingCaptures(Loc loc, vector<Loc> &buf, int bufStart, int bufIdx) const
{
  Player opp = getOpp(colors[loc]);

  //For performance, avoid checking for captures on any chain twice
  //int arrSize = x_size*y_size;
  Loc chainHeadsChecked[MAX_PLAY_SIZE];
  int numChainHeadsChecked = 0;

  int numFound = 0;
  Loc cur = loc;
  do
  {
    for (int i = 0; i < 4; i++)
    {
      Loc adj = cur + adj_offsets[i];
      if (colors[adj] == opp)
      {
        Loc head = chain_head[adj];
        if (chain_data[head].num_liberties == 1)
        {
          bool alreadyChecked = false;
          for (int j = 0; j < numChainHeadsChecked; j++)
          {
            if (chainHeadsChecked[j] == head)
            {
              alreadyChecked = true;
              break;
            }
          }
          if (!alreadyChecked)
          {
            //Capturing moves are precisely the liberties of the groups around us with 1 liberty.
            numFound += findLiberties(adj, buf, bufStart, bufIdx + numFound);
            chainHeadsChecked[numChainHeadsChecked++] = head;
          }
        }
      }
    }

    cur = next_in_chain[cur];
  } while (cur != loc);

  return numFound;
}

//Helper, does the group at loc have at least one opponent group adjacent to it in atari?
bool Board::hasLibertyGainingCaptures(Loc loc) const
{
  Player opp = getOpp(colors[loc]);
  Loc cur = loc;
  do
  {
    FOREACHADJ(
        Loc adj = cur + ADJOFFSET;
        if (colors[adj] == opp) {
          Loc head = chain_head[adj];
          if (chain_data[head].num_liberties == 1)
            return true;
        });
    cur = next_in_chain[cur];
  } while (cur != loc);

  return false;
}

void Board::calculateArea(
    Color *result,
    bool nonPassAliveStones,
    bool safeBigTerritories,
    bool unsafeBigTerritories,
    bool isMultiStoneSuicideLegal) const
{
  std::fill(result, result + MAX_ARR_SIZE, C_EMPTY);
  calculateAreaForPla(P_BLACK, safeBigTerritories, unsafeBigTerritories, isMultiStoneSuicideLegal, result);
  calculateAreaForPla(P_WHITE, safeBigTerritories, unsafeBigTerritories, isMultiStoneSuicideLegal, result);

  //TODO can we merge this in to calculate area for pla?
  if (nonPassAliveStones)
  {
    for (int y = 0; y < y_size; y++)
    {
      for (int x = 0; x < x_size; x++)
      {
        Loc loc = Location::getLoc(x, y, x_size);
        if (result[loc] == C_EMPTY)
          result[loc] = colors[loc];
      }
    }
  }
}

void Board::calculateIndependentLifeArea(
    Color *result,
    int &whiteMinusBlackIndependentLifeRegionCount,
    bool keepTerritories,
    bool keepStones,
    bool isMultiStoneSuicideLegal) const
{
  //First, just compute basic area.
  Color basicArea[MAX_ARR_SIZE];
  std::fill(result, result + MAX_ARR_SIZE, C_EMPTY);
  std::fill(basicArea, basicArea + MAX_ARR_SIZE, C_EMPTY);
  calculateAreaForPla(P_BLACK, true, true, isMultiStoneSuicideLegal, basicArea);
  calculateAreaForPla(P_WHITE, true, true, isMultiStoneSuicideLegal, basicArea);

  //TODO can we merge this in to calculate area for pla?
  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (basicArea[loc] == C_EMPTY)
        basicArea[loc] = colors[loc];
    }
  }

  calculateIndependentLifeAreaHelper(basicArea, result, whiteMinusBlackIndependentLifeRegionCount);

  if (keepTerritories)
  {
    for (int y = 0; y < y_size; y++)
    {
      for (int x = 0; x < x_size; x++)
      {
        Loc loc = Location::getLoc(x, y, x_size);
        if (basicArea[loc] != C_EMPTY && basicArea[loc] != colors[loc])
          result[loc] = basicArea[loc];
      }
    }
  }
  if (keepStones)
  {
    for (int y = 0; y < y_size; y++)
    {
      for (int x = 0; x < x_size; x++)
      {
        Loc loc = Location::getLoc(x, y, x_size);
        if (basicArea[loc] != C_EMPTY && basicArea[loc] == colors[loc])
          result[loc] = basicArea[loc];
      }
    }
  }
}

//This marks pass-alive stones, pass-alive territory always.
//If safeBigTerritories, marks empty regions bordered by pla stones and no opp stones, where all pla stones are pass-alive.
//If unsafeBigTerritories, marks empty regions bordered by pla stones and no opp stones, but ONLY on locations in result that are C_EMPTY.
//The reason for this is to avoid overwriting the opponent's pass-alive territory in situations like this:
// .ox.x.x
// oxxxxxx
// xx.....
//The top left corner is black's pass-alive territory. It's also an empty region bordered only by white, but we should not mark
//it as white's unsafeBigTerritory because it's already marked as black's pass alive territory.

void Board::calculateAreaForPla(
    Player pla,
    bool safeBigTerritories,
    bool unsafeBigTerritories,
    bool isMultiStoneSuicideLegal,
    Color *result) const
{
  Color opp = getOpp(pla);

  //https://senseis.xmp.net/?BensonsAlgorithm
  //https://zhuanlan.zhihu.com/p/110998764
  //First compute all empty-or-opp regions

  //For each loc, if it's empty or opp, the index of the region
  int16_t regionIdxByLoc[MAX_ARR_SIZE];
  //For each loc, if it's empty or opp, the next empty or opp belonging to the same region
  Loc nextEmptyOrOpp[MAX_ARR_SIZE];
  //Does this border a pla group that has been marked as not pass alive?
  bool bordersNonPassAlivePlaByHead[MAX_ARR_SIZE];

  //A list for each region head, indicating which pla group heads the region is vital for.
  //A region is vital for a pla group if all its spaces are adjacent to that pla group.
  //All lists are concatenated together, the most we can have is bounded by (MAX_LEN * MAX_LEN+1) / 2
  //independent regions, each one vital for at most 4 pla groups, add some extra just in case.
  static constexpr int maxRegions = (MAX_LEN * MAX_LEN + 1) / 2 + 1;
  static constexpr int vitalForPlaHeadsListsMaxLen = maxRegions * 4;
  Loc vitalForPlaHeadsLists[vitalForPlaHeadsListsMaxLen];
  int vitalForPlaHeadsListsTotal = 0;

  //A list of region heads
  int numRegions = 0;
  Loc regionHeads[maxRegions];
  //Start indices and list lengths in vitalForPlaHeadsLists
  uint16_t vitalStart[maxRegions];
  uint16_t vitalLen[maxRegions];
  //For each region, are there 0, 1, or 2+ spaces of that region not bordering any pla?
  uint8_t numInternalSpacesMax2[maxRegions];
  bool containsOpp[maxRegions];

  for (int i = 0; i < MAX_ARR_SIZE; i++)
  {
    regionIdxByLoc[i] = -1;
    nextEmptyOrOpp[i] = NULL_LOC;
    bordersNonPassAlivePlaByHead[i] = false;
  }

  auto isAdjacentToPlaHead = [pla, this](Loc loc, Loc plaHead) {
    FOREACHADJ(
        Loc adj = loc + ADJOFFSET;
        if (colors[adj] == pla && chain_head[adj] == plaHead) return true;);
    return false;
  };

  //Breadth-first-search trace maximal non-pla regions of the board and record their properties and join them into a
  //linked list through nextEmptyOrOpp.
  //Takes as input the location serving as the head, the tip node of the linked list so far, the next loc, and the
  //numeric index of the region
  //Returns the loc serving as the current tip node ("tailTarget") of the linked list.

  Loc buildRegionQueue[MAX_ARR_SIZE];

  auto buildRegion = [pla, opp, isMultiStoneSuicideLegal,
                      &regionIdxByLoc,
                      &vitalForPlaHeadsLists,
                      &vitalStart, &vitalLen, &numInternalSpacesMax2, &containsOpp,
                      &buildRegionQueue,
                      this,
                      &isAdjacentToPlaHead, &nextEmptyOrOpp](Loc tailTarget, Loc initialLoc, int regionIdx) -> Loc {
    //Already traced this location, skip
    if (regionIdxByLoc[initialLoc] != -1)
      return tailTarget;

    int buildRegionQueueHead = 0;
    int buildRegionQueueTail = 1;
    buildRegionQueue[0] = initialLoc;
    regionIdxByLoc[initialLoc] = regionIdx;

    while (buildRegionQueueHead != buildRegionQueueTail)
    {
      //Pop next location off queue
      Loc loc = buildRegionQueue[buildRegionQueueHead];
      buildRegionQueueHead += 1;

      //First, filter out any pla heads it turns out we're not vital for because we're not adjacent to them
      //In the case where suicide is disallowed, we only do this filtering on intersections that are actually empty
      {
        if (isMultiStoneSuicideLegal || colors[loc] == C_EMPTY)
        {
          uint16_t vStart = vitalStart[regionIdx];
          uint16_t oldVLen = vitalLen[regionIdx];
          uint16_t newVLen = 0;
          for (uint16_t i = 0; i < oldVLen; i++)
          {
            if (isAdjacentToPlaHead(loc, vitalForPlaHeadsLists[vStart + i]))
            {
              vitalForPlaHeadsLists[vStart + newVLen] = vitalForPlaHeadsLists[vStart + i];
              newVLen += 1;
            }
          }
          vitalLen[regionIdx] = newVLen;
        }
      }

      //Determine if this point is internal, unless we already have many internal points
      if (numInternalSpacesMax2[regionIdx] < 2)
      {
        if (!isAdjacentToPla(loc, pla))
          numInternalSpacesMax2[regionIdx] += 1;
      }

      if (colors[loc] == opp)
        containsOpp[regionIdx] = true;

      nextEmptyOrOpp[loc] = tailTarget;
      tailTarget = loc;

      //Push adjacent locations on to queue
      FOREACHADJ(
          Loc adj = loc + ADJOFFSET;
          if ((colors[adj] == C_EMPTY || colors[adj] == opp) && regionIdxByLoc[adj] == -1) {
            buildRegionQueue[buildRegionQueueTail] = adj;
            buildRegionQueueTail += 1;
            regionIdxByLoc[adj] = regionIdx;
          });
    }

    assert(buildRegionQueueTail < MAX_ARR_SIZE);

    return tailTarget;
  };

  bool atLeastOnePla = false;
  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (regionIdxByLoc[loc] != -1)
        continue;
      if (colors[loc] != C_EMPTY)
      {
        atLeastOnePla |= (colors[loc] == pla);
        continue;
      }
      int16_t regionIdx = numRegions;
      numRegions++;
      assert(numRegions <= maxRegions);

      //Initialize region metadata
      Loc head = loc;
      regionHeads[regionIdx] = head;
      vitalStart[regionIdx] = vitalForPlaHeadsListsTotal;
      vitalLen[regionIdx] = 0;
      numInternalSpacesMax2[regionIdx] = 0;
      containsOpp[regionIdx] = false;

      //Fill in all adjacent pla heads as vital, which will get filtered during buildRegion
      {
        uint16_t vStart = vitalStart[regionIdx];
        assert(vStart + 4 <= vitalForPlaHeadsListsMaxLen);
        uint16_t initialVLen = 0;
        for (int i = 0; i < 4; i++)
        {
          Loc adj = loc + adj_offsets[i];
          if (colors[adj] == pla)
          {
            Loc plaHead = chain_head[adj];
            bool alreadyPresent = false;
            for (int j = 0; j < initialVLen; j++)
            {
              if (vitalForPlaHeadsLists[vStart + j] == plaHead)
              {
                alreadyPresent = true;
                break;
              }
            }
            if (!alreadyPresent)
            {
              vitalForPlaHeadsLists[vStart + initialVLen] = plaHead;
              initialVLen += 1;
            }
          }
        }
        vitalLen[regionIdx] = initialVLen;
      }
      Loc tailTarget = buildRegion(head, loc, regionIdx);
      nextEmptyOrOpp[head] = tailTarget;

      vitalForPlaHeadsListsTotal += vitalLen[regionIdx];

      // for(int k = 0; k<vitalLen[regionIdx]; k++)
      //   cout << Location::toString(head,x_size) << "is vital for" << Location::toString(vitalForPlaHeadsLists[vitalStart[regionIdx]+k],x_size) << endl;
    }
  }

  //Also accumulate all player heads
  int numPlaHeads = 0;
  Loc allPlaHeads[MAX_PLAY_SIZE];
  for (Loc loc = 0; loc < MAX_ARR_SIZE; loc++)
  {
    if (colors[loc] == pla && chain_head[loc] == loc)
      allPlaHeads[numPlaHeads++] = loc;
  }

  bool plaHasBeenKilled[MAX_PLAY_SIZE];
  for (int i = 0; i < numPlaHeads; i++)
    plaHasBeenKilled[i] = false;

  //Now, we can begin the benson iteration
  uint16_t vitalCountByPlaHead[MAX_ARR_SIZE];
  while (true)
  {
    //Zero out vital liberties by head
    for (int i = 0; i < numPlaHeads; i++)
      vitalCountByPlaHead[allPlaHeads[i]] = 0;

    //Walk all regions that are still bordered only by pass-alive stuff and accumulate a vital liberty to each pla it is vital for.
    for (int i = 0; i < numRegions; i++)
    {
      if (bordersNonPassAlivePlaByHead[regionHeads[i]])
        continue;

      int vStart = vitalStart[i];
      int vLen = vitalLen[i];
      for (int j = 0; j < vLen; j++)
      {
        Loc plaHead = vitalForPlaHeadsLists[vStart + j];
        vitalCountByPlaHead[plaHead] += 1;
      }
    }

    //Walk all player heads and kill them if they haven't accumulated at least 2 vital liberties
    bool killedAnything = false;
    for (int i = 0; i < numPlaHeads; i++)
    {
      //Already killed - skip
      if (plaHasBeenKilled[i])
        continue;

      Loc plaHead = allPlaHeads[i];
      if (vitalCountByPlaHead[plaHead] < 2)
      {
        plaHasBeenKilled[i] = true;
        killedAnything = true;
        //Walk the pla chain to update bordering regions
        Loc cur = plaHead;
        do
        {
          FOREACHADJ(
              Loc adj = cur + ADJOFFSET;
              if (colors[adj] == C_EMPTY || colors[adj] == opp)
                  bordersNonPassAlivePlaByHead[regionHeads[regionIdxByLoc[adj]]] = true;);
          cur = next_in_chain[cur];
        } while (cur != plaHead);
      }
    }

    if (!killedAnything)
      break;
  }

  //Mark result with pass-alive groups
  for (int i = 0; i < numPlaHeads; i++)
  {
    if (!plaHasBeenKilled[i])
    {
      Loc plaHead = allPlaHeads[i];
      Loc cur = plaHead;
      do
      {
        result[cur] = pla;
        cur = next_in_chain[cur];
      } while (cur != plaHead);
    }
  }

  //Mark result with territory
  for (int i = 0; i < numRegions; i++)
  {
    Loc head = regionHeads[i];

    //Mark pass alive territory and pass-alive stones and large empty regions bordered only own pass-alive stones unconditionally
    //These should be mutually exclusive with these same regions but for the opponent, so this is safe.
    //We need to mark unconditionally since we WILL sometimes overwrite points of the opponent's color marked earlier, in the
    //case that the opponent was marking unsafeBigTerritories and marked an empty spot surrounded by a pass-dead group.
    bool shouldMark = numInternalSpacesMax2[i] <= 1 && atLeastOnePla && !bordersNonPassAlivePlaByHead[head];
    shouldMark = shouldMark || (safeBigTerritories && atLeastOnePla && !containsOpp[i] && !bordersNonPassAlivePlaByHead[head]);
    if (shouldMark)
    {
      Loc cur = head;
      do
      {
        result[cur] = pla;
        cur = nextEmptyOrOpp[cur];
      } while (cur != head);
    }
    else
    {
      //Mark unsafeBigTerritories only if the opponent didn't already claim the very stones we're using to surround it as
      //pass-dead and therefore the whole thing as pass-alive-territory.
      bool shouldMarkIfEmpty = (unsafeBigTerritories && atLeastOnePla && !containsOpp[i]);
      if (shouldMarkIfEmpty)
      {
        Loc cur = head;
        do
        {
          if (result[cur] == C_EMPTY)
            result[cur] = pla;
          cur = nextEmptyOrOpp[cur];
        } while (cur != head);
      }
    }
  }
}

void Board::calculateIndependentLifeAreaHelper(
    const Color *basicArea,
    Color *result,
    int &whiteMinusBlackIndependentLifeRegionCount) const
{
  Loc queue[MAX_ARR_SIZE];
  whiteMinusBlackIndependentLifeRegionCount = 0;

  //Iterate through all the regions that players own via area scoring and mark
  //all the ones that are touching dame OR that contain an atari stone
  bool isSeki[MAX_ARR_SIZE];
  for (int i = 0; i < MAX_ARR_SIZE; i++)
    isSeki[i] = false;

  int queueHead = 0;
  int queueTail = 0;

  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (basicArea[loc] != C_EMPTY && !isSeki[loc])
      {
        if (
            //Stone of player owning the area is in atari? Treat as seki.
            (colors[loc] == basicArea[loc] && getNumLiberties(loc) == 1) ||
            //Touches dame? Treat as seki
            ((colors[loc + ADJ0] == C_EMPTY && basicArea[loc + ADJ0] == C_EMPTY) ||
             (colors[loc + ADJ1] == C_EMPTY && basicArea[loc + ADJ1] == C_EMPTY) ||
             (colors[loc + ADJ2] == C_EMPTY && basicArea[loc + ADJ2] == C_EMPTY) ||
             (colors[loc + ADJ3] == C_EMPTY && basicArea[loc + ADJ3] == C_EMPTY)))
        {
          Player pla = basicArea[loc];
          isSeki[loc] = true;
          queue[queueTail++] = loc;
          while (queueHead != queueTail)
          {
            //Pop next location off queue
            Loc nextLoc = queue[queueHead++];

            //Look all around it, floodfill
            FOREACHADJ(
                Loc adj = nextLoc + ADJOFFSET;
                if (basicArea[adj] == pla && !isSeki[adj]) {
                  isSeki[adj] = true;
                  queue[queueTail++] = adj;
                });
          }
        }
      }
    }
  }

  queueHead = 0;
  queueTail = 0;

  //Now, walk through and copy all non-seki-touching basic areas into the result counting
  //how many there are.
  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, x_size);
      if (basicArea[loc] != C_EMPTY && !isSeki[loc] && result[loc] != basicArea[loc])
      {
        Player pla = basicArea[loc];
        whiteMinusBlackIndependentLifeRegionCount += (pla == P_WHITE ? 1 : -1);
        result[loc] = basicArea[loc];
        queue[queueTail++] = loc;
        while (queueHead != queueTail)
        {
          //Pop next location off queue
          Loc nextLoc = queue[queueHead++];

          //Look all around it, floodfill
          FOREACHADJ(
              Loc adj = nextLoc + ADJOFFSET;
              if (basicArea[adj] == pla && result[adj] != basicArea[adj]) {
                result[adj] = basicArea[adj];
                queue[queueTail++] = adj;
              });
        }
      }
    }
  }
}

void Board::checkConsistency() const
{
  const string errLabel = string("Board::checkConsistency(): ");

  bool chainLocChecked[MAX_ARR_SIZE];
  for (int i = 0; i < MAX_ARR_SIZE; i++)
    chainLocChecked[i] = false;

  vector<Loc> buf;
  auto checkChainConsistency = [&buf, errLabel, &chainLocChecked, this](Loc loc) {
    Player pla = colors[loc];
    Loc head = chain_head[loc];
    Loc cur = loc;
    int stoneCount = 0;
    int pseudoLibs = 0;
    bool foundChainHead = false;
    do
    {
      chainLocChecked[cur] = true;

      if (colors[cur] != pla)
        throw StringError(errLabel + "Chain is not all the same color");
      if (chain_head[cur] != head)
        throw StringError(errLabel + "Chain does not all have the same head");

      stoneCount++;
      pseudoLibs += getNumImmediateLiberties(cur);
      if (cur == head)
        foundChainHead = true;

      if (stoneCount > MAX_PLAY_SIZE)
        throw StringError(errLabel + "Chain exceeds size of board - broken circular list?");
      cur = next_in_chain[cur];

      if (cur < 0 || cur >= MAX_ARR_SIZE)
        throw StringError(errLabel + "Chain location is outside of board bounds, data corruption?");

    } while (cur != loc);

    if (!foundChainHead)
      throw StringError(errLabel + "Chain loop does not contain head");

    const ChainData &data = chain_data[head];
    if (data.owner != pla)
      throw StringError(errLabel + "Chain data owner does not match stones");
    if (data.num_locs != stoneCount)
      throw StringError(errLabel + "Chain data num_locs does not match actual stone count");
    if (data.num_liberties > pseudoLibs)
      throw StringError(errLabel + "Chain data liberties exceeds pseudoliberties");
    if (data.num_liberties <= 0)
      throw StringError(errLabel + "Chain data liberties is nonpositive");

    int numFoundLibs = findLiberties(loc, buf, 0, 0);
    if (numFoundLibs != data.num_liberties)
      throw StringError(errLabel + "FindLiberties found a different number of libs");
  };

  Hash128 tmp_pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];
  int emptyCount = 0;
  for (Loc loc = 0; loc < MAX_ARR_SIZE; loc++)
  {
    int x = Location::getX(loc, x_size);
    int y = Location::getY(loc, x_size);
    if (x < 0 || x >= x_size || y < 0 || y >= y_size)
    {
      if (colors[loc] != C_WALL)
        throw StringError(errLabel + "Non-WALL value outside of board legal area");
    }
    else
    {
      if (colors[loc] == C_BLACK || colors[loc] == C_WHITE)
      {
        if (!chainLocChecked[loc])
          checkChainConsistency(loc);
        // if(empty_list.contains(loc))
        //   throw StringError(errLabel + "Empty list contains filled location");

        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
        tmp_pos_hash ^= ZOBRIST_BOARD_HASH[loc][C_EMPTY];
      }
      else if (colors[loc] == C_EMPTY)
      {
        // if(!empty_list.contains(loc))
        //   throw StringError(errLabel + "Empty list doesn't contain empty location");
        emptyCount += 1;
      }
      else
        throw StringError(errLabel + "Non-(black,white,empty) value within board legal area");
    }
  }

  if (pos_hash != tmp_pos_hash)
    throw StringError(errLabel + "Pos hash does not match expected");

  // if(empty_list.size_ != emptyCount)
  //   throw StringError(errLabel + "Empty list size is not the number of empty points");
  // for(int i = 0; i<emptyCount; i++) {
  //   Loc loc = empty_list.list_[i];
  //   int x = Location::getX(loc,x_size);
  //   int y = Location::getY(loc,x_size);
  //   if(x < 0 || x >= x_size || y < 0 || y >= y_size)
  //     throw StringError(errLabel + "Invalid empty list loc");
  //   if(empty_list.indices_[loc] != i)
  //     throw StringError(errLabel + "Empty list index for loc in index i is not i");
  // }

  if (ko_loc != NULL_LOC)
  {
    int x = Location::getX(ko_loc, x_size);
    int y = Location::getY(ko_loc, x_size);
    if (x < 0 || x >= x_size || y < 0 || y >= y_size)
      throw StringError(errLabel + "Invalid simple ko loc");
    if (getNumImmediateLiberties(ko_loc) != 0)
      throw StringError(errLabel + "Simple ko loc has immediate liberties");
  }

  short tmpAdjOffsets[8];
  Location::getAdjacentOffsets(tmpAdjOffsets, x_size);
  for (int i = 0; i < 8; i++)
    if (tmpAdjOffsets[i] != adj_offsets[i])
      throw StringError(errLabel + "Corrupted adj_offsets array");
}

bool Board::isEqualForTesting(const Board &other, bool checkNumCaptures, bool checkSimpleKo) const
{
  checkConsistency();
  other.checkConsistency();
  if (x_size != other.x_size)
    return false;
  if (y_size != other.y_size)
    return false;
  if (checkSimpleKo && ko_loc != other.ko_loc)
    return false;
  if (checkNumCaptures && numBlackCaptures != other.numBlackCaptures)
    return false;
  if (checkNumCaptures && numWhiteCaptures != other.numWhiteCaptures)
    return false;
  if (pos_hash != other.pos_hash)
    return false;
  for (int i = 0; i < MAX_ARR_SIZE; i++)
  {
    if (colors[i] != other.colors[i])
      return false;
  }
  //We don't require that the chain linked lists are in the same order.
  //Consistency check ensures that all the linked lists are consistent with colors array, which we checked.
  return true;
}

//IO FUNCS------------------------------------------------------------------------------------------

char PlayerIO::colorToChar(Color c)
{
  switch (c)
  {
  case C_BLACK:
    return 'X';
  case C_WHITE:
    return 'O';
  case C_EMPTY:
    return '.';
  default:
    return '#';
  }
}

string PlayerIO::playerToString(Color c)
{
  switch (c)
  {
  case C_BLACK:
    return "Black";
  case C_WHITE:
    return "White";
  case C_EMPTY:
    return "Empty";
  default:
    return "Wall";
  }
}

string PlayerIO::playerToStringShort(Color c)
{
  switch (c)
  {
  case C_BLACK:
    return "B";
  case C_WHITE:
    return "W";
  case C_EMPTY:
    return "E";
  default:
    return "";
  }
}

bool PlayerIO::tryParsePlayer(const string &s, Player &pla)
{
  string str = Global::toLower(s);
  if (str == "black" || str == "b")
  {
    pla = P_BLACK;
    return true;
  }
  else if (str == "white" || str == "w")
  {
    pla = P_WHITE;
    return true;
  }
  return false;
}

Player PlayerIO::parsePlayer(const string &s)
{
  Player pla = C_EMPTY;
  bool suc = tryParsePlayer(s, pla);
  if (!suc)
    throw StringError("Could not parse player: " + s);
  return pla;
}

string Location::toStringMach(Loc loc, int x_size)
{
  if (loc == Board::PASS_LOC)
    return string("pass");
  if (loc == Board::NULL_LOC)
    return string("null");
  char buf[128];
  sprintf(buf, "(%d,%d)", getX(loc, x_size), getY(loc, x_size));
  return string(buf);
}

string Location::toString(Loc loc, int x_size, int y_size)
{
  if (x_size > 25 * 25)
    return toStringMach(loc, x_size);
  if (loc == Board::PASS_LOC)
    return string("pass");
  if (loc == Board::NULL_LOC)
    return string("null");
  const char *xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
  int x = getX(loc, x_size);
  int y = getY(loc, x_size);
  if (x >= x_size || x < 0 || y < 0 || y >= y_size)
    return toStringMach(loc, x_size);

  char buf[128];
  if (x <= 24)
    sprintf(buf, "%c%d", xChar[x], y_size - y);
  else
    sprintf(buf, "%c%c%d", xChar[x / 25 - 1], xChar[x % 25], y_size - y);
  return string(buf);
}

string Location::toString(Loc loc, const Board &b)
{
  return toString(loc, b.x_size, b.y_size);
}

string Location::toStringMach(Loc loc, const Board &b)
{
  return toStringMach(loc, b.x_size);
}

static bool tryParseLetterCoordinate(char c, int &x)
{
  if (c >= 'A' && c <= 'H')
    x = c - 'A';
  else if (c >= 'a' && c <= 'h')
    x = c - 'a';
  else if (c >= 'J' && c <= 'Z')
    x = c - 'A' - 1;
  else if (c >= 'j' && c <= 'z')
    x = c - 'a' - 1;
  else
    return false;
  return true;
}

bool Location::tryOfString(const string &str, int x_size, int y_size, Loc &result)
{
  string s = Global::trim(str);
  if (s.length() < 2)
    return false;
  if (Global::isEqualCaseInsensitive(s, string("pass")) || Global::isEqualCaseInsensitive(s, string("pss")))
  {
    result = Board::PASS_LOC;
    return true;
  }
  if (s[0] == '(')
  {
    if (s[s.length() - 1] != ')')
      return false;
    s = s.substr(1, s.length() - 2);
    vector<string> pieces = Global::split(s, ',');
    if (pieces.size() != 2)
      return false;
    int x;
    int y;
    bool sucX = Global::tryStringToInt(pieces[0], x);
    bool sucY = Global::tryStringToInt(pieces[1], y);
    if (!sucX || !sucY)
      return false;
    result = Location::getLoc(x, y, x_size);
    return true;
  }
  else
  {
    int x;
    if (!tryParseLetterCoordinate(s[0], x))
      return false;

    //Extended format
    if ((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z'))
    {
      int x1;
      if (!tryParseLetterCoordinate(s[1], x1))
        return false;
      x = (x + 1) * 25 + x1;
      s = s.substr(2, s.length() - 2);
    }
    else
    {
      s = s.substr(1, s.length() - 1);
    }

    int y;
    bool sucY = Global::tryStringToInt(s, y);
    if (!sucY)
      return false;
    y = y_size - y;
    if (x < 0 || y < 0 || x >= x_size || y >= y_size)
      return false;
    result = Location::getLoc(x, y, x_size);
    return true;
  }
}

bool Location::tryOfString(const string &str, const Board &b, Loc &result)
{
  return tryOfString(str, b.x_size, b.y_size, result);
}

Loc Location::ofString(const string &str, int x_size, int y_size)
{
  Loc result;
  if (tryOfString(str, x_size, y_size, result))
    return result;
  throw StringError("Could not parse board location: " + str);
}

Loc Location::ofString(const string &str, const Board &b)
{
  return ofString(str, b.x_size, b.y_size);
}

vector<Loc> Location::parseSequence(const string &str, const Board &board)
{
  vector<string> pieces = Global::split(Global::trim(str), ' ');
  vector<Loc> locs;
  for (size_t i = 0; i < pieces.size(); i++)
  {
    string piece = Global::trim(pieces[i]);
    if (piece.length() <= 0)
      continue;
    locs.push_back(Location::ofString(piece, board));
  }
  return locs;
}

void Board::printBoard(ostream &out, const Board &board, Loc markLoc, const vector<Move> *hist)
{
  if (hist != NULL)
    out << "MoveNum: " << hist->size() << " ";
  out << "HASH: " << board.pos_hash << "\n";
  bool showCoords = board.x_size <= 50 && board.y_size <= 50;
  if (showCoords)
  {
    const char *xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    out << "  ";
    for (int x = 0; x < board.x_size; x++)
    {
      if (x <= 24)
      {
        out << " ";
        out << xChar[x];
      }
      else
      {
        out << "A" << xChar[x - 25];
      }
    }
    out << "\n";
  }

  for (int y = 0; y < board.y_size; y++)
  {
    if (showCoords)
    {
      char buf[16];
      sprintf(buf, "%2d", board.y_size - y);
      out << buf << ' ';
    }
    for (int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, board.x_size);
      char s = PlayerIO::colorToChar(board.colors[loc]);
      if (board.colors[loc] == C_EMPTY && markLoc == loc)
        out << '@';
      else
        out << s;

      bool histMarked = false;
      if (hist != NULL)
      {
        size_t start = hist->size() >= 3 ? hist->size() - 3 : 0;
        for (size_t i = 0; start + i < hist->size(); i++)
        {
          if ((*hist)[start + i].loc == loc)
          {
            out << (1 + i);
            histMarked = true;
            break;
          }
        }
      }

      if (x < board.x_size - 1 && !histMarked)
        out << ' ';
    }
    out << "\n";
  }
  out << "\n";
}

ostream &operator<<(ostream &out, const Board &board)
{
  Board::printBoard(out, board, Board::NULL_LOC, NULL);
  return out;
}

string Board::toStringSimple(const Board &board, char lineDelimiter)
{
  string s;
  for (int y = 0; y < board.y_size; y++)
  {
    for (int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x, y, board.x_size);
      s += PlayerIO::colorToChar(board.colors[loc]);
    }
    s += lineDelimiter;
  }
  return s;
}

Board Board::parseBoard(int xSize, int ySize, const string &s)
{
  return parseBoard(xSize, ySize, s, '\n');
}

Board Board::parseBoard(int xSize, int ySize, const string &s, char lineDelimiter)
{
  Board board(xSize, ySize);
  vector<string> lines = Global::split(Global::trim(s), lineDelimiter);

  //Throw away coordinate labels line if it exists
  if (lines.size() == ySize + 1 && Global::isPrefix(lines[0], "A"))
    lines.erase(lines.begin());

  if (lines.size() != ySize)
    throw StringError("Board::parseBoard - string has different number of board rows than ySize");

  for (int y = 0; y < ySize; y++)
  {
    string line = Global::trim(lines[y]);
    //Throw away coordinates if they exist
    size_t firstNonDigitIdx = 0;
    while (firstNonDigitIdx < line.length() && Global::isDigit(line[firstNonDigitIdx]))
      firstNonDigitIdx++;
    line.erase(0, firstNonDigitIdx);
    line = Global::trim(line);

    if (line.length() != xSize && line.length() != 2 * xSize - 1)
      throw StringError("Board::parseBoard - line length not compatible with xSize");

    for (int x = 0; x < xSize; x++)
    {
      char c;
      if (line.length() == xSize)
        c = line[x];
      else
        c = line[x * 2];

      Loc loc = Location::getLoc(x, y, board.x_size);
      if (c == '.' || c == ' ' || c == '*' || c == ',' || c == '`')
        continue;
      else if (c == 'o' || c == 'O')
        board.setStone(loc, P_WHITE);
      else if (c == 'x' || c == 'X')
        board.setStone(loc, P_BLACK);
      else
        throw StringError(string("Board::parseBoard - could not parse board character: ") + c);
    }
  }
  return board;
}
