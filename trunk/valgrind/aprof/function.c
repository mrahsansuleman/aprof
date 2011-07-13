/*
 * Entry and exit function handler, simulated thread stack handler
 * 
 * Last changed: $Date: 2011-07-09 16:20:22 +0200 (Sat, 09 Jul 2011) $
 * Revision:     $Rev: 74 $
 */

#include "aprof.h"
 
#if EVENTCOUNT
UWord fn_in_n  = 0;
UWord fn_out_n = 0;
#endif

Activation * get_activation(ThreadData * tdata, unsigned int depth) {

	#if DEBUG
	if (tdata == NULL) failure("Invalid tdata in get_activation");
	if (depth == 0) failure("Inavalid depth in get_activation");
	#endif

	/* Expand stack if necessary */
	if (depth -1 >= tdata->max_stack_size) {
		
		tdata->max_stack_size = tdata->max_stack_size * 2;
		
		#if VERBOSE
		VG_(printf)("Relocate stack\n");
		#endif
		
		tdata->stack = VG_(realloc)("stack", tdata->stack, tdata->max_stack_size);
		if (tdata->stack == NULL) failure("stack not reallocable");
	
	}
	
	return tdata->stack + depth - 1;

}

void destroy_routine_info(void * data) {
	
	RoutineInfo * ri = (RoutineInfo *) data;
	#if CCT
	HT_destruct(ri->context_sms_map);
	#else
	HT_destruct(ri->sms_map);
	#endif
	VG_(free)(ri);

}

