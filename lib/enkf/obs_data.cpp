/*
   Copyright (C) 2011  Equinor ASA, Norway.

   The file 'obs_data.c' is part of ERT - Ensemble based Reservoir Tool.

   ERT is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   ERT is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.

   See the GNU General Public License at <http://www.gnu.org/licenses/gpl.html>
   for more details.
*/

/**
See the file README.obs for ducumentation of the varios datatypes
involved with observations/measurement/+++.


The file contains two different variables holding the number of
observations, nrobs_total and nrobs_active. The first holds the total
number of observations at this timestep, and the second holds the
number of active measurements at this timestep; the inactive
measurements have been deactivated the obs_data_deactivate_outliers()
function.

The flow is as follows:

 1. All the observations have been collected in an obs_data instance,
    and all the corresponding measurements of the state have been
    collected in a meas_data instance - we are ready for analysis.

 2. The functions meas_data_alloc_stats() is called to calculate
    the ensemble mean and std of all the measurements.

 3. The function obs_data_deactivate_outliers() is called to compare
    the ensemble mean and std with the observations, in the case of
    outliers the number obs_active flag of the obs_data instance is
    set to false.

 4. The remaining functions (and matrices) now refer to the number of
    active observations, however the "raw" observations found in the
    obs_data instance are in a vector with nrobs_total observations;
    i.e. we must handle two indices and two total lengths. A bit
    messy.


Variables of size nrobs_total:
------------------------------
 o obs->value / obs->std / obs->obs_active
 o meanS , innov, stdS


variables of size nrobs_active:
-------------------------------
Matrices: S, D, E and various internal variables.
*/


#include <stdlib.h>
#include <cmath>
#include <stdio.h>
#include <pthread.h>

#include <ert/util/util.h>
#include <ert/util/vector.h>
#include <ert/res_util/matrix.hpp>
#include <ert/util/rng.h>
#include <ert/util/bool_vector.h>

#include <ert/enkf/obs_data.hpp>
#include <ert/enkf/meas_data.hpp>
#include <ert/enkf/enkf_util.hpp>

#define OBS_BLOCK_TYPE_ID 995833

struct obs_block_struct {
  UTIL_TYPE_ID_DECLARATION;
  char               * obs_key;
  int                  size;
  double             * value;
  double             * std;

  active_type        * active_mode;
  int                  active_size;
  matrix_type        * error_covar;
  bool                 error_covar_owner;   /* If true the error_covar matrix is free'd when construction of the R matrix is complete. */
  double               global_std_scaling;
};



struct obs_data_struct {
  vector_type      * data;            /* vector with obs_block instances. */
  bool_vector_type * mask;
  double             global_std_scaling;
};



static UTIL_SAFE_CAST_FUNCTION(obs_block , OBS_BLOCK_TYPE_ID )

obs_block_type * obs_block_alloc( const char * obs_key , int obs_size , matrix_type * error_covar , bool error_covar_owner, double global_std_scaling) {
  obs_block_type * obs_block = (obs_block_type *)util_malloc( sizeof * obs_block );

  UTIL_TYPE_ID_INIT( obs_block , OBS_BLOCK_TYPE_ID );
  obs_block->size        = obs_size;
  obs_block->obs_key     = util_alloc_string_copy( obs_key );
  obs_block->value       = (double *) util_calloc( obs_size , sizeof * obs_block->value       );
  obs_block->std         = (double *) util_calloc( obs_size , sizeof * obs_block->std         );
  obs_block->active_mode = (active_type *) util_calloc( obs_size , sizeof * obs_block->active_mode );
  obs_block->error_covar = error_covar;
  obs_block->error_covar_owner = error_covar_owner;
  obs_block->global_std_scaling = global_std_scaling;
  {
    for (int iobs = 0; iobs < obs_size; iobs++)
      obs_block->active_mode[iobs] = LOCAL_INACTIVE;
  }
  obs_block->active_size = 0;
  return obs_block;
}



void obs_block_free( obs_block_type * obs_block ) {
  free( obs_block->obs_key );
  free( obs_block->value );
  free( obs_block->std );
  free( obs_block->active_mode );
  free( obs_block );
}


static void obs_block_free__( void * arg ) {
  obs_block_type * obs_block = obs_block_safe_cast( arg );
  obs_block_free( obs_block );
}


