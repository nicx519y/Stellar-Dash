#include "fault_log_api.h"
#include "game_link_log.h"
unsigned policy_arguments;
void policy_client(void) {
    XoraLog_Event(FL_EVENT_CPU, FL_CATEGORY_FAULT, FL_FATAL, ++policy_arguments);
    XoraLog_Service(++policy_arguments);
    XoraLog_GameFault(GL_MODE_RF, GL_INPUT_STALE, ++policy_arguments);
}
