/*
 * Report generator
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

// last modification time of the current program
static ULong APROF_(binary_time) = 0;

static HChar * report_name(HChar * filename_priv, UInt tid, 
                                UInt postfix_c, HChar * prog_name) {


    UInt offset = 0;
    #if REPORT_NAME == 1
    offset += VG_(sprintf)(filename_priv, "%s_", VG_(basename)(prog_name));
    #endif

    VG_(sprintf)(filename_priv + offset, "%d_%u_%d", VG_(getpid)(), 
                                    tid - 1, APROF_(addr_multiple));

    char postfix[128];
    if (postfix_c > 0) VG_(sprintf)(postfix, "_%u.aprof", postfix_c);
    else VG_(sprintf)(postfix, ".aprof");
    
    VG_(strcat)(filename_priv, postfix);

    return filename_priv;

}

void APROF_(generate_report)(ThreadData * tdata, ThreadId tid) {
    
    HChar filename_priv[1024] = {0};
    HChar * prog_name = (HChar *) VG_(args_the_exename);
    
    /*
     * This does not work because we don't have the real path
    if (APROF_(binary_time) == 0) {
        
        // Get binary time
        struct vg_stat buf;
        SysRes b = VG_(stat)(prog_name, &buf);
        binary_time = buf.mtime;
        
    }
    */
    
    /* Add path to log filename */
    HChar * filename = NULL;
    if (APROF_(log_dir) != NULL) {
        
        filename = VG_(calloc)("log", 2048, 1);
        VG_(sprintf)(filename, "%s/%s", APROF_(log_dir), 
                    report_name(filename_priv, tid, 0, prog_name));
                    
    } else {
        
        filename = VG_(expand_file_name)("aprof log", 
                            report_name(filename_priv, tid, 0, prog_name));
    
    }

    // open report file
    FILE * report = APROF_(fopen)(filename);
    UInt attempt = 0;
    while (report == NULL && attempt < 1024) {

        if (APROF_(log_dir) != NULL) {
        
            VG_(sprintf)(filename, "%s/%s", APROF_(log_dir), 
                        report_name(filename_priv, tid, attempt, prog_name));
        
        } else {

            VG_(free)(filename);
            filename = VG_(expand_file_name)("aprof log", 
                report_name(filename_priv, tid, attempt, prog_name));
        
        }
        
        report = APROF_(fopen)(filename);
        
        attempt++;
    }

    //VG_(umsg)("Writing report: %s\n", filename);
    AP_ASSERT(report != NULL, "Can't create report file");

    // write header
    APROF_(fprintf)(report, "c -------------------------------------\n");
    APROF_(fprintf)(report, "c report generated by aprof (valgrind) \n");
    //APROF_(fprintf)(report, "c %s\n", filename);
    APROF_(fprintf)(report, "c -------------------------------------\n");
    
    // write version 
    APROF_(fprintf)(report, "v %d\n", REPORT_VERSION);
    
    // Maximum value for the metric
    #if TIME == BB_COUNT
    APROF_(fprintf)(report, "k %llu\n", tdata->bb_c + tdata->other_metric);
    #elif TIME == RDTSC
    APROF_(fprintf)(report, "k %llu\n", APROF_(time)() - tdata->entry_time + tdata->other_metric);
    #endif
    
    // write mtime binary
    APROF_(fprintf)(report, "e %llu\n", APROF_(binary_time));
    
    // write performance metric type
    #if TIME == BB_COUNT
    APROF_(fprintf)(report, "m bb-count\n");
    #elif TIME == RDTSC
    APROF_(fprintf)(report, "m time-usec\n");
    #endif
    
    // write input metric type
    #if INPUT_METRIC == RMS
    APROF_(fprintf)(report, "i rms\n");
    #else 
    APROF_(fprintf)(report, "i rvms\n");
    #endif
    
    // write memory resolution
    APROF_(fprintf)(report, "t %d\n", APROF_(addr_multiple));
    
    #if EVENTCOUNT
    APROF_(fprintf)(report, "c JSR=%llu - RTS=%llu - RD=%llu - WR=%llu\n", 
            tdata->num_func_enter, tdata->num_func_exit, 
            tdata->num_read + tdata->num_modify,
            tdata->num_write + tdata->num_modify);
    #endif
    
    // write application name
    APROF_(fprintf)(report, "a %s\n", prog_name);
    
    // write commandline
    APROF_(fprintf)(report, "f %s", prog_name);
    XArray * x = VG_(args_for_client);
    int i = 0;
    for (i = 0; i < VG_(sizeXA)(x); i++) {
        HChar ** c = VG_(indexXA)(x, i);
        if (c != NULL) {
            APROF_(fprintf)(report, " %s", *c);
        }
    }
    APROF_(fprintf)(report, "\n");

    #if DEBUG
    AP_ASSERT(tdata->routine_hash_table != NULL, "Invalid rtn ht");
    #endif

    // iterate over routines
    HT_ResetIter(tdata->routine_hash_table);

    RoutineInfo * rtn_info = HT_RemoveNext(tdata->routine_hash_table);
    while (rtn_info != NULL) {
        
        #if DEBUG
        AP_ASSERT(rtn_info != NULL, "Invalid rtn");
        AP_ASSERT(rtn_info->fn != NULL, "Invalid fn");
        AP_ASSERT(rtn_info->fn->name != NULL, "Invalid name fn");
        #endif
        
        #if DISCARD_UNKNOWN
        if (!rtn_info->fn->discard_info) {
        #endif
        
        const HChar * obj_name = "NONE";
        if (rtn_info->fn->obj != NULL) obj_name = rtn_info->fn->obj->name; 
        
        APROF_(fprintf)(report, "r \"%s\" \"%s\" %llu\n", 
                    rtn_info->fn->name, obj_name, 
                    rtn_info->routine_id);
        
        if (rtn_info->fn->mangled != NULL) {
            APROF_(fprintf)(report, "u %llu \"%s\"\n", rtn_info->routine_id, 
                            rtn_info->fn->mangled);
        }

        #if CCT

        HT_ResetIter(rtn_info->context_rms_map);
        HashTable * ht = HT_RemoveNext(rtn_info->context_rms_map);
        
        while (ht != NULL) {
            
            HT_ResetIter(ht);
            RMSInfo * info_access = HT_RemoveNext(ht);
            
            while (info_access != NULL) {
                
                APROF_(fprintf)(report,
                                #if INPUT_METRIC == RVMS
                                "q %lu %lu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                                #else
                                "q %lu %lu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                                #endif
                                ht->key, 
                                info_access->key,
                                info_access->min_cumulative_time,
                                info_access->max_cumulative_time,
                                info_access->cumulative_time_sum, 
                                info_access->cumulative_sum_sqr,  
                                info_access->calls_number,
                                info_access->cumul_real_time_sum,
                                info_access->self_time_sum,
                                info_access->self_time_min,
                                info_access->self_time_max,
                                info_access->self_sum_sqr
                                
                                #if INPUT_METRIC == RVMS
                                , info_access->rms_input_sum,
                                info_access->rms_input_sum_sqr
                                
                                #if INPUT_STATS
                                , info_access->rvms_syscall_sum,
                                info_access->rvms_thread_sum,
                                info_access->rvms_syscall_self,
                                info_access->rvms_thread_self
                                #endif
                                
                                #endif
                            );

                VG_(free)(info_access);
                
                #if DEBUG_ALLOCATION
                APROF_(remove_alloc)(RMS_S);
                #endif
                
                info_access = HT_RemoveNext(ht);

            }
            
            HT_destruct(ht);
            ht = HT_RemoveNext(rtn_info->context_rms_map);
        }
        #else

        // iterate over rms records of current routine
        #if INPUT_METRIC == RMS
        HT_ResetIter(rtn_info->rms_map);
        RMSInfo * info_access = HT_RemoveNext(rtn_info->rms_map);
        #elif INPUT_METRIC == RVMS
        HT_ResetIter(rtn_info->rvms_map);
        RMSInfo * info_access = HT_RemoveNext(rtn_info->rvms_map);
        #endif
        
        while (info_access != NULL) {
            
            APROF_(fprintf)(report,
                            #if INPUT_METRIC == RVMS
                            "p %llu %lu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n", 
                            #else
                            "p %llu %lu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                            #endif
                            rtn_info->routine_id,
                            info_access->key,
                            info_access->min_cumulative_time,
                            info_access->max_cumulative_time,
                            info_access->cumulative_time_sum, 
                            info_access->cumulative_sum_sqr,
                            info_access->calls_number,
                            info_access->cumul_real_time_sum,
                            info_access->self_time_sum,
                            info_access->self_time_min,
                            info_access->self_time_max,
                            info_access->self_sum_sqr
                            #if INPUT_METRIC == RVMS
                            , info_access->rms_input_sum,
                            info_access->rms_input_sum_sqr
                            
                            #if INPUT_STATS
                            , info_access->rvms_syscall_sum,
                            info_access->rvms_thread_sum,
                            info_access->rvms_syscall_self,
                            info_access->rvms_thread_self
                            #endif
                            
                            #endif
                            );

            VG_(free)(info_access);
            #if DEBUG_ALLOCATION
            APROF_(remove_alloc)(RMS_S);
            #endif
            
            #if INPUT_METRIC == RMS
            info_access = HT_RemoveNext(rtn_info->rms_map);
            #elif INPUT_METRIC == RVMS
            info_access = HT_RemoveNext(rtn_info->rvms_map);;
            #endif
        }
        #endif
        
        #if DISTINCT_RMS
        HT_ResetIter(rtn_info->distinct_rms);
        RMSValue * node = (RMSValue *) HT_RemoveNext(rtn_info->distinct_rms);
        while(node != NULL) {
            
            APROF_(fprintf)(report, "g %llu %lu %llu\n", 
                                rtn_info->routine_id, node->key,
                                node->calls);
            
            VG_(free)(node);
            #if DEBUG_ALLOCATION
            APROF_(remove_alloc)(HTN_S);
            #endif
            
            node = HT_RemoveNext(rtn_info->distinct_rms);
        
        }
        #endif

        #if DISCARD_UNKNOWN
        }
        #endif

        APROF_(destroy_routine_info)(rtn_info);
        rtn_info = HT_RemoveNext(tdata->routine_hash_table);

    }
    
    #if CCT
    APROF_(print_report_CCT)(report, tdata->root, 0);
    #endif
    
    #if CCT_GRAPHIC
    VG_(sprintf)(filename_priv, "%s_%u.graph", VG_(basename)(prog_name), tid - 1);
    filename = VG_(expand_file_name)("aprof log", filename_priv);
    FILE * cct_rep = APROF_(fopen)(filename);
    AP_ASSERT(cct_rep != NULL, "Can't create CCT report file");
    APROF_(fprintf)(report, "digraph G {\n");
    APROF_(print_cct_graph)(cct_rep, tdata->root, 0, NULL);
    APROF_(fprintf)(report, "}\n");
    APROF_(fclose)(cct_rep);
    VG_(free)(filename);
    #endif
    
    // close report file
    APROF_(fclose)(report);
    
    VG_(umsg)("Report: %s\n", filename);
    VG_(free)(filename);
    
    #if MEM_USAGE_INFO
    if (APROF_(running_threads) == 1)
        APROF_(print_info_mem_usage)();
    #endif
}