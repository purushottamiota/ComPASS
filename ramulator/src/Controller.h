#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <list>
#include <string>
#include <vector>

#include "Config.h"
#include "DRAM.h"
#include "Refresh.h"
#include "Request.h"
#include "Scheduler.h"
#include "Statistics.h"

#include "ALDRAM.h"
#include "SALP.h"
#include "TLDRAM.h"

using namespace std;

namespace ramulator
{

    extern bool warmup_complete;

template <typename T>
class Controller
{
protected:
    // For counting bandwidth
    ScalarStat read_transaction_bytes;
    ScalarStat write_transaction_bytes;

    ScalarStat row_hits;
    ScalarStat row_misses;
    ScalarStat row_conflicts;
    VectorStat read_row_hits;
    VectorStat read_row_misses;
    VectorStat read_row_conflicts;
    VectorStat write_row_hits;
    VectorStat write_row_misses;
    VectorStat write_row_conflicts;
    ScalarStat useless_activates;

    ScalarStat read_latency_avg;
    ScalarStat read_latency_sum;

    ScalarStat req_queue_length_avg;
    ScalarStat req_queue_length_sum;
    ScalarStat read_req_queue_length_avg;
    ScalarStat read_req_queue_length_sum;
    ScalarStat write_req_queue_length_avg;
    ScalarStat write_req_queue_length_sum;

    ScalarStat pim_requests_served;
    ScalarStat pim_transaction_bytes;
    ScalarStat pim_row_hits;
    ScalarStat pim_row_misses;
    ScalarStat pim_row_conflicts;
    ScalarStat host_requests_served;

#ifndef INTEGRATED_WITH_GEM5
    VectorStat record_read_hits;
    VectorStat record_read_misses;
    VectorStat record_read_conflicts;
    VectorStat record_write_hits;
    VectorStat record_write_misses;
    VectorStat record_write_conflicts;
#endif

public:
    /* Member Variables */
    long clk = 0;
    DRAM<T>* channel;

    Scheduler<T>* scheduler;  // determines the highest priority request whose commands will be issued
    RowPolicy<T>* rowpolicy;  // determines the row-policy (e.g., closed-row vs. open-row)
    RowTable<T>* rowtable;  // tracks metadata about rows (e.g., which are open and for how long)
    Refresh<T>* refresh;

    struct Queue {
        list<Request> q;
        unsigned int max = 32;
        unsigned int size() {return q.size();}
    };

    Queue readq;  // queue for read requests
    Queue writeq;  // queue for write requests
    Queue actq; // read and write requests for which activate was issued are moved to 
                   // actq, which has higher priority than readq and writeq.
                   // This is an optimization
                   // for avoiding useless activations (i.e., PRECHARGE
                   // after ACTIVATE w/o READ of WRITE command)
    Queue otherq;  // queue for all "other" requests (e.g., refresh)
    Queue pim_q;   // separate 2-entry PIM request queue

    enum class PolicyType {
        FR_FCFS,
        BATCH,
        ST_BLC,
        AT_BLC,
        E_BLC
    } policy_type = PolicyType::FR_FCFS;


    struct PIMMacroRequest {
        long id = 0;
        long row_idx = 0;
        int cols_remaining = 0;
        bool completed = false;
    };

    struct PIMRequestGenerator {
        static const int MACRO_BUFFER_SIZE = 32;
        static const int DATA_BUFFER_SIZE = 4096; // 4 KB data buffer

        vector<PIMMacroRequest> macro_buffer;
        int macro_head = 0;
        int macro_tail = 0;
        int macro_count = 0;

        long total_pim_work = 0;
        long pim_work_generated = 0;
        long pim_work_completed = 0;
        long current_macro_id = 0;
        long next_row = 0;
        int next_col = 0;
        int cols_per_row = 64; // 2KB row / 32B = 64 columns
        int channel_id = 0;
        int rank_count = 2;
        int bank_count = 16;
        int current_rank = 0;
        int current_bank = 0;
        int current_bank_idx = 0;

        uint8_t data_buffer[DATA_BUFFER_SIZE];

        void init(int chan_id, int ranks, int banks, int cols, long total_work) {
            channel_id = chan_id;
            rank_count = ranks > 0 ? ranks : 1;
            bank_count = banks > 0 ? banks : 1;
            cols_per_row = cols > 0 ? cols : 64;
            total_pim_work = total_work;
            pim_work_generated = 0;
            pim_work_completed = 0;
            current_macro_id = 0;
            next_row = 0;
            next_col = 0;
            current_rank = 0;
            current_bank = 0;
            current_bank_idx = 0;
            macro_count = 0;
            macro_head = 0;
            macro_tail = 0;
            macro_buffer.resize(MACRO_BUFFER_SIZE);
            memset(data_buffer, 0x5A, DATA_BUFFER_SIZE);
            refill_macro_buffer();
        }

        long get_buffered_work() const {
            long work = 0;
            for (int i = 0; i < macro_count; i++) {
                int idx = (macro_head + i) % MACRO_BUFFER_SIZE;
                work += macro_buffer[idx].cols_remaining;
            }
            return work;
        }

