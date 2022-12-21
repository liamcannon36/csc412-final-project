//
//  main.c
//  Final Project CSC412
//
//  Created by Jean-Yves Hervé on 2020-12-01
//    This is public domain code.  By all means appropriate it and change is to your
//    heart's content.
#include <iostream>
#include <string>
#include <random>
#include <thread>
#include <memory>
#include <unistd.h>
//
#include <cstdio>
#include <cstdlib>
#include <ctime>
//
#include "gl_frontEnd.h"

//    feel free to "un-use" std if this is against your beliefs.
using namespace std;

//==================================================================================
//    Function prototypes
//==================================================================================
void initializeApplication(void);
GridPosition getNewFreePosition(void);
Direction newDirection(Direction forbiddenDir = Direction::NUM_DIRECTIONS);
TravelerSegment newTravelerSegment(const TravelerSegment& currentSeg, bool& canAdd);
void generateWalls(void);
void generatePartitions(void);
void travelerThreadFunc(Traveler* traveler);
void moveTraveler(Traveler* traveler);
vector<Direction> findPossibleMoves(const TravelerSegment& headSeg);
shared_ptr<TravelerSegment> findtargetSeg(const TravelerSegment& headSeg);
void joinThreads(void);
//==================================================================================
//    Application-level global variables
//==================================================================================

//    Don't rename any of these variables
//-------------------------------------
//    The state grid and its dimensions (arguments to the program)
SquareType** grid;
unsigned int numRows = 0;    //    height of the grid
unsigned int numCols = 0;    //    width
unsigned int numTravelers = 0;    //    initial number
unsigned int growthMove = 0;
unsigned int numTravelersDone = 0;
unsigned int numLiveThreads = 0;        //    the number of live traveler threads
vector<Traveler> travelerList;
vector<SlidingPartition> partitionList;
GridPosition exitPos;    //    location of the exit
bool keepGoing = true;
//    travelers' sleep time between moves (in microseconds)
const int MIN_SLEEP_TIME = 1000;
int travelerSleepTime = 100000;
vector<thread*> threadPointerList;
//    An array of C-string where you can store things you want displayed
//    in the state pane to display (for debugging purposes?)
//    Dont change the dimensions as this may break the front end
const int MAX_NUM_MESSAGES = 8;
const int MAX_LENGTH_MESSAGE = 32;
char** message;
time_t launchTime;
std::mutex travelerLock;
std::mutex gridLock;
//    Random generators:  For uniform distributions
const unsigned int MAX_NUM_INITIAL_SEGMENTS = 6;
random_device randDev;
default_random_engine engine(randDev());
uniform_int_distribution<unsigned int> unsignedNumberGenerator(0, numeric_limits<unsigned int>::max());
uniform_int_distribution<unsigned int> segmentNumberGenerator(0, MAX_NUM_INITIAL_SEGMENTS);
uniform_int_distribution<unsigned int> segmentDirectionGenerator(0, static_cast<unsigned int>(Direction::NUM_DIRECTIONS)-1);
uniform_int_distribution<unsigned int> headsOrTails(0, 1);
uniform_int_distribution<unsigned int> rowGenerator;
uniform_int_distribution<unsigned int> colGenerator;



//==================================================================================
//    These are the functions that tie the simulation with the rendering.
//    Some parts are "don't touch."  Other parts need your intervention
//    to make sure that access to critical section is properly synchronized
//==================================================================================

void drawTravelers(void)
{
    //-----------------------------
    //    You may have to sychronize things here
    //-----------------------------
    for (unsigned int k=0; k<travelerList.size(); k++)
    {
        //    here I would test if the traveler thread is still live
        if (travelerList[k].status == TravelerStatus::RUNNING)
            drawTraveler(travelerList[k]);
    }
}

