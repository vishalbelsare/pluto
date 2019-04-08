/*
 * PLUTO: An automatic parallelizer and locality optimizer
 * 
 * Copyright (C) 2007-2012 Uday Bondhugula
 *
 * This file is part of Pluto.
 *
 * Pluto is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU General Public Licence can be found in the file
 * `LICENSE' in the top-level directory of this distribution. 
 *
 */
#include <stdlib.h>
#include <stdio.h>

#include "pluto.h"
#include "program.h"
#include "ast_transform.h"

#include "cloog/cloog.h"

/*
 * Clast-based parallel loop marking */
void pluto_mark_parallel(struct clast_stmt *root, const PlutoProg *prog,
        CloogOptions *cloogOptions)
{
    unsigned i, j, nloops, nstmts, nploops, k;
    unsigned npbands, ninloops;
    struct clast_for **loops;
    int *stmts;
    Band **pbands;
    int innermost_split_level;
    Ploop **inloops, *curr_loop;
    assert(root != NULL);

    // int filter[1] = {1};

    /* get dominating band instead of a dominating loop */
    Ploop **ploops = pluto_get_dom_parallel_loops(prog, &nploops);
    pbands = (Band **)malloc(nploops*sizeof(Band*));
    // pluto_print_depsat_vectors(prog->deps, prog->ndeps, prog->num_hyperplanes);

    IF_DEBUG(printf("[pluto_mark_parallel] parallel loops\n"););
    IF_DEBUG(pluto_loops_print(ploops, nploops););

    for (i=0; i<nploops; i++) {
       pbands[i] = pluto_get_parallel_band(ploops[i], prog, &innermost_split_level);
       assert (pbands[i]->width > 0);
    }
    npbands = nploops;
    // clast_pprint(stdout, root, 0, cloogOptions);

    /* iterate over bands and for each loop in the band do the following */
    /* Note that the number of bands is equal to the number of dominating parallel loops */
    //
    // Band iterator
    for (k=0; k<npbands; k++) {
        /* Iterate over loops in a band */
        Band *band = pbands[k];
        curr_loop = pbands[k]->loop;
        /* replace this with a do while loop */
        /* for (i=band->loop->depth; i<band->loop->depth + band->width; i++) { */
        do {
            /* A scalar dimension can not be parallelized */
            /* if (pluto_is_depth_scalar(curr_loop, i)) { */
            /*     continue; */
            /* } */
            char iter[13];
            sprintf(iter, "t%d", curr_loop->depth+1);
            int *stmtids = malloc(curr_loop->nstmts*sizeof(int));
            int max_depth = 0;
            for (j=0; j<curr_loop->nstmts; j++) {
                Stmt *stmt = curr_loop->stmts[j];
                if (stmt->trans->nrows > max_depth) max_depth = stmt->trans->nrows;
                stmtids[j] = stmt->id+1;
            }

            IF_DEBUG(printf("\tLooking for loop\n"););
            IF_DEBUG(pluto_loop_print(curr_loop););
            // IF_DEBUG(clast_pprint(stdout, root, 0, cloogOptions););

            ClastFilter filter = {iter, stmtids, curr_loop->nstmts, subset};
            clast_filter(root, filter, &loops, (int *)&nloops, &stmts, (int *)&nstmts);

            /* There should be at least one */
            if (nloops==0) {
                /* in this fails, reiterate over the other loops in the band */
                /* Sometimes loops may disappear (1) tile size larger than trip count
                 * 2) it's a scalar dimension but can't be determined from the
                 * trans matrix */

                inloops = pluto_get_loops_immediately_inner (curr_loop, prog, &ninloops);

/* TODO: Fix this. Ideally a cloog clast has to be created for each loop till it succeeds. Hence a worklist based algrithm is necessary. */
                /* curr_loop = inloop[0]; */
                /* No parallel loops in this band */
                if (ninloops == 0 ||
                        inloops[0]->depth > band->loop->depth + band->width) {
                    printf("Warning: parallel poly loop not found in AST\n");
                    curr_loop = NULL;
                } else {
                    curr_loop = inloops[0];
                }
                /* continue; */
            }else{
                for (j=0; j<nloops; j++) {
                    loops[j]->parallel = CLAST_PARALLEL_NOT;
                    char *private_vars = malloc(512);
                    strcpy(private_vars, "lbv,ubv");
                    if (options->parallel) {
                        IF_DEBUG(printf("Marking %s parallel\n", loops[j]->iterator););
                        loops[j]->parallel = CLAST_PARALLEL_OMP;
                        int depth = curr_loop->depth+1;
                        for (depth++;depth<=max_depth;depth++) {
                            sprintf(private_vars+strlen(private_vars), ",t%d", depth);
                        }
                    }
                    loops[j]->private_vars = strdup(private_vars);
                    free(private_vars);
                }
                /* A loop has been marked parallel. Move to the next band */
                break;
            }
            free(stmtids);
            free(loops);
            free(stmts);
        } while (curr_loop!=NULL);
    }

    pluto_loops_free(ploops, nploops);
}


/*
 * Clast-based vector loop marking */
void pluto_mark_vector(struct clast_stmt *root, const PlutoProg *prog,
        CloogOptions *cloogOptions)
{
    unsigned i, j, nloops, nstmts, nploops;
    struct clast_for **loops;
    int *stmts;
    assert(root != NULL);

    Ploop **ploops = pluto_get_parallel_loops(prog, &nploops);

    IF_DEBUG(printf("[pluto_mark_vector] parallel loops\n"););
    IF_DEBUG(pluto_loops_print(ploops, nploops););

    // pluto_print_depsat_vectors(prog->deps, prog->ndeps, prog->num_hyperplanes);
    // clast_pprint(stdout, root, 0, cloogOptions);

    for (i=0; i<nploops; i++) {
        /* Only the innermost ones */
        if (!pluto_loop_is_innermost(ploops[i], prog)) continue;

        IF_DEBUG(printf("[pluto_mark_vector] marking loop vectorizable\n"););
        IF_DEBUG(pluto_loop_print(ploops[i]););
        char iter[13];
        sprintf(iter, "t%d", ploops[i]->depth+1);
        int *stmtids = malloc(ploops[i]->nstmts*sizeof(int));
        for (j=0; j<ploops[i]->nstmts; j++) {
            stmtids[j] = ploops[i]->stmts[j]->id+1;
        }

        // IF_DEBUG(printf("Looking for loop\n"););
        // IF_DEBUG(pluto_loop_print(ploops[i]););
        // IF_DEBUG(printf("%s in \n", iter););
        // IF_DEBUG(clast_pprint(stdout, root, 0, cloogOptions););

        ClastFilter filter = {iter, stmtids, ploops[i]->nstmts, subset};
        clast_filter(root, filter, &loops, (int *)&nloops, &stmts, (int *)&nstmts);

        /* There should be at least one */
        if (nloops==0) {
            /* Sometimes loops may disappear (1) tile size larger than trip count
             * 2) it's a scalar dimension but can't be determined from the
             * trans matrix */
            printf("[pluto] pluto_mark_vector: WARNING: vectorizable poly loop not found in AST\n");
            continue;
        }
        for (j=0; j<nloops; j++) {
            // printf("\tMarking %s ivdep\n", loops[j]->iterator);
            loops[j]->parallel += CLAST_PARALLEL_VEC;
        }
        free(stmtids);
        free(loops);
        free(stmts);
    }

    pluto_loops_free(ploops, nploops);
}
