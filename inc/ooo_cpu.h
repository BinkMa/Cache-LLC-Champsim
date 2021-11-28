#ifndef OOO_CPU_H
#define OOO_CPU_H

#include <array>
#include <bitset>
#include <queue>
#include <functional>
#include <queue>

#include "champsim_constants.h"
#include "delay_queue.hpp"
#include "instruction.h"
#include "instruction.h"
#include "operable.h"
#include "ptw.h"

using namespace std;

class CACHE;

class CacheBus : public MemoryRequestProducer
{
    public:
        champsim::circular_buffer<PACKET> PROCESSED;
        CacheBus(std::size_t q_size, MemoryRequestConsumer *ll) : MemoryRequestProducer(ll), PROCESSED(q_size) {}
        void return_data(PACKET *packet);
};

// cpu
class O3_CPU : public champsim::operable {
  public:
    uint32_t cpu = 0;
    bool operated = false;

    // instruction
    uint64_t instr_unique_id = 0, completed_executions = 0,
             begin_sim_cycle = 0, begin_sim_instr = 0,
             last_sim_cycle = 0, last_sim_instr = 0,
             finish_sim_cycle = 0, finish_sim_instr = 0,
             instrs_to_read_this_cycle = 0, instrs_to_fetch_this_cycle = 0,
             next_print_instruction = STAT_PRINTING_PERIOD, num_retired = 0;
    uint32_t inflight_reg_executions = 0, inflight_mem_executions = 0;

    struct dib_entry_t
    {
        bool valid = false;
        unsigned lru = 999999;
        uint64_t address = 0;
    };

    // instruction buffer
    using dib_t= std::vector<dib_entry_t>;
    const std::size_t dib_set, dib_way, dib_window;
    dib_t DIB{dib_set*dib_way};

    // reorder buffer, load/store queue, register file
    champsim::circular_buffer<ooo_model_instr> IFETCH_BUFFER;
    champsim::delay_queue<ooo_model_instr> DISPATCH_BUFFER;
    champsim::delay_queue<ooo_model_instr> DECODE_BUFFER;
    champsim::circular_buffer<ooo_model_instr> ROB;
    std::vector<LSQ_ENTRY> LQ;
    std::vector<LSQ_ENTRY> SQ;

    // Constants
    const unsigned FETCH_WIDTH, DECODE_WIDTH, DISPATCH_WIDTH, SCHEDULER_SIZE, EXEC_WIDTH, LQ_WIDTH, SQ_WIDTH, RETIRE_WIDTH;
    const unsigned BRANCH_MISPREDICT_PENALTY, SCHEDULING_LATENCY, EXEC_LATENCY;

    // store array, this structure is required to properly handle store instructions
    std::queue<uint64_t> STA;

    // Ready-To-Execute
    std::queue<champsim::circular_buffer<ooo_model_instr>::iterator> ready_to_execute;

    // Ready-To-Load
    std::queue<std::vector<LSQ_ENTRY>::iterator> RTL0, RTL1;

    // Ready-To-Store
    std::queue<std::vector<LSQ_ENTRY>::iterator> RTS0, RTS1;

    // branch
    int branch_mispredict_stall_fetch = 0; // flag that says that we should stall because a branch prediction was wrong
    int mispredicted_branch_iw_index = 0; // index in the instruction window of the mispredicted branch.  fetch resumes after the instruction at this index executes
    uint8_t  fetch_stall = 0;
    uint64_t fetch_resume_cycle = 0;
    uint64_t num_branch = 0, branch_mispredictions = 0;
    uint64_t total_rob_occupancy_at_branch_mispredict;

    uint64_t total_branch_types[8] = {};
    uint64_t branch_type_misses[8] = {};

    CacheBus ITLB_bus, DTLB_bus, L1I_bus, L1D_bus;
  
	PageTableWalker *PTW;

    void operate();