void updateMessages(void)
{
    //    Here I hard-code a few messages that I want to see displayed
    //    in my state pane.  The number of live robot threads will
    //    always get displayed.  No need to pass a message about it.
    unsigned int numMessages = 4;
    sprintf(message[0], "We created %d travelers", numTravelers);
    sprintf(message[1], "%d travelers solved the maze", numTravelersDone);
    sprintf(message[2], "I like cheese");
    sprintf(message[3], "Simulation run time: %ld s", time(NULL)-launchTime);
    
    //---------------------------------------------------------
    //    This is the call that makes OpenGL render information
    //    about the state of the simulation.
    //
    //    You *must* synchronize this call.
    //---------------------------------------------------------
    drawMessages(numMessages, message);
}

void handleKeyboardEvent(unsigned char c, int x, int y)
{
    int ok = 0;

    switch (c)
    {
        //    'esc' to quit
        case 27:
            keepGoing = false;
            joinThreads();
            exit(0);
            break;

        //    slowdown
        case ',':
            slowdownTravelers();
            ok = 1;
            break;

        //    speedup
        case '.':
            speedupTravelers();
            ok = 1;
            break;

        default:
            ok = 1;
            break;
    }
    if (!ok){
        //    do something?
    }
}


//------------------------------------------------------------------------
//    You shouldn't have to touch this one.  Definitely if you don't
//    add the "producer" threads, and probably not even if you do.
//------------------------------------------------------------------------
void speedupTravelers(void)
{
    //    decrease sleep time by 20%, but don't get too small
    int newSleepTime = (8 * travelerSleepTime) / 10;
    
    if (newSleepTime > MIN_SLEEP_TIME)
    {
        travelerSleepTime = newSleepTime;
    }
}

void slowdownTravelers(void)
{
    //    increase sleep time by 20%.  No upper limit on sleep time.
    //    We can slow everything down to admistrative pace if we want.
    travelerSleepTime = (12 * travelerSleepTime) / 10;
}




//------------------------------------------------------------------------
//    You shouldn't have to change anything in the main function besides
//    initialization of the various global variables and lists
//------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    //    We know that the arguments  of the program  are going
    //    to be the width (number of columns) and height (number of rows) of the
    //    grid, the number of travelers, etc.
    //    So far, I hard code-some values
    numRows = atoi(argv[1]);
    numCols = atoi(argv[2]);
    numTravelers = atoi(argv[3]);
    growthMove = atoi(argv[4]); //growth after N moves
    
    numLiveThreads = 0;
    numTravelersDone = 0;

    //    Even though we extracted the relevant information from the argument
    //    list, I still need to pass argc and argv to the front-end init
    //    function because that function passes them to glutInit, the required call
    //    to the initialization of the glut library.
    initializeFrontEnd(argc, argv);
    
    //    Now we can do application-level initialization
    initializeApplication();

    launchTime = time(NULL);

    //    Now we enter the main loop of the program and to a large extend
    //    "lose control" over its execution.  The callback functions that
    //    we set up earlier will be called when the corresponding event
    //    occurs
    glutMainLoop();
    
    //    Free allocated resource before leaving (not absolutely needed, but
    //    just nicer.  Also, if you crash there, you know something is wrong
    //    in your code.
    for (unsigned int i=0; i< numRows; i++){
        free(grid[i]);
    }
    free(grid);
    for (int k=0; k<MAX_NUM_MESSAGES; k++){
        free(message[k]);
    }
    free(message);
    
    //    This will probably never be executed (the exit point will be in one of the
    //    call back functions).
    return 0;
}


//==================================================================================
//
//    This is a function that you have to edit and add to.
//
//==================================================================================


