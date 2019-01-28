#include "parmmg.h"
#include <stddef.h>

typedef struct {
  double min;
  int iel, iel_grp, cpu;
} min_iel_t;

static void PMMG_min_iel_compute( void* in1, void* out1, int *len, MPI_Datatype *dptr )
{
  min_iel_t *in;
  min_iel_t *out;

  in = (min_iel_t*) in1;
  out = (min_iel_t*) out1;
  (void)dptr;
  for (int i=0; i<*len; i++) {
    if ( in[ i ].min < out[ i ]. min ) {
      out[ i ].min = in[ i ].min;
      out[ i ].iel = in[ i ].iel;
      out[ i ].iel_grp = in[ i ].iel_grp;
      out[ i ].cpu = in[ i ].cpu;
    }
  }
}

/**
 * \param parmesh pointer to parmesh structure
 *
 * \return 1 if success, 0 if fail;
 *
 * Print quality histogram among all group meshes and all processors
 */
int PMMG_qualhisto( PMMG_pParMesh parmesh, int opt )
{
  PMMG_pGrp    grp;
  int          i, j, iel_grp;
  int          ne, ne_cur, ne_result;
  double       max, max_cur, max_result;
  double       avg, avg_cur, avg_result;
  double       min, min_cur;
  int          iel, iel_cur;
  int          good, good_cur,good_result,optimLES,optimLES_result;
  int          med, med_cur, med_result;
  int          his[PMMG_QUAL_HISSIZE],his_cur[PMMG_QUAL_HISSIZE],his_result[PMMG_QUAL_HISSIZE];
  int          nrid, nrid_cur, nrid_result,ier;
  MPI_Op       iel_min_op;
  MPI_Datatype mpi_iel_min_t;
  MPI_Datatype types[ PMMG_QUAL_MPISIZE ] = { MPI_DOUBLE, MPI_INT, MPI_INT, MPI_INT };
  min_iel_t    min_iel, min_iel_result = { DBL_MAX, 0, 0, 0 };
  MPI_Aint     disps[ PMMG_QUAL_MPISIZE ] = { offsetof( min_iel_t, min ),
                                           offsetof( min_iel_t, iel ),
                                           offsetof( min_iel_t, iel_grp ),
                                           offsetof( min_iel_t, cpu ) };
  int lens[ PMMG_QUAL_MPISIZE ]           = { 1, 1, 1, 1 };

  /* Calculate the quality values for local process */
  iel_grp = 0;
  ne = 0;
  max = DBL_MIN;
  avg = 0.;
  min = DBL_MAX;
  iel = 0;
  good = 0;
  med = 0;
  grp = &parmesh->listgrp[0];
  optimLES = ( grp && grp->mesh ) ? grp->mesh->info.optimLES : 0;

  for ( i = 0; i < PMMG_QUAL_HISSIZE; ++i )
    his[ i ] = 0;

  nrid = 0;
  for ( i = 0; i < parmesh->ngrp; ++i ) {
    grp  = &parmesh->listgrp[ i ];

    nrid_cur = 0;

    if ( grp->mesh->info.optimLES ) {
      MMG3D_computeLESqua(grp->mesh,grp->met,&ne_cur,&max_cur,&avg_cur,&min_cur,
                          &iel_cur,&good_cur,&med_cur,his_cur,parmesh->info.imprim);
    }
    else {
      if ( opt == PMMG_INQUA ) {
        MMG3D_computeInqua( grp->mesh, grp->met, &ne_cur, &max_cur, &avg_cur, &min_cur,
                             &iel_cur, &good_cur, &med_cur, his_cur,parmesh->info.imprim );
      }
      else {
        assert ( opt == PMMG_OUTQUA );
        MMG3D_computeOutqua( grp->mesh, grp->met, &ne_cur, &max_cur, &avg_cur, &min_cur,
                             &iel_cur, &good_cur, &med_cur, his_cur, &nrid_cur,parmesh->info.imprim );

      }
    }

    if ( parmesh->info.imprim0 <= PMMG_VERB_VERSION )
      return 1;

    ne   += ne_cur;
    avg  += avg_cur;
    med  += med_cur;
    good += good_cur;

    if ( max_cur > max )
      max = max_cur;

    if ( min_cur < min ) {
      min = min_cur;
      iel = iel_cur;
      iel_grp = i;
    }

    for ( j = 0; j < PMMG_QUAL_HISSIZE; ++j )
      his[ j ] += his_cur[ j ];

    nrid += nrid_cur;
  }

  /* Calculate the quality values for all processes */
  MPI_Reduce( &ne, &ne_result, 1, MPI_INT, MPI_SUM, 0, parmesh->comm );
  MPI_Reduce( &avg, &avg_result, 1, MPI_DOUBLE, MPI_SUM, 0, parmesh->comm );
  MPI_Reduce( &med, &med_result, 1, MPI_INT, MPI_SUM, 0, parmesh->comm );
  MPI_Reduce( &good, &good_result, 1, MPI_INT, MPI_SUM, 0, parmesh->comm );
  MPI_Reduce( &max, &max_result, 1, MPI_DOUBLE, MPI_MAX, 0, parmesh->comm );
  MPI_Reduce( &optimLES,&optimLES_result,1,MPI_INT,MPI_MAX,0,parmesh->comm );


  MPI_Type_create_struct( PMMG_QUAL_MPISIZE, lens, disps, types, &mpi_iel_min_t );
  MPI_Type_commit( &mpi_iel_min_t );
  MPI_Op_create( PMMG_min_iel_compute, 1, &iel_min_op );
  min_iel.min = min;
  min_iel.iel = iel;
  min_iel.iel_grp = iel_grp;
  min_iel.cpu = parmesh->myrank;
  MPI_Reduce( &min_iel, &min_iel_result, 1, mpi_iel_min_t, iel_min_op, 0, parmesh->comm );
  MPI_Op_free( &iel_min_op );

  MPI_Reduce( his, his_result, PMMG_QUAL_HISSIZE, MPI_INT, MPI_SUM, 0, parmesh->comm );
  MPI_Reduce( &nrid, &nrid_result, 1, MPI_INT, MPI_SUM, 0, parmesh->comm );

  if ( parmesh->myrank == 0 ) {

    if ( parmesh->info.imprim > PMMG_VERB_VERSION ) {
      fprintf(stdout,"\n  -- PARALLEL MESH QUALITY");

      if ( optimLES_result ) {
        fprintf( stdout," (LES)" );
      }

      fprintf( stdout, "  %d\n", ne_result );

      fprintf( stdout, "     BEST   %8.6f  AVRG.   %8.6f  WRST.   %8.6f (",
               max_result, avg_result / ne_result, min_iel_result.min);

      if ( parmesh->ngrp>1 )
        fprintf( stdout, "GROUP %d - ",min_iel_result.iel_grp);

      if ( parmesh->nprocs>1 )
        fprintf( stdout, "PROC %d - ",min_iel_result.cpu);

      fprintf( stdout,"ELT %d)\n", min_iel_result.iel );
    }

    ier =
      MMG3D_displayQualHisto_internal( ne_result, max_result, avg_result,
                                       min_iel_result.min, min_iel_result.iel,
                                       good_result, med_result, his_result,
                                       nrid_result,optimLES_result,
                                       parmesh->info.imprim );
    if ( !ier ) return 0;
  }

  return 1;
}