static void obs_block_fprintf( const obs_block_type * obs_block , FILE * stream ) {
  for (int iobs=0; iobs < obs_block->size; iobs++)
    fprintf(stream , "[ %12.5f  +/-  %12.5f ] \n" , obs_block->value[iobs] , obs_block->std[iobs]);
}


void obs_block_deactivate( obs_block_type * obs_block , int iobs , bool verbose , const char * msg) {
  if (obs_block->active_mode[ iobs ] == ACTIVE) {
    if (verbose)
      printf("Deactivating: %s(%d) : %s \n",obs_block->obs_key , iobs , msg);
    obs_block->active_mode[ iobs ] = DEACTIVATED;
    obs_block->active_size--;
  }
}


const char * obs_block_get_key( const obs_block_type * obs_block) { return obs_block->obs_key; }

void obs_block_iset( obs_block_type * obs_block , int iobs , double value , double std) {
  obs_block->value[ iobs ] = value;
  obs_block->std[ iobs ]   = std;
  if (obs_block->active_mode[ iobs ] != ACTIVE) {
    obs_block->active_mode[iobs]  = ACTIVE;
    obs_block->active_size++;
  }
}

void obs_block_iset_missing( obs_block_type * obs_block , int iobs ) {
  if (obs_block->active_mode[ iobs ] == ACTIVE)
    obs_block->active_size--;
  obs_block->active_mode[iobs] = MISSING;
}


double obs_block_iget_std( const obs_block_type * obs_block , int iobs) {
  return obs_block->std[ iobs ] * obs_block->global_std_scaling;
}


double obs_block_iget_value( const obs_block_type * obs_block , int iobs) {
  return obs_block->value[ iobs ];
}


active_type obs_block_iget_active_mode( const obs_block_type * obs_block , int iobs) {
  return obs_block->active_mode[ iobs ];
}



int obs_block_get_size( const obs_block_type * obs_block ) {
  return obs_block->size;
}


int obs_block_get_active_size( const obs_block_type * obs_block ) {
  return obs_block->active_size;
}




/*Function that sets each element of the scaling factor equal to 1 divided by the prior standard deviation (from the
  obs_data input file.
*/
static void obs_block_init_scaling( const obs_block_type * obs_block , double * scale_factor , int * __obs_offset) {
  int obs_offset = *__obs_offset;
  int iobs;
  for (iobs =0; iobs < obs_block->size; iobs++) {
    if (obs_block->active_mode[iobs] == ACTIVE) {
      scale_factor[ obs_offset ] = 1.0 / obs_block_iget_std(obs_block, iobs);
      obs_offset++;
    }
  }
  *__obs_offset = obs_offset;
}


/*
static void obs_block_init_innov( const obs_block_type * obs_block , const meas_block_type * meas_block , matrix_type * innov , int * __obs_offset) {
  int obs_offset = *__obs_offset;
  int iobs;
  for (iobs =0; iobs < obs_block->size; iobs++) {
    if (obs_block->active_mode[iobs] == ACTIVE) {
      matrix_iset( innov , obs_offset , 0 , obs_block->value[ iobs ] - meas_block_iget_ens_mean( meas_block , iobs ));
      obs_offset++;
    }
  }
  *__obs_offset = obs_offset;
}
*/

static void obs_block_initdObs( const obs_block_type * obs_block , matrix_type * dObs , int * __obs_offset) {
  int obs_offset = *__obs_offset;
  int iobs;
  for (iobs =0; iobs < obs_block->size; iobs++) {
    if (obs_block->active_mode[iobs] == ACTIVE) {
      matrix_iset( dObs , obs_offset , 0 , obs_block->value[ iobs ]);
      matrix_iset( dObs , obs_offset , 1 , obs_block->std[ iobs ]);
      obs_offset++;
    }
  }
  *__obs_offset = obs_offset;
}






static void obs_block_initR( const obs_block_type * obs_block , matrix_type * R, int * __obs_offset) {
  int obs_offset = *__obs_offset;
  if (obs_block->error_covar == NULL) {
    int iobs;
    int iactive = 0;
    for (iobs =0; iobs < obs_block->size; iobs++) {
      if (obs_block->active_mode[iobs] == ACTIVE) {
        double var = obs_block_iget_std(obs_block, iobs) * obs_block_iget_std(obs_block, iobs);
        matrix_iset_safe(R , obs_offset + iactive, obs_offset + iactive, var);
        iactive++;
      }
    }
  } else {
    int row_active = 0;   /* We have a covar matrix */
    for (int row = 0; row < obs_block->size; row++) {
      if (obs_block->active_mode[row] == ACTIVE) {
        int col_active = 0;
        for (int col = 0; col < obs_block->size; col++) {
          if (obs_block->active_mode[col] == ACTIVE) {
            matrix_iset_safe(R , obs_offset + row_active , obs_offset + col_active , matrix_iget( obs_block->error_covar , row , col ));
            col_active++;
          }
        }
        row_active++;
      }
    }
  }

  *__obs_offset = obs_offset + obs_block->active_size;
  if ((obs_block->error_covar_owner) && (obs_block->error_covar != NULL))
    matrix_free( obs_block->error_covar );
}