void initializeApplication(void)
{
    //    Initialize some random generators
    rowGenerator = uniform_int_distribution<unsigned int>(0, numRows-1);
    colGenerator = uniform_int_distribution<unsigned int>(0, numCols-1);

    //    Allocate the grid
    grid = new SquareType*[numRows];
    for (unsigned int i=0; i<numRows; i++)
    {
        grid[i] = new SquareType[numCols];
        for (unsigned int j=0; j< numCols; j++)
            grid[i][j] = SquareType::FREE_SQUARE;
        
    }

    message = new char*[MAX_NUM_MESSAGES];
    for (unsigned int k=0; k<MAX_NUM_MESSAGES; k++)
        message[k] = new char[MAX_LENGTH_MESSAGE+1];
        
    //---------------------------------------------------------------
    //    All the code below to be replaced/removed
    //    I initialize the grid's pixels to have something to look at
    //---------------------------------------------------------------
    //    Yes, I am using the C random generator after ranting in class that the C random
    //    generator was junk.  Here I am not using it to produce "serious" data (as in a
    //    real simulation), only wall/partition location and some color
    srand(static_cast<unsigned int>(time(NULL)));

    //    generate a random exit
    exitPos = getNewFreePosition();
    grid[exitPos.row][exitPos.col] = SquareType::EXIT;

    //    Generate walls and partitions
    generateWalls();
    generatePartitions();
    
    float** travelerColor = createTravelerColors(numTravelers);
    for (unsigned int k=0; k<numTravelers; k++){
        GridPosition pos = getNewFreePosition();
        //    Note that treating an enum as a sort of integer is increasingly
        //    frowned upon, as C++ versions progress
        Direction dir = static_cast<Direction>(segmentDirectionGenerator(engine));

        TravelerSegment seg = {pos.row, pos.col, dir};
        Traveler traveler;
        
        traveler.segmentList.push_back(seg);
        grid[pos.row][pos.col] = SquareType::TRAVELER;

        //    I add 0-n segments to my travelers
        unsigned int numAddSegments = segmentNumberGenerator(engine);
        TravelerSegment currSeg = traveler.segmentList[0];
        bool canAddSegment = true;
        cout << "Traveler " << k << " at (row=" << pos.row << ", col=" <<
        pos.col << "), direction: " << dirStr(dir) << ", with up to " << numAddSegments << " additional segments" << endl;
        cout << "\t";

        for (unsigned int s=0; s<numAddSegments && canAddSegment; s++){
            TravelerSegment newSeg = newTravelerSegment(currSeg, canAddSegment);
            if (canAddSegment){
                traveler.segmentList.push_back(newSeg);
                grid[newSeg.row][newSeg.col] = SquareType::TRAVELER;
                currSeg = newSeg;
                cout << dirStr(newSeg.dir) << "  ";
            }
        }
        cout << endl;

        for (unsigned int c=0; c<4; c++)
            traveler.rgba[c] = travelerColor[k][c];
        travelerList.push_back(traveler);
    }
    /**
            Thread stuff starts here
     */
    
        thread** travelerThread = new thread*[numTravelers];
        for (unsigned int k=0; k<numTravelers; k++) {
            travelerThread[k] = new thread(travelerThreadFunc,&travelerList[k]);
            numLiveThreads++;
            threadPointerList.push_back(travelerThread[k]);
        }
    
    //    free array of colors
    for (unsigned int k=0; k<numTravelers; k++)
        delete []travelerColor[k];
    delete []travelerColor;
}

//------------------------------------------------------
#if 0
#pragma mark -
#pragma mark Traveler's Movement Helper Functions
#endif
//------------------------------------------------------

void travelerThreadFunc(Traveler* traveler){
    //The boolean which helps us to keep track if the thread is terminated yet or no
    travelerLock.lock();
    traveler->status = TravelerStatus::RUNNING;
    travelerLock.unlock();
    while(keepGoing && traveler->status == TravelerStatus::RUNNING){
        moveTraveler(traveler);
        usleep(travelerSleepTime);
    }
    travelerLock.lock();
    traveler->status = TravelerStatus::TERMINATED;
    travelerLock.unlock();
    numLiveThreads--;
}

