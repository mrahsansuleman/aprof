/*
 * Report generator
 * 
 * Last changed: $Date: 2013-08-30 11:29:14 +0200 (ven, 30 ago 2013) $
 * Revision:     $Rev: 884 $
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

#ifndef EXTERNAL
    #include "aprof.h"
#else
    #include "extra/aprof-helper.h"
    #include "data-common.h"
#endif

#if APROF_TOOL
static inline HChar * APROF_(report_name)(  HChar * name, 
                                            UInt tid, 
                                            UInt attempt, 
                                            const HChar * prog_name) {
    
    UInt offset = 0;
    #if REPORT_NAME == 1
    offset += VG_(sprintf)(name, "%s_", VG_(basename)(prog_name));
    #endif
    
    if (attempt > 0) attempt += 1024;
    VG_(sprintf)(name + offset, "%d_%u_%d.aprof", VG_(getpid)(), 
                    tid - 1 + attempt, APROF_(runtime).memory_resolution);
                    
    return name;
}

static FILE * APROF_(create_report)(const HChar * prog_name, ThreadId tid, 
                                        HChar ** filename_p) {
    
    HChar filename_priv[NAME_SIZE] = {0};
    HChar * filename = NULL;
    HChar * name = NULL;
    FILE * report = NULL;
    UInt attempt = 0;
    
    while (report == NULL && attempt < 1024) {
    
        name = APROF_(report_name)(filename_priv, tid, attempt, prog_name);
        if (APROF_(runtime).log_dir != NULL) {
            
            filename = VG_(calloc)("log", NAME_SIZE, 1);
            VG_(sprintf)(filename, "%s/%s", APROF_(runtime).log_dir, name);
            
        } else {
            
            filename = VG_(expand_file_name)("log", name);
        }
        
        report = APROF_(fopen)(filename);
        attempt++;
    }
    
    *filename_p = filename;
    return report;
}
#endif // APROF_TOOL

// This will destroy your routines!
void APROF_(print_report)(  FILE * report,
                            Runtime * r,
                            HashTable * rtn_ht,
                            ULong cost,
                            CCTNode * root
                            ) {
    
    // write header
    APROF_(fprintf)(report, "c -------------------------------------\n");
    APROF_(fprintf)(report, "c report generated by aprof (valgrind) \n");
    APROF_(fprintf)(report, "c -------------------------------------\n");
    
    // write version 
    APROF_(fprintf)(report, "v %d\n", REPORT_VERSION);
    
    // Maximum value for the metric
    APROF_(fprintf)(report, "k %llu\n", cost);
    
    // write mtime binary
    APROF_(fprintf)(report, "e %llu\n", r->binary_mtime);
    
    // write performance metric type
    #if COST == BB_COUNT
    APROF_(fprintf)(report, "m bb-count\n");
    #elif COST == RDTSC
    APROF_(fprintf)(report, "m time-usec\n");
    #elif COST == INSTR
    APROF_(fprintf)(report, "m vex-instr\n");
    #endif
    
    // write input metric type
    if (r->input_metric == RMS)
        APROF_(fprintf)(report, "i rms\n");
    else
        APROF_(fprintf)(report, "i drms\n");
    
    // write memory resolution
    APROF_(fprintf)(report, "t %d\n", r->memory_resolution);
    
    // write application name
    APROF_(fprintf)(report, "a %s\n", r->application);
    
    // write commandline
    APROF_(fprintf)(report, "f %s\n", r->cmd_line);

    HashTable * routines = rtn_ht;
    if (routines == NULL) routines = r->fn_ht;

    // iterate over routines
    HT_ResetIter(routines);
    void * rtn = HT_RemoveNext(routines);
    while (rtn != NULL) {
        
        Function * fn; HashTable * input_map; UInt routine_id;
        if (rtn_ht == NULL) {
            
            fn = (Function *) rtn;
            input_map = fn->input_map;
            routine_id = fn->function_id;
            
        } else {
        
            fn = ((RoutineInfo *) rtn)->fn;
            input_map = ((RoutineInfo *) rtn)->input_map;
            routine_id = ((RoutineInfo *) rtn)->routine_id;
        }
        
        if (!fn->discard) {
        
            const HChar * obj_name = "NONE";
            if (fn->obj != NULL)
                obj_name = fn->obj->name; 
        
            APROF_(fprintf)(report, "r \"%s\" \"%s\" %u\n", 
                        fn->name, obj_name, routine_id);
        
            if (fn->mangled != NULL) {
                APROF_(fprintf)(report, "u %u \"%s\"\n", 
                                routine_id, fn->mangled);
            }

            // iterate over tuples
            HT_ResetIter(input_map);
            Input * tuple = HT_RemoveNext(input_map);
            while (tuple != NULL) {
            
                if (r->collect_CCT)
                    APROF_(fprintf)(report, "q %lu ", tuple->context_id);
                else
                    APROF_(fprintf)(report, "p %u ", routine_id);
                
                APROF_(fprintf)(report,
                                "%lu %llu %llu %llu ",
                                tuple->input_size,
                                tuple->min_cumulative_cost,
                                tuple->max_cumulative_cost,
                                tuple->sum_cumulative_cost);
                
                APROF_(fprintf)(report,
                                #if APROF_TOOL
                                "%llu ",
                                #else
                                "%.0f ",
                                #endif
                                tuple->sqr_cumulative_cost);
                                
                APROF_(fprintf)(report, "%llu %llu %llu %llu %llu ",
                                tuple->calls,
                                tuple->sum_cumul_real_cost,
                                tuple->sum_self_cost,
                                tuple->min_self_cost,
                                tuple->max_self_cost
                                );
                                
                APROF_(fprintf)(report,
                                #if APROF_TOOL
                                "%llu",
                                #else
                                "%.0f",
                                #endif
                                tuple->sqr_self_cost);
                
                #if INPUT_STATS
                if (r->input_metric == DRMS) {
                    
                    APROF_(fprintf)(report,
                                    " 0 0 %llu %llu %llu %llu",
                                    tuple->sum_cumul_syscall,
                                    tuple->sum_cumul_thread,
                                    tuple->sum_self_syscall,
                                    tuple->sum_self_thread
                                    );
                }
                #endif
                
                APROF_(fprintf)(report, "\n");
                APROF_(delete)(INPUT_S, tuple);
                tuple = HT_RemoveNext(input_map);
            }
            
        }
        
        if (rtn_ht != NULL) 
            APROF_(destroy_routine_info)(rtn);
        else
            APROF_(destroy_function)(rtn);
        
        rtn = HT_RemoveNext(routines);
    }
    
    #if APROF_TOOL
    if (r->collect_CCT)
        APROF_(print_report_CCT)(report, root, 0);
    #endif
    
    // close report file
    APROF_(fclose)(report);
} 

#if APROF_TOOL
void APROF_(generate_report)(ThreadData * tdata, ThreadId tid) {
    
    ULong cost = APROF_(time)(tdata);
    #if COST == RDTSC
    cost -= tdata->cost; // time? start - end
    #endif

    cost -= tdata->skip_cost;
    
    HashTable * routines = tdata->rtn_ht;
    if (APROF_(runtime).single_report) {
    
        APROF_(runtime).extra_cost += cost;
        if (APROF_(runtime).running_threads > 1) return;
        else cost = APROF_(runtime).extra_cost;
        
        if (APROF_(runtime).incremental_report) {
            APROF_(load_reports)();
        }
        
        routines = NULL;
    }
    
    HChar * filename = NULL;
    FILE * file = APROF_(create_report)(APROF_(runtime).application, tid, &filename);
    APROF_(assert)(file != NULL, "Report can not be created: %s", filename);
    
    APROF_(print_report)(file, &APROF_(runtime), routines, cost, tdata->root);
    
    VG_(umsg)("Report: %s\n", filename);
    VG_(free)(filename);
    
    #if MEM_USAGE_INFO
    if (APROF_(runtime).running_threads == 1)
        APROF_(print_info_mem_usage)();
    #endif
}
#endif // APROF_TOOL