static void obs_block_initE( const obs_block_type * obs_block , matrix_type * E, const double * pert_var , int * __obs_offset) {
  int ens_size   = matrix_get_columns( E );
  int obs_offset = *__obs_offset;
  int iobs;
  for (iobs =0; iobs < obs_block->size; iobs++) {
    if (obs_block->active_mode[iobs] == ACTIVE) {
      double factor = obs_block_iget_std(obs_block, iobs) * sqrt( ens_size / pert_var[ obs_offset ]);
      for (int iens = 0; iens < ens_size; iens++)
        matrix_imul(E , obs_offset , iens , factor );

      obs_offset++;
    }
  }

  *__obs_offset = obs_offset;
}


static void obs_block_initE_non_centred( const obs_block_type * obs_block , matrix_type * E, int * __obs_offset) {
  int ens_size   = matrix_get_columns( E );
  int obs_offset = *__obs_offset;
  int iobs;
  for (iobs =0; iobs < obs_block->size; iobs++) {
    if (obs_block->active_mode[iobs] == ACTIVE) {
      double factor = obs_block_iget_std(obs_block, iobs);
      for (int iens = 0; iens < ens_size; iens++)
        matrix_imul(E , obs_offset , iens , factor );

      obs_offset++;
    }
  }

  *__obs_offset = obs_offset;
}



static void obs_block_initD( const obs_block_type * obs_block , matrix_type * D, int * __obs_offset) {
  int ens_size   = matrix_get_columns( D );
  int obs_offset = *__obs_offset;
  int iobs;
  for (iobs =0; iobs < obs_block->size; iobs++) {
    if (obs_block->active_mode[iobs] == ACTIVE) {
      for (int iens = 0; iens < ens_size; iens++)
        matrix_iadd(D , obs_offset , iens , obs_block->value[ iobs ]);

      obs_offset++;
    }
  }

  *__obs_offset = obs_offset;
}


static void obs_block_set_active_mask( const obs_block_type * obs_block, bool_vector_type * mask, int * offset) {
  for (int i = 0; i < obs_block->size; i++) {
    if (obs_block->active_mode[i] == ACTIVE)
      bool_vector_iset(mask, *offset, true);
    else
      bool_vector_iset(mask, *offset, false);
    (*offset)++;
  }   
}


/*****************************************************************/


obs_data_type * obs_data_alloc(double global_std_scaling) {
  obs_data_type * obs_data = (obs_data_type *)util_malloc(sizeof * obs_data );
  obs_data->data = vector_alloc_new();
  obs_data->mask = bool_vector_alloc(0, false);
  obs_data->global_std_scaling = global_std_scaling;
  obs_data_reset(obs_data);
  return obs_data;
}



void obs_data_reset(obs_data_type * obs_data) {
  vector_clear( obs_data->data );
}


obs_block_type * obs_data_add_block( obs_data_type * obs_data , const char * obs_key , int obs_size , matrix_type * error_covar, bool error_covar_owner) {
  obs_block_type * new_block = obs_block_alloc( obs_key , obs_size , error_covar , error_covar_owner, obs_data->global_std_scaling);
  vector_append_owned_ref( obs_data->data , new_block , obs_block_free__ );
  return new_block;
}


obs_block_type * obs_data_iget_block( obs_data_type * obs_data , int index ) {
  return (obs_block_type * ) vector_iget( obs_data->data , index); // CXX_CAST_ERROR
}


const obs_block_type * obs_data_iget_block_const( const obs_data_type * obs_data , int index ) {
  return (const obs_block_type * ) vector_iget_const( obs_data->data , index ); // CXX_CAST_ERROR
}


void obs_data_free(obs_data_type * obs_data) {
  vector_free( obs_data->data );
  bool_vector_free( obs_data->mask );
  free(obs_data);
}