void moveTraveler(Traveler* traveler){
    // Check where is the head of the traveler.
    TravelerSegment headSeg = traveler->segmentList.front();
    // find a possible move, and make a segment of it
    shared_ptr<TravelerSegment> targetSegment = findtargetSeg(headSeg);
    // unless the segment is not null, append it to the front of the traveler's deque and pop segment from back
    //if segment is the exit square kill the traveler thread
    if (targetSegment != nullptr){
        traveler->moveCount++;
        
        if(targetSegment->row == exitPos.row && targetSegment->col == exitPos.col){
               // erase this traveler's "trail"
               // all the squares that it was occupying are now free.
            for(int i = 0; i<traveler->segmentList.size();i++){
                TravelerSegment currTravelerSeg = traveler->segmentList.at(i);
                
                gridLock.lock();
                grid[currTravelerSeg.row][currTravelerSeg.col] = SquareType::FREE_SQUARE;
                gridLock.unlock();
            }
            travelerLock.lock();
            traveler->status = TravelerStatus::TERMINATED;
            travelerLock.unlock();
            numTravelersDone++;
        }
        else{
            //the grid square which is free, cover it by traveler
            travelerLock.lock();
            traveler->segmentList.push_front(*targetSegment);
            travelerLock.unlock();
            
            gridLock.lock();
            grid[targetSegment->row][targetSegment->col] = SquareType::TRAVELER;
            gridLock.unlock();
            
            travelerLock.lock();
            TravelerSegment tailSeg = traveler->segmentList.back();
            travelerLock.unlock();
            
            // if not growing tail segment  move count % num moves to grow == 0
            if(traveler->moveCount % growthMove ==0){
                Direction tailDir = tailSeg.dir;
                int row = 0;
                int col = 0;
                switch (tailDir){
                    case Direction::NORTH:
                        row = tailSeg.row + 1;
                        col = tailSeg.col;
                        break;
                    case Direction::SOUTH:
                        row = tailSeg.row - 1;
                        col = tailSeg.col;
                        break;
                    case Direction::WEST:
                        row = tailSeg.row;
                        col = tailSeg.col + 1;
                        break;
                    case Direction::EAST:
                        row = tailSeg.row;
                        col = tailSeg.col - 1;
                        break;
                    default:
                        break;
                }
                TravelerSegment growSeg = {growSeg.row = row, growSeg.col = col, growSeg.dir = tailSeg.dir};
                
                travelerLock.lock();
                traveler->segmentList.push_back(growSeg);
                travelerLock.unlock();
                
                gridLock.lock();
                grid[growSeg.row][growSeg.col] = SquareType::TRAVELER;
                gridLock.unlock();
            }
            else{
                //the grid square which is not covered by traveler, free it
                gridLock.lock();
                grid[tailSeg.row][tailSeg.col] = SquareType::FREE_SQUARE;
                gridLock.unlock();
                
                travelerLock.lock();
                traveler->segmentList.pop_back();
                travelerLock.unlock();
            }
        }
    }
}


vector<Direction> findPossibleMoves(const TravelerSegment& headSeg){

    Direction headDir = headSeg.dir;
    vector<Direction> possibleMoves;
    Direction forbiddenDirection = Direction::NORTH;
    // a traveler can not go in the inverse direction
    switch (headDir){
        case Direction::NORTH:
            forbiddenDirection = Direction::SOUTH;
            break;
        case Direction::SOUTH:
            forbiddenDirection = Direction::NORTH;
            break;
        case Direction::WEST:
            forbiddenDirection = Direction::EAST;
            break;
        case Direction::EAST:
            forbiddenDirection = Direction::WEST;
            break;
        default:
            break;
    }
    // Unless the direction is not forbidden push all the other possible moves in a vector and return that vector
    if(headSeg.row > 0 && (grid[headSeg.row-1][headSeg.col] == SquareType::FREE_SQUARE || grid[headSeg.row-1][headSeg.col] == SquareType::EXIT)){
        if (forbiddenDirection != Direction::NORTH)
            possibleMoves.push_back(Direction::NORTH);
    }
    if(headSeg.row < numRows-1 && (grid[headSeg.row+1][headSeg.col] == SquareType::FREE_SQUARE || grid[headSeg.row+1][headSeg.col] == SquareType::EXIT)){
        if (forbiddenDirection != Direction::SOUTH)
         possibleMoves.push_back(Direction::SOUTH);
    }
    if((headSeg.col < numCols-1 )&& (grid[headSeg.row][headSeg.col+1] == SquareType::FREE_SQUARE || grid[headSeg.row][headSeg.col+1] == SquareType::EXIT)){
        if (forbiddenDirection != Direction::EAST)
        possibleMoves.push_back(Direction::EAST);
    }
    if( headSeg.col > 0  && (grid[headSeg.row][headSeg.col-1] == SquareType::FREE_SQUARE || grid[headSeg.row][headSeg.col-1] == SquareType::EXIT)){
        if (forbiddenDirection != Direction::WEST)
        possibleMoves.push_back(Direction::WEST);
    }

    return possibleMoves;
}

