/**************************************************************************
** Greg Koenig (koenig@uiuc.edu)
*/

/** @file
 * VMI machine layer
 * @ingroup Machine
 * @{
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "converse.h"

#define VMI_DEVICENAME "converse"
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "vmi.h"

/* These settings can only be changed at compile time. */
#define CMI_VMI_OPTIMIZE        0
#define CMI_VMI_USE_MEMORY_POOL 1

/*
  These settings are defaults that can be overridden at runtime by
  setting environment variables of the same name.
*/
#define CMI_VMI_CONNECTION_TIMEOUT           300       /* seconds */
#define CMI_VMI_MAXIMUM_HANDLES              10000
#define CMI_VMI_WAN_LATENCY                  1000      /* microseconds */
#define CMI_VMI_SMALL_MESSAGE_BOUNDARY       512       /* bytes */
#define CMI_VMI_MEDIUM_MESSAGE_BOUNDARY      16384     /* bytes */
#define CMI_VMI_EAGER_INTERVAL               10000
#define CMI_VMI_EAGER_THRESHOLD              1000
#define CMI_VMI_EAGER_SHORT_POLLSIZE_MAXIMUM 32
#define CMI_VMI_EAGER_SHORT_SLOTS            16
#define CMI_VMI_EAGER_LONG_BUFFERS           3
#define CMI_VMI_EAGER_LONG_BUFFER_SIZE       1048576   /* bytes */

/*
  If CMI_VMI_OPTIMIZE is defined, all of the checks for success for
  calls to VMI are removed by the use of the following macro.  The
  performance improvements seen by this are probably not worth the
  headaches caused when error conditions within VMI are not immediately
  discovered, however most of the code from NCSA that uses VMI skips
  checking VMI return codes, so we provide the same option here.
*/
#if CMI_VMI_OPTIMIZE
#define CMI_VMI_CHECK_SUCCESS(status,message)
#else
#define CMI_VMI_CHECK_SUCCESS(status,message)   \
          if (!VMI_SUCCESS (status)) {          \
            VMI_perror (message, status);       \
          }
#endif


/*
  The VMI versions of CmiAlloc() and CmiFree() prepend additional information
  onto each memory allocation, similar to what Converse already does.  This
  information holds a context associated with the memory chunk.  Normally
  this context is NULL, but in the case of eager message buffers the
  context points to the handle associated with the message buffer.  When the
  application calls CmiFree() on this message buffer, the call is intercepted
  and the message buffer is marked as free rather than deallocating the
  memory.
*/
#define CONTEXTFIELD(m) (((CMI_VMI_Memory_Chunk_T *)(m))[-1].context)

typedef struct
{
  void           *context;
  CmiChunkHeader  chunk_header;
} CMI_VMI_Memory_Chunk_T;

#define CMI_VMI_EAGER_SHORT_SENTINEL_READY    0
#define CMI_VMI_EAGER_SHORT_SENTINEL_DATA     1
#define CMI_VMI_EAGER_SHORT_SENTINEL_RECEIVED 2
#define CMI_VMI_EAGER_SHORT_SENTINEL_FREE     3

typedef struct
{
  unsigned short msgsize;
  unsigned short sentinel;
} CMI_VMI_Eager_Short_Slot_Footer_T;


/*
  If the memory pool is enabled, the following settings determine the
  sizes of the memory pool buckets as well as the number of entries in
  each bucket to pre-define and by which to grow.  The memory pool
  provides an efficient method of allocating and deallocating memory
  that does not need to call malloc() and free() repeatedly.
*/
#if CMI_VMI_USE_MEMORY_POOL
#define CMI_VMI_BUCKET1_SIZE 1024
#define CMI_VMI_BUCKET2_SIZE 2048
#define CMI_VMI_BUCKET3_SIZE 4096
#define CMI_VMI_BUCKET4_SIZE 8192
#define CMI_VMI_BUCKET5_SIZE 16384

