/*
 * Setup of valgrind and instrumentation  
 * 
 * Last changed: $Date$
 * Revision:     $Rev$
 */

#include "aprof.h"

static
IRSB* instrument (  VgCallbackClosure* closure, 
					IRSB* sbIn,
					VexGuestLayout* layout, 
					VexGuestExtents* vge,
					IRType gWordTy, IRType hWordTy ) {

	Int        i;
	IRSB*      sbOut;
	IRTypeEnv* tyenv = sbIn->tyenv;

	if (gWordTy != hWordTy) /* We don't currently support this case. */
		VG_(tool_panic)("host/guest word size mismatch");

	/* Set up SB */
	sbOut = deepCopyIRSBExceptStmts(sbIn);

	// Copy verbatim any IR preamble preceding the first IMark
	i = 0;
	while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
		addStmtToIRSB( sbOut, sbIn->stmts[i] );
		i++;
	}
   
	for (/*use current i*/; i < sbIn->stmts_used; i++) {
		
		IRStmt* st = sbIn->stmts[i];
		if (!st || st->tag == Ist_NoOp) continue;
		
		IRDirty * di = NULL;

		/* Count one VEX statement. */
		#if COUNT_VEX_I
		di = unsafeIRDirty_0_N( 0, "add_one_IRStmt", 
		VG_(fnptr_to_fnentry)( &add_one_IRStmt ), 
		mkIRExprVec_0() );
		addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
		#endif

		switch (st->tag) {

			case Ist_NoOp:
			case Ist_AbiHint:
			case Ist_Put:
			case Ist_PutI:
			case Ist_MBE: {
				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_IMark: {

				#if TIME == INSTR
				di = unsafeIRDirty_0_N( 0, "add_one_guest_instr",
										VG_(fnptr_to_fnentry)( &add_one_guest_instr ), 
										mkIRExprVec_0() );
				addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
				#endif

				addEvent_Ir( sbOut, mkIRExpr_HWord( (HWord)st->Ist.IMark.addr ),
								st->Ist.IMark.len );

				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_WrTmp:{

				IRExpr* data = st->Ist.WrTmp.data;
				if (data->tag == Iex_Load) {
					addEvent_Dr( sbOut, data->Iex.Load.addr,
					sizeofIRType(data->Iex.Load.ty) );
				}

				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_Store: {
			 
				IRExpr* data  = st->Ist.Store.data;
				addEvent_Dw(sbOut, st->Ist.Store.addr,
							sizeofIRType(typeOfIRExpr(tyenv, data)) );
									
				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_Dirty: {

				Int dsize;
				IRDirty* d = st->Ist.Dirty.details;
				if (d->mFx != Ifx_None) {
					// This dirty helper accesses memory.  Collect the details.
					tl_assert(d->mAddr != NULL);
					tl_assert(d->mSize != 0);
					dsize = d->mSize;
					if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
						addEvent_Dr( sbOut, d->mAddr, dsize );
					if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
						addEvent_Dw( sbOut, d->mAddr, dsize );
				} else {
					tl_assert(d->mAddr == NULL);
					tl_assert(d->mSize == 0);
				}

				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_CAS: {
				/* We treat it as a read and a write of the location.  I
				think that is the same behaviour as it was before IRCAS
				was introduced, since prior to that point, the Vex
				front ends would translate a lock-prefixed instruction
				into a (normal) read followed by a (normal) write. */
				Int    dataSize;
				IRType dataTy;
				IRCAS* cas = st->Ist.CAS.details;
				tl_assert(cas->addr != NULL);
				tl_assert(cas->dataLo != NULL);
				dataTy   = typeOfIRExpr(tyenv, cas->dataLo);
				dataSize = sizeofIRType(dataTy);
				if (cas->dataHi != NULL)
					dataSize *= 2; /* since it's a doubleword-CAS */

				addEvent_Dr( sbOut, cas->addr, dataSize );
				addEvent_Dw( sbOut, cas->addr, dataSize );

				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_LLSC: {
				IRType dataTy;
				if (st->Ist.LLSC.storedata == NULL) {
					/* LL */
					dataTy = typeOfIRTemp(tyenv, st->Ist.LLSC.result);
					addEvent_Dr( sbOut, st->Ist.LLSC.addr, sizeofIRType(dataTy) );
				} else {
					/* SC */
					dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
					addEvent_Dw( sbOut, st->Ist.LLSC.addr, sizeofIRType(dataTy) );
				}
				addStmtToIRSB( sbOut, st );
				break;
			}

			case Ist_Exit: {
				addStmtToIRSB( sbOut, st );      // Original statement
				break;
			}

			default:
				tl_assert(0);
		}
	}

	/* At the end of the sbIn.  Flush outstandings. */
	flushEvents(sbOut);

	return sbOut;
}

/* aprof initialization */
static void post_clo_init(void) {
	return;
}

/* Funzione per presentare risultati in fase finale */
static void fini(Int exitcode) {
	
	HT_destroy_pool();
	
	#if EVENTCOUNT && !TRACER
	VG_(printf)("Load: %lu\nStore: %lu\nModify: %lu\n", read_n, write_n, modify_n);
	VG_(printf)("Function entry: %lu\nFunction exit: %lu\n", fn_in_n, fn_out_n);
	VG_(printf)("Thread: %lu\n", thread_n);
	#endif

}

/* Valgrind init */
static void pre_clo_init(void) {

	VG_(details_name)				("Aprof");
	VG_(details_version)			("0.1_alpha");
	VG_(details_description)		("Asymptotic Profiler");
	VG_(details_copyright_author)	("By Camil Demetrescu, Irene Finocchi, Bruno Aleandri, Emilio Coppa");
	VG_(details_bug_reports_to)		("ercoppa@gmail.com");

	VG_(basic_tool_funcs) 				(post_clo_init, instrument, fini);
	VG_(needs_client_requests)			(trace_function);
	VG_(track_pre_thread_first_insn)	(thread_start);
	VG_(track_pre_thread_ll_exit)		(thread_exit);
}

VG_DETERMINE_INTERFACE_VERSION(pre_clo_init)
