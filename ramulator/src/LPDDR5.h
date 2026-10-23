#ifndef __LPDDR5_H
#define __LPDDR5_H

#include "DRAM.h"
#include "Request.h"
#include <vector>
#include <functional>

using namespace std;

namespace ramulator
{

class LPDDR5
{
public:
    static string standard_name;
    enum class Org;
    enum class Speed;
    LPDDR5(Org org, Speed speed);
    LPDDR5(const string& org_str, const string& speed_str);
    
    static map<string, enum Org> org_map;
    static map<string, enum Speed> speed_map;

    /* Level */
    enum class Level : int
    { 
        Channel, Rank, Bank, Row, Column, MAX
    };
    
    static std::string level_str [int(Level::MAX)];

    /* Command */
    enum class Command : int
    { 
        ACT, PRE, PREA, 
        RD,  WR,  RDA,  WRA, 
        REF, REFPB, PDE, PDX, SREF, SREFX, 
        MAX
    };

    string command_name[int(Command::MAX)] = {
        "ACT", "PRE", "PREA", 
        "RD",  "WR",  "RDA",  "WRA", 
        "REF", "REFPB", "PDE", "PDX", "SREF", "SREFX"
    };

    Level scope[int(Command::MAX)] = {
        Level::Row,    Level::Bank,   Level::Rank,   
        Level::Column, Level::Column, Level::Column, Level::Column,
        Level::Rank,   Level::Bank,   Level::Rank,   Level::Rank,   Level::Rank,   Level::Rank
    };

    bool is_opening(Command cmd) 
    {
        switch(int(cmd)) {
            case int(Command::ACT):
                return true;
            default:
                return false;
        }
    }

    bool is_accessing(Command cmd) 
    {
        switch(int(cmd)) {
            case int(Command::RD):
            case int(Command::WR):
            case int(Command::RDA):
            case int(Command::WRA):
                return true;
            default:
                return false;
        }
    }

    bool is_closing(Command cmd) 
    {
        switch(int(cmd)) {
            case int(Command::RDA):
            case int(Command::WRA):
            case int(Command::PRE):
            case int(Command::PREA):
                return true;
            default:
                return false;
        }
    }

    bool is_refreshing(Command cmd) 
    {
        switch(int(cmd)) {
            case int(Command::REF):
            case int(Command::REFPB):
                return true;
            default:
                return false;
        }
    }

    /* State */
    enum class State : int
    {
        Opened, Closed, PowerUp, ActPowerDown, PrePowerDown, SelfRefresh, MAX
    } start[int(Level::MAX)] = {
        State::MAX, State::PowerUp, State::Closed, State::Closed, State::MAX
    };

    /* Translate */
    Command translate[int(Request::Type::MAX)] = {
        Command::RD,  Command::WR,
        Command::REF, Command::PDE, Command::SREF,
        Command::RD,  // EXTENSION
        Command::RD,  // PIM_READ
        Command::WR   // PIM_WRITE
    };

    function<Command(DRAM<LPDDR5>*, Command cmd, int)> prereq[int(Level::MAX)][int(Command::MAX)];

    // SAUGATA: added functions to check for row hits and open rows
    /* Rowhit */
    function<bool(DRAM<LPDDR5>*, Command cmd, int)> rowhit[int(Level::MAX)][int(Command::MAX)];
    function<bool(DRAM<LPDDR5>*, Command cmd, int)> rowopen[int(Level::MAX)][int(Command::MAX)];

    /* Timing */
    struct TimingEntry
    {
        Command cmd;
        int dist;
        int val;
        bool sibling;
    }; 
    vector<TimingEntry> timing[int(Level::MAX)][int(Command::MAX)];

    /* Lambda */
    function<void(DRAM<LPDDR5>*, int)> lambda[int(Level::MAX)][int(Command::MAX)];

    /* Organization */
    enum class Org : int
    {
        LPDDR5_8Gb_x16,
        MAX
    };

    struct OrgEntry {
        int size;
        int dq;
        int count[int(Level::MAX)];
    } org_table[int(Org::MAX)] = {
        // 8Gb per rank, 16 dq, 16 banks, 65536 rows, 64 columns (2KB row size: 64 cols * 32B access = 2048B)
        {1024, 16, {0, 0, 16, 1<<16, 64}},
    }, org_entry;

    void set_channel_number(int channel);
    void set_rank_number(int rank);

    /* Speed */
    enum class Speed : int
    {
        LPDDR5_6400,
        MAX
    };

    enum class RefreshMode : int
    {
        Refresh_1X,
        Refresh_2X,
        Refresh_4X,
        MAX
    } refresh_mode = RefreshMode::Refresh_1X;

    int prefetch_size = 16; // 16n prefetch DDR
    int channel_width = 16; // 16-bit channel width, 32 B access granularity with BL16

    struct SpeedEntry {
        int rate;
        double freq, tCK;
        int nBL, nCCD, nRTRS, nDQSCK;
        int nCL, nRCD, nRPpb, nRPab, nCWL;
        int nRAS, nRC;
        int nRTP, nWTR, nWR;
        int nPPD, nRRD, nFAW;
        int nRFCab, nRFCpb, nREFI;
        int nCKE, nXP;
        int nSR, nXSR;
    } speed_table[int(Speed::MAX)] = {
        // LPDDR5-6400: rate 6400, freq 3200MHz, tCK 0.3125ns
        {6400, 3200.0, 0.3125, 8, 4, 2, 2, 58, 58, 58, 64, 30, 134, 192, 24, 24, 60, 4, 16, 64, 672, 320, 12480, 15, 24, 32, 696}
    }, speed_entry;

    int read_latency;

private:
    void init_speed();
    void init_lambda();
    void init_prereq();
    void init_rowhit();
    void init_rowopen();
    void init_timing();
};

} /*namespace ramulator*/

#endif /*__LPDDR5_H*/
