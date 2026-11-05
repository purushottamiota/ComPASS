#include "LPDDR5.h"
#include "DRAM.h"

#include <vector>
#include <functional>
#include <cassert>

using namespace std;
using namespace ramulator;

string LPDDR5::standard_name = "LPDDR5";
string LPDDR5::level_str [int(Level::MAX)] = {"Ch", "Ra", "Ba", "Ro", "Co"};

map<string, enum LPDDR5::Org> LPDDR5::org_map = {
    {"LPDDR5_8Gb_x16", LPDDR5::Org::LPDDR5_8Gb_x16},
};

map<string, enum LPDDR5::Speed> LPDDR5::speed_map = {
    {"LPDDR5_6400", LPDDR5::Speed::LPDDR5_6400},
};

LPDDR5::LPDDR5(Org org, Speed speed)
    : org_entry(org_table[int(org)]),
    speed_entry(speed_table[int(speed)]),
    read_latency(speed_entry.nCL + speed_entry.nBL)
{
    init_speed();
    init_prereq();
    init_rowhit();
    init_rowopen();
    init_lambda();
    init_timing();
}

LPDDR5::LPDDR5(const string& org_str, const string& speed_str) :
    LPDDR5(org_map[org_str], speed_map[speed_str])
{
}

void LPDDR5::set_channel_number(int channel) {
  org_entry.count[int(Level::Channel)] = channel;
}

void LPDDR5::set_rank_number(int rank) {
  org_entry.count[int(Level::Rank)] = rank;
}

void LPDDR5::init_speed()
{
    speed_entry.nRFCpb = 320;
    speed_entry.nRFCab = 672;
    speed_entry.nREFI = 12480;
    speed_entry.nXSR = 696;
}

void LPDDR5::init_prereq()
{
    // RD
    prereq[int(Level::Rank)][int(Command::RD)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::PowerUp): return Command::MAX;
            case int(State::ActPowerDown): return Command::PDX;
            case int(State::PrePowerDown): return Command::PDX;
            case int(State::SelfRefresh): return Command::SREFX;
            default: assert(false);
        }};
    prereq[int(Level::Bank)][int(Command::RD)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::Closed): return Command::ACT;
            case int(State::Opened):
                if (node->row_state.find(id) != node->row_state.end())
                    return cmd;
                return Command::PRE;
            default: assert(false);
        }};

    // WR
    prereq[int(Level::Rank)][int(Command::WR)] = prereq[int(Level::Rank)][int(Command::RD)];
    prereq[int(Level::Bank)][int(Command::WR)] = prereq[int(Level::Bank)][int(Command::RD)];

    // REF
    prereq[int(Level::Rank)][int(Command::REF)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        for (auto bank : node->children) {
            if (bank->state == State::Opened)
                return Command::PREA;
        }
        return Command::REF;
    };

    // REFPB
    prereq[int(Level::Bank)][int(Command::REFPB)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        if (node->state == State::Opened)
            return Command::PRE;
        return Command::REFPB;
    };

    // PD
    prereq[int(Level::Rank)][int(Command::PDE)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::PowerUp): return Command::PDE;
            case int(State::ActPowerDown): return Command::PDE;
            case int(State::PrePowerDown): return Command::PDE;
            case int(State::SelfRefresh): return Command::SREFX;
            default: assert(false);
        }};

    // SR
    prereq[int(Level::Rank)][int(Command::SREF)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::PowerUp): return Command::SREF;
            case int(State::ActPowerDown): return Command::PDX;
            case int(State::PrePowerDown): return Command::PDX;
            case int(State::SelfRefresh): return Command::SREF;
            default: assert(false);
        }};
}

void LPDDR5::init_rowhit()
{
    // RD
    rowhit[int(Level::Bank)][int(Command::RD)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::Closed): return false;
            case int(State::Opened):
                if (node->row_state.find(id) != node->row_state.end())
                    return true;
                return false;
            default: assert(false);
        }};

    // WR
    rowhit[int(Level::Bank)][int(Command::WR)] = rowhit[int(Level::Bank)][int(Command::RD)];
}