#define CMI_VMI_BUCKET1_PREALLOCATE 128
#define CMI_VMI_BUCKET1_GROW        128

#define CMI_VMI_BUCKET2_PREALLOCATE 128
#define CMI_VMI_BUCKET2_GROW        128

#define CMI_VMI_BUCKET3_PREALLOCATE 128
#define CMI_VMI_BUCKET3_GROW        128

#define CMI_VMI_BUCKET4_PREALLOCATE 128
#define CMI_VMI_BUCKET4_GROW        128

#define CMI_VMI_BUCKET5_PREALLOCATE 128
#define CMI_VMI_BUCKET5_GROW        128
#endif


/*
  The following settings are used to describe the startup methods that
  may be used to assign ranks to the processes in the computation.
  (So far, only startup via the Charm++ Resource Manager (CRM) is possible.)
*/
#define CMI_VMI_STARTUP_TYPE_UNKNOWN  0
#define CMI_VMI_STARTUP_TYPE_CHARMRUN 1
#define CMI_VMI_STARTUP_TYPE_CRM      2


/*
  The following message structures are used for communicating with
  Charmrun.  These are only needed for startup CMI_VMI_STARTUP_TYPE_CHARMRUN.
*/
typedef struct Charmrun_Header
{
  int  msg_len;
  char msg_type[12];
} Charmrun_Header;

typedef struct Charmrun_InitnodeMsg
{
  int node_number;
  int nPE;
  int dataport;
  int mach_id;
  int IP;
} Charmrun_InitnodeMsg;

typedef struct Charmrun_Nodetab
{
  int nPE;
  int dataport;
  int mach_id;
  int IP;
} Charmrun_Nodetab;


/*
  If CMK_BROADCAST_SPANNING_TREE is defined, broadcasts are done via a
  spanning tree.  (Otherwise, broadcasts are done by iterating through
  each process in the process list and sending a separate message.)
*/
#if CMK_BROADCAST_SPANNING_TREE
#ifndef CMI_VMI_BROADCAST_SPANNING_FACTOR
#define CMI_VMI_BROADCAST_SPANNING_FACTOR 4
#endif

#define CMI_BROADCAST_ROOT(msg)   ((CmiMsgHeaderBasic *)msg)->tree_root
#define CMI_DEST_RANK(msg)        ((CmiMsgHeaderBasic *)msg)->tree_rank

#define CMI_SET_BROADCAST_ROOT(msg,tree_root) CMI_BROADCAST_ROOT(msg) = (tree_root);
#endif


/*
  Messages sent from one process to another using the VMI machine layer
  are tagged with a "type" field which is part of the Converse message
  header.

  "Standard" messages are simply placed into the Converse remote message
  queue when they received.

  "Barrier" messages are used to organize a barrier of all processes in
  the computation.  These have to be handled out-of-band at a lower
  layer than Converse to avoid race conditions.

  "Persistent Request" messages are used by the persistent handle setup
  function CmiCreatePersistent() to signal the receiver that a particular
  sender thinks it will be sending many messages to the receiver.  The
  receiver can use this hint to decide whether to set up an eager channel.

  "Credit" messages are used to replentish eager short credits on a sender.
  Normally credit replentishes are sent in the message headers of other
  messages ("piggyback") but if a process does not communicate frequently
  with its eager sender, no opportunities to piggyback credits may happen.
  In these cases, a separate message is sent.
*/
#define CMI_VMI_MESSAGE_TYPE(msg) ((CmiMsgHeaderBasic *)msg)->vmitype
#define CMI_VMI_MESSAGE_CREDITS(msg) ((CmiMsgHeaderBasic *)msg)->vmicredits

#define CMI_VMI_MESSAGE_TYPE_STANDARD           1
#define CMI_VMI_MESSAGE_TYPE_BARRIER            2
#define CMI_VMI_MESSAGE_TYPE_PERSISTENT_REQUEST 3
#define CMI_VMI_MESSAGE_TYPE_CREDIT             4


