/*
 * board.h
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modifications).
 */

#ifndef GAME_BOARD_H_
#define GAME_BOARD_H_

#include "../core/global.h"
#include "../core/hash.h"

#ifndef COMPILE_MAX_BOARD_LEN
#define COMPILE_MAX_BOARD_LEN 6
#endif

//TYPES AND CONSTANTS-----------------------------------------------------------------

struct Board;

//Player
typedef int8_t Player;
static constexpr Player P_BLACK = 1;
static constexpr Player P_WHITE = 2;

//Color of a point on the board
typedef int8_t Color;
static constexpr Color C_EMPTY = 0;
static constexpr Color C_BLACK = 1;
static constexpr Color C_WHITE = 2;
static constexpr Color C_WALL = 3;
static constexpr int NUM_BOARD_COLORS = 4;

//the loops on the board
constexpr bool kIsOuter[56] = {
    false,false,false,false,false,false,false,
    false,false,false, true, true,false,false,
    false,false,false, true, true,false,false,
    false,true , true, true, true, true, true,
    false,true , true, true, true, true, true,
    false,false,false, true, true,false,false,
    false,false,false, true, true,false,false,
    false,false,false,false,false,false,false,};
constexpr bool kIsInter[56] = {
    false,false,false,false,false,false,false,
    false,false, true,false,false, true,false,
    false, true, true, true, true, true, true,
    false,false, true,false,false, true,false,
    false,false, true,false,false, true,false,
    false, true, true, true, true, true, true,
    false,false, true,false,false, true,false,
    false,false,false,false,false,false,false,};
/*
0 ,1 ,2 ,3 ,4 ,5
6 ,7 ,8 ,9 ,10,11
12,13,14,15,16,17
18,19,20,21,22,23
24,25,26,27,28,29
30,31,32,33,34,35

8 ,9 ,10,11,12,13
15,16,17,18,19,20
22,23,24,25,26,27
29,30,31,32,33,34
36,37,38,39,40,41
43,44,45,46,47,48
*/
constexpr std::array<int, 56> kInter = {
    9 , 16, 23, 30, 37, 44, -1,
    36, 37, 38, 39, 40, 41, -1,
    47, 40, 33, 26, 19, 12, -1,
    20, 19, 18, 17, 16, 15, -1,
    9 , 16, 23, 30, 37, 44, -1,
    36, 37, 38, 39, 40, 41, -1,
    47, 40, 33, 26, 19, 12, -1,
    20, 19, 18, 17, 16, 15, -1};
constexpr std::array<int, 56> kOuter = {
    45, 38, 31, 24, 17, 10, -1,
    22, 23, 24, 25, 26, 27, -1,
    11, 18, 25, 32, 39, 46, -1,
    34, 33, 32, 31, 30, 29, -1,
    45, 38, 31, 24, 17, 10, -1,
    22, 23, 24, 25, 26, 27, -1,
    11, 18, 25, 32, 39, 46, -1,
    34, 33, 32, 31, 30, 29, -1};
    /*
constexpr std::array<int, 56> kOuter = {
    32, 26, 20, 14, 8 , 2 , -1,
    12, 13, 14, 15, 16, 17, -1,
    3 , 9 , 15, 21, 27, 33, -1,
    23, 22, 21, 20, 19, 18, -1,
    32, 26, 20, 14, 8 , 2 , -1,
    12, 13, 14, 15, 16, 17, -1,
    3 , 9 , 15, 21, 27, 33, -1,
    23, 22, 21, 20, 19, 18, -1};
constexpr std::array<int, 56> kOuterReverse = {
    18, 19, 20, 21, 22, 23, -1,
    33, 27, 21, 15, 9, 3, -1,
    17, 16, 15, 14, 13, 12, -1,
    2, 8, 14, 20, 26, 32, -1,
    18, 19, 20, 21, 22, 23, -1,
    33, 27, 21, 15, 9, 3, -1,
    17, 16, 15, 14, 13, 12, -1,
    2, 8, 14, 20, 26, 32, -1};
constexpr std::array<int, 56> kInter = {
    1, 7, 13, 19, 25, 31, -1,
    24, 25, 26, 27, 28, 29, -1,
    34, 28, 22, 16, 10, 4, -1,
    11, 10, 9, 8, 7, 6, -1,
    1, 7, 13, 19, 25, 31, -1,
    24, 25, 26, 27, 28, 29, -1,
    34, 28, 22, 16, 10, 4, -1,
    11, 10, 9, 8, 7, 6, -1};
constexpr std::array<int, 56> kInterReverse = {
    6, 7, 8, 9, 10, 11, -1,
    4, 10, 16, 22, 28, 34, -1,
    29, 28, 27, 26, 25, 24, -1,
    31, 25, 19, 13, 7, 1, -1,
    6, 7, 8, 9, 10, 11, -1,
    4, 10, 16, 22, 28, 34, -1,
    29, 28, 27, 26, 25, 24, -1,
    31, 25, 19, 13, 7, 1, -1};
    */