void LPDDR5::init_rowopen()
{
    // RD
    rowopen[int(Level::Bank)][int(Command::RD)] = [] (DRAM<LPDDR5>* node, Command cmd, int id) {
        switch (int(node->state)) {
            case int(State::Closed): return false;
            case int(State::Opened): return true;
            default: assert(false);
        }};

    // WR
    rowopen[int(Level::Bank)][int(Command::WR)] = rowopen[int(Level::Bank)][int(Command::RD)];
}

void LPDDR5::init_lambda()
{
    lambda[int(Level::Bank)][int(Command::ACT)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::Opened;
        node->row_state[id] = State::Opened;};
    lambda[int(Level::Bank)][int(Command::PRE)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::Closed;
        node->row_state.clear();};
    lambda[int(Level::Rank)][int(Command::PREA)] = [] (DRAM<LPDDR5>* node, int id) {
        for (auto bank : node->children) {
            bank->state = State::Closed;
            bank->row_state.clear();
        }};
    lambda[int(Level::Bank)][int(Command::RDA)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::Closed;
        node->row_state.clear();};
    lambda[int(Level::Bank)][int(Command::WRA)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::Closed;
        node->row_state.clear();};
    lambda[int(Level::Rank)][int(Command::PDE)] = [] (DRAM<LPDDR5>* node, int id) {
        for (auto bank : node->children) {
            if (bank->state == State::Opened) {
                node->state = State::ActPowerDown;
                return;
            }
        }
        node->state = State::PrePowerDown;};
    lambda[int(Level::Rank)][int(Command::PDX)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::PowerUp;};
    lambda[int(Level::Rank)][int(Command::SREF)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::SelfRefresh;};
    lambda[int(Level::Rank)][int(Command::SREFX)] = [] (DRAM<LPDDR5>* node, int id) {
        node->state = State::PowerUp;};
}