        void refill_macro_buffer() {
            while (macro_count < MACRO_BUFFER_SIZE && (pim_work_generated + get_buffered_work()) < total_pim_work) {
                macro_buffer[macro_tail].id = current_macro_id++;
                macro_buffer[macro_tail].row_idx = next_row;
                long remaining = total_pim_work - (pim_work_generated + get_buffered_work());
                int cols = (remaining > cols_per_row) ? cols_per_row : remaining;
                macro_buffer[macro_tail].cols_remaining = cols;
                macro_buffer[macro_tail].completed = false;
                macro_tail = (macro_tail + 1) % MACRO_BUFFER_SIZE;
                macro_count++;
                next_row++;
            }
        }

        bool has_work() const {
            return (macro_count > 0 || pim_work_generated < total_pim_work);
        }

        bool can_issue_micro() const {
            return (macro_count > 0 && macro_buffer[macro_head].cols_remaining > 0);
        }

        Request generate_micro_request(long clk) {
            assert(can_issue_micro());
            PIMMacroRequest& mreq = macro_buffer[macro_head];

            Request req(0, Request::Type::READ, [](Request& r){});
            req.is_pim = true;
            req.optype = 1;
            req.coreid = -1;
            req.arrive = clk;
            req.depart = 0;

            req.addr_vec.resize(int(T::Level::MAX));
            req.addr_vec[int(T::Level::Channel)] = channel_id;
            req.addr_vec[int(T::Level::Rank)] = current_rank;
            req.addr_vec[int(T::Level::Bank)] = current_bank;
            req.addr_vec[int(T::Level::Row)] = mreq.row_idx;
            req.addr_vec[int(T::Level::Column)] = next_col;

            next_col++;
            mreq.cols_remaining--;
            pim_work_generated++;

            if (mreq.cols_remaining == 0) {
                mreq.completed = true;
                macro_head = (macro_head + 1) % MACRO_BUFFER_SIZE;
                macro_count--;
                next_col = 0;
                // Bank-group aware interleaving (HetMap & OptiPIM)
                current_bank_idx = (current_bank_idx + 1) % bank_count;
                int num_bg = 4;
                current_bank = (current_bank_idx % num_bg) * (bank_count / num_bg) + (current_bank_idx / num_bg);
                if (current_bank_idx == 0) {
                    current_rank = (current_rank + 1) % rank_count;
                }
                refill_macro_buffer();
            }
            return req;
        }
    };

    PIMRequestGenerator pim_gen;
    bool pim_enabled = false;

    // Batch scheduling state (PAR-BS)
    int batch_size = 64;
    int current_batch_id = 0;
    int batch_remaining = 0;



    ScalarStat prea_count;
    bool last_was_pim = false;
    long epoch_host_served = 0;
    double blc_lambda = 0.01;
    vector<pair<long, int>> N_timeline;
    int eblc_elastic_quota = 0;

    bool tb_pim_turn = true;
    int consecutive_pim = 0;
    int consecutive_host = 0;
    int P_limit = 64;
    int N_limit = 64;
    int N_min = 16;
    int N_max = 128;
    int blc_step = 8;
    long target_time_T = 100000;
    long effective_target_T = 90000; // 0.90 * T
    long total_pim_work = 65536;
    long last_ref_epoch = 0;
    long ref_interval = 12480; // tREFI
    long pim_start_clk = -1;



    bool find_best_req(Queue* queue, list<Request>::iterator& out_req, typename T::Command& out_cmd) {
        if (!queue || queue->q.empty()) return false;

        list<Request>::iterator best_hit = queue->q.end();
        list<Request>::iterator best_other = queue->q.end();
        typename T::Command best_hit_cmd = T::Command::MAX;
        typename T::Command best_other_cmd = T::Command::MAX;

        for (auto it = queue->q.begin(); it != queue->q.end(); ++it) {
            typename T::Command c = get_first_cmd(it);
            if (is_ready(c, it->addr_vec)) {
                if (is_row_hit(it)) {
                    if (best_hit == queue->q.end() || it->arrive < best_hit->arrive) {
                        best_hit = it;
                        best_hit_cmd = c;
                    }
                } else {
                    if (best_other == queue->q.end() || it->arrive < best_other->arrive) {
                        best_other = it;
                        best_other_cmd = c;
                    }
                }
            }
        }

        if (best_hit != queue->q.end()) {
            out_req = best_hit;
            out_cmd = best_hit_cmd;
            return true;
        }
        if (best_other != queue->q.end()) {
            out_req = best_other;
            out_cmd = best_other_cmd;
            return true;
        }
        return false;
    }

    deque<Request> pending;  // read requests that are about to receive data from DRAM
    bool write_mode = false;  // whether write requests should be prioritized over reads
    float wr_high_watermark = 0.8f; // threshold for switching to write mode
    float wr_low_watermark = 0.2f; // threshold for switching back to read mode
    //long refreshed = 0;  // last time refresh requests were generated

    /* Command trace for DRAMPower 3.1 */
    string cmd_trace_prefix = "cmd-trace-";
    vector<ofstream> cmd_trace_files;
    bool record_cmd_trace = false;
    /* Commands to stdout */
    bool print_cmd_trace = false;