/*
  The following structures define messages that are sent within the VMI
  machine layer.

  CMI_VMI_Connect_Message_T are connect messages that are only sent during
  the connection setup phase.

  CMI_VMI_Barrier_Message_T are used to signal barrier operations.

  CMI_VMI_Persistent_Request_Message_T are used to indicate to a receiver
  that the sender will likely perform a lot of send operations, thus if
  the receiver has resources it is a good idea to set up eager channels.

  CMI_VMI_Credit_Message_T are used to send eager credit replentish messages
  when no opportunity to piggyback credits has happened.
*/
typedef struct
{
  int rank;
} CMI_VMI_Connect_Message_T;

typedef struct
{
  char header[CmiMsgHeaderSizeBytes];
} CMI_VMI_Barrier_Message_T;

typedef struct
{ 
  char header[CmiMsgHeaderSizeBytes];
  int  maxsize;
} CMI_VMI_Persistent_Request_Message_T;

typedef struct
{
  char header[CmiMsgHeaderSizeBytes];
} CMI_VMI_Credit_Message_T;


/* Publish messages (sent as payload with VMI_RDMA_Publish_Buffer() call). */
typedef enum
{
  CMI_VMI_PUBLISH_TYPE_GET,
  CMI_VMI_PUBLISH_TYPE_EAGER_SHORT,
  CMI_VMI_PUBLISH_TYPE_EAGER_LONG
} CMI_VMI_Publish_Type_T;

typedef struct
{
  CMI_VMI_Publish_Type_T type;
} CMI_VMI_Publish_Message_T;


/*
  Send and receive operations which cannot be completed immediately need
  to have some amount of state associated with them.  The following data
  structures are used to hold state for these operations in Send and
  Receive handles.
*/
typedef enum
{
  CMI_VMI_HANDLE_TYPE_SEND,
  CMI_VMI_HANDLE_TYPE_RECEIVE
} CMI_VMI_Handle_Type_T;

typedef enum
{
  CMI_VMI_SEND_HANDLE_TYPE_STREAM,
  CMI_VMI_SEND_HANDLE_TYPE_RDMAGET,
  CMI_VMI_SEND_HANDLE_TYPE_RDMABROADCAST,
  CMI_VMI_SEND_HANDLE_TYPE_EAGER_SHORT,
  CMI_VMI_SEND_HANDLE_TYPE_EAGER_LONG
} CMI_VMI_Send_Handle_Type_T;

typedef enum
{
  CMI_VMI_MESSAGE_DISPOSITION_NONE,
  CMI_VMI_MESSAGE_DISPOSITION_FREE,
  CMI_VMI_MESSAGE_DISPOSITION_ENQUEUE
} CMI_VMI_Message_Disposition_T;

typedef struct
{
  PVMI_CACHE_ENTRY cacheentry;
} CMI_VMI_Send_Handle_Stream_T;

typedef struct
{
  PVMI_CACHE_ENTRY cacheentry;
} CMI_VMI_Send_Handle_RDMAGet_T;

typedef struct
{
  PVMI_CACHE_ENTRY cacheentry;
} CMI_VMI_Send_Handle_RDMABroadcast_T;

typedef struct
{
  PVMI_REMOTE_BUFFER remote_buffer;
  int                offset;
  PVMI_CACHE_ENTRY   cacheentry;
  PVMI_RDMA_OP       rdmaop;
} CMI_VMI_Send_Handle_Eager_Short_T;

typedef struct
{
  int                maxsize;
  PVMI_REMOTE_BUFFER remote_buffer;
  PVMI_CACHE_ENTRY   cacheentry;
} CMI_VMI_Send_Handle_Eager_Long_T;

typedef struct
{
  CMI_VMI_Send_Handle_Type_T    send_handle_type;
  CMI_VMI_Message_Disposition_T message_disposition;

  union
  {
    CMI_VMI_Send_Handle_Stream_T        stream;
    CMI_VMI_Send_Handle_RDMAGet_T       rdmaget;
    CMI_VMI_Send_Handle_RDMABroadcast_T rdmabroadcast;
    CMI_VMI_Send_Handle_Eager_Short_T   eager_short;
    CMI_VMI_Send_Handle_Eager_Long_T    eager_long;
  } data;
} CMI_VMI_Send_Handle_T;