shared_ptr<TravelerSegment> findtargetSeg(const TravelerSegment& headSeg){
    //Get the vector of all possible moves
    vector<Direction> possibleMoves = findPossibleMoves(headSeg);
    //shared ptr which can delete itself when all the links to it are broken
    shared_ptr<TravelerSegment> ptr;
    //    if there is at least one possible move, pick one (random/AI)
    if (!possibleMoves.empty()){
        Direction targetDir = possibleMoves[unsignedNumberGenerator(engine) % possibleMoves.size()];
        //once a direction is found, initialize target segment
        TravelerSegment targetSegment;
        switch (targetDir){
            case Direction::EAST:
                targetSegment.row = headSeg.row;
                targetSegment.col = headSeg.col + 1;
                targetSegment.dir = targetDir;
                break;
            case Direction::WEST:
                targetSegment.row = headSeg.row;
                targetSegment.col = headSeg.col - 1;
                targetSegment.dir = targetDir;
                break;
            case Direction::NORTH:
                targetSegment.row = headSeg.row - 1;
                targetSegment.col = headSeg.col;
                targetSegment.dir = targetDir;
                break;
            case Direction::SOUTH:
                targetSegment.row = headSeg.row + 1;
                targetSegment.col = headSeg.col;
                targetSegment.dir = targetDir;
                break;
            default:
                break;
        }
        // return this segment
        ptr = make_shared<TravelerSegment>(targetSegment);
    }
    // if the traveler is blocked and has no direction to go, return null ptr
    else{
        ptr = nullptr;
    }
    return ptr;
}

void joinThreads(void){
    for(int i = 0; i<threadPointerList.size(); i++){
        if (travelerList[i].status != TravelerStatus::JOINED){
            numTravelersDone++;
            numLiveThreads--;
            threadPointerList[i]->join();
            travelerList[i].status = TravelerStatus::JOINED;
        }
    }
}
//------------------------------------------------------
#if 0
#pragma mark -
#pragma mark Generation Helper Functions
#endif
//------------------------------------------------------

GridPosition getNewFreePosition(void)
{
    GridPosition pos;

    bool noGoodPos = true;
    while (noGoodPos)
    {
        unsigned int row = rowGenerator(engine);
        unsigned int col = colGenerator(engine);
        if (grid[row][col] == SquareType::FREE_SQUARE)
        {
            pos.row = row;
            pos.col = col;
            noGoodPos = false;
        }
    }
    return pos;
}

Direction newDirection(Direction forbiddenDir)
{
    bool noDir = true;

    Direction dir = Direction::NUM_DIRECTIONS;
    while (noDir)
    {
        dir = static_cast<Direction>(segmentDirectionGenerator(engine));
        noDir = (dir==forbiddenDir);
    }
    return dir;
}