    /* Constructor */
    Controller(const Config& configs, DRAM<T>* channel) :
        channel(channel),
        scheduler(new Scheduler<T>(this)),
        rowpolicy(new RowPolicy<T>(this)),
        rowtable(new RowTable<T>(this)),
        refresh(new Refresh<T>(this)),
        cmd_trace_files(channel->children.size())
    {
        record_cmd_trace = configs.record_cmd_trace();
        print_cmd_trace = configs.print_cmd_trace();
        if (record_cmd_trace){
            if (configs["cmd_trace_prefix"] != "") {
              cmd_trace_prefix = configs["cmd_trace_prefix"];
            }
            string prefix = cmd_trace_prefix + "chan-" + to_string(channel->id) + "-rank-";
            string suffix = ".cmdtrace";
            for (unsigned int i = 0; i < channel->children.size(); i++)
                cmd_trace_files[i].open(prefix + to_string(i) + suffix);
        }

        // regStats

        row_hits
            .name("row_hits_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row hits per channel per core")
            .precision(0)
            ;
        row_misses
            .name("row_misses_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row misses per channel per core")
            .precision(0)
            ;
        row_conflicts
            .name("row_conflicts_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row conflicts per channel per core")
            .precision(0)
            ;

        read_row_hits
            .init(configs.get_core_num())
            .name("read_row_hits_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row hits for read requests per channel per core")
            .precision(0)
            ;
        read_row_misses
            .init(configs.get_core_num())
            .name("read_row_misses_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row misses for read requests per channel per core")
            .precision(0)
            ;
        read_row_conflicts
            .init(configs.get_core_num())
            .name("read_row_conflicts_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row conflicts for read requests per channel per core")
            .precision(0)
            ;

        write_row_hits
            .init(configs.get_core_num())
            .name("write_row_hits_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row hits for write requests per channel per core")
            .precision(0)
            ;
        write_row_misses
            .init(configs.get_core_num())
            .name("write_row_misses_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row misses for write requests per channel per core")
            .precision(0)
            ;
        write_row_conflicts
            .init(configs.get_core_num())
            .name("write_row_conflicts_channel_"+to_string(channel->id) + "_core")
            .desc("Number of row conflicts for write requests per channel per core")
            .precision(0)
            ;

        useless_activates
            .name("useless_activates_"+to_string(channel->id)+ "_core")
            .desc("Number of useless activations. E.g, ACT -> PRE w/o RD or WR")
            .precision(0)
            ;

        read_transaction_bytes
            .name("read_transaction_bytes_"+to_string(channel->id))
            .desc("The total byte of read transaction per channel")
            .precision(0)
            ;
        write_transaction_bytes
            .name("write_transaction_bytes_"+to_string(channel->id))
            .desc("The total byte of write transaction per channel")
            .precision(0)
            ;

        read_latency_sum
            .name("read_latency_sum_"+to_string(channel->id))
            .desc("The memory latency cycles (in memory time domain) sum for all read requests in this channel")
            .precision(0)
            ;
        read_latency_avg
            .name("read_latency_avg_"+to_string(channel->id))
            .desc("The average memory latency cycles (in memory time domain) per request for all read requests in this channel")
            .precision(6)
            ;

        req_queue_length_sum
            .name("req_queue_length_sum_"+to_string(channel->id))
            .desc("Sum of read and write queue length per memory cycle per channel.")
            .precision(0)
            ;
        req_queue_length_avg
            .name("req_queue_length_avg_"+to_string(channel->id))
            .desc("Average of read and write queue length per memory cycle per channel.")
            .precision(6)
            ;

        read_req_queue_length_sum
            .name("read_req_queue_length_sum_"+to_string(channel->id))
            .desc("Read queue length sum per memory cycle per channel.")
            .precision(0)
            ;
        read_req_queue_length_avg
            .name("read_req_queue_length_avg_"+to_string(channel->id))
            .desc("Read queue length average per memory cycle per channel.")
            .precision(6)
            ;

        write_req_queue_length_sum
            .name("write_req_queue_length_sum_"+to_string(channel->id))
            .desc("Write queue length sum per memory cycle per channel.")
            .precision(0)
            ;
        write_req_queue_length_avg
            .name("write_req_queue_length_avg_"+to_string(channel->id))
            .desc("Write queue length average per memory cycle per channel.")
            .precision(6)
            ;

#ifndef INTEGRATED_WITH_GEM5
        record_read_hits
            .init(configs.get_core_num())
            .name("record_read_hits")
            .desc("record read hit count for this core when it reaches request limit or to the end")
            ;

        record_read_misses
            .init(configs.get_core_num())
            .name("record_read_misses")
            .desc("record_read_miss count for this core when it reaches request limit or to the end")
            ;

        record_read_conflicts
            .init(configs.get_core_num())
            .name("record_read_conflicts")
            .desc("record read conflict count for this core when it reaches request limit or to the end")
            ;

        record_write_hits
            .init(configs.get_core_num())
            .name("record_write_hits")
            .desc("record write hit count for this core when it reaches request limit or to the end")
            ;

        record_write_misses
            .init(configs.get_core_num())
            .name("record_write_misses")
            .desc("record write miss count for this core when it reaches request limit or to the end")
            ;

        record_write_conflicts
            .init(configs.get_core_num())
            .name("record_write_conflicts")
            .desc("record write conflict for this core when it reaches request limit or to the end")
            ;
#endif

        pim_requests_served
            .name("pim_requests_served_" + to_string(channel->id))
            .desc("Total PIM requests served per channel")
            .precision(0)
            ;
        pim_transaction_bytes
            .name("pim_transaction_bytes_" + to_string(channel->id))
            .desc("Total byte of PIM transactions per channel")
            .precision(0)
            ;
        pim_row_hits
            .name("pim_row_hits_" + to_string(channel->id))
            .desc("Total PIM row hits per channel")
            .precision(0)
            ;
        pim_row_misses
            .name("pim_row_misses_" + to_string(channel->id))
            .desc("Total PIM row misses per channel")
            .precision(0)
            ;
        pim_row_conflicts
            .name("pim_row_conflicts_" + to_string(channel->id))
            .desc("Total PIM row conflicts per channel")
            .precision(0)
            ;
        host_requests_served
            .name("host_requests_served_" + to_string(channel->id))
            .desc("Total host requests served per channel")
            .precision(0)
            ;
        prea_count
            .name("prea_count_" + to_string(channel->id))
            .desc("Total PREA commands issued per channel")
            .precision(0)
            ;

        readq.max = 64;
        writeq.max = 64;
        pim_q.max = 2;

        pim_enabled = (configs["pim"] == "true" || configs["pim"] == "1");
        string pol_str = configs["scheduling_policy"];
        if (pol_str == "BATCH" || pol_str == "batch") {
            policy_type = PolicyType::BATCH;
        } else if (pol_str == "ST_BLC" || pol_str == "st_blc" || pol_str == "ST-BLC") {
            policy_type = PolicyType::ST_BLC;
        } else if (pol_str == "AT_BLC" || pol_str == "at_blc" || pol_str == "AT-BLC") {
            policy_type = PolicyType::AT_BLC;
        } else if (pol_str == "E_BLC" || pol_str == "e_blc" || pol_str == "E-BLC" || pol_str == "eblc") {
            policy_type = PolicyType::E_BLC;
        } else {
            policy_type = PolicyType::FR_FCFS;
        }

        if (configs["blc_lambda"] != "") {
            blc_lambda = stod(configs["blc_lambda"]);
        }

        if (pim_enabled) {
            long work = 65536;
            if (configs["pim_work"] != "") work = stol(configs["pim_work"]);
            total_pim_work = work;
            if (configs["pim_target_time"] != "") target_time_T = stol(configs["pim_target_time"]);
            effective_target_T = (long)(0.90 * target_time_T);
            int ranks = channel->spec->org_entry.count[int(T::Level::Rank)];
            int banks = channel->spec->org_entry.count[int(T::Level::Bank)];
            pim_gen.init(channel->id, ranks, banks, 64, total_pim_work);
        }
    }