typedef enum
{
  CMI_VMI_RECEIVE_HANDLE_TYPE_RDMAGET,
  CMI_VMI_RECEIVE_HANDLE_TYPE_EAGER_SHORT,
  CMI_VMI_RECEIVE_HANDLE_TYPE_EAGER_LONG
} CMI_VMI_Receive_Handle_Type_T;

typedef struct
{
  PVMI_CACHE_ENTRY  cacheentry;
  void             *process;
} CMI_VMI_Receive_Handle_RDMAGet_T;

typedef struct
{
  int                                sender_rank;
  char                              *publish_buffer;
  PVMI_CACHE_ENTRY                   cacheentry;
  char                              *eager_buffer;
  CMI_VMI_Eager_Short_Slot_Footer_T *footer;
} CMI_VMI_Receive_Handle_Eager_Short_T;

typedef struct
{
  int              sender_rank;
  int              maxsize;
  PVMI_CACHE_ENTRY cacheentry;
} CMI_VMI_Receive_Handle_Eager_Long_T;

typedef struct
{
  CMI_VMI_Receive_Handle_Type_T receive_handle_type;

  union
  {
    CMI_VMI_Receive_Handle_RDMAGet_T     rdmaget;
    CMI_VMI_Receive_Handle_Eager_Short_T eager_short;
    CMI_VMI_Receive_Handle_Eager_Long_T  eager_long;
  } data;
} CMI_VMI_Receive_Handle_T;

typedef struct
{
  int                    index;
  int                    refcount;
  char                  *msg;
  int                    msgsize;
  CMI_VMI_Handle_Type_T  handle_type;

  union
  {
    CMI_VMI_Send_Handle_T    send;
    CMI_VMI_Receive_Handle_T receive;
  } data;
} CMI_VMI_Handle_T;


/*
  The following data structure holds per-process state information for
  each process in the computation.  The machine layer determines the
  total number of processes in the computation during startup and then
  creates an array of CMI_VMI_Process_T structures.
*/
typedef enum
{
  CMI_VMI_CONNECTION_CONNECTING,
  CMI_VMI_CONNECTION_CONNECTED,
  CMI_VMI_CONNECTION_DISCONNECTING,
  CMI_VMI_CONNECTION_DISCONNECTED,
  CMI_VMI_CONNECTION_ERROR
} CMI_VMI_Connection_State_T;

typedef struct
{
  int                        rank;
  int                        node_IP;
  PVMI_CONNECT               connection;
  CMI_VMI_Connection_State_T connection_state;

  CMI_VMI_Handle_T *eager_short_send_handles[CMI_VMI_EAGER_SHORT_SLOTS];
  int               eager_short_send_size;
  int               eager_short_send_index;
  int               eager_short_send_credits_available;

  CMI_VMI_Handle_T *eager_short_receive_handles[CMI_VMI_EAGER_SHORT_SLOTS];
  int               eager_short_receive_size;
  int               eager_short_receive_index;
  int               eager_short_receive_dirty;
  int               eager_short_receive_credits_replentish;

  CMI_VMI_Handle_T *eager_long_send_handles[CMI_VMI_EAGER_LONG_BUFFERS];
  int               eager_long_send_size;

  CMI_VMI_Handle_T *eager_long_receive_handles[CMI_VMI_EAGER_LONG_BUFFERS];
  int               eager_long_receive_size;
} CMI_VMI_Process_T;


/*
  The following bits of code are taken directly from the NCSA version
  of the CRM.  This is very old code that should be rewritten sometime.
*/
#define MAX_STR_LENGTH 256
#define CRM_DEFAULT_PORT 7777
#define SOCKET_ERROR -1

