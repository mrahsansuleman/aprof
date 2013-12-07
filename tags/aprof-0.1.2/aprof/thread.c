/*
 * Thread event handlers (start, exit, running)
 * 
 * Last changed: $Date$
 * Revision:     $Rev$
 */

/*
   This file is part of aprof, an input sensitive profiler.

   Copyright (C) 2011-2013, Emilio Coppa (ercoppa@gmail.com),
                            Camil Demetrescu,
                            Irene Finocchi,
                            Romolo Marotta

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "aprof.h"

#define OVERFLOW_DEBUG 0

/* # running threads */
UInt APROF_(running_threads) = 0;

/* All threads running */
static ThreadData * threads[VG_N_THREADS];

/* Current running thread ID */
ThreadId APROF_(current_TID) = VG_INVALID_THREADID; /* 0 */

/* Current running thread data */
ThreadData * APROF_(current_tdata) = NULL; 

static ThreadData * APROF_(thread_start)(ThreadId tid){

    #if DEBUG_ALLOCATION
    VG_(umsg)("\nThread start:\n");
    APROF_(print_alloc)();
    #endif

    #if VERBOSE
    VG_(printf)("start thread %d\n", tid);
    #endif
    
    ThreadData * tdata = VG_(calloc)("thread_data", sizeof(ThreadData), 1);
    #if DEBUG
    AP_ASSERT(tdata != NULL, "thread data not allocable");
    #endif
    
    #if DEBUG_ALLOCATION
    APROF_(add_alloc)(T_S);
    #endif
    
    #if DEBUG
    AP_ASSERT(tid - 1 < VG_N_THREADS && tid > 0, "Tid is too big");
    #endif
    threads[tid-1] = tdata;
    
    /* we deallocate all routine info when we generate the report */
    tdata->routine_hash_table = HT_construct(NULL);
    AP_ASSERT(tdata->routine_hash_table != NULL, "rtn ht not allocable");
    
    tdata->stack_depth = 0;
    tdata->max_stack_size = STACK_SIZE;
    tdata->stack = VG_(calloc)("stack", STACK_SIZE * sizeof(Activation), 1);
    
    #if DEBUG_ALLOCATION
    UInt j = 0;
    for (j = 0; j < STACK_SIZE; j++) APROF_(add_alloc)(ACT_S);
    #endif
    
    #if TRACE_FUNCTION
    tdata->last_bb = NULL;
    #endif
    
    #if EMPTY_ANALYSIS
    return tdata;
    #endif
    
    #if INPUT_METRIC == RMS || DEBUG_DRMS
    tdata->next_aid = 1;
    tdata->accesses_rms = LK_create();
    #endif
    
    #if INPUT_METRIC == RVMS
    tdata->accesses_rvms = LK_create();
    #endif
    
    #if CCT
    
    tdata->next_context_id = 1;
    // allocate dummy CCT root
    tdata->root = (CCTNode*) VG_(calloc)("CCT", sizeof(CCTNode), 1);

    #if CCT_GRAPHIC
    char * n = VG_(calloc)("nome root", 32, 1);
    n = "ROOT";
    tdata->root->name = n;
    #endif

    #if DEBUG_ALLOCATION
    APROF_(add_alloc)(CCT_S);
    #endif
 
    #endif // CCT
    
    #if TIME == RDTSC
    tdata->entry_time = APROF_(time)();
    #endif

    APROF_(running_threads)++;

    return tdata;

}

void APROF_(thread_exit)(ThreadId tid){

    #if DEBUG_ALLOCATION
    VG_(umsg)("\nThread exit:\n");
    APROF_(print_alloc)();
    #endif
    
    APROF_(current_TID) = VG_INVALID_THREADID;
    APROF_(current_tdata) = NULL;

    #if VERBOSE
    VG_(printf)("Exit thread %d\n", tid);
    #endif
    
    #if DEBUG
    AP_ASSERT(tid - 1 < VG_N_THREADS && tid > 0, "Invalid tid")
    #endif
    ThreadData * tdata = threads[tid - 1];
    #if DEBUG
    AP_ASSERT(tdata != NULL, "Invalid tdata")
    #endif
    
    /* Unregister thread info */
    threads[tid -1] = NULL;
    APROF_(current_TID) =  VG_INVALID_THREADID;
    APROF_(current_tdata) = NULL;
    
    #if EVENTCOUNT
    VG_(printf)("[TID=%d] Load: %llu\n", tid, tdata->num_read);
    VG_(printf)("[TID=%d] Store: %llu\n", tid, tdata->num_write);
    VG_(printf)("[TID=%d] Modify: %llu\n", tid, tdata->num_modify);
    VG_(printf)("[TID=%d] Function entry: %llu\n", tid, tdata->num_func_enter);
    VG_(printf)("[TID=%d] Function exit: %llu\n", tid, tdata->num_func_exit);
    VG_(printf)("[TID=%d] Total accesses: %llu\n", tid, 
                                                    tdata->num_modify +
                                                    tdata->num_write +
                                                    tdata->num_read
                                                    );
    #endif
    
    #if EMPTY_ANALYSIS 
    HT_destruct(tdata->routine_hash_table);
    VG_(free)(tdata->stack);
    VG_(free)(tdata);
    return;
    #endif
    
    #if TRACE_FUNCTION
    APROF_(unwind_stack)(tdata);
    #endif
    
    APROF_(generate_report)(tdata, tid);
    APROF_(running_threads)--;
    
    /* destroy all thread data data */
    
    #if INPUT_METRIC == RMS || DEBUG_DRMS
    LK_destroy(tdata->accesses_rms);
    #endif
    
    #if INPUT_METRIC == RVMS
    LK_destroy(tdata->accesses_rvms);
    #endif
    
    #if CCT
    // deallocate CCT
    APROF_(free_CCT)(tdata->root);
    #endif
    
    HT_destruct(tdata->routine_hash_table);
    
    #if DEBUG_ALLOCATION
    APROF_(remove_alloc)(T_S);
    UInt i;
    for (i = 0; i < tdata->max_stack_size; i++) 
        APROF_(remove_alloc)(ACT_S);
    #endif

    VG_(free)(tdata->stack);
    VG_(free)(tdata);

}