    ~Controller(){
        delete scheduler;
        delete rowpolicy;
        delete rowtable;
        delete channel;
        delete refresh;
        for (auto& file : cmd_trace_files)
            file.close();
        cmd_trace_files.clear();
    }

    void finish(long read_req, long dram_cycles) {
      read_latency_avg = read_latency_sum.value() / read_req;
      req_queue_length_avg = req_queue_length_sum.value() / dram_cycles;
      read_req_queue_length_avg = read_req_queue_length_sum.value() / dram_cycles;
      write_req_queue_length_avg = write_req_queue_length_sum.value() / dram_cycles;
      if (!N_timeline.empty()) {
          string fname = "results/atblc_N_timeline_ch" + to_string(channel->id) + ".csv";
          ofstream n_out(fname);
          if (n_out.is_open()) {
              n_out << "cycle,N_limit\n";
              for (auto& p : N_timeline) {
                  n_out << p.first << "," << p.second << "\n";
              }
              n_out.close();
          }
      }
      // call finish function of each channel
      channel->finish(dram_cycles);
    }

    /* Member Functions */
    Queue& get_queue(Request::Type type)
    {
        switch (int(type)) {
            case int(Request::Type::READ):
            case int(Request::Type::PIM_READ): return readq;
            case int(Request::Type::WRITE):
            case int(Request::Type::PIM_WRITE): return writeq;
            default: return otherq;
        }
    }

    bool enqueue(Request& req)
    {
        Queue& queue = get_queue(req.type);
        if (queue.max == queue.size())
            return false;

        req.arrive = clk;
        queue.q.push_back(req);
        // shortcut for read requests, if a write to same addr exists
        // necessary for coherence
        if (req.type == Request::Type::READ && find_if(writeq.q.begin(), writeq.q.end(),
                [req](Request& wreq){ return req.addr == wreq.addr;}) != writeq.q.end()){
            req.depart = clk + 1;
            pending.push_back(req);
            readq.q.pop_back();
        }
        return true;
    }

