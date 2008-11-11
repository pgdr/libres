#ifndef __CONF_H__
#define __CONF_H__
#include <stdbool.h>
#include <set.h>
#include <conf_data.h>
#include <stringlist.h>


typedef struct conf_class_struct      conf_class_type;
typedef struct conf_instance_struct   conf_instance_type;
typedef struct conf_item_spec_struct  conf_item_spec_type;
typedef struct conf_item_struct       conf_item_type;



/** D E F A U L T   A L L O C / F R E E    F U N C T I O N S */



conf_class_type * conf_class_alloc_empty(
  const char              * class_name,
  bool                      require_instance);

void conf_class_free(
  conf_class_type * conf_class);

void conf_class_free__(
  void * conf_class);



conf_instance_type * conf_instance_alloc_default(
  const conf_class_type * conf_class,
  const char              * name);

conf_instance_type * conf_instance_copyc(
  const conf_instance_type * conf_instance);

void conf_instance_free(
  conf_instance_type * conf_instance);

void conf_instance_free__(
  void * conf_instance);



conf_item_spec_type * conf_item_spec_alloc(
  char                    * name,        
  bool                      required_set,
  dt_enum                   dt);

void conf_item_spec_free(
  conf_item_spec_type * conf_item_spec);

void conf_item_spec_free__(
  void * conf_item_spec);



conf_item_type * conf_item_alloc(
  const conf_item_spec_type * conf_item_spec,
  const char                  * value);

conf_item_type * conf_item_copyc(
  const conf_item_type * conf_item);

void conf_item_free(
  conf_item_type * conf_item);

void conf_item_free__(
  void * conf_item);



/** M A N I P U L A T O R S ,   I N S E R T I O N */ 



void conf_class_insert_owned_sub_class(
  conf_class_type * conf_class,
  conf_class_type * sub_conf_class);

void conf_class_insert_owned_item_spec(
  conf_class_type     * conf_class,
  conf_item_spec_type * item_spec);



void conf_instance_insert_owned_sub_instance(
  conf_instance_type * conf_instance,
  conf_instance_type * sub_conf_instance);

void conf_instance_insert_owned_item(
  conf_instance_type * conf_instance,
  conf_item_type     * conf_item);

void conf_instance_insert_item(
  conf_instance_type * conf_instance,
  const char           * item_name,
  const char           * value);

void conf_instance_overload(
  conf_instance_type       * conf_instance_target,
  const conf_instance_type * conf_instance_source);



/** M A N I P U L A T O R S ,   C L A S S   A N D   I T E M   S P E C I F I C A T I O N */ 



void conf_class_set_help(
  conf_class_type * conf_class,
  const char        * help);



void conf_item_spec_add_restriction(
  conf_item_spec_type * conf_item_spec,
  const char            * restriction);

void conf_item_spec_set_default_value(
  conf_item_spec_type * conf_item_spec,
  const char            * default_value);

void conf_item_spec_set_help(
  conf_item_spec_type * conf_item_spec,
  const char            * help);



/** A C C E S S O R S */



bool conf_class_has_item_spec(
  const conf_class_type * conf_class,
  const char              * item_name);

bool conf_class_has_sub_class(
  const conf_class_type * conf_class,
  const char              * sub_class_name);

const conf_item_spec_type * conf_class_get_item_spec_ref(
  const conf_class_type * conf_class,
  const char              * item_name);

const conf_class_type * conf_class_get_sub_class_ref(
  const conf_class_type * conf_class,
  const char              * sub_class_name);



bool conf_instance_is_of_class(
  const conf_instance_type * conf_instance,
  const char                 * class_name);

bool conf_instance_has_item(
  const conf_instance_type * conf_instance,
  const char                 * item_name);

bool conf_instance_has_sub_instance(
  const conf_instance_type * conf_instance,
  const char                 * sub_instance_name);

const conf_instance_type * conf_instance_get_sub_instance_ref(
  const conf_instance_type * conf_instance,
  const char                 * sub_instance_name);

stringlist_type * conf_instance_alloc_list_of_sub_instances_of_class(
  const conf_instance_type * conf_instance,
  const conf_class_type    * conf_class);

stringlist_type * conf_instance_alloc_list_of_sub_instances_of_class_by_name(
  const conf_instance_type * conf_instance,
  const char                 * sub_class_name);

const conf_class_type * conf_instance_get_class_ref(
  const conf_instance_type * conf_instance);

const char * conf_instance_get_class_name_ref(
  const conf_instance_type * conf_instance);

const char * conf_instance_get_item_value_ref(
  const conf_instance_type * conf_instance,
  const char                 * item_name);

/** If the dt supports it, these functions will parse the item
    value to the requested types.

    NOTE:
    If the dt does not support it, or the conf_instance
    does not have the item, the functions will abort your program.
*/
int conf_instance_get_item_value_int(
  const conf_instance_type * conf_instance,
  const char                 * item_name);

double conf_instance_get_item_value_double(
  const conf_instance_type * conf_instance,
  const char                 * item_name);

time_t conf_instance_get_item_value_time_t(
  const conf_instance_type * conf_instance,
  const char                 * item_name);



/** V A L I D A T O R S */



bool conf_instance_validate(
  const conf_instance_type * conf_instance);



/** A L L O C   F R O M   F I L E */


conf_instance_type * conf_instance_alloc_from_file(
  const conf_class_type * conf_class,
  const char              * name,
  const char              * file_name);

#endif  