matrix_type * obs_data_allocE(const obs_data_type * obs_data , rng_type * rng , int active_ens_size ) {
  double *pert_mean , *pert_var;
  matrix_type * E;
  int iens, iobs_active;
  int active_obs_size = obs_data_get_active_size( obs_data );

  E         = matrix_alloc( active_obs_size , active_ens_size);

  pert_mean = (double *) util_calloc(active_obs_size , sizeof * pert_mean );
  pert_var  = (double *) util_calloc(active_obs_size , sizeof * pert_var  );
  {

    for (int j=0; j < active_ens_size; j++) {
      for (int i=0; i < active_obs_size; i++) {
        matrix_iset( E , i , j , enkf_util_rand_normal(0, 1, rng));
      }
    }
  }

  for (iobs_active = 0; iobs_active < active_obs_size; iobs_active++) {
    pert_mean[iobs_active] = 0;
    pert_var[iobs_active]  = 0;
  }

  for (iens = 0; iens < active_ens_size; iens++)
    for (iobs_active = 0; iobs_active < active_obs_size; iobs_active++)
      pert_mean[iobs_active] += matrix_iget(E , iobs_active , iens);


  for (iobs_active = 0; iobs_active < active_obs_size; iobs_active++)
    pert_mean[iobs_active] /= active_ens_size;

  for  (iens = 0; iens < active_ens_size; iens++) {
    for (iobs_active = 0; iobs_active < active_obs_size; iobs_active++) {
      double tmp;
      matrix_iadd(E , iobs_active , iens , -pert_mean[iobs_active]);
      tmp = matrix_iget(E , iobs_active , iens);
      pert_var[iobs_active] += tmp * tmp;
    }
  }

  /*
    The actual observed data are not accessed before this last block.
  */
  {
    int obs_offset = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr);
      obs_block_initE( obs_block , E , pert_var , &obs_offset);
    }
  }

  free(pert_mean);
  free(pert_var);

  matrix_set_name( E , "E");
  matrix_assert_finite( E );
  return E;
}


matrix_type * obs_data_allocD(const obs_data_type * obs_data , const matrix_type * E  , const matrix_type * S) {
  matrix_type * D = matrix_alloc_copy( E );
  matrix_inplace_sub( D , S );

  {
    int obs_offset = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr);
      obs_block_initD( obs_block , D , &obs_offset);
    }
  }

  matrix_set_name( D , "D");
  matrix_assert_finite( D );
  return D;
}




matrix_type * obs_data_allocR(const obs_data_type * obs_data) {
  int active_size = obs_data_get_active_size( obs_data );
  matrix_type * R = matrix_alloc( active_size , active_size );
  {
    int obs_offset = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr);
      obs_block_initR( obs_block , R , &obs_offset);
    }
  }

  matrix_set_name( R , "R");
  matrix_assert_finite( R );
  return R;
}

/*
matrix_type * obs_data_alloc_innov(const obs_data_type * obs_data , const meas_data_type * meas_data , int active_size) {
  matrix_type * innov = matrix_alloc( active_size , 1 );
  {
    int obs_offset = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr );
      const meas_block_type * meas_block = meas_data_iget_block_const( meas_data , block_nr );

      obs_block_init_innov( obs_block , meas_block , innov , &obs_offset);
    }
  }
  return innov;
}
*/

matrix_type * obs_data_allocdObs(const obs_data_type * obs_data ) {
  int active_size = obs_data_get_active_size( obs_data );
  matrix_type * dObs = matrix_alloc( active_size , 2 );
  {
    int obs_offset = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr );

      obs_block_initdObs( obs_block ,  dObs , &obs_offset);
    }
  }
  return dObs;
}


static void obs_data_scale_matrix__(matrix_type * m , const double * scale_factor) {
  const int rows    = matrix_get_rows( m );
  const int columns = matrix_get_columns( m );
  int i, j;

  for  (i = 0; i < columns; i++)
    for (j = 0; j < rows; j++)
      matrix_imul(m , j,i, scale_factor[j]);

}


static void obs_data_scale_Rmatrix__( matrix_type * R , const double * scale_factor) {
  int nrobs_active = matrix_get_rows( R );

  /* Scale the error covariance matrix*/
  for (int i=0; i < nrobs_active; i++)
      for (int j=0; j < nrobs_active; j++)
        matrix_imul(R , i , j , scale_factor[i] * scale_factor[j]);
}