    void tick()
    {
        clk++;
        req_queue_length_sum += readq.size() + writeq.size() + pending.size() + pim_q.size();
        read_req_queue_length_sum += readq.size() + pending.size();
        write_req_queue_length_sum += writeq.size();

        /*** 0. PIM request generator ***/
        if (pim_enabled && warmup_complete && pim_gen.can_issue_micro()) {
            if (policy_type == PolicyType::FR_FCFS || policy_type == PolicyType::BATCH) {
                // Shared queue per paper specification: PIM enters readq directly
                if (readq.size() < readq.max) {
                    Request preq = pim_gen.generate_micro_request(clk);
                    preq.type = Request::Type::PIM_READ;
                    preq.arrive = clk;
                    readq.q.push_back(preq);
                }
            } else {
                // Dedicated pim_q for Balancers (ST_BLC, AT_BLC, E_BLC)
                if (pim_q.size() < pim_q.max) {
                    Request preq = pim_gen.generate_micro_request(clk);
                    preq.type = Request::Type::PIM_READ;
                    preq.arrive = clk;
                    pim_q.q.push_back(preq);
                }
            }
        }

        /*** 1. Serve completed reads ***/
        if (pending.size()) {
            Request& req = pending[0];
            if (req.depart <= clk) {
                if (req.depart - req.arrive > 1) { // this request really accessed a row
                  read_latency_sum += req.depart - req.arrive;
                  channel->update_serving_requests(
                      req.addr_vec.data(), -1, clk);
                }
                if (req.is_pim_req()) {
                    pim_requests_served++;
                    pim_gen.pim_work_completed++;
                }
                req.callback(req);
                pending.pop_front();
            }
        }

        /*** 2. Refresh scheduler & AT-BLC epoch ***/
        refresh->tick_ref();

        if (warmup_complete && (policy_type == PolicyType::AT_BLC || policy_type == PolicyType::E_BLC)) {
            if (pim_start_clk < 0) {
                pim_start_clk = clk;
                last_ref_epoch = clk;
            }
            if (clk - last_ref_epoch >= ref_interval) {
                last_ref_epoch = clk;
                long active_pim_cycles = clk - pim_start_clk;
                if (active_pim_cycles > 0 && effective_target_T > 0 && total_pim_work > 0) {
                    double expected_rate = (double)total_pim_work / (double)effective_target_T;
                    double current_rate = (double)pim_gen.pim_work_completed / (double)active_pim_cycles;
                    double delta = (current_rate - expected_rate) / expected_rate;

                    double host_util = (double)epoch_host_served / (double)max(1L, (long)N_limit);
                    epoch_host_served = 0;

                    // Proportional step: scale with magnitude of delta, clamped to [4, 32]
                    int dynamic_step = max(4, min(32, (int)(fabs(delta) * 100)));

                    if (delta > blc_lambda && host_util > 0.50) {
                        N_limit = min(N_max, N_limit + dynamic_step);
                        // Adaptive Target Margin: when PIM progress is comfortably ahead,
                        // relax effective target from 0.90*T towards 0.95*T to yield more turns to the host
                        if (delta > 0.05 && effective_target_T < (long)(0.95 * target_time_T)) {
                            effective_target_T = (long)(0.95 * target_time_T);
                        }
                    } else if (delta < -blc_lambda) {
                        N_limit = max(N_min, N_limit - dynamic_step);
                        // Tighten back to conservative 90% margin if PIM falls behind schedule
                        effective_target_T = (long)(0.90 * target_time_T);
                    }
                    if (pim_gen.pim_work_completed >= total_pim_work) {
                        N_limit = N_max;
                        tb_pim_turn = false;
                    }
                    N_timeline.push_back(make_pair(clk, N_limit));
                    }
            }
        }

        /*** 3. Should we schedule writes? ***/
        if (!write_mode) {
            // yes -- write queue is almost full or read queue is empty
            if (writeq.size() > int(wr_high_watermark * writeq.max) || readq.size() == 0)
                write_mode = true;
        }
        else {
            // no -- write queue is almost empty and read queue is not empty
            if (writeq.size() < int(wr_low_watermark * writeq.max) && readq.size() != 0)
                write_mode = false;
        }

        /*** 4. Find the best command to schedule, if any ***/
        Queue* queue = nullptr;
        typename T::Command cmd;
        list<Request>::iterator req;
        bool is_valid_req = false;

        // otherq (refresh, etc) has highest priority
        if (otherq.size()) {
            req = scheduler->get_head(otherq.q);
            if (req != otherq.q.end()) {
                cmd = get_first_cmd(req);
                if (is_ready(cmd, req->addr_vec)) {
                    queue = &otherq;
                    is_valid_req = true;
                }
            }
        }

        // actq has second priority
        if (!is_valid_req && actq.size()) {
            list<Request>::iterator act_req;
            typename T::Command act_cmd;
            if (find_best_req(&actq, act_req, act_cmd)) {
                queue = &actq;
                req = act_req;
                cmd = act_cmd;
                is_valid_req = true;
            }
        }

        // Scheduling policies:
        if (!is_valid_req) {
            Queue* hostq = !write_mode ? &readq : &writeq;

            if (policy_type == PolicyType::FR_FCFS) {
                // Shared queue FR-FCFS (prioritizes row hits across host and PIM)
                list<Request>::iterator best_r;
                typename T::Command best_c;
                if (find_best_req(hostq, best_r, best_c)) {
                    queue = hostq; req = best_r; cmd = best_c; is_valid_req = true;
                }
            }
            else if (policy_type == PolicyType::BATCH) {
                // Batch (PAR-BS): default batch size 64 over shared queue
                if (batch_remaining <= 0) {
                    current_batch_id++;
                    batch_remaining = 0;
                    for (auto& r : hostq->q) {
                        if (batch_remaining >= batch_size) break;
                        r.batch_id = current_batch_id;
                        batch_remaining++;
                    }
                }

                // Look for ready requests with r.batch_id == current_batch_id
                list<Request>::iterator best_batch_req = hostq->q.end();
                typename T::Command best_batch_cmd;
                bool best_is_hit = false;

                for (auto it = hostq->q.begin(); it != hostq->q.end(); ++it) {
                    if (it->batch_id == current_batch_id) {
                        typename T::Command c = get_first_cmd(it);
                        if (is_ready(c, it->addr_vec)) {
                            bool hit = is_row_hit(it);
                            if (best_batch_req == hostq->q.end() || (hit && !best_is_hit) || ((hit == best_is_hit) && it->arrive < best_batch_req->arrive)) {
                                best_batch_req = it;
                                best_batch_cmd = c;
                                best_is_hit = hit;
                            }
                        }
                    }
                }

                if (best_batch_req != hostq->q.end()) {
                    queue = hostq;
                    req = best_batch_req;
                    cmd = best_batch_cmd;
                    is_valid_req = true;
                } else {
                    // Fallback to any ready request
                    list<Request>::iterator best_r;
                    typename T::Command best_c;
                    if (find_best_req(hostq, best_r, best_c)) {
                        queue = hostq; req = best_r; cmd = best_c; is_valid_req = true;
                    }
                }
            }
            else if (policy_type == PolicyType::E_BLC) {
                // Novel Contribution: Elastic Bank-Conflict Aware Balancer (E-BLC)
                list<Request>::iterator host_req, pim_req;
                typename T::Command host_cmd, pim_cmd;
                bool have_host = find_best_req(hostq, host_req, host_cmd);
                bool have_pim = pim_enabled && find_best_req(&pim_q, pim_req, pim_cmd);

                if (have_pim && have_host) {
                    int pim_bg = pim_req->addr_vec[int(T::Level::Bank)] / 4;
                    int host_bg = host_req->addr_vec[int(T::Level::Bank)] / 4;
                    bool host_hit = is_row_hit(host_req);
                    bool pim_hit = is_row_hit(pim_req);

                    if (pim_bg != host_bg && host_hit && !pim_hit) {
                        // Exploit Bank-Level Parallelism (BLP): schedule host without PREA penalty
                        queue = hostq; req = host_req; cmd = host_cmd; is_valid_req = true;
                        consecutive_host++;
                    } else if (tb_pim_turn) {
                        if (!pim_hit && host_hit) {
                            // Elastic quota lending: PIM lends turn slot to host
                            queue = hostq; req = host_req; cmd = host_cmd; is_valid_req = true;
                            consecutive_host++;
                            eblc_elastic_quota++;
                        } else {
                            queue = &pim_q; req = pim_req; cmd = pim_cmd; is_valid_req = true;
                            consecutive_pim++;
                        }
                    } else {
                        queue = hostq; req = host_req; cmd = host_cmd; is_valid_req = true;
                        consecutive_host++;
                    }
                } else if (have_pim) {
                    queue = &pim_q; req = pim_req; cmd = pim_cmd; is_valid_req = true;
                    consecutive_pim++;
                } else if (have_host) {
                    queue = hostq; req = host_req; cmd = host_cmd; is_valid_req = true;
                    consecutive_host++;
                }

                if (pim_gen.pim_work_completed >= total_pim_work) {
                    tb_pim_turn = false;
                } else if (tb_pim_turn && (consecutive_pim >= P_limit || pim_q.q.empty())) {
                    tb_pim_turn = false;
                    consecutive_host = 0;
                } else if (!tb_pim_turn && (consecutive_host >= (N_limit + eblc_elastic_quota) || hostq->q.empty())) {
                    tb_pim_turn = true;
                    consecutive_pim = 0;
                    eblc_elastic_quota = 0;
                }
            }
            else { // ST_BLC or AT_BLC (Throughput Balancer)
                if (pim_gen.pim_work_completed >= total_pim_work) {
                    tb_pim_turn = false;
                } else if (tb_pim_turn) {
                    bool pim_empty = pim_q.q.empty();
                    bool pim_limit_reached = (consecutive_pim >= P_limit);
                    bool pim_row_conflict = false;
                    if (!pim_q.q.empty()) {
                        auto head = pim_q.q.begin();
                        if (!is_row_hit(head) && is_row_open(head)) {
                            pim_row_conflict = true;
                        }
                    }
                    if (pim_empty || pim_limit_reached || pim_row_conflict) {
                        tb_pim_turn = false;
                        consecutive_host = 0;
                    }
                } else {
                    bool host_empty = hostq->q.empty();
                    bool host_limit_reached = (consecutive_host >= N_limit);
                    if (host_empty || host_limit_reached) {
                        tb_pim_turn = true;
                        consecutive_pim = 0;
                    }
                }

                list<Request>::iterator host_req, pim_req;
                typename T::Command host_cmd, pim_cmd;
                bool have_host = find_best_req(hostq, host_req, host_cmd);
                bool have_pim = pim_enabled && find_best_req(&pim_q, pim_req, pim_cmd);

                if (tb_pim_turn) {
                    if (have_pim) {
                        queue = &pim_q; req = pim_req; cmd = pim_cmd; is_valid_req = true;
                        consecutive_pim++;
                    } else if (have_host) {
                        queue = hostq; req = host_req; cmd = host_cmd; is_valid_req = true;
                        consecutive_host++;
                    }
                } else {
                    if (have_host) {
                        queue = hostq; req = host_req; cmd = host_cmd; is_valid_req = true;
                        consecutive_host++;
                    } else if (have_pim) {
                        queue = &pim_q; req = pim_req; cmd = pim_cmd; is_valid_req = true;
                        consecutive_pim++;
                    }
                }
            }
        }


        if (!is_valid_req) {
            // we couldn't find a command to schedule -- let's try to be speculative
            auto scmd = T::Command::PRE;
            vector<int> victim = rowpolicy->get_victim(scmd);
            if (!victim.empty()){
                issue_cmd(scmd, victim);
            }
            return;  // nothing more to be done this cycle
        }

        if (req->is_first_command) {
            req->is_first_command = false;
            int coreid = req->coreid;
            if (req->type == Request::Type::READ || req->type == Request::Type::WRITE ||
                req->type == Request::Type::PIM_READ || req->type == Request::Type::PIM_WRITE) {
              channel->update_serving_requests(req->addr_vec.data(), 1, clk);
            }
            int tx = (channel->spec->prefetch_size * channel->spec->channel_width / 8);
            if (req->is_pim_req()) {
                pim_transaction_bytes += tx;
                if (is_row_hit(req)) {
                    ++pim_row_hits;
                    ++row_hits;
                } else if (is_row_open(req)) {
                    ++pim_row_conflicts;
                    ++row_conflicts;
                } else {
                    ++pim_row_misses;
                    ++row_misses;
                }
            } else if (req->type == Request::Type::READ) {
                epoch_host_served++;
                if (coreid >= 0) {
                    if (is_row_hit(req)) {
                        ++read_row_hits[coreid];
                        ++row_hits;
                    } else if (is_row_open(req)) {
                        ++read_row_conflicts[coreid];
                        ++row_conflicts;
                    } else {
                        ++read_row_misses[coreid];
                        ++row_misses;
                    }
                }
                read_transaction_bytes += tx;
                host_requests_served++;
            } else if (req->type == Request::Type::WRITE) {
                epoch_host_served++;
                if (coreid >= 0) {
                    if (is_row_hit(req)) {
                        ++write_row_hits[coreid];
                        ++row_hits;
                    } else if (is_row_open(req)) {
                        ++write_row_conflicts[coreid];
                        ++row_conflicts;
                    } else {
                        ++write_row_misses[coreid];
                        ++row_misses;
                    }
                }
                write_transaction_bytes += tx;
                host_requests_served++;
            }
        }

        // issue command on behalf of request
        issue_cmd(cmd, get_addr_vec(cmd, req));
        if (channel->spec->is_closing(cmd) && (req->is_pim_req() || channel->spec->scope[int(cmd)] == T::Level::Rank)) {
            prea_count++;
        }
        bool current_is_pim = req->is_pim_req();
        if (last_was_pim != current_is_pim) {
            prea_count++;
            last_was_pim = current_is_pim;
        }

        // check whether this is the last command (which finishes the request)
        if (cmd != channel->spec->translate[int(req->type)]) {
            if(channel->spec->is_opening(cmd)) {
                // promote the request that caused issuing activation to actq
                actq.q.push_back(*req);
                queue->q.erase(req);
            }

            return;
        }

        // set a future completion time for read requests
        if (req->type == Request::Type::READ || req->type == Request::Type::PIM_READ) {
            req->depart = clk + channel->spec->read_latency;
            pending.push_back(*req);
        }

        if (req->type == Request::Type::WRITE || req->type == Request::Type::PIM_WRITE) {
            channel->update_serving_requests(req->addr_vec.data(), -1, clk);
            // req->callback(*req);
        }


        if (policy_type == PolicyType::BATCH && req->batch_id == current_batch_id) {
            batch_remaining--;
        }

        // remove request from queue
        queue->q.erase(req);
    }