TravelerSegment newTravelerSegment(const TravelerSegment& currentSeg, bool& canAdd)
{
    TravelerSegment newSeg;
    switch (currentSeg.dir)
    {
        case Direction::NORTH:
            if (    currentSeg.row < numRows-1 &&
                    grid[currentSeg.row+1][currentSeg.col] == SquareType::FREE_SQUARE){
                newSeg.row = currentSeg.row+1;
                newSeg.col = currentSeg.col;
                newSeg.dir = newDirection(Direction::SOUTH);
                grid[newSeg.row][newSeg.col] = SquareType::TRAVELER;
                canAdd = true;
            }
            //    no more segment
            else
                canAdd = false;
            break;

        case Direction::SOUTH:
            if (    currentSeg.row > 0 &&
                    grid[currentSeg.row-1][currentSeg.col] == SquareType::FREE_SQUARE)
            {
                newSeg.row = currentSeg.row-1;
                newSeg.col = currentSeg.col;
                newSeg.dir = newDirection(Direction::NORTH);
                grid[newSeg.row][newSeg.col] = SquareType::TRAVELER;
                canAdd = true;
            }
            //    no more segment
            else
                canAdd = false;
            break;

        case Direction::WEST:
            if (    currentSeg.col < numCols-1 &&
                    grid[currentSeg.row][currentSeg.col+1] == SquareType::FREE_SQUARE)
            {
                newSeg.row = currentSeg.row;
                newSeg.col = currentSeg.col+1;
                newSeg.dir = newDirection(Direction::EAST);
                grid[newSeg.row][newSeg.col] = SquareType::TRAVELER;
                canAdd = true;
            }
            //    no more segment
            else
                canAdd = false;
            break;

        case Direction::EAST:
            if (    currentSeg.col > 0 &&
                    grid[currentSeg.row][currentSeg.col-1] == SquareType::FREE_SQUARE)
            {
                newSeg.row = currentSeg.row;
                newSeg.col = currentSeg.col-1;
                newSeg.dir = newDirection(Direction::WEST);
                grid[newSeg.row][newSeg.col] = SquareType::TRAVELER;
                canAdd = true;
            }
            //    no more segment
            else
                canAdd = false;
            break;
        
        default:
            canAdd = false;
    }
    
    return newSeg;
}

void generateWalls(void)
{
    const unsigned int NUM_WALLS = (numCols+numRows)/4;

    //    I decide that a wall length  cannot be less than 3  and not more than
    //    1/4 the grid dimension in its Direction
    const unsigned int MIN_WALL_LENGTH = 3;
    const unsigned int MAX_HORIZ_WALL_LENGTH = numCols / 3;
    const unsigned int MAX_VERT_WALL_LENGTH = numRows / 3;
    const unsigned int MAX_NUM_TRIES = 20;

    bool goodWall = true;
    
    //    Generate the vertical walls
    for (unsigned int w=0; w< NUM_WALLS; w++)
    {
        goodWall = false;
        
        //    Case of a vertical wall
        if (headsOrTails(engine))
        {
            //    I try a few times before giving up
            for (unsigned int k=0; k<MAX_NUM_TRIES && !goodWall; k++)
            {
                //    let's be hopeful
                goodWall = true;
                
                //    select a column index
                unsigned int HSP = numCols/(NUM_WALLS/2+1);
                unsigned int col = (1+ unsignedNumberGenerator(engine)%(NUM_WALLS/2-1))*HSP;
                unsigned int length = MIN_WALL_LENGTH + unsignedNumberGenerator(engine)%(MAX_VERT_WALL_LENGTH-MIN_WALL_LENGTH+1);
                
                //    now a random start row
                unsigned int startRow = unsignedNumberGenerator(engine)%(numRows-length);
                for (unsigned int row=startRow, i=0; i<length && goodWall; i++, row++)
                {
                    if (grid[row][col] != SquareType::FREE_SQUARE)
                        goodWall = false;
                }
                
                //    if the wall first, add it to the grid
                if (goodWall)
                {
                    for (unsigned int row=startRow, i=0; i<length && goodWall; i++, row++)
                    {
                        grid[row][col] = SquareType::WALL;
                    }
                }
            }
        }
        // case of a horizontal wall
        else
        {
            goodWall = false;
            
            //    I try a few times before giving up
            for (unsigned int k=0; k<MAX_NUM_TRIES && !goodWall; k++)
            {
                //    let's be hopeful
                goodWall = true;
                
                //    select a column index
                unsigned int VSP = numRows/(NUM_WALLS/2+1);
                unsigned int row = (1+ unsignedNumberGenerator(engine)%(NUM_WALLS/2-1))*VSP;
                unsigned int length = MIN_WALL_LENGTH + unsignedNumberGenerator(engine)%(MAX_HORIZ_WALL_LENGTH-MIN_WALL_LENGTH+1);
                
                //    now a random start row
                unsigned int startCol = unsignedNumberGenerator(engine)%(numCols-length);
                for (unsigned int col=startCol, i=0; i<length && goodWall; i++, col++)
                {
                    if (grid[row][col] != SquareType::FREE_SQUARE)
                        goodWall = false;
                }
                
                //    if the wall first, add it to the grid
                if (goodWall)
                {
                    for (unsigned int col=startCol, i=0; i<length && goodWall; i++, col++)
                    {
                        grid[row][col] = SquareType::WALL;
                    }
                }
            }
        }
    }
}