Bool trace_function(ThreadId tid, UWord * arg, UWord * ret) {
	
	/* Is this client request for aprof? */
	if (!VG_IS_TOOL_USERREQ('V','A',arg[0])) return False;
	
	UWord64 start = ap_time();
	
	#if EMPTY_ANALYSIS
	return True;
	#endif
	
	UWord target = arg[1]; /* routine address */
	ThreadData * tdata = get_thread_data(tid);
	
	#if DEBUG
	if (tdata == NULL) failure("Invalid tdata in trace_function");
	#endif
	
	#if TRACER
	char buf[24];
	if (arg[2] == 1) {
		VG_(sprintf)(buf, "i:0:%u\n", ((unsigned int) arg[1]));
		tdata->stack_depth++;
	} else if (arg[2] == 2) {
		VG_(sprintf)(buf, "o:0:%u\n", ((unsigned int) arg[1]));
		tdata->stack_depth--;
	}
	
	ap_fwrite(tdata->log, (void*)buf, VG_(strlen)(buf));
	return True;
	#endif
	
	#if VERBOSE
	char * debug_name = VG_(calloc)("name", 256, 1);
	if (!VG_(get_fnname)(arg[1], debug_name, 256))
		debug_name = "???";
	//UF_print_count(tdata->accesses);
	#endif
	
	if (arg[2] == 1) { /* Function entry */
	
		#if VERBOSE
		VG_(printf)("Start function: %s\n", debug_name); 
		#endif
		
		#if EVENTCOUNT
		fn_in_n++;
		#endif
	
		tdata->stack_depth++;
	
		/* check if routine has already been encountered */
		RoutineInfo * rtn_info = HT_lookup(tdata->routine_hash_table, target, NULL); 
		
		/* new routine, make routine record */
		if (rtn_info == NULL) {
			
			/* Get routine name */
			char * rtn_name = VG_(calloc)("rtn_name", 256, 1);
			//VG_(sprintf)(rtn_name, "%lu", target);
			if (rtn_name == NULL) failure("rtn_name not allocable");
			if (!VG_(get_fnname)(arg[1], rtn_name, 256))
				rtn_name = "???";
			
			//VG_(printf)("New routine info\n");
			rtn_info = (RoutineInfo * )VG_(calloc)("rtn_info", 1, sizeof(RoutineInfo));
			if (rtn_info == NULL) failure("rtn_info not allocable");
			rtn_info->routine_id = tdata->next_routine_id++;
			rtn_info->name       = rtn_name;
			DebugInfo * di = VG_(find_DebugInfo)(target);
			if (di == NULL) failure("Invalid DebugInfo");
			rtn_info->image_name = VG_(DebugInfo_get_soname)(di);
			#if CCT
			rtn_info->context_sms_map = HT_construct(HT_destruct);
			if (rtn_info->context_sms_map == NULL) failure("context_sms_map not allocable");
			#else
			rtn_info->sms_map = HT_construct(VG_(free));
			if (rtn_info->sms_map == NULL) failure("sms_map not allocable");
			#endif
			
			/* add routine to routine to ht */
			HT_add_node(tdata->routine_hash_table, target, rtn_info);
			
		}
		
		// bookkeeping
		rtn_info->calls++;
		rtn_info->recursion_pending++;
		if (rtn_info->recursion_pending > 1) rtn_info->recursive = 1;
		
		Activation * activation = get_activation(tdata, tdata->stack_depth);
		#if DEBUG
		if (activation == NULL) failure("Invalid activation in function entry");
		#endif
		activation->rtn_info            = rtn_info;
		activation->entry_time          = start;
		activation->sms                 = 0;
		activation->total_children_time = 0;
		
		#if CCT
		
		CCTNode * parent = parent_CCT(tdata);
		#if DEBUG
		if (parent == NULL) failure("Invalid parent CCT in function entry");
		#endif 
		CCTNode * cnode = parent->firstChild;
		
		// did we already encounter this context?
		while (cnode != NULL) {
			
			if (cnode->target == target) break;
			cnode = cnode->nextSibling;
		
		}

		if (cnode == NULL) {
			
			// create new context node
			cnode = (CCTNode*) VG_(calloc)("CCT", sizeof(CCTNode), 1);
			if (cnode == NULL) 
				failure("Can't allocate CTT node\n");

			// add node to tree
			cnode->nextSibling = parent->firstChild;
			parent->firstChild = cnode;
			cnode->target = target;
			cnode->routine_id = activation->rtn_info->routine_id;
			cnode->context_id = tdata->next_context_id++;
			
		}

		// store context node into current activation record
		activation->node = cnode;
		#endif
		
		#if TIME == NO_TIME
		activation->overhead = 0;
		#else
		activation->overhead = ap_time() - start;
		#endif
		
	} else if (arg[2] == 2) { /* Function exit */
		
		#if VERBOSE
		VG_(printf)("Exit function: %s\n", debug_name); 
		#endif
		
		#if EVENTCOUNT
		fn_out_n++;
		#endif
		
		Activation * activation = get_activation(tdata, tdata->stack_depth);
		#if DEBUG
		if (activation == NULL) failure("Invalid activation in function exit");
		#endif
		RoutineInfo * rtn_info = activation->rtn_info;

		UWord partial_cumulative =  start - activation->entry_time - activation->overhead;
		if (rtn_info->recursion_pending < 2) rtn_info->total_cumulative_time += partial_cumulative;

		UWord partial_self = partial_cumulative - activation->total_children_time;
		rtn_info->total_self_time += partial_self;
		
		// check if routine has ever been called with this seen memory size (SMS)
		SMSInfo * info_access = NULL;
		#if CCT
		HashTable * sms_map  = HT_lookup(rtn_info->context_sms_map, 
										activation->node->context_id, NULL);
		
		if (sms_map == NULL) {
			
			//VG_(printf)("New sms map\n");
			sms_map = HT_construct(VG_(free));
			if (sms_map == NULL) failure("sms_map not allocable");
			HT_add_node(rtn_info->context_sms_map, activation->node->context_id, sms_map);

		} else {
			
			info_access = HT_lookup(sms_map, activation->sms, NULL);
		
		}
		#else
		
		info_access = HT_lookup(rtn_info->sms_map, activation->sms, NULL);
		
		#endif
		
		// make new unique SMS entry
		if (info_access == NULL) {
			
			//VG_(printf)("New sms info\n");
			info_access = (SMSInfo * ) VG_(calloc)("sms_info", 1, sizeof(SMSInfo));
			if (info_access == NULL) failure("sms_info not allocable in function exit");
			info_access->sms = activation->sms;
			#if CCT
			HT_add_node(sms_map, info_access->sms, info_access);
			#else
			HT_add_node(rtn_info->sms_map, info_access->sms, info_access);
			#endif

		}

		// bookkeeping
		info_access->partial_cumulative_time += partial_cumulative;
		info_access->partial_calls_number++;
		if (info_access->max_cumulative_time < partial_cumulative) info_access->max_cumulative_time = partial_cumulative;
		if (info_access->min_cumulative_time > partial_cumulative) info_access->min_cumulative_time = partial_cumulative;
		rtn_info->recursion_pending--;
		
		// merge accesses of current activation with those of the parent activation
		if (tdata->stack_depth > 1) {

			Activation * parent_activation = get_activation(tdata, tdata->stack_depth - 1);
			#ifdef DEBUG
			if (parent_activation == NULL) failure("Invalid parent in function exit");
			#endif

			UF_merge(tdata->accesses, tdata->stack_depth);

			parent_activation->sms                 += activation->sms;
			parent_activation->total_children_time += partial_cumulative;
			
		}
		
		tdata->stack_depth--;
		
	} else
		failure("Invalid client req\n");
	
	return True;
}