    bool is_ready(list<Request>::iterator req)
    {
        typename T::Command cmd = get_first_cmd(req);
        return channel->check(cmd, req->addr_vec.data(), clk);
    }

    bool is_ready(typename T::Command cmd, const vector<int>& addr_vec)
    {
        return channel->check(cmd, addr_vec.data(), clk);
    }

    bool is_row_hit(list<Request>::iterator req)
    {
        // cmd must be decided by the request type, not the first cmd
        typename T::Command cmd = channel->spec->translate[int(req->type)];
        return channel->check_row_hit(cmd, req->addr_vec.data());
    }

    bool is_row_hit(typename T::Command cmd, const vector<int>& addr_vec)
    {
        return channel->check_row_hit(cmd, addr_vec.data());
    }

    bool is_row_open(list<Request>::iterator req)
    {
        // cmd must be decided by the request type, not the first cmd
        typename T::Command cmd = channel->spec->translate[int(req->type)];
        return channel->check_row_open(cmd, req->addr_vec.data());
    }

    bool is_row_open(typename T::Command cmd, const vector<int>& addr_vec)
    {
        return channel->check_row_open(cmd, addr_vec.data());
    }

    void update_temp(ALDRAM::Temp current_temperature)
    {
    }

    // For telling whether this channel is busying in processing read or write
    bool is_active() {
      return (channel->cur_serving_requests > 0);
    }