static inline Color getOpp(Color c)
{return c ^ 3;}

//Conversions for players and colors
namespace PlayerIO {
  char colorToChar(Color c);
  std::string playerToStringShort(Player p);
  std::string playerToString(Player p);
  bool tryParsePlayer(const std::string& s, Player& pla);
  Player parsePlayer(const std::string& s);
}

//Location of a point on the board
//(x,y) is represented as (x+1) + (y+1)*(x_size+1)
typedef short Loc;
namespace Location
{
  Loc getLoc(int x, int y, int x_size);
  int getX(Loc loc, int x_size);
  int getY(Loc loc, int x_size);

  void getAdjacentOffsets(short adj_offsets[8], int x_size);
  bool isAdjacent(Loc loc0, Loc loc1, int x_size);
  Loc getMirrorLoc(Loc loc, int x_size, int y_size);
  Loc getCenterLoc(int x_size, int y_size);
  bool isCentral(Loc loc, int x_size, int y_size);
  int distance(Loc loc0, Loc loc1, int x_size);
  int euclideanDistanceSquared(Loc loc0, Loc loc1, int x_size);

  std::string toString(Loc loc, int x_size, int y_size);
  std::string toString(Loc loc, const Board& b);
  std::string toStringMach(Loc loc, int x_size);
  std::string toStringMach(Loc loc, const Board& b);

  bool tryOfString(const std::string& str, int x_size, int y_size, Loc& result);
  bool tryOfString(const std::string& str, const Board& b, Loc& result);
  Loc ofString(const std::string& str, int x_size, int y_size);
  Loc ofString(const std::string& str, const Board& b);

  std::vector<Loc> parseSequence(const std::string& str, const Board& b);
}

//Simple structure for storing moves. Not used below, but this is a convenient place to define it.
STRUCT_NAMED_TRIPLE(Loc, fromLoc,Loc, toLoc, Player, pla, Move);

//Fast lightweight board designed for playouts and simulations, where speed is essential.
//Simple ko rule only.
//Does not enforce player turn order.

struct Board
{
  //Initialization------------------------------
  //Initialize the zobrist hash.
  //MUST BE CALLED AT PROGRAM START!
  static void initHash();

  //Board parameters and Constants----------------------------------------

  static const int MAX_LEN = COMPILE_MAX_BOARD_LEN;  //Maximum edge length allowed for the board
  static const int MAX_PLAY_SIZE = MAX_LEN * MAX_LEN;  //Maximum number of playable spaces
  static const int MAX_ARR_SIZE = (MAX_LEN+1)*(MAX_LEN+2)+1; //Maximum size of arrays needed

  //Location used to indicate an invalid spot on the board.
  static const Loc NULL_LOC = 0;
  //Location used to indicate a pass move is desired.
  static const Loc PASS_LOC = 1;

  //Zobrist Hashing------------------------------
  static bool IS_ZOBRIST_INITALIZED;
  static Hash128 ZOBRIST_SIZE_X_HASH[MAX_LEN+1];
  static Hash128 ZOBRIST_SIZE_Y_HASH[MAX_LEN+1];
  static Hash128 ZOBRIST_BOARD_HASH[MAX_ARR_SIZE][4];
  static Hash128 ZOBRIST_PLAYER_HASH[4];
  static Hash128 ZOBRIST_KO_LOC_HASH[MAX_ARR_SIZE];
  static Hash128 ZOBRIST_KO_MARK_HASH[MAX_ARR_SIZE][4];
  static Hash128 ZOBRIST_ENCORE_HASH[3];
  static Hash128 ZOBRIST_SECOND_ENCORE_START_HASH[MAX_ARR_SIZE][4];
  static const Hash128 ZOBRIST_PASS_ENDS_PHASE;
  static const Hash128 ZOBRIST_GAME_IS_OVER;

  //Structs---------------------------------------

  //Tracks a chain/string/group of stones
  struct ChainData {
    Player owner;        //Owner of chain
    short num_locs;      //Number of stones in chain
    short num_liberties; //Number of liberties in chain
  };

