#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <enkf_types.h>
#include <enkf_state.h>
#include <util.h>
#include <havana_fault_config.h>
#include <havana_fault.h>
#include <enkf_util.h>
#include <math.h>
#include <scalar.h>
#include <assert.h>

#define  DEBUG
#define  TARGET_TYPE HAVANA_FAULT
#include "enkf_debug.h"


GET_DATA_SIZE_HEADER(havana_fault);


struct havana_fault_struct 
{
  DEBUG_DECLARE
  const havana_fault_config_type *config;
  scalar_type                    *scalar;
};

/*****************************************************************/

void havana_fault_free_data(havana_fault_type *havana_fault) {
  assert(havana_fault->scalar);
  scalar_free_data(havana_fault->scalar);
}



void havana_fault_free(havana_fault_type *havana_fault) {
  scalar_free(havana_fault->scalar);
  free(havana_fault);
}



void havana_fault_realloc_data(havana_fault_type *havana_fault) {
  scalar_realloc_data(havana_fault->scalar);
}


void havana_fault_output_transform(const havana_fault_type * havana_fault) {
  scalar_transform(havana_fault->scalar);
}

void havana_fault_set_data(havana_fault_type * havana_fault , const double * data) {
  scalar_set_data(havana_fault->scalar , data);
}


void havana_fault_get_data(const havana_fault_type * havana_fault , double * data) {
  scalar_get_data(havana_fault->scalar , data);
}

void havana_fault_get_output_data(const havana_fault_type * havana_fault , double * output_data) {
  scalar_get_output_data(havana_fault->scalar , output_data);
}


const double * havana_fault_get_data_ref(const havana_fault_type * havana_fault) {
  return scalar_get_data_ref(havana_fault->scalar);
}


const double * havana_fault_get_output_ref(const havana_fault_type * havana_fault) {
  return scalar_get_output_ref(havana_fault->scalar);
}



havana_fault_type * havana_fault_alloc(const havana_fault_config_type * config) {
  havana_fault_type * havana_fault  = malloc(sizeof * havana_fault);
  havana_fault->config = config;
  gen_kw_config_type * gen_kw_config = config->gen_kw_config;
  havana_fault->scalar               = scalar_alloc(gen_kw_config->scalar_config); 
  DEBUG_ASSIGN(havana_fault)
  return havana_fault;
}



void havana_fault_clear(havana_fault_type * havana_fault) {
  scalar_clear(havana_fault->scalar);
}



havana_fault_type * havana_fault_copyc(const havana_fault_type *havana_fault) {
  havana_fault_type * new = havana_fault_alloc(havana_fault->config); 
  scalar_memcpy(new->scalar , havana_fault->scalar);
  return new; 
}


void havana_fault_fwrite(const havana_fault_type *havana_fault , FILE * stream) {
  DEBUG_ASSERT(havana_fault)
  enkf_util_fwrite_target_type(stream , HAVANA_FAULT);
  scalar_stream_fwrite(havana_fault->scalar , stream);
}


void havana_fault_fread(havana_fault_type * havana_fault , FILE * stream) {
  DEBUG_ASSERT(havana_fault)
  enkf_util_fread_assert_target_type(stream , HAVANA_FAULT , __func__);
  scalar_stream_fread(havana_fault->scalar , stream);
}



void havana_fault_swapout(havana_fault_type * havana_fault , FILE * stream) {
  havana_fault_fwrite(havana_fault , stream);
  havana_fault_free_data(havana_fault);
}


void havana_fault_swapin(havana_fault_type * havana_fault , FILE * stream) {
  havana_fault_realloc_data(havana_fault);
  havana_fault_fread(havana_fault , stream);
}


void havana_fault_truncate(havana_fault_type * havana_fault) {
  DEBUG_ASSERT(havana_fault)
  scalar_truncate( havana_fault->scalar );  
}


void  havana_fault_initialize(havana_fault_type *havana_fault, int iens) { 
   DEBUG_ASSERT(havana_fault) 
   scalar_sample(havana_fault->scalar);
} 


int havana_fault_serialize(const havana_fault_type *havana_fault , int internal_offset , size_t serial_data_size , double *serial_data , size_t ens_size , size_t offset, bool * complete) {
  DEBUG_ASSERT(havana_fault);
  return scalar_serialize(havana_fault->scalar , internal_offset , serial_data_size, serial_data , ens_size , offset , complete);
}


int havana_fault_deserialize(havana_fault_type *havana_fault , int internal_offset , size_t serial_size , const double * serial_data , size_t stride , size_t offset) {
  DEBUG_ASSERT(havana_fault);
  return scalar_deserialize(havana_fault->scalar , internal_offset , serial_size , serial_data , stride , offset);
}




havana_fault_type * havana_fault_alloc_mean(int ens_size , const havana_fault_type **havana_fault_ens) {
  int iens;
  havana_fault_type * avg_havana_fault = havana_fault_copyc(havana_fault_ens[0]);
  for (iens = 1; iens < ens_size; iens++) 
    havana_fault_iadd(avg_havana_fault , havana_fault_ens[iens]);
  havana_fault_iscale(avg_havana_fault , 1.0 / ens_size);
  return avg_havana_fault;
}