    // For telling whether this channel is under refresh
    bool is_refresh() {
      return clk <= channel->end_of_refreshing;
    }

    void set_high_writeq_watermark(const float watermark) {
       wr_high_watermark = watermark; 
    }

    void set_low_writeq_watermark(const float watermark) {
       wr_low_watermark = watermark;
    }

    void record_core(int coreid) {
#ifndef INTEGRATED_WITH_GEM5
      record_read_hits[coreid] = read_row_hits[coreid];
      record_read_misses[coreid] = read_row_misses[coreid];
      record_read_conflicts[coreid] = read_row_conflicts[coreid];
      record_write_hits[coreid] = write_row_hits[coreid];
      record_write_misses[coreid] = write_row_misses[coreid];
      record_write_conflicts[coreid] = write_row_conflicts[coreid];
#endif
    }

private:
    typename T::Command get_first_cmd(list<Request>::iterator req)
    {
        typename T::Command cmd = channel->spec->translate[int(req->type)];
        return channel->decode(cmd, req->addr_vec.data());
    }

    // upgrade to an autoprecharge command
    void cmd_issue_autoprecharge(typename T::Command& cmd,
                                            const vector<int>& addr_vec) {

        // currently, autoprecharge is only used with closed row policy
        if(channel->spec->is_accessing(cmd) && rowpolicy->type == RowPolicy<T>::Type::ClosedAP) {
            // check if it is the last request to the opened row
            Queue* queue = write_mode ? &writeq : &readq;

            auto begin = addr_vec.begin();
            vector<int> rowgroup(begin, begin + int(T::Level::Row) + 1);

			int num_row_hits = 0;

            for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr) {
                if (is_row_hit(itr)) { 
                    auto begin2 = itr->addr_vec.begin();
                    vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                    if(rowgroup == rowgroup2)
                        num_row_hits++;
                }
            }

            if(num_row_hits == 0) {
                Queue* queue = &actq;
                for (auto itr = queue->q.begin(); itr != queue->q.end(); ++itr) {
                    if (is_row_hit(itr)) {
                        auto begin2 = itr->addr_vec.begin();
                        vector<int> rowgroup2(begin2, begin2 + int(T::Level::Row) + 1);
                        if(rowgroup == rowgroup2)
                            num_row_hits++;
                    }
                }
            }

            assert(num_row_hits > 0); // The current request should be a hit, 
                                      // so there should be at least one request 
                                      // that hits in the current open row
            if(num_row_hits == 1) {
                if(cmd == T::Command::RD)
                    cmd = T::Command::RDA;
                else if (cmd == T::Command::WR)
                    cmd = T::Command::WRA;
                else
                    assert(false && "Unimplemented command type.");
            }
        }

    }