void APROF_(thread_switch)(ThreadId tid, ULong blocks_dispatched) {
    
    /* 
     * Note: Callgrind says that if you have not done at least
     * 5000 blocks you can ignore this call.
        -----
        static ULong last_blocks_done = 0;
        // throttle calls to CLG_(run_thread) by number of BBs executed
        if (blocks_done - blocks_dispatched < 5000) return;
        ------
     * Why? Investigate! 
     */
     
    #if DEBUG
    AP_ASSERT(tid == VG_(get_running_tid)(), "TID mismatch");
    #endif
    
    //if (LIKELY(tid == APROF_(current_TID))) return;
    if (tid == APROF_(current_TID)) return;
    
    #if VERBOSE
    VG_(printf)("switch to thread %d\n", tid);
    #endif
    
    #if INPUT_METRIC == RVMS
    APROF_(global_counter)++;
    if(APROF_(global_counter) == MAX_COUNT_VAL)
        APROF_(global_counter) = APROF_(overflow_handler)();
    #endif
    
    #if TRACE_FUNCTION
    /* save last exit of the previous thread */
    if (APROF_(current_tdata) != NULL)
        APROF_(current_tdata)->last_exit = APROF_(last_exit);
    #endif
    
    APROF_(current_TID) = tid;
    
    if (tid == VG_INVALID_THREADID) {
        APROF_(current_tdata) = NULL;
        return;
    }
    
    if (threads[tid-1] == NULL) 
        APROF_(current_tdata) = APROF_(thread_start)(tid);
    else 
        APROF_(current_tdata) = threads[tid -1];
    
    #if DEBUG
    AP_ASSERT(APROF_(current_tdata) != NULL, "Invalid tdata");
    #endif
    
    #if TRACE_FUNCTION
    /* restore exit value of the current thread */
    APROF_(last_exit) = APROF_(current_tdata)->last_exit;
    #endif
}

void APROF_(kill_threads)(void) {
    
    UInt i = 0;
    while (i < VG_N_THREADS) {
        
        if (threads[i] != NULL)
            APROF_(thread_exit)(i+1);
        
        i++;
    }
    
}

#if INPUT_METRIC == RVMS
/*
 * global_counter is 32 bit integer so it can overflow. To overcome this
 * issue we periodically compress our set of valid timestamps 
 * (e.g., after an overflow).
 * 
 * 1) scan all shadow thread stacks (in parallel): collect the (sorted) 
 *    set of used timestamps by all activations (active_ts).
 * 
 * 3) element i-th of the set is associated to the new timestamp 
 *    i * 3. Re-assign ts to all activations.
 *
 * 4) re-assign ts in all shadow memories (see LK_compress)
 * 
 * Return the new starting value for the global counter: 
 *      3 * size(active_ts).
 */