static double * obs_data_alloc_scale_factor(const obs_data_type * obs_data ) {
  int nrobs_active = obs_data_get_active_size( obs_data );
  double * scale_factor = (double *)util_calloc(nrobs_active , sizeof * scale_factor );
  int obs_offset = 0;
  for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
    const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr );

    /* Init. the scaling factor ( 1/std(dObs) ) */
    obs_block_init_scaling( obs_block , scale_factor  , &obs_offset);
  }

  return scale_factor;
}


void obs_data_scale_matrix(const obs_data_type * obs_data , matrix_type * matrix) {
  double * scale_factor  = obs_data_alloc_scale_factor( obs_data );
  obs_data_scale_matrix__( matrix , scale_factor );
  free( scale_factor );
}


void obs_data_scale_Rmatrix(const obs_data_type * obs_data , matrix_type * R) {
  double * scale_factor  = obs_data_alloc_scale_factor( obs_data );
  obs_data_scale_Rmatrix__( R , scale_factor );
  free( scale_factor );
}


void obs_data_scale(const obs_data_type * obs_data , matrix_type *S , matrix_type *E , matrix_type *D , matrix_type *R , matrix_type * dObs) {
  double * scale_factor  = obs_data_alloc_scale_factor( obs_data );

  /* Scale the forecasted data so that they (in theory) have the same variance
     (if the prior distribution for the observation errors is correct) */
  obs_data_scale_matrix__( S , scale_factor );

  /* Scale the combined data matrix: D = DObs + E - S, where DObs is the iobs_active times ens_size matrix where
     each column contains a copy of the observed data
  */
  if (D != NULL)
    obs_data_scale_matrix__( D , scale_factor );

  /* Same with E (used for low rank representation of the error covariance matrix*/
  if (E != NULL)
    obs_data_scale_matrix__( E , scale_factor );

  if (dObs != NULL)
    obs_data_scale_matrix__( dObs , scale_factor );

  if (R != NULL)
    obs_data_scale_Rmatrix__(R , scale_factor);

  free(scale_factor);
}


int obs_data_get_active_size( const obs_data_type * obs_data ) {
    int active_size = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr );
      active_size += obs_block->active_size;
    }

    return active_size;
}


int obs_data_get_num_blocks( const obs_data_type * obs_data ) {
  return vector_get_size( obs_data->data );
}



int obs_data_get_total_size( const obs_data_type * obs_data ) {
    int total_size = 0;
    for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
      const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr );
      total_size += obs_block->size;
    }
    return total_size;
}


static const obs_block_type * obs_data_lookup_block( const obs_data_type * obs_data, int total_index , int * block_offset) {
  if (total_index < obs_data_get_total_size( obs_data )) {
      const obs_block_type * obs_block;
      int total_offset = 0;
      int block_index = 0;
      int block_size;


      while (true) {
        obs_block   = (const obs_block_type * ) vector_iget_const( obs_data->data , block_index );
        block_size = obs_block->size;
        if ((block_size + total_offset) > total_index)
          break;

        total_offset += block_size;
        block_index++;
      }
      *block_offset = total_offset;
      return obs_block;
  } else {
    util_abort("%s: could not lookup obs-block \n",__func__);
    return NULL;
  }
}


double obs_data_iget_value( const obs_data_type * obs_data , int total_index ) {
  int total_offset;
  const obs_block_type * obs_block = obs_data_lookup_block( obs_data , total_index , &total_offset );
  return obs_block_iget_value( obs_block , total_index - total_offset );
}


double obs_data_iget_std( const obs_data_type * obs_data , int total_index ) {
  int total_offset;
  const obs_block_type * obs_block = obs_data_lookup_block( obs_data , total_index , &total_offset );
  return obs_block_iget_std( obs_block , total_index - total_offset );
}


const bool_vector_type * obs_data_get_active_mask( const obs_data_type * obs_data ) {
  int total_size = obs_data_get_total_size( obs_data );
  bool_vector_resize(obs_data->mask, total_size, false);  //too account for extra data blocks added/removed

  int offset = 0;
  for (int block_nr = 0; block_nr < vector_get_size( obs_data->data ); block_nr++) {
    const obs_block_type * obs_block = (const obs_block_type *)vector_iget_const( obs_data->data , block_nr );
    obs_block_set_active_mask( obs_block, obs_data->mask, &offset);
  }
  
  return obs_data->mask;
}