    void issue_cmd(typename T::Command cmd, const vector<int>& addr_vec)
    {
        cmd_issue_autoprecharge(cmd, addr_vec);
        assert(is_ready(cmd, addr_vec));
        channel->update(cmd, addr_vec.data(), clk);

        if(cmd == T::Command::PRE){
            if(rowtable->get_hits(addr_vec, true) == 0){
                useless_activates++;
            }
        }
 
        rowtable->update(cmd, addr_vec, clk);
        if (record_cmd_trace){
            // select rank
            auto& file = cmd_trace_files[addr_vec[1]];
            string& cmd_name = channel->spec->command_name[int(cmd)];
            file<<clk<<','<<cmd_name;
            // TODO bad coding here
            if (cmd_name == "PREA" || cmd_name == "REF")
                file<<endl;
            else{
                int bank_id = addr_vec[int(T::Level::Bank)];
                if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                    bank_id += addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                file<<','<<bank_id<<endl;
            }
        }
        if (print_cmd_trace){
            printf("%5s %10ld:", channel->spec->command_name[int(cmd)].c_str(), clk);
            for (int lev = 0; lev < int(T::Level::MAX); lev++)
                printf(" %5d", addr_vec[lev]);
            printf("\n");
        }
    }
    vector<int> get_addr_vec(typename T::Command cmd, list<Request>::iterator req){
        return req->addr_vec;
    }
};

template <>
vector<int> Controller<SALP>::get_addr_vec(
    SALP::Command cmd, list<Request>::iterator req);

template <>
bool Controller<SALP>::is_ready(list<Request>::iterator req);

template <>
void Controller<ALDRAM>::update_temp(ALDRAM::Temp current_temperature);

template <>
void Controller<TLDRAM>::tick();

template <>
void Controller<TLDRAM>::cmd_issue_autoprecharge(typename TLDRAM::Command& cmd,
                                                    const vector<int>& addr_vec);

} /*namespace ramulator*/

#endif /*__CONTROLLER_H*/