void LPDDR5::init_timing()
{
    SpeedEntry& s = speed_entry;
    vector<TimingEntry>* t;

    /*** Channel ***/
    t = timing[int(Level::Channel)];

    // CAS <-> CAS
    t[int(Command::RD)].push_back({Command::RD, 1, s.nBL});
    t[int(Command::RD)].push_back({Command::RDA, 1, s.nBL});
    t[int(Command::RDA)].push_back({Command::RD, 1, s.nBL});
    t[int(Command::RDA)].push_back({Command::RDA, 1, s.nBL});
    t[int(Command::WR)].push_back({Command::WR, 1, s.nBL});
    t[int(Command::WR)].push_back({Command::WRA, 1, s.nBL});
    t[int(Command::WRA)].push_back({Command::WR, 1, s.nBL});
    t[int(Command::WRA)].push_back({Command::WRA, 1, s.nBL});

    /*** Rank ***/
    t = timing[int(Level::Rank)];

    // CAS <-> CAS
    t[int(Command::RD)].push_back({Command::RD, 1, s.nCCD});
    t[int(Command::RD)].push_back({Command::RDA, 1, s.nCCD});
    t[int(Command::RDA)].push_back({Command::RD, 1, s.nCCD});
    t[int(Command::RDA)].push_back({Command::RDA, 1, s.nCCD});
    t[int(Command::WR)].push_back({Command::WR, 1, s.nCCD});
    t[int(Command::WR)].push_back({Command::WRA, 1, s.nCCD});
    t[int(Command::WRA)].push_back({Command::WR, 1, s.nCCD});
    t[int(Command::WRA)].push_back({Command::WRA, 1, s.nCCD});

    t[int(Command::RD)].push_back({Command::WR, 1, s.nCL + s.nBL + s.nDQSCK + 1 - s.nCWL});
    t[int(Command::RD)].push_back({Command::WRA, 1, s.nCL + s.nBL + s.nDQSCK + 1 - s.nCWL});
    t[int(Command::RDA)].push_back({Command::WR, 1, s.nCL + s.nBL + s.nDQSCK + 1 - s.nCWL});
    t[int(Command::RDA)].push_back({Command::WRA, 1, s.nCL + s.nBL + s.nDQSCK + 1 - s.nCWL});

    t[int(Command::WR)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nWTR + 1});
    t[int(Command::WR)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nWTR + 1});
    t[int(Command::WRA)].push_back({Command::RD, 1, s.nCWL + s.nBL + s.nWTR + 1});
    t[int(Command::WRA)].push_back({Command::RDA, 1, s.nCWL + s.nBL + s.nWTR + 1});

    // CAS <-> CAS (between sibling ranks)
    t[int(Command::RD)].push_back({Command::RD, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::RDA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::RD, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::RDA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::WR, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RD)].push_back({Command::WRA, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::WR, 1, s.nBL + s.nRTRS, true});
    t[int(Command::RDA)].push_back({Command::WRA, 1, s.nBL + s.nRTRS, true});

    // CAS <-> PREA
    t[int(Command::RD)].push_back({Command::PREA, 1, s.nRTP});
    t[int(Command::RDA)].push_back({Command::PREA, 1, s.nRTP});
    t[int(Command::WR)].push_back({Command::PREA, 1, s.nCWL + s.nBL + s.nWR});
    t[int(Command::WRA)].push_back({Command::PREA, 1, s.nCWL + s.nBL + s.nWR});

    // RAS <-> RAS
    t[int(Command::ACT)].push_back({Command::ACT, 1, s.nRRD});
    t[int(Command::ACT)].push_back({Command::ACT, 4, s.nFAW});
    t[int(Command::ACT)].push_back({Command::PREA, 1, s.nRAS});
    t[int(Command::PREA)].push_back({Command::ACT, 1, s.nRPab});
    t[int(Command::PREA)].push_back({Command::PREA, 1, s.nRPab});
    t[int(Command::PREA)].push_back({Command::PRE, 1, s.nRPab});
    t[int(Command::PRE)].push_back({Command::PREA, 1, s.nRPpb});
    t[int(Command::PRE)].push_back({Command::PRE, 1, s.nPPD});

    // RAS <-> REF
    t[int(Command::PRE)].push_back({Command::REF, 1, s.nRPpb});
    t[int(Command::PREA)].push_back({Command::REF, 1, s.nRPab});
    t[int(Command::REF)].push_back({Command::ACT, 1, s.nRFCab});

    /*** Bank ***/
    t = timing[int(Level::Bank)];

    // CAS <-> RAS
    t[int(Command::ACT)].push_back({Command::RD, 1, s.nRCD});
    t[int(Command::ACT)].push_back({Command::RDA, 1, s.nRCD});
    t[int(Command::ACT)].push_back({Command::WR, 1, s.nRCD});
    t[int(Command::ACT)].push_back({Command::WRA, 1, s.nRCD});

    t[int(Command::RD)].push_back({Command::PRE, 1, s.nRTP});
    t[int(Command::WR)].push_back({Command::PRE, 1, s.nCWL + s.nBL + s.nWR});

    t[int(Command::RDA)].push_back({Command::ACT, 1, s.nRTP + s.nRPpb});
    t[int(Command::WRA)].push_back({Command::ACT, 1, s.nCWL + s.nBL + s.nWR + s.nRPpb});

    // RAS <-> RAS
    t[int(Command::ACT)].push_back({Command::ACT, 1, s.nRC});
    t[int(Command::ACT)].push_back({Command::PRE, 1, s.nRAS});
    t[int(Command::PRE)].push_back({Command::ACT, 1, s.nRPpb});
    t[int(Command::PRE)].push_back({Command::REFPB, 1, s.nRPpb});

    // between different banks
    t[int(Command::ACT)].push_back({Command::REFPB, 1, s.nRRD, true});
    t[int(Command::REFPB)].push_back({Command::ACT, 1, s.nRRD, true});

    // REFPB
    t[int(Command::REFPB)].push_back({Command::REFPB, 1, s.nRFCpb});
    t[int(Command::REFPB)].push_back({Command::ACT, 1, s.nRFCpb});
}
