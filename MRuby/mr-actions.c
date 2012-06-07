////////////////////////////////////////////////////////////////////////////////
/// @brief mruby actions
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "mr-utils.h"

#include "BasicsC/strings.h"

#include "mruby/array.h"
#include "mruby/class.h"
#include "mruby/compile.h"
#include "mruby/data.h"
#include "mruby/hash.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/variable.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    ruby functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief defines an action
////////////////////////////////////////////////////////////////////////////////

static mrb_value MR_Mount (mrb_state* mrb, mrb_value self) {
  char* s;
  size_t l;
  mrb_value cl;
  struct RClass* rcl;

  mrb_get_args(mrb, "so", &s, &l, &cl);

  if (s == NULL) {
    return mrb_nil_value();
  }

  rcl = mrb_class_ptr(cl);

  if (rcl == 0) {
    printf("########### NILL CLASS\n");
    return mrb_nil_value();
  }

  printf("########### %s\n", s);
  return mrb_class_new_instance(mrb, 0, 0, rcl);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  module functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief init mruby utilities
////////////////////////////////////////////////////////////////////////////////

void TRI_InitMRActions (MR_state_t* mrs) {
  struct RClass *rcl;
  struct RClass *arango;

  arango = mrb_define_module(&mrs->_mrb, "Arango");

  // .............................................................................
  // HttpServer
  // .............................................................................

  rcl = mrb_define_class_under(&mrs->_mrb, arango, "HttpServer", mrs->_mrb.object_class);
  
  mrb_define_class_method(&mrs->_mrb, rcl, "mount", MR_Mount, ARGS_REQ(2));

  // .............................................................................
  // HttpRequest
  // .............................................................................

  rcl = mrs->_arangoRequest = mrb_define_class_under(&mrs->_mrb, arango, "HttpRequest", mrs->_mrb.object_class);

  // .............................................................................
  // HttpResponse
  // .............................................................................

  rcl = mrs->_arangoResponse = mrb_define_class_under(&mrs->_mrb, arango, "HttpResponse", mrs->_mrb.object_class);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
