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
  init(6, 6);
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
  //fixed size
  if (xS < 0 || yS < 0 || xS > MAX_LEN || yS > MAX_LEN)
    throw StringError("Board::init - invalid board size");

  x_size = 6;
  y_size = 6;

  for (int y = 0; y < y_size; y++)
  {
    for (int x = 0; x < x_size; x++)
    {
      Loc loc = (x + 1) + (y + 1) * (x_size + 1);
      if (y < 3)
        colors[loc] = C_BLACK;
      else if (y > 4)
        colors[loc] = C_WHITE;
      else
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
  if (repeat_[colors]>1)
    return false;
  return isLegal(fromLoc, toLoc, pla, isMultiStoneSuicideLegal);
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

//Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
bool Board::playMove(Loc fromLoc, Loc toLoc, Player pla, bool isMultiStoneSuicideLegal)
{
  if (isLegal(fromLoc, toLoc, pla, isMultiStoneSuicideLegal))
  {
    playMoveAssumeLegal(fromLoc, toLoc, pla);
    return true;
  }
  return false;
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
    /*
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
    */
  }
  return board;
}