#if OVERFLOW_DEBUG
static UInt round = 0;
#endif
UInt APROF_(overflow_handler)(void) {

    VG_(umsg)("Global counter overflow\n");

    #if OVERFLOW_DEBUG
    HChar name[128];
    VG_(sprintf)(name, "overflow_log_%u", round++);
    FILE * f = APROF_(fopen)(name);
    if (f == NULL) AP_ASSERT(0, "Cannot open overflow log");
    #endif
    
    UInt sum = 0; // # valid timestamps
    UInt count_thread = APROF_(running_threads);

    // pointers to active shadow memories
    LookupTable ** shamem = VG_(calloc)("pointers to all PSM", 
                                            count_thread, 
                                            sizeof(LookupTable *));
    
    // current stack depth of each thread
    UInt * stack_depths = VG_(calloc)("index for merge", 
                                    count_thread, sizeof(UInt));
    
    #if OVERFLOW_DEBUG
    VG_(umsg)("Estimating # valid ts\n");
    #endif
    
    UInt i = 0;
    UInt j = 0;
    // compute the number of different activation-ts
    while(i < count_thread && j < VG_N_THREADS){
        
        if(threads[j] == NULL) {
        
            j++;
        
        } else {
        
            #if OVERFLOW_DEBUG
            APROF_(fprintf)(f, "Thread: %u ~ depth: %d\n", j, 
                                    threads[j]->stack_depth);
            APROF_(fflush)(f);
            #endif
        
            shamem[i] = threads[j]->accesses_rvms;
            stack_depths[i] = threads[j]->stack_depth;
            sum += stack_depths[i]; // over-estimation
            i++; j++;
        
        }
    }
    
    #if OVERFLOW_DEBUG
    VG_(umsg)("Collecting valid ts & stack re-assignment...\n");
    #endif
 
    // current valid timestamps 
    sum++; // in order to have an initiali zero
    UInt * active_ts = VG_(calloc)("array overflow", sum, sizeof(UInt));
    
    /* 
     * Collect valid activation-ts using a merge 
     * and re-assign the new ts to every shadow stacks.
     * 
     * stack_depths[i] contains the lower activation (of the shadow
     * stack for i-th thread) already checked as candidate
     * for the current max. Basically, we find the max ts on
     * top of all shadow stacks, we store this ts in active_ts,
     * we decrease the stack_depths[i] of i-th thread (the one
     * with max_act), then we find the new max ts (it can be on 
     * top of another shadow stack).
     * 
     */
    for(i = (sum - 1); i > 0; i--){
        
        UInt k = 0;
        UInt max = 0; 
        Activation * act_tmp;
        
        /*
         * Info about activation/thread with the max activation-ts
         * 
         *  act_max: the current activation with the higher ts
         *  max_ind: this is the index in stack_depths[] related
         *           to the thread with highest ts (act_max).
         */
        Activation * act_max = NULL;
        UInt max_ind = 0;

        /* find the max activation ts among all shadow stacks */ 
        for(j = 0; j < count_thread; j++){

            while(threads[k] == NULL) k++;

            if(stack_depths[j] > 0){
                
                act_tmp = APROF_(get_activation)(threads[k], stack_depths[j]);
                
                #if OVERFLOW_DEBUG
                APROF_(fprintf)(f, "Thread[%u]: aid = %u\n", k, act_tmp->aid_rvms);
                APROF_(fflush)(f);
                #endif

                if(max < act_tmp->aid_rvms){
                    
                    max = act_tmp->aid_rvms;
                    act_max = act_tmp;
                    max_ind = j;
                
                }
            
            }
            k++;
    
        }

        #if OVERFLOW_DEBUG
        APROF_(fprintf)(f, "Max: %u\n", max);
        APROF_(fflush)(f);
        #endif

        active_ts[i] = max;
        
        // next time we check for the max the caller of this act
        stack_depths[max_ind]--; 
        act_max->aid_rvms = i*3; // re-assign ts
    
    }
    
    #if OVERFLOW_DEBUG
    VG_(umsg)("ts collected...\n");
    APROF_(fprintf)(f, "ACTIVE_TS: ");
    APROF_(fflush)(f);
    for (i = 0; i < sum; i++) {
        APROF_(fprintf)(f, " %u=%u ", i, active_ts[i]);
        APROF_(fflush)(f);
    }
    APROF_(fprintf)(f, "\n");
    APROF_(fflush)(f);
    #endif
    
    VG_(free)(stack_depths);

    // compress shadow memories
    #if OVERFLOW_DEBUG
    VG_(umsg)("Compressing\n");
    LK_compress(active_ts, sum, shamem, f);
    #else
    LK_compress(active_ts, sum, shamem);
    #endif
    
    VG_(free)(active_ts);
    VG_(free)(shamem);
    
    #if OVERFLOW_DEBUG
    APROF_(fclose)(f);
    #endif
    //VG_(umsg)("Global counter overflow handler end\n");
    //AP_ASSERT(0, "test");
    
    return 3 * (sum + 1);
}
#endif

#if INPUT_METRIC == RMS || DEBUG_DRMS
UInt APROF_(overflow_handler_rms)(void) {

    VG_(umsg)("Local counter overflow\n");

    ThreadData * tdata = APROF_(current_tdata);
    #if DEBUG
    AP_ASSERT(tdata != NULL, "Invalid tdata");
    #endif

    /* Collect all valid aid */
    UInt * arr_aid = VG_(calloc)("arr rid", tdata->stack_depth, sizeof(UInt));
    int j = 0;
    //VG_(umsg)("Stack depth: %d\n", tdata->stack_depth);
    for (j = 0; j < tdata->stack_depth; j++) {
    
        Activation * act_c = APROF_(get_activation)(tdata, j + 1);
        //VG_(umsg)("%u=%u ", j + 1, act_c->aid_rms);
        arr_aid[j] = act_c->aid_rms;
        act_c->aid_rms = j + 1;
        
    }
    
    //VG_(umsg)("\n");
    LK_compress_rms(tdata->accesses_rms, arr_aid, tdata->stack_depth);
    VG_(free)(arr_aid);

    //VG_(umsg)("Local counter: %u\n", tdata->stack_depth + 1);
    return tdata->stack_depth + 1;

}
#endif // INPUT_METRIC == RMS || DISTINCT_RMS