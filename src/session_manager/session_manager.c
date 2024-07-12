/*
  * If not stated otherwise in this file or this component's Licenses.txt file
  * the following copyright and licenses apply:
  *
  * Copyright 2016 RDK Management
  *
  * Licensed under the Apache License, Version 2.0 (the "License");
  * you may not use this file except in compliance with the License.
  * You may obtain a copy of the License at
  *
  * http://www.apache.org/licenses/LICENSE-2.0
  *
  * Unless required by applicable law or agreed to in writing, software
  * distributed under the License is distributed on an "AS IS" BASIS,
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "rbuscore.h"
#include "rtLog.h"
#include "rbus_session_mgr.h"

#define CCSP_CURRENT_SESSION_ID_SIGNAL      "currentSessionIDSignal"

static int g_counter;
static int g_current_session_id;

static int request_session_id(const char * destination, const char * method, rtMessage request, void * user_data, rtMessage *response, const rtMessageHeader* hdr)
{
    (void) request;
    (void) user_data;
    (void) destination;
    (void) method;
    (void) hdr;
    rtMessage msg;
    rbusCoreError_t err = RBUSCORE_SUCCESS;

    rtMessage_Create(response);
    if(0 == g_current_session_id)
    {
        g_current_session_id = 1;
        ++g_counter;
        printf("Creating new session %d\n", g_current_session_id);
        // For debugging purpose
        //printf("Total number of times sessions created = %d\n", g_counter);
        rtMessage_SetInt32(*response, "response",RBUSCORE_SUCCESS);
        rtMessage_SetInt32(*response, "session_id",g_current_session_id);
    }
    else
    {
        printf("Cannot create new session when session %d is active.\n", g_current_session_id);
        rtMessage_SetInt32(*response, "response",RBUSCORE_ERROR_INVALID_STATE);
    }
    rtMessage_Create(&msg);
    rtMessage_SetInt32(msg, "response",RBUSCORE_SUCCESS);
    rtMessage_SetInt32(msg, "session_id",(int32_t)g_current_session_id);
    err = rbus_publishEvent(RBUS_SMGR_DESTINATION_NAME, CCSP_CURRENT_SESSION_ID_SIGNAL, msg);
    if(err != RBUSCORE_SUCCESS)
    {
        printf("rbus_publishEvent failed with ret = %d\n", err);
    }
    rtMessage_Release(msg);

    return 0;
}

static int get_session_id(const char * destination, const char * method, rtMessage request, void * user_data, rtMessage *response, const rtMessageHeader* hdr)
{
    (void) request;
    (void) user_data;
    (void) destination;
    (void) method;
    (void) hdr;
    rtMessage_Create(response);
    printf("Current session id is %d\n", g_current_session_id);
    rtMessage_SetInt32(*response, "response",RBUSCORE_SUCCESS);
    rtMessage_SetInt32(*response, "session_id",g_current_session_id);
    return 0;
}

static int end_session(const char * destination, const char * method, rtMessage request, void * user_data, rtMessage *response, const rtMessageHeader* hdr)
{
    (void) user_data;
    (void) destination;
    (void) method;
    (void) hdr;
    rtMessage msg;

    rtMessage_Create(response);
    int sessionid = 0;
    rbusCoreError_t result = RBUSCORE_SUCCESS;

    if(RT_OK == rtMessage_GetInt32(request, "sessionid",&sessionid))
    {
        if(sessionid == g_current_session_id)
        {
            printf("End of session %d\n", g_current_session_id);
            g_current_session_id = 0;
            //Cue event announcing end of session.
        }
        else
        {
            printf("Cannot end session %d. It doesn't match active session, which is %d.\n", sessionid, g_current_session_id);
            result = RBUSCORE_ERROR_INVALID_STATE;
        }
    }
    else
    {
        printf("Session id not found. Cannot process end of session.\n");
        result = RBUSCORE_ERROR_INVALID_PARAM;
    }
    rtMessage_SetInt32(*response, "response",result);
    rtMessage_Create(&msg);
    rtMessage_SetInt32(msg, "response",RBUSCORE_SUCCESS);
    rtMessage_SetInt32(msg, "session_id",(int32_t)g_current_session_id);
    result = rbus_publishEvent(RBUS_SMGR_DESTINATION_NAME, CCSP_CURRENT_SESSION_ID_SIGNAL, msg);
    if(result != RBUSCORE_SUCCESS)
    {
        printf("rbus_publishEvent failed with ret = %d\n", result);
    }
    rtMessage_Release(msg);
    return 0;
}


static int callback(const char * destination, const char * method, rtMessage message, void * user_data, rtMessage *response, const rtMessageHeader* hdr)
{
    (void) user_data;
    (void) response;
    (void) destination;
    (void) method;
    (void) hdr;
    printf("Received message in base callback.\n");
    char* buff = NULL;
    uint32_t buff_length = 0;

    rtMessage_ToString(message, &buff, &buff_length);
    printf("%s\n", buff);
    free(buff);

    return 0;
}
/*Signal handler for closing broker connection*/
static void handle_signal(int sig)
{
    (void) sig;
    rbus_closeBrokerConnection();
    printf("rbus session manager exiting.\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    rbusCoreError_t err = RBUSCORE_SUCCESS;
    printf("rbus session manager launching.\n");
    rtLog_SetLevel(RT_LOG_INFO);

    if (argc == 2)
    {
        if (-1 == daemon(0 /*chdir to "/"*/, 1 /*redirect stdout/stderr to /dev/null*/ ))
        {
            rtLog_Fatal("failed to fork off daemon. %s", rtStrError(errno));
            exit(1);
        }
        signal(SIGTERM, handle_signal);
    }

    while(1)
    {
        if((err = rbus_openBrokerConnection(RBUS_SMGR_DESTINATION_NAME)) == RBUSCORE_SUCCESS)
        {
            printf("Successfully connected to bus.\n");
            break;
        }
        else
        {
            printf("Error in connecting to the bus.\n");
            sleep(10);
        }
    }

    if((err = rbus_registerObj(RBUS_SMGR_DESTINATION_NAME, callback, NULL)) == RBUSCORE_SUCCESS)
    {
        printf("Successfully registered object.\n");
    }

    if((err = rbus_registerEvent(RBUS_SMGR_DESTINATION_NAME, CCSP_CURRENT_SESSION_ID_SIGNAL, NULL, NULL)) == RBUSCORE_SUCCESS)
    {
        printf("Successfully registered Event.\n");
    }

    rbus_method_table_entry_t table[3] = {
        {RBUS_SMGR_METHOD_GET_CURRENT_SESSION_ID, NULL, get_session_id}, 
        {RBUS_SMGR_METHOD_REQUEST_SESSION_ID, NULL, request_session_id}, 
        {RBUS_SMGR_METHOD_END_SESSION, NULL, end_session}};
    rbus_registerMethodTable(RBUS_SMGR_DESTINATION_NAME, table, 3); 
    pause();

    if((err = rbus_closeBrokerConnection()) == RBUSCORE_SUCCESS)
    {
        printf("Successfully disconnected from bus.\n");
    }
    printf("rbus session manager exiting.\n");
    return 0;
}