/* Message Codes to CRM  */
#define CRM_MSG_SUCCESS		0 /* Response from CRM */
#define CRM_MSG_REGISTER	1
#define CRM_MSG_REMOVE		2
#define CRM_MSG_FAILED		3

/* Register Context. */
typedef struct regMsg{
  int np; /* Number of processors. */
  int shutdownPort; /* Port shutdown server is runnig on */
  int keyLength;
  char key[MAX_STR_LENGTH];
} regMsg, *PRegMsg;

/* Remove CRM Context Request Message */
typedef struct delMsg{
  int keyLength;
  char key[MAX_STR_LENGTH];
} delMsg, *PDelMsg;

/* Error Codes for failed response */
typedef struct errMsg{
#define CRM_ERR_CTXTCONFLICT	1
#define CRM_ERR_INVALIDCTXT	2
#define CRM_ERR_TIMEOUT         3
#define CRM_ERR_OVERFLOW        4
  int errCode;
} errMsg, *PErrMsg;

/* Response for valid registration */
typedef struct nodeCtx{
  int nodeIP;
  int shutdownPort;
  int nodePE;
} nodeCtx, *PNodeCtx;

typedef struct ctxMsg{
  int np; /* # of PE */
  struct nodeCtx *node;
} ctxMsg, *PCtxMsg;

typedef struct CRM_Msg{
  int msgCode;
  union{
    regMsg CRMReg;
    delMsg CRMDel;
    errMsg CRMErr;
    ctxMsg CRMCtx;
  } msg;
} CRM_Msg, *PCRM_Msg;





/***********************/
/* FUNCTION PROTOTYPES */
/***********************/

/* Externally-visible API functions */
void ConverseInit (int argc, char **argv, CmiStartFn start_function, int user_calls_scheduler, int init_returns);
void ConverseExit ();
void CmiAbort (const char *message);

void CmiNotifyIdle ();

void CmiMemLock ();
void CmiMemUnlock ();

void CmiPrintf (const char *format, ...);
void CmiError (const char *format, ...);

void CmiBarrier ();
void CmiBarrierZero ();

void CmiSyncSendFn (int destrank, int msgsize, char *msg);
CmiCommHandle CmiAsyncSendFn (int destrank, int msgsize, char *msg);
void CmiFreeSendFn (int destrank, int msgsize, char *msg);

void CmiSyncBroadcastFn (int msgsize, char *msg);
CmiCommHandle CmiAsyncBroadcastFn (int msgsize, char *msg);
void CmiFreeBroadcastFn (int msgsize, char *msg);

void CmiSyncBroadcastAllFn (int msgsize, char *msg);
CmiCommHandle CmiAsyncBroadcastAllFn (int msgsize, char *msg);
void CmiFreeBroadcastAllFn (int msgsize, char *msg);

int CmiAsyncMsgSent (CmiCommHandle commhandle);
int CmiAllAsyncMsgsSent ();
void CmiReleaseCommHandle (CmiCommHandle commhandle);

void *CmiGetNonLocal (void);

#if CMK_PERSISTENT_COMM
void CmiPersistentInit ();
PersistentHandle CmiCreatePersistent (int destrank, int maxsize);
void CmiUsePersistentHandle (PersistentHandle *handle_array, int array_size);
void CmiDestroyPersistent (PersistentHandle phandle);
void CmiDestroyAllPersistent ();
PersistentReq CmiCreateReceiverPersistent (int maxsize);
PersistentHandle CmiRegisterReceivePersistent (PersistentReq request);
#endif


/* Startup and shutdown functions */
void CMI_VMI_Read_Environment ();

int CMI_VMI_Startup_Charmrun ();

int CMI_VMI_Startup_CRM ();
int CMI_VMI_CRM_Register (char *key, int numProcesses, BOOLEAN reg);

int CMI_VMI_Initialize_VMI ();

int CMI_VMI_Terminate_VMI ();