void havana_fault_filter_file(const havana_fault_type * havana_fault , const char * run_path, int *ntarget_ref, char ***target_ref) 
{
  const int size             = havana_fault_config_get_data_size(havana_fault->config);
  const double * output_data = scalar_get_output_ref(havana_fault->scalar);
  hash_type * kw_hash = hash_alloc(10);
  int ikw;
  int ntemplates = 0;
  char ** target;
  
  havana_fault_output_transform(havana_fault);
  for (ikw = 0; ikw < size; ikw++)
    hash_insert_hash_owned_ref(kw_hash , havana_fault_config_get_name(havana_fault->config , ikw) , void_arg_alloc_double(output_data[ikw]) , void_arg_free__);


  /* 
     Scan through the list of template files and create target files. 
  */
 {
   const char *template_file_list = havana_fault_config_get_template_ref(havana_fault->config);
   FILE * stream = util_fopen(template_file_list,"r");
   bool end_of_file;
   fscanf(stream,"%d",&ntemplates);
   printf("%s %d\n","Number of template files: ",ntemplates);
   
   target             = util_malloc(ntemplates * sizeof(char *) , __func__);
   
   for( int i=0; i < ntemplates; i++) {
       char * target_file_root;
       char * template_file;
       
       util_forward_line(stream,&end_of_file);
       if(end_of_file) 
	util_abort("%s: Premature end of file when reading list of template files for Havana from:%s \n",__func__ , template_file_list);
      
      /* Read template file */
      template_file =  util_fscanf_alloc_token(stream);
      /* printf("%s\n",template_file); */

      /* Read target file root */
      target_file_root = util_fscanf_alloc_token(stream);
      /* printf("%s\n",target_file_root); */

      target[i] = util_alloc_full_path(run_path , target_file_root);


      printf("%s   %s  \n",template_file,target[i]); 
      
      util_filter_file(template_file , NULL , target[i] , '<' , '>' , kw_hash , util_filter_warn0 );      

      free(target_file_root);
      free(template_file);
  }
  fclose(stream);
 }
 
 
  /* Return values */
  *ntarget_ref     = ntemplates;
  *target_ref      = target;
  hash_free(kw_hash);
}


/**
  This function writes the results for eclipse to use. Observe that
  for this function the second argument is a target_path (the
  config_object has been allocated with target_file == NULL).

  Observe that with the current interaction with havanna *ONLY* one
  instance of this function can run at a time - conflict with the
  ".faultlist" file - this must be modularized!!
*/



void havana_fault_ecl_write(const havana_fault_type * havana_fault , const char * run_path) {
  DEBUG_ASSERT(havana_fault);
  havana_fault_config_run_havana(havana_fault->config , havana_fault->scalar ,  run_path);
}


/*   const char * executable; */
/*   char ** target_files; */
/*   int ntarget_files; */

/*   /\* Create havana model file (target_file) in run directory *\/ */
/*   havana_fault_filter_file(havana_fault , run_path , &ntarget_files , &target_files); */
  

/*   /\* Execute Havana from the run directory. The output from Havana should be saved in the run directory *\/ */
/*   executable = havana_fault->config->havana_executable; */

  
/*   for (int i=0; i< ntarget_files; i++) */
/*   { */
/*     /\* Go to the run directory and execute the Havana model from there *\/ */
/*     char * command = util_alloc_joined_string((const char *[5]) {"cd" , run_path , ";" , executable , target_files[i]} , 5 , " "); */
/*     system(command); */
/*     free(command); */
/*   } */
/*   util_free_string_list( target_files , ntarget_files ); */
/* } */




void havana_fault_export(const havana_fault_type * havana_fault , int * _size , char ***_kw_list , double **_output_values) {
  havana_fault_output_transform(havana_fault);

  *_kw_list       = havana_fault_config_get_name_list(havana_fault->config);
  *_size          = havana_fault_config_get_data_size(havana_fault->config);
  *_output_values = (double *) scalar_get_output_ref(havana_fault->scalar);

}



const char * havana_fault_get_name(const havana_fault_type * havana_fault, int kw_nr) {
  return  havana_fault_config_get_name(havana_fault->config , kw_nr);
}


/******************************************************************/
/* Anonumously generated functions used by the enkf_node object   */
/******************************************************************/

MATH_OPS_SCALAR(havana_fault);
VOID_ALLOC(havana_fault);
VOID_REALLOC_DATA(havana_fault);
VOID_SERIALIZE (havana_fault);
VOID_DESERIALIZE (havana_fault);
VOID_INITIALIZE(havana_fault);
VOID_FREE_DATA(havana_fault)
VOID_FWRITE (havana_fault)
VOID_FREAD  (havana_fault)
VOID_COPYC  (havana_fault)
VOID_FREE   (havana_fault)
VOID_ECL_WRITE(havana_fault)