void generatePartitions(void)
{
    const unsigned int NUM_PARTS = (numCols+numRows)/4;

    //    I decide that a partition length  cannot be less than 3  and not more than
    //    1/4 the grid dimension in its Direction
    const unsigned int MIN_PARTITION_LENGTH = 3;
    const unsigned int MAX_HORIZ_PART_LENGTH = numCols / 3;
    const unsigned int MAX_VERT_PART_LENGTH = numRows / 3;
    const unsigned int MAX_NUM_TRIES = 20;

    bool goodPart = true;

    for (unsigned int w=0; w< NUM_PARTS; w++)
    {
        goodPart = false;
        
        //    Case of a vertical partition
        if (headsOrTails(engine))
        {
            //    I try a few times before giving up
            for (unsigned int k=0; k<MAX_NUM_TRIES && !goodPart; k++)
            {
                //    let's be hopeful
                goodPart = true;
                
                //    select a column index
                unsigned int HSP = numCols/(NUM_PARTS/2+1);
                unsigned int col = (1+ unsignedNumberGenerator(engine)%(NUM_PARTS/2-2))*HSP + HSP/2;
                unsigned int length = MIN_PARTITION_LENGTH + unsignedNumberGenerator(engine)%(MAX_VERT_PART_LENGTH-MIN_PARTITION_LENGTH+1);
                
                //    now a random start row
                unsigned int startRow = unsignedNumberGenerator(engine)%(numRows-length);
                for (unsigned int row=startRow, i=0; i<length && goodPart; i++, row++)
                {
                    if (grid[row][col] != SquareType::FREE_SQUARE)
                        goodPart = false;
                }
                
                //    if the partition is possible,
                if (goodPart)
                {
                    //    add it to the grid and to the partition list
                    SlidingPartition part;
                    part.isVertical = true;
                    for (unsigned int row=startRow, i=0; i<length && goodPart; i++, row++)
                    {
                        grid[row][col] = SquareType::VERTICAL_PARTITION;
                        GridPosition pos = {row, col};
                        part.blockList.push_back(pos);
                    }
                }
            }
        }
        // case of a horizontal partition
        else
        {
            goodPart = false;
            
            //    I try a few times before giving up
            for (unsigned int k=0; k<MAX_NUM_TRIES && !goodPart; k++)
            {
                //    let's be hopeful
                goodPart = true;
                
                //    select a column index
                unsigned int VSP = numRows/(NUM_PARTS/2+1);
                unsigned int row = (1+ unsignedNumberGenerator(engine)%(NUM_PARTS/2-2))*VSP + VSP/2;
                unsigned int length = MIN_PARTITION_LENGTH + unsignedNumberGenerator(engine)%(MAX_HORIZ_PART_LENGTH-MIN_PARTITION_LENGTH+1);
                
                //    now a random start row
                unsigned int startCol = unsignedNumberGenerator(engine)%(numCols-length);
                for (unsigned int col=startCol, i=0; i<length && goodPart; i++, col++)
                {
                    if (grid[row][col] != SquareType::FREE_SQUARE)
                        goodPart = false;
                }
                
                //    if the wall first, add it to the grid and build SlidingPartition object
                if (goodPart)
                {
                    SlidingPartition part;
                    part.isVertical = false;
                    for (unsigned int col=startCol, i=0; i<length && goodPart; i++, col++)
                    {
                        grid[row][col] = SquareType::HORIZONTAL_PARTITION;
                        GridPosition pos = {row, col};
                        part.blockList.push_back(pos);
                    }
                }
            }
        }
    }
}