/* Socket send and receive functions */
int CMI_VMI_Socket_Send (int sockfd, const void *msg, int size);
int CMI_VMI_Socket_Receive (int sockfd, void *msg, int size);


/* Connection open and close functions */
int CMI_VMI_Open_Connections ();
int CMI_VMI_Open_Connection (int remote_rank, char *remote_key, PVMI_BUFFER connect_message_buffer);
VMI_CONNECT_RESPONSE CMI_VMI_Connection_Handler (PVMI_CONNECT connection, PVMI_SLAB slab, ULONG data_size);
void CMI_VMI_Connection_Response_Handler (PVOID context, PVOID response, USHORT size, PVOID handle, VMI_CONNECT_RESPONSE status);

int CMI_VMI_Close_Connections ();
void CMI_VMI_Disconnection_Handler (PVMI_CONNECT connection);
void CMI_VMI_Disconnection_Response_Handler (PVMI_CONNECT connection, PVOID context, VMI_STATUS status);



/* Memory allocation and deallocation functions */
void *CMI_VMI_CmiAlloc (int request_size);
void CMI_VMI_CmiFree (void *ptr);

PVMI_CACHE_ENTRY CMI_VMI_CacheEntry_From_Context (void *context);


/* Handle allocation and deallocation functions */
CMI_VMI_Handle_T *CMI_VMI_Allocate_Handle ();
void CMI_VMI_Deallocate_Handle (CMI_VMI_Handle_T *handle);


/* Send and receive functions */
VMI_RECV_STATUS CMI_VMI_Stream_Notification_Handler (PVMI_CONNECT connection, PVMI_STREAM_RECV stream, VMI_STREAM_COMMAND command, PVOID context, PVMI_SLAB slab);
void CMI_VMI_Stream_Completion_Handler (PVOID context, VMI_STATUS sstatus);

void CMI_VMI_RDMA_Publish_Handler (PVMI_CONNECT connection, PVMI_REMOTE_BUFFER remote_buffer, PVMI_SLAB publish_data, ULONG publish_data_size);

void CMI_VMI_RDMA_Put_Notification_Handler (PVMI_CONNECT connection, UINT32 rdma_size, UINT32 context, VMI_STATUS remote_status);
void CMI_VMI_RDMA_Put_Completion_Handler (PVMI_RDMA_OP rdmaop, PVOID context, VMI_STATUS remote_status);

void CMI_VMI_RDMA_Get_Notification_Handler (PVMI_CONNECT connection, UINT32 context, VMI_STATUS remote_status);
void CMI_VMI_RDMA_Get_Completion_Handler (PVMI_RDMA_OP rdmaop, PVOID context, VMI_STATUS sstatus);


/* Spanning tree send functions */
#if CMK_BROADCAST_SPANNING_TREE
int CMI_VMI_Spanning_Children_Count (char *msg);
void CMI_VMI_Send_Spanning_Children (int msgsize, char *msg);
#endif


/* Eager communication setup and send (short, long) functions */
void CMI_VMI_Eager_Setup (int sender_rank, int maxsize);

void CMI_VMI_Send_Eager_Short (int destrank, int msgsize, char *msg);

void CMI_VMI_Sync_Send_Eager_Long (int destrank, int msgsize, char *msg, CMI_VMI_Handle_T *handle);
CmiCommHandle CMI_VMI_Async_Send_Eager_Long (int destrank, int msgsize, char *msg, CMI_VMI_Handle_T *handle);
void CMI_VMI_Free_Send_Eager_Long (int destrank, int msgsize, char *msg, CMI_VMI_Handle_T *handle);


/* NCSA CRM functions */
BOOLEAN CRMInit ();
SOCKET createSocket(char *hostName, int port, int *localAddr);
BOOLEAN CRMRegister (char *key, ULONG numPE, int shutdownPort, SOCKET *clientSock, int *clientAddr, PCRM_Msg *msg2);
BOOLEAN CRMParseMsg (PCRM_Msg msg, int rank, int *nodeIP, int *shutdownPort, int *nodePE);

/*@}*/