  //Tracks locations for fast random selection
  /* struct PointList { */
  /*   PointList(); */
  /*   PointList(const PointList&); */
  /*   void operator=(const PointList&); */
  /*   void add(Loc); */
  /*   void remove(Loc); */
  /*   int size() const; */
  /*   Loc& operator[](int); */
  /*   bool contains(Loc loc) const; */

  /*   Loc list_[MAX_PLAY_SIZE];   //Locations in the list */
  /*   int indices_[MAX_ARR_SIZE]; //Maps location to index in the list */
  /*   int size_; */
  /* }; */

  //Move data passed back when moves are made to allow for undos
  struct MoveRecord {
    Player pla;
    Loc fromLoc;
    Loc toLoc;
    Color eaten;
  };

  //Constructors---------------------------------
  Board();  //Create Board of size (19,19)
  Board(int x, int y); //Create Board of size (x,y)
  Board(const Board& other);

  Board& operator=(const Board&) = default;

  //Functions------------------------------------
  //Check if this is legal capture
  bool getIsLegalCapture(Player Pla, Loc fromLoc, Loc toLoc) const;
  bool get_is_legal_capture(Player Pla, Loc fromLoc, Loc toLoc, const std::array<int, 56> &circle,bool reverse) const;
  //Check if moving here is legal. Equivalent to isLegalIgnoringKo && !isKoBanned
  bool isLegal(Loc fromLoc, Loc toLoc, Player pla ) const;
  //Check if this location is on the board
  bool isOnBoard(Loc loc) const;
  //Check if this location contains a simple eye for the specified player.
  bool isSimpleEye(Loc loc, Player pla) const;
  //Check if a move at this location would be a capture of an opponent group.
  //change to getIsLegalCapture
  //bool wouldBeCapture(Loc loc, Player pla) const;
  //Check if this location is adjacent to stones of the specified color
  bool isAdjacentToPla(Loc loc, Player pla) const;
  bool isAdjacentOrDiagonalToPla(Loc loc, Player pla) const;
  //Is this board empty?
  bool isEmpty() const;
  //Count the number of stones on the board
  int numStonesOnBoard() const;
  int numPlaStonesOnBoard(Player pla) const;

  //Get a hash that combines the position of the board with simple ko prohibition and a player to move.
  Hash128 getSitHashWithSimpleKo(Player pla) const;

  //Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
  bool playMove(Loc fromLoc, Loc toLoc, Player pla, bool isMultiStoneSuicideLegal);

  //Plays the specified move, assuming it is legal.
  void playMoveAssumeLegal(Loc fromLoc, Loc toLoc, Player pla);

  //Plays the specified move, assuming it is legal, and returns a MoveRecord for the move
  MoveRecord playMoveRecorded(Loc fromLoc, Loc toLoc, Player pla);

  //Undo the move given by record. Moves MUST be undone in the order they were made.
  //Undos will NOT typically restore the precise representation in the board to the way it was. The heads of chains
  //might change, the order of the circular lists might change, etc.
  void undo(MoveRecord record);

  //Get what the position hash would be if we were to play this move and resolve captures and suicides.
  //Assumes the move is on an empty location.
  Hash128 getPosHashAfterMove(Loc fromLoc, Loc toLoc, Player pla) const;

  //Run some basic sanity checks on the board state, throws an exception if not consistent, for testing/debugging
  void checkConsistency() const;
  //For the moment, only used in testing since it does extra consistency checks.
  //If we need a version to be used in "prod", we could make an efficient version maybe as operator==.
  bool isEqualForTesting(const Board& other, bool checkNumCaptures, bool checkSimpleKo) const;

  static Board parseBoard(int xSize, int ySize, const std::string& s);
  static Board parseBoard(int xSize, int ySize, const std::string& s, char lineDelimiter);
  static void printBoard(std::ostream& out, const Board& board, Loc markLoc, const std::vector<Move>* hist);
  static std::string toStringSimple(const Board& board, char lineDelimiter);

  //Data--------------------------------------------

  int x_size;                  //Horizontal size of board
  int y_size;                  //Vertical size of board
  Color colors[MAX_ARR_SIZE];  //Color of each location on the board.

  Hash128 pos_hash; //A zobrist hash of the current board position (does not include ko point or player to move)

  int numBlackCaptures; //Number of b stones captured, informational and used by board history when clearing pos
  int numWhiteCaptures; //Number of w stones captured, informational and used by board history when clearing pos

  short adj_offsets[8]; //Indices 0-3: Offsets to add for adjacent points. Indices 4-7: Offsets for diagonal points. 2 and 3 are +x and +y.

  private:
  void init(int xS, int yS);

  friend std::ostream& operator<<(std::ostream& out, const Board& board);
};




#endif // GAME_BOARD_H_