    // functions
    void init_instruction(ooo_model_instr instr);
    void check_dib(),
         translate_fetch(),
         fetch_instruction(),
         promote_to_decode(),
         decode_instruction(),
         dispatch_instruction(),
         schedule_instruction(),
         execute_instruction(),
         schedule_memory_instruction(),
         execute_memory_instruction(),
         do_check_dib(ooo_model_instr &instr),
         do_translate_fetch(champsim::circular_buffer<ooo_model_instr>::iterator begin, champsim::circular_buffer<ooo_model_instr>::iterator end),
         do_fetch_instruction(champsim::circular_buffer<ooo_model_instr>::iterator begin, champsim::circular_buffer<ooo_model_instr>::iterator end),
         do_dib_update(const ooo_model_instr &instr),
         do_scheduling(champsim::circular_buffer<ooo_model_instr>::iterator rob_it),
         do_execution(champsim::circular_buffer<ooo_model_instr>::iterator rob_it),
         do_memory_scheduling(champsim::circular_buffer<ooo_model_instr>::iterator rob_it),
         operate_lsq(),
         do_complete_execution(champsim::circular_buffer<ooo_model_instr>::iterator rob_it),
         do_sq_forward_to_lq(LSQ_ENTRY &sq_entry, LSQ_ENTRY &lq_entry);

    void initialize_core();
    void add_load_queue(champsim::circular_buffer<ooo_model_instr>::iterator rob_index, uint32_t data_index),
         add_store_queue(champsim::circular_buffer<ooo_model_instr>::iterator rob_index, uint32_t data_index);
    void execute_store(std::vector<LSQ_ENTRY>::iterator sq_it);
    int  execute_load(std::vector<LSQ_ENTRY>::iterator lq_it);
    int  do_translate_store(std::vector<LSQ_ENTRY>::iterator sq_it);
    int  do_translate_load(std::vector<LSQ_ENTRY>::iterator sq_it);
    void check_dependency(int prior, int current);
    void operate_cache();
    void complete_inflight_instruction();
    void handle_memory_return();
    void retire_rob();

    int prefetch_code_line(uint64_t pf_v_addr);

#include "ooo_cpu_modules.inc"

    const std::bitset<NUM_BRANCH_MODULES> bpred_type;
    const int btb_type;
    const int ipref_type;

    O3_CPU(uint32_t cpu, double freq_scale, std::size_t dib_set, std::size_t dib_way, std::size_t dib_window,
            std::size_t ifetch_buffer_size, std::size_t decode_buffer_size, std::size_t dispatch_buffer_size,
            std::size_t rob_size, std::size_t lq_size, std::size_t sq_size,
            unsigned fetch_width, unsigned decode_width, unsigned dispatch_width, unsigned schedule_width,
            unsigned execute_width, unsigned lq_width, unsigned sq_width, unsigned retire_width,
            unsigned mispredict_penalty, unsigned decode_latency, unsigned dispatch_latency, unsigned schedule_latency, unsigned execute_latency,
            CACHE *itlb, CACHE *dtlb, CACHE *l1i, CACHE *l1d, PageTableWalker *ptw,
            std::bitset<NUM_BRANCH_MODULES> bpred_type, int btb_type, int ipref_type
            ) :
        champsim::operable(freq_scale), cpu(cpu), dib_set(dib_set), dib_way(dib_way), dib_window(dib_window),
        IFETCH_BUFFER(ifetch_buffer_size), DISPATCH_BUFFER(dispatch_buffer_size, dispatch_latency), DECODE_BUFFER(decode_buffer_size, decode_latency),
        ROB(rob_size), LQ(lq_size), SQ(sq_size),
        FETCH_WIDTH(fetch_width), DECODE_WIDTH(decode_width), DISPATCH_WIDTH(dispatch_width), SCHEDULER_SIZE(schedule_width),
        EXEC_WIDTH(execute_width), LQ_WIDTH(lq_width), SQ_WIDTH(sq_width), RETIRE_WIDTH(retire_width),
        BRANCH_MISPREDICT_PENALTY(mispredict_penalty), SCHEDULING_LATENCY(schedule_latency), EXEC_LATENCY(execute_latency),
        ITLB_bus(rob_size, (MemoryRequestConsumer*) itlb), DTLB_bus(rob_size, (MemoryRequestConsumer*) dtlb), L1I_bus(rob_size, (MemoryRequestConsumer*) l1i), L1D_bus(rob_size, (MemoryRequestConsumer*) l1d), PTW(ptw),
        bpred_type(bpred_type), btb_type(btb_type), ipref_type(ipref_type)
    {
        ptw->cpu = this->cpu;
    }

};

#endif

