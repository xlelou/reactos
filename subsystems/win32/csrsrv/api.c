/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Client/Server Runtime SubSystem
 * FILE:            subsystems/win32/csrsrv/api.c
 * PURPOSE:         CSR Server DLL API LPC Implementation
 *                  "\windows\ApiPort" port process management functions
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include "srv.h"

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

static unsigned ApiDefinitionsCount = 0;
static PCSRSS_API_DEFINITION ApiDefinitions = NULL;

BOOLEAN (*CsrClientThreadSetup)(VOID) = NULL;
UNICODE_STRING CsrApiPortName;
volatile LONG CsrpStaticThreadCount;
volatile LONG CsrpDynamicThreadTotal;
extern ULONG CsrMaxApiRequestThreads;

/* FUNCTIONS *****************************************************************/

#if 0
NTSTATUS FASTCALL
CsrApiRegisterDefinitions(PCSRSS_API_DEFINITION NewDefinitions)
{
  unsigned NewCount;
  PCSRSS_API_DEFINITION Scan;
  PCSRSS_API_DEFINITION New;

  DPRINT("CSR: %s called\n", __FUNCTION__);

  NewCount = 0;
  for (Scan = NewDefinitions; 0 != Scan->Handler; Scan++)
    {
      NewCount++;
    }

  New = RtlAllocateHeap(CsrHeap, 0,
                        (ApiDefinitionsCount + NewCount)
                        * sizeof(CSRSS_API_DEFINITION));
  if (NULL == New)
    {
      DPRINT1("Unable to allocate memory\n");
      return STATUS_NO_MEMORY;
    }
  if (0 != ApiDefinitionsCount)
    {
      RtlCopyMemory(New, ApiDefinitions,
                    ApiDefinitionsCount * sizeof(CSRSS_API_DEFINITION));
      RtlFreeHeap(CsrHeap, 0, ApiDefinitions);
    }
  RtlCopyMemory(New + ApiDefinitionsCount, NewDefinitions,
                NewCount * sizeof(CSRSS_API_DEFINITION));
  ApiDefinitions = New;
  ApiDefinitionsCount += NewCount;

  return STATUS_SUCCESS;
}
#endif

/*
VOID
FASTCALL
CsrApiCallHandler(PCSR_PROCESS ProcessData,
                  PCSR_API_MESSAGE Request)
*/
VOID
FASTCALL
CsrApiCallHandler(
    IN OUT PCSR_API_MESSAGE ApiMessage,
    OUT PULONG Reply
)
{
  unsigned DefIndex;
  ULONG ApiId;

  DPRINT("CSR: Calling handler for ApiNumber: %x.\n", ApiMessage->ApiNumber);
  ApiId = CSR_API_NUMBER_TO_API_ID(ApiMessage->ApiNumber);
  DPRINT("CSR: ApiID: %x ServerID: %x\n", ApiId, CSR_API_NUMBER_TO_SERVER_ID(ApiMessage->ApiNumber));

  /* FIXME: Extract DefIndex instead of looping */
  for (DefIndex = 0; DefIndex < ApiDefinitionsCount; DefIndex++)
    {
      if (ApiDefinitions[DefIndex].ApiID == ApiId)
        {
          if (ApiMessage->Header.u1.s1.DataLength < ApiDefinitions[DefIndex].MinRequestSize)
            {
              DPRINT1("Request ApiID %d min request size %d actual %d\n",
                      ApiId, ApiDefinitions[DefIndex].MinRequestSize,
                      ApiMessage->Header.u1.s1.DataLength);
              ApiMessage->Status = STATUS_INVALID_PARAMETER;
            }
          else
            {
              ApiMessage->Status = (ApiDefinitions[DefIndex].Handler)(ApiMessage, Reply);
            }
          return;
        }
    }
  DPRINT1("CSR: Unknown request ApiNumber 0x%x\n", ApiMessage->ApiNumber);
  ApiMessage->Status = STATUS_INVALID_SYSTEM_SERVICE;
}

VOID
CallHardError(IN PCSR_THREAD ThreadData,
              IN PHARDERROR_MSG HardErrorMessage);

#if 0
static
VOID
NTAPI
CsrHandleHardError(IN PCSR_THREAD ThreadData,
                   IN OUT PHARDERROR_MSG Message)
{
    DPRINT1("CSR: received hard error %lx\n", Message->Status);

    /* Call the hard error handler in win32csr */
    CallHardError(ThreadData, Message);
}
#endif

/*++
 * @name CsrCallServerFromServer
 * @implemented NT4
 *
 * The CsrCallServerFromServer routine calls a CSR API from within a server.
 * It avoids using LPC messages since the request isn't coming from a client.
 *
 * @param ReceiveMsg
 *        Pointer to the CSR API Message to send to the server.
 *
 * @param ReplyMsg
 *        Pointer to the CSR API Message to receive from the server.
 *
 * @return STATUS_SUCCESS in case of success, STATUS_ILLEGAL_FUNCTION
 *         if the ApiNumber is invalid, or STATUS_ACCESS_VIOLATION if there
 *         was a problem executing the API.
 *
 * @remarks None.
 *
 *--*/
NTSTATUS
NTAPI
CsrCallServerFromServer(IN PCSR_API_MESSAGE ReceiveMsg,
                        IN OUT PCSR_API_MESSAGE ReplyMsg)
{
#if 1 // Real code
    ULONG ServerId;
    PCSR_SERVER_DLL ServerDll;
    ULONG ApiId;
    ULONG Reply;
    NTSTATUS Status;

    /* Get the Server ID */
    ServerId = CSR_API_NUMBER_TO_SERVER_ID(ReceiveMsg->ApiNumber);

    /* Make sure that the ID is within limits, and the Server DLL loaded */
    if ((ServerId >= CSR_SERVER_DLL_MAX) ||
        (!(ServerDll = CsrLoadedServerDll[ServerId])))
    {
        /* We are beyond the Maximum Server ID */
        DPRINT1("CSRSS: %lx is invalid ServerDllIndex (%08x)\n", ServerId, ServerDll);
        ReplyMsg->Status = (ULONG)STATUS_ILLEGAL_FUNCTION;
        return STATUS_ILLEGAL_FUNCTION;
    }
    else
    {
        /* Get the API ID */
        ApiId = CSR_API_NUMBER_TO_API_ID(ReceiveMsg->ApiNumber);

        /* Normalize it with our Base ID */
        ApiId -= ServerDll->ApiBase;

        /* Make sure that the ID is within limits, and the entry exists */
        if ((ApiId >= ServerDll->HighestApiSupported) ||
            ((ServerDll->ValidTable) && !(ServerDll->ValidTable[ApiId])))
        {
            /* We are beyond the Maximum API ID, or it doesn't exist */
            DPRINT1("CSRSS: %lx (%s) is invalid ApiTableIndex for %Z or is an "
                    "invalid API to call from the server.\n",
                    ServerDll->ValidTable[ApiId],
                    ((ServerDll->NameTable) && (ServerDll->NameTable[ApiId])) ?
                    ServerDll->NameTable[ApiId] : "*** UNKNOWN ***", &ServerDll->Name);
            DbgBreakPoint();
            ReplyMsg->Status = (ULONG)STATUS_ILLEGAL_FUNCTION;
            return STATUS_ILLEGAL_FUNCTION;
        }
    }

    if (CsrDebug & 2)
    {
        DPRINT1("CSRSS: %s Api Request received from server process\n",
                ServerDll->NameTable[ApiId]);
    }

    /* Validation complete, start SEH */
    _SEH2_TRY
    {
        /* Call the API and get the result */
        /// CsrApiCallHandler(ReplyMsg, /*ProcessData*/ &ReplyCode); ///
        Status = (ServerDll->DispatchTable[ApiId])(ReceiveMsg, &Reply);

        /* Return the result, no matter what it is */
        ReplyMsg->Status = Status;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* If we got an exception, return access violation */
        ReplyMsg->Status = STATUS_ACCESS_VIOLATION;
    }
    _SEH2_END;

    /* Return success */
    return STATUS_SUCCESS;

#else // Hacky reactos code

    PCSR_PROCESS ProcessData;
    ULONG ReplyCode;

    /* Get the Process Data */
    CsrLockProcessByClientId(&ReceiveMsg->Header.ClientId.UniqueProcess, &ProcessData);
    if (!ProcessData)
    {
        DPRINT1("Message: Unable to find data for process 0x%x\n",
                ReceiveMsg->Header.ClientId.UniqueProcess);
        return STATUS_NOT_SUPPORTED;
    }

    /* Validation complete, start SEH */
    _SEH2_TRY
    {
        /* Call the API and get the result */
        CsrApiCallHandler(ReplyMsg, /*ProcessData*/ &ReplyCode);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* If we got an exception, return access violation */
        ReplyMsg->Status = STATUS_ACCESS_VIOLATION;
    }
    _SEH2_END;

    /* Release the process reference */
    CsrUnlockProcess(ProcessData);

    /* Return success */
    return STATUS_SUCCESS;
#endif
}

/*++
 * @name CsrApiHandleConnectionRequest
 *
 * The CsrApiHandleConnectionRequest routine handles and accepts a new
 * connection request to the CSR API LPC Port.
 *
 * @param ApiMessage
 *        Pointer to the incoming CSR API Message which contains the
 *        connection request.
 *
 * @return STATUS_SUCCESS in case of success, or status code which caused
 *         the routine to error.
 *
 * @remarks This routine is responsible for attaching the Shared Section to
 *          new clients connecting to CSR.
 *
 *--*/
NTSTATUS
NTAPI
CsrApiHandleConnectionRequest(IN PCSR_API_MESSAGE ApiMessage)
{
    PCSR_THREAD CsrThread = NULL;
    PCSR_PROCESS CsrProcess = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    PCSR_CONNECTION_INFO ConnectInfo = &ApiMessage->ConnectionInfo;
    BOOLEAN AllowConnection = FALSE;
    REMOTE_PORT_VIEW RemotePortView;
    HANDLE hPort;

    /* Acquire the Process Lock */
    CsrAcquireProcessLock();

    /* Lookup the CSR Thread */
    CsrThread = CsrLocateThreadByClientId(NULL, &ApiMessage->Header.ClientId);

    /* Check if we have a thread */
    if (CsrThread)
    {
        /* Get the Process */
        CsrProcess = CsrThread->Process;

        /* Make sure we have a Process as well */
        if (CsrProcess)
        {
            /* Reference the Process */
            CsrLockedReferenceProcess(CsrThread->Process);

            /* Release the lock */
            CsrReleaseProcessLock();

            /* Duplicate the Object Directory */
            Status = NtDuplicateObject(NtCurrentProcess(),
                                       CsrObjectDirectory,
                                       CsrProcess->ProcessHandle,
                                       &ConnectInfo->ObjectDirectory,
                                       0,
                                       0,
                                       DUPLICATE_SAME_ACCESS |
                                       DUPLICATE_SAME_ATTRIBUTES);

            /* Acquire the lock */
            CsrAcquireProcessLock();

            /* Check for success */
            if (NT_SUCCESS(Status))
            {
                /* Attach the Shared Section */
                Status = CsrSrvAttachSharedSection(CsrProcess, ConnectInfo);

                /* Check how this went */
                if (NT_SUCCESS(Status)) AllowConnection = TRUE;
            }

            /* Dereference the project */
            CsrLockedDereferenceProcess(CsrProcess);
        }
    }

    /* Release the lock */
    CsrReleaseProcessLock();

    /* Setup the Port View Structure */
    RemotePortView.Length = sizeof(REMOTE_PORT_VIEW);
    RemotePortView.ViewSize = 0;
    RemotePortView.ViewBase = NULL;

    /* Save the Process ID */
    ConnectInfo->ProcessId = NtCurrentTeb()->ClientId.UniqueProcess;

    /* Accept the Connection */
    Status = NtAcceptConnectPort(&hPort,
                                 AllowConnection ? UlongToPtr(CsrProcess->SequenceNumber) : 0,
                                 &ApiMessage->Header,
                                 AllowConnection,
                                 NULL,
                                 &RemotePortView);
    if (!NT_SUCCESS(Status))
    {
         DPRINT1("CSRSS: NtAcceptConnectPort - failed.  Status == %X\n", Status);
    }
    else if (AllowConnection)
    {
        if (CsrDebug & 2)
        {
            DPRINT1("CSRSS: ClientId: %lx.%lx has ClientView: Base=%p, Size=%lx\n",
                    ApiMessage->Header.ClientId.UniqueProcess,
                    ApiMessage->Header.ClientId.UniqueThread,
                    RemotePortView.ViewBase,
                    RemotePortView.ViewSize);
        }

        /* Set some Port Data in the Process */
        CsrProcess->ClientPort = hPort;
        CsrProcess->ClientViewBase = (ULONG_PTR)RemotePortView.ViewBase;
        CsrProcess->ClientViewBounds = (ULONG_PTR)((ULONG_PTR)RemotePortView.ViewBase +
                                                   (ULONG_PTR)RemotePortView.ViewSize);

        /* Complete the connection */
        Status = NtCompleteConnectPort(hPort);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CSRSS: NtCompleteConnectPort - failed.  Status == %X\n", Status);
        }
    }
    else
    {
        DPRINT1("CSRSS: Rejecting Connection Request from ClientId: %lx.%lx\n",
                ApiMessage->Header.ClientId.UniqueProcess,
                ApiMessage->Header.ClientId.UniqueThread);
    }

    /* Return status to caller */
    return Status;
}

/*++
 * @name CsrpCheckRequestThreads
 *
 * The CsrpCheckRequestThreads routine checks if there are no more threads
 * to handle CSR API Requests, and creates a new thread if possible, to
 * avoid starvation.
 *
 * @param None.
 *
 * @return STATUS_SUCCESS in case of success, STATUS_UNSUCCESSFUL
 *         if a new thread couldn't be created.
 *
 * @remarks None.
 *
 *--*/
NTSTATUS
NTAPI
CsrpCheckRequestThreads(VOID)
{
    HANDLE hThread;
    CLIENT_ID ClientId;
    NTSTATUS Status;

    /* Decrease the count, and see if we're out */
    if (!(_InterlockedDecrement(&CsrpStaticThreadCount)))
    {
        /* Check if we've still got space for a Dynamic Thread */
        if (CsrpDynamicThreadTotal < CsrMaxApiRequestThreads)
        {
            /* Create a new dynamic thread */
            Status = RtlCreateUserThread(NtCurrentProcess(),
                                         NULL,
                                         TRUE,
                                         0,
                                         0,
                                         0,
                                         (PVOID)CsrApiRequestThread,
                                         NULL,
                                         &hThread,
                                         &ClientId);
            /* Check success */
            if (NT_SUCCESS(Status))
            {
                /* Increase the thread counts */
                _InterlockedIncrement(&CsrpStaticThreadCount);
                _InterlockedIncrement(&CsrpDynamicThreadTotal);

                /* Add a new server thread */
                if (CsrAddStaticServerThread(hThread,
                                             &ClientId,
                                             CsrThreadIsServerThread))
                {
                    /* Activate it */
                    NtResumeThread(hThread, NULL);
                }
                else
                {
                    /* Failed to create a new static thread */
                    _InterlockedDecrement(&CsrpStaticThreadCount);
                    _InterlockedDecrement(&CsrpDynamicThreadTotal);

                    /* Terminate it */
                    DPRINT1("Failing\n");
                    NtTerminateThread(hThread, 0);
                    NtClose(hThread);

                    /* Return */
                    return STATUS_UNSUCCESSFUL;
                }
            }
        }
    }

    /* Success */
    return STATUS_SUCCESS;
}

/*++
 * @name CsrApiRequestThread
 *
 * The CsrApiRequestThread routine handles incoming messages or connection
 * requests on the CSR API LPC Port.
 *
 * @param Parameter
 *        System-default user-defined parameter. Unused.
 *
 * @return The thread exit code, if the thread is terminated.
 *
 * @remarks Before listening on the port, the routine will first attempt
 *          to connect to the user subsystem.
 *
 *--*/
NTSTATUS
NTAPI
CsrApiRequestThread(IN PVOID Parameter)
#if 1
{
    PTEB Teb = NtCurrentTeb();
    LARGE_INTEGER TimeOut;
    PCSR_THREAD CurrentThread, CsrThread;
    NTSTATUS Status;
    PCSR_API_MESSAGE ReplyMsg;
    CSR_API_MESSAGE ReceiveMsg;
    PCSR_PROCESS CsrProcess;
    PHARDERROR_MSG HardErrorMsg;
    PVOID PortContext;
    PCSR_SERVER_DLL ServerDll;
    PCLIENT_DIED_MSG ClientDiedMsg;
    PDBGKM_MSG DebugMessage;
    ULONG ServerId, ApiId, Reply, MessageType, i;
    HANDLE ReplyPort;

    /* Setup LPC loop port and message */
    ReplyMsg = NULL;
    ReplyPort = CsrApiPort;

    /* Connect to user32 */
    while (!CsrConnectToUser())
    {
        /* Set up the timeout for the connect (30 seconds) */
        TimeOut.QuadPart = -30 * 1000 * 1000 * 10;

        /* Keep trying until we get a response */
        Teb->Win32ClientInfo[0] = 0;
        NtDelayExecution(FALSE, &TimeOut);
    }

    /* Get our thread */
    CurrentThread = Teb->CsrClientThread;

    /* If we got an event... */
    if (Parameter)
    {
        /* Set it, to let stuff waiting on us load */
        Status = NtSetEvent((HANDLE)Parameter, NULL);
        ASSERT(NT_SUCCESS(Status));

        /* Increase the Thread Counts */
        _InterlockedIncrement(&CsrpStaticThreadCount);
        _InterlockedIncrement(&CsrpDynamicThreadTotal);
    }

    /* Now start the loop */
    while (TRUE)
    {
        /* Make sure the real CID is set */
        Teb->RealClientId = Teb->ClientId;

        /* Debug check */
        if (Teb->CountOfOwnedCriticalSections)
        {
            DPRINT1("CSRSRV: FATAL ERROR. CsrThread is Idle while holding %lu critical sections\n",
                    Teb->CountOfOwnedCriticalSections);
            DPRINT1("CSRSRV: Last Receive Message %lx ReplyMessage %lx\n",
                    &ReceiveMsg, ReplyMsg);
            DbgBreakPoint();
        }

        /* Wait for a message to come through */
        Status = NtReplyWaitReceivePort(ReplyPort,
                                        &PortContext,
                                        &ReplyMsg->Header,
                                        &ReceiveMsg.Header);

        /* Check if we didn't get success */
        if (Status != STATUS_SUCCESS)
        {
            /* Was it a failure or another success code? */
            if (!NT_SUCCESS(Status))
            {
                /* Check for specific status cases */
                if ((Status != STATUS_INVALID_CID) &&
                    (Status != STATUS_UNSUCCESSFUL) &&
                    ((Status == STATUS_INVALID_HANDLE) || (ReplyPort == CsrApiPort)))
                {
                    /* Notify the debugger */
                    DPRINT1("CSRSS: ReceivePort failed - Status == %X\n", Status);
                    DPRINT1("CSRSS: ReplyPortHandle %lx CsrApiPort %lx\n", ReplyPort, CsrApiPort);
                }

                /* We failed big time, so start out fresh */
                ReplyMsg = NULL;
                ReplyPort = CsrApiPort;
                continue;
            }
            else
            {
                /* A bizare "success" code, just try again */
                DPRINT1("NtReplyWaitReceivePort returned \"success\" status 0x%x\n", Status);
                continue;
            }
        }

        /* Use whatever Client ID we got */
        Teb->RealClientId = ReceiveMsg.Header.ClientId;

        /* Get the Message Type */
        MessageType = ReceiveMsg.Header.u2.s2.Type;

        /* Handle connection requests */
        if (MessageType == LPC_CONNECTION_REQUEST)
        {
            /* Handle the Connection Request */
            CsrApiHandleConnectionRequest(&ReceiveMsg);
            ReplyPort = CsrApiPort;
            ReplyMsg = NULL;
            continue;
        }

        /* It's some other kind of request. Get the lock for the lookup */
        CsrAcquireProcessLock();

        /* Now do the lookup to get the CSR_THREAD */
        CsrThread = CsrLocateThreadByClientId(&CsrProcess,
                                              &ReceiveMsg.Header.ClientId);

        /* Did we find a thread? */
        if (!CsrThread)
        {
            /* This wasn't a CSR Thread, release lock */
            CsrReleaseProcessLock();

            /* If this was an exception, handle it */
            if (MessageType == LPC_EXCEPTION)
            {
                ReplyMsg = &ReceiveMsg;
                ReplyPort = CsrApiPort;
                ReplyMsg->Status = DBG_CONTINUE;
            }
            else if (MessageType == LPC_PORT_CLOSED ||
                     MessageType == LPC_CLIENT_DIED)
            {
                /* The Client or Port are gone, loop again */
                ReplyMsg = NULL;
                ReplyPort = CsrApiPort;
            }
            else if (MessageType == LPC_ERROR_EVENT)
            {
                /* If it's a hard error, handle this too */
                HardErrorMsg = (PHARDERROR_MSG)&ReceiveMsg;

                /* Default it to unhandled */
                HardErrorMsg->Response = ResponseNotHandled;

                /* Check if there are free api threads */
                CsrpCheckRequestThreads();
                if (CsrpStaticThreadCount)
                {
                    /* Loop every Server DLL */
                    for (i = 0; i < CSR_SERVER_DLL_MAX; i++)
                    {
                        /* Get the Server DLL */
                        ServerDll = CsrLoadedServerDll[i];

                        /* Check if it's valid and if it has a Hard Error Callback */
                        if ((ServerDll) && (ServerDll->HardErrorCallback))
                        {
                            /* Call it */
                            ServerDll->HardErrorCallback(NULL, HardErrorMsg);

                            /* If it's handled, get out of here */
                            if (HardErrorMsg->Response != ResponseNotHandled) break;
                        }
                    }
                }

                /* Increase the thread count */
                _InterlockedIncrement(&CsrpStaticThreadCount);

                /* If the response was 0xFFFFFFFF, we'll ignore it */
                if (HardErrorMsg->Response == 0xFFFFFFFF)
                {
                    ReplyMsg = NULL;
                    ReplyPort = CsrApiPort;
                }
                else
                {
                    ReplyMsg = &ReceiveMsg;
                }
            }
            else if (MessageType == LPC_REQUEST)
            {
                /* This is an API Message coming from a non-CSR Thread */
                ReplyMsg = &ReceiveMsg;
                ReplyPort = CsrApiPort;
                ReplyMsg->Status = STATUS_ILLEGAL_FUNCTION;
            }
            else if (MessageType == LPC_DATAGRAM)
            {
                /* This is an API call, get the Server ID */
                ServerId = CSR_API_NUMBER_TO_SERVER_ID(ReceiveMsg.ApiNumber);

                /* Make sure that the ID is within limits, and the Server DLL loaded */
                ServerDll = NULL;
                if ((ServerId >= CSR_SERVER_DLL_MAX) ||
                    (!(ServerDll = CsrLoadedServerDll[ServerId])))
                {
                    /* We are beyond the Maximum Server ID */
                    DPRINT1("CSRSS: %lx is invalid ServerDllIndex (%08x)\n",
                            ServerId, ServerDll);
                    DbgBreakPoint();
                    ReplyPort = CsrApiPort;
                    ReplyMsg = NULL;
                    continue;
                }

                   /* Get the API ID */
                ApiId = CSR_API_NUMBER_TO_API_ID(ReceiveMsg.ApiNumber);

                /* Normalize it with our Base ID */
                ApiId -= ServerDll->ApiBase;

                /* Make sure that the ID is within limits, and the entry exists */
                if (ApiId >= ServerDll->HighestApiSupported)
                {
                    /* We are beyond the Maximum API ID, or it doesn't exist */
                    DPRINT1("CSRSS: %lx is invalid ApiTableIndex for %Z\n",
                            CSR_API_NUMBER_TO_API_ID(ReceiveMsg.ApiNumber),
                            &ServerDll->Name);
                    ReplyPort = CsrApiPort;
                    ReplyMsg = NULL;
                    continue;
                }

                if (CsrDebug & 2)
                {
                    DPRINT1("[%02x] CSRSS: [%02x,%02x] - %s Api called from %08x\n",
                            Teb->ClientId.UniqueThread,
                            ReceiveMsg.Header.ClientId.UniqueProcess,
                            ReceiveMsg.Header.ClientId.UniqueThread,
                            ServerDll->NameTable[ApiId],
                            NULL);
                }

                /* Assume success */
                ReceiveMsg.Status = STATUS_SUCCESS;

                /* Validation complete, start SEH */
                _SEH2_TRY
                {
                    /* Make sure we have enough threads */
                    CsrpCheckRequestThreads();

                    /* Call the API and get the result */
                    ReplyMsg = NULL;
                    ReplyPort = CsrApiPort;
                    ServerDll->DispatchTable[ApiId](&ReceiveMsg, &Reply);

                    /* Increase the static thread count */
                    _InterlockedIncrement(&CsrpStaticThreadCount);
                }
                _SEH2_EXCEPT(CsrUnhandledExceptionFilter(_SEH2_GetExceptionInformation()))
                {
                    ReplyMsg = NULL;
                    ReplyPort = CsrApiPort;
                }
                _SEH2_END;
            }
            else
            {
                /* Some other ignored message type */
                ReplyMsg = NULL;
                ReplyPort = CsrApiPort;
            }

            /* Keep going */
            continue;
        }

        /* We have a valid thread, was this an LPC Request? */
        if (MessageType != LPC_REQUEST)
        {
            /* It's not an API, check if the client died */
            if (MessageType == LPC_CLIENT_DIED)
            {
                /* Get the information and check if it matches our thread */
                ClientDiedMsg = (PCLIENT_DIED_MSG)&ReceiveMsg;
                if (ClientDiedMsg->CreateTime.QuadPart == CsrThread->CreateTime.QuadPart)
                {
                    /* Reference the thread */
                    CsrLockedReferenceThread(CsrThread);

                    /* Destroy the thread in the API Message */
                    CsrDestroyThread(&ReceiveMsg.Header.ClientId);

                    /* Check if the thread was actually ourselves */
                    if (CsrProcess->ThreadCount == 1)
                    {
                        /* Kill the process manually here */
                        CsrDestroyProcess(&CsrThread->ClientId, 0);
                    }

                    /* Remove our extra reference */
                    CsrLockedDereferenceThread(CsrThread);
                }

                /* Release the lock and keep looping */
                CsrReleaseProcessLock();
                ReplyMsg = NULL;
                ReplyPort = CsrApiPort;
                continue;
            }

            /* Reference the thread and release the lock */
            CsrLockedReferenceThread(CsrThread);
            CsrReleaseProcessLock();

            /* Check if this was an exception */
            if (MessageType == LPC_EXCEPTION)
            {
                /* Kill the process */
                NtTerminateProcess(CsrProcess->ProcessHandle, STATUS_ABANDONED);

                /* Destroy it from CSR */
                CsrDestroyProcess(&ReceiveMsg.Header.ClientId, STATUS_ABANDONED);

                /* Return a Debug Message */
                DebugMessage = (PDBGKM_MSG)&ReceiveMsg;
                DebugMessage->ReturnedStatus = DBG_CONTINUE;
                ReplyMsg = &ReceiveMsg;
                ReplyPort = CsrApiPort;

                /* Remove our extra reference */
                CsrDereferenceThread(CsrThread);
            }
            else if (MessageType == LPC_ERROR_EVENT)
            {
                /* If it's a hard error, handle this too */
                HardErrorMsg = (PHARDERROR_MSG)&ReceiveMsg;

                /* Default it to unhandled */
                HardErrorMsg->Response = ResponseNotHandled;

                /* Check if there are free api threads */
                CsrpCheckRequestThreads();
                if (CsrpStaticThreadCount)
                {
                    /* Loop every Server DLL */
                    for (i = 0; i < CSR_SERVER_DLL_MAX; i++)
                    {
                        /* Get the Server DLL */
                        ServerDll = CsrLoadedServerDll[i];

                        /* Check if it's valid and if it has a Hard Error Callback */
                        if ((ServerDll) && (ServerDll->HardErrorCallback))
                        {
                            /* Call it */
                            ServerDll->HardErrorCallback(CsrThread, HardErrorMsg);

                            /* If it's handled, get out of here */
                            if (HardErrorMsg->Response != ResponseNotHandled) break;
                        }
                    }
                }

                /* Increase the thread count */
                _InterlockedIncrement(&CsrpStaticThreadCount);

                /* If the response was 0xFFFFFFFF, we'll ignore it */
                if (HardErrorMsg->Response == 0xFFFFFFFF)
                {
                    ReplyMsg = NULL;
                    ReplyPort = CsrApiPort;
                }
                else
                {
                    CsrDereferenceThread(CsrThread);
                    ReplyMsg = &ReceiveMsg;
                    ReplyPort = CsrApiPort;
                }
            }
            else
            {
                /* Something else */
                CsrDereferenceThread(CsrThread);
                ReplyMsg = NULL;
            }

            /* Keep looping */
            continue;
        }

        /* We got an API Request */
        CsrLockedReferenceThread(CsrThread);
        CsrReleaseProcessLock();

        /* This is an API call, get the Server ID */
        ServerId = CSR_API_NUMBER_TO_SERVER_ID(ReceiveMsg.ApiNumber);

        /* Make sure that the ID is within limits, and the Server DLL loaded */
        ServerDll = NULL;
        if ((ServerId >= CSR_SERVER_DLL_MAX) ||
            (!(ServerDll = CsrLoadedServerDll[ServerId])))
        {
            /* We are beyond the Maximum Server ID */
            DPRINT1("CSRSS: %lx is invalid ServerDllIndex (%08x)\n",
                    ServerId, ServerDll);
            DbgBreakPoint();

            ReplyPort = CsrApiPort;
            ReplyMsg = &ReceiveMsg;
            ReplyMsg->Status = STATUS_ILLEGAL_FUNCTION;
            CsrDereferenceThread(CsrThread);
            continue;
        }

        /* Get the API ID */
        ApiId = CSR_API_NUMBER_TO_API_ID(ReceiveMsg.ApiNumber);

        /* Normalize it with our Base ID */
        ApiId -= ServerDll->ApiBase;

        /* Make sure that the ID is within limits, and the entry exists */
        if (ApiId >= ServerDll->HighestApiSupported)
        {
            /* We are beyond the Maximum API ID, or it doesn't exist */
            DPRINT1("CSRSS: %lx is invalid ApiTableIndex for %Z\n",
                    CSR_API_NUMBER_TO_API_ID(ReceiveMsg.ApiNumber),
                    &ServerDll->Name);

            ReplyPort = CsrApiPort;
            ReplyMsg = &ReceiveMsg;
            ReplyMsg->Status = STATUS_ILLEGAL_FUNCTION;
            CsrDereferenceThread(CsrThread);
            continue;
        }

        if (CsrDebug & 2)
        {
            DPRINT1("[%02x] CSRSS: [%02x,%02x] - %s Api called from %08x\n",
                    Teb->ClientId.UniqueThread,
                    ReceiveMsg.Header.ClientId.UniqueProcess,
                    ReceiveMsg.Header.ClientId.UniqueThread,
                    ServerDll->NameTable[ApiId],
                    CsrThread);
        }

        /* Assume success */
        ReplyMsg = &ReceiveMsg;
        ReceiveMsg.Status = STATUS_SUCCESS;

        /* Now we reply to a particular client */
        ReplyPort = CsrThread->Process->ClientPort;

        /* Check if there's a capture buffer */
        if (ReceiveMsg.CsrCaptureData)
        {
            /* Capture the arguments */
            if (!CsrCaptureArguments(CsrThread, &ReceiveMsg))
            {
                /* Ignore this message if we failed to get the arguments */
                CsrDereferenceThread(CsrThread);
                continue;
            }
        }

        /* Validation complete, start SEH */
        _SEH2_TRY
        {
            /* Make sure we have enough threads */
            CsrpCheckRequestThreads();

            Teb->CsrClientThread = CsrThread;

            /* Call the API and get the result */
            Reply = 0;
            ServerDll->DispatchTable[ApiId](&ReceiveMsg, &Reply);

            /* Increase the static thread count */
            _InterlockedIncrement(&CsrpStaticThreadCount);

            Teb->CsrClientThread = CurrentThread;

            if (Reply == 3)
            {
                ReplyMsg = NULL;
                if (ReceiveMsg.CsrCaptureData)
                {
                    CsrReleaseCapturedArguments(&ReceiveMsg);
                }
                CsrDereferenceThread(CsrThread);
                ReplyPort = CsrApiPort;
            }
            else if (Reply == 2)
            {
                NtReplyPort(ReplyPort, &ReplyMsg->Header);
                ReplyPort = CsrApiPort;
                ReplyMsg = NULL;
                CsrDereferenceThread(CsrThread);
            }
            else if (Reply == 1)
            {
                ReplyPort = CsrApiPort;
                ReplyMsg = NULL;
            }
            else
            {
                if (ReceiveMsg.CsrCaptureData)
                {
                    CsrReleaseCapturedArguments(&ReceiveMsg);
                }
                CsrDereferenceThread(CsrThread);
            }
        }
        _SEH2_EXCEPT(CsrUnhandledExceptionFilter(_SEH2_GetExceptionInformation()))
        {
            ReplyMsg = NULL;
            ReplyPort = CsrApiPort;
        }
        _SEH2_END;
    }

    /* We're out of the loop for some reason, terminate! */
    NtTerminateThread(NtCurrentThread(), Status);
    return Status;
}
#else
{
    PTEB Teb = NtCurrentTeb();
    LARGE_INTEGER TimeOut;
    NTSTATUS Status;
    BYTE RawRequest[LPC_MAX_DATA_LENGTH];
    PCSR_API_MESSAGE Request = (PCSR_API_MESSAGE)RawRequest;
    PCSR_API_MESSAGE Reply;
    PCSR_PROCESS CsrProcess;
    PCSR_THREAD ServerThread, CsrThread;
    ULONG MessageType;
    HANDLE ReplyPort;
    PDBGKM_MSG DebugMessage;
    PHARDERROR_MSG HardErrorMsg;
    PCLIENT_DIED_MSG ClientDiedMsg;
    DPRINT("CSR: %s called\n", __FUNCTION__);

    /* Setup LPC loop port and message */
    Reply = NULL;
    ReplyPort = CsrApiPort;

    /* Connect to user32 */
    while (!CsrConnectToUser())
    {
        /* Set up the timeout for the connect (30 seconds) */
        TimeOut.QuadPart = -30 * 1000 * 1000 * 10;

        /* Keep trying until we get a response */
        Teb->Win32ClientInfo[0] = 0;
        NtDelayExecution(FALSE, &TimeOut);
    }

    /* Get our thread */
    ServerThread = Teb->CsrClientThread;

    /* If we got an event... */
    if (Parameter)
    {
        /* Set it, to let stuff waiting on us load */
        Status = NtSetEvent((HANDLE)Parameter, NULL);
        ASSERT(NT_SUCCESS(Status));

        /* Increase the Thread Counts */
        _InterlockedIncrement(&CsrpStaticThreadCount);
        _InterlockedIncrement(&CsrpDynamicThreadTotal);
    }

    /* Now start the loop */
    while (TRUE)
    {
        /* Make sure the real CID is set */
        Teb->RealClientId = Teb->ClientId;

        /* Debug check */
        if (Teb->CountOfOwnedCriticalSections)
        {
            DPRINT1("CSRSRV: FATAL ERROR. CsrThread is Idle while holding %lu critical sections\n",
                    Teb->CountOfOwnedCriticalSections);
            DPRINT1("CSRSRV: Last Receive Message %lx ReplyMessage %lx\n",
                    Request, Reply);
            DbgBreakPoint();
        }

        /* Send the reply and wait for a new request */
        DPRINT("Replying to: %lx (%lx)\n", ReplyPort, CsrApiPort);
        Status = NtReplyWaitReceivePort(ReplyPort,
                                        0,
                                        &Reply->Header,
                                        &Request->Header);
        /* Check if we didn't get success */
        if (Status != STATUS_SUCCESS)
        {
            /* Was it a failure or another success code? */
            if (!NT_SUCCESS(Status))
            {
                /* Check for specific status cases */
                if ((Status != STATUS_INVALID_CID) &&
                    (Status != STATUS_UNSUCCESSFUL) &&
                    ((Status == STATUS_INVALID_HANDLE) || (ReplyPort == CsrApiPort)))
                {
                    /* Notify the debugger */
                    DPRINT1("CSRSS: ReceivePort failed - Status == %X\n", Status);
                    DPRINT1("CSRSS: ReplyPortHandle %lx CsrApiPort %lx\n", ReplyPort, CsrApiPort);
                }

                /* We failed big time, so start out fresh */
                Reply = NULL;
                ReplyPort = CsrApiPort;
                DPRINT1("failed: %lx\n", Status);
                continue;
            }
            else
            {
                /* A bizare "success" code, just try again */
                DPRINT1("NtReplyWaitReceivePort returned \"success\" status 0x%x\n", Status);
                continue;
            }
        }

        /* Use whatever Client ID we got */
        Teb->RealClientId = Request->Header.ClientId;

        /* Get the Message Type */
        MessageType = Request->Header.u2.s2.Type;

        /* Handle connection requests */
        if (MessageType == LPC_CONNECTION_REQUEST)
        {
            /* Handle the Connection Request */
            DPRINT("Accepting new connection\n");
            CsrpHandleConnectionRequest((PPORT_MESSAGE)Request);
            Reply = NULL;
            ReplyPort = CsrApiPort;
            continue;
        }

        /* It's some other kind of request. Get the lock for the lookup */
        CsrAcquireProcessLock();

        /* Now do the lookup to get the CSR_THREAD */
        CsrThread = CsrLocateThreadByClientId(&CsrProcess,
                                              &Request->Header.ClientId);

        /* Did we find a thread? */
        if (!CsrThread)
        {
            /* This wasn't a CSR Thread, release lock */
            CsrReleaseProcessLock();

            /* If this was an exception, handle it */
            if (MessageType == LPC_EXCEPTION)
            {
                DPRINT1("Exception from unknown thread, just continue\n");
                Reply = Request;
                ReplyPort = CsrApiPort;
                Reply->Status = DBG_CONTINUE;
            }
            else if (MessageType == LPC_PORT_CLOSED ||
                     MessageType == LPC_CLIENT_DIED)
            {
                /* The Client or Port are gone, loop again */
                DPRINT("Death from unknown thread, just continue\n");
                Reply = NULL;
                ReplyPort = CsrApiPort;
            }
            else if (MessageType == LPC_ERROR_EVENT)
            {
                /* If it's a hard error, handle this too */
                DPRINT1("Hard error from unknown thread, call handlers\n");
HandleHardError:
                HardErrorMsg = (PHARDERROR_MSG)Request;

                /* Default it to unhandled */
                HardErrorMsg->Response = ResponseNotHandled;

                /* Check if there are free api threads */
                CsrpCheckRequestThreads();
                if (CsrpStaticThreadCount)
                {
                    CsrHandleHardError(CsrThread, (PHARDERROR_MSG)Request);
                }

                /* If the response was 0xFFFFFFFF, we'll ignore it */
                if (HardErrorMsg->Response == 0xFFFFFFFF)
                {
                    Reply = NULL;
                    ReplyPort = CsrApiPort;
                }
                else
                {
                    if (CsrThread) CsrDereferenceThread(CsrThread);
                    Reply = Request;
                    ReplyPort = CsrApiPort;
                }
            }
            else if (MessageType == LPC_REQUEST)
            {
                /* This is an API Message coming from a non-CSR Thread */
                DPRINT1("No thread found for request %lx and clientID %lx.%lx\n",
                        Request->ApiNumber & 0xFFFF,
                        Request->Header.ClientId.UniqueProcess,
                        Request->Header.ClientId.UniqueThread);
                Reply = Request;
                ReplyPort = CsrApiPort;
                Reply->Status = STATUS_ILLEGAL_FUNCTION;
            }
            else if (MessageType == LPC_DATAGRAM)
            {
                DPRINT1("Kernel datagram: not yet supported\n");
                Reply = NULL;
                ReplyPort = CsrApiPort;
            }
            else
            {
                /* Some other ignored message type */
                Reply = NULL;
                ReplyPort = CsrApiPort;
            }

            /* Keep going */
            continue;
        }

        /* We have a valid thread, was this an LPC Request? */
        if (MessageType != LPC_REQUEST)
        {
            /* It's not an API, check if the client died */
            if (MessageType == LPC_CLIENT_DIED)
            {
                /* Get the information and check if it matches our thread */
                ClientDiedMsg = (PCLIENT_DIED_MSG)Request;
                if (ClientDiedMsg->CreateTime.QuadPart == CsrThread->CreateTime.QuadPart)
                {
                    /* Reference the thread */
                    CsrLockedReferenceThread(CsrThread);

                    /* Destroy the thread in the API Message */
                    CsrDestroyThread(&Request->Header.ClientId);

                    /* Check if the thread was actually ourselves */
                    if (CsrProcess->ThreadCount == 1)
                    {
                        /* Kill the process manually here */
                        DPRINT1("Last thread\n");
                        CsrDestroyProcess(&CsrThread->ClientId, 0);
                    }

                    /* Remove our extra reference */
                    CsrLockedDereferenceThread(CsrThread);
                }

                /* Release the lock and keep looping */
                CsrReleaseProcessLock();
                Reply = NULL;
                ReplyPort = CsrApiPort;
                continue;
            }

            /* Reference the thread and release the lock */
            CsrLockedReferenceThread(CsrThread);
            CsrReleaseProcessLock();

            /* If this was an exception, handle it */
            if (MessageType == LPC_EXCEPTION)
            {
                /* Kill the process */
                DPRINT1("Exception in %lx.%lx. Killing...\n",
                        Request->Header.ClientId.UniqueProcess,
                        Request->Header.ClientId.UniqueThread);
                NtTerminateProcess(CsrProcess->ProcessHandle, STATUS_ABANDONED);

                /* Destroy it from CSR */
                CsrDestroyProcess(&Request->Header.ClientId, STATUS_ABANDONED);

                /* Return a Debug Message */
                DebugMessage = (PDBGKM_MSG)Request;
                DebugMessage->ReturnedStatus = DBG_CONTINUE;
                Reply = Request;
                ReplyPort = CsrApiPort;

                /* Remove our extra reference */
                CsrDereferenceThread(CsrThread);
            }
            else if (MessageType == LPC_ERROR_EVENT)
            {
                DPRINT1("Hard error from known CSR thread... handling\n");
                goto HandleHardError;
            }
            else
            {
                /* Something else */
                DPRINT1("Unhandled message type: %lx\n", MessageType);
                CsrDereferenceThread(CsrThread);
                Reply = NULL;
            }

            /* Keep looping */
            continue;
        }

        /* We got an API Request */
        CsrLockedReferenceThread(CsrThread);
        CsrReleaseProcessLock();

        /* Assume success */
        Reply = Request;
        Request->Status = STATUS_SUCCESS;

        /* Now we reply to a particular client */
        ReplyPort = CsrThread->Process->ClientPort;

        DPRINT("CSR: Got CSR API: %x [Message Origin: %x]\n",
                Request->ApiNumber,
                Request->Header.ClientId.UniqueThread);

        /* Validation complete, start SEH */
        _SEH2_TRY
        {
            ULONG ReplyCode;

            /* Make sure we have enough threads */
            CsrpCheckRequestThreads();

            /* Set the client thread pointer */
            NtCurrentTeb()->CsrClientThread = CsrThread;

            /* Call the Handler */
            CsrApiCallHandler(Request, &ReplyCode);

            /* Increase the static thread count */
            _InterlockedIncrement(&CsrpStaticThreadCount);

            /* Restore the server thread */
            NtCurrentTeb()->CsrClientThread = ServerThread;

            /* Check if this is a dead client now */
            if (Request->ApiNumber == 0xBABE)
            {
                /* Reply to the death message */
                NtReplyPort(ReplyPort, &Reply->Header);

                /* Reply back to the API port now */
                ReplyPort = CsrApiPort;
                Reply = NULL;

                /* Drop the reference */
                CsrDereferenceThread(CsrThread);
            }
            else
            {
                /* Drop the reference */
                CsrDereferenceThread(CsrThread);
            }
        }
        _SEH2_EXCEPT(CsrUnhandledExceptionFilter(_SEH2_GetExceptionInformation()))
        {
            Reply = NULL;
            ReplyPort = CsrApiPort;
        }
        _SEH2_END;
    }

    /* Close the port and exit the thread */
    // NtClose(ServerPort);

    DPRINT1("CSR: %s done\n", __FUNCTION__);
    /* We're out of the loop for some reason, terminate! */
    NtTerminateThread(NtCurrentThread(), Status);
    //return Status;
}
#endif

/*++
 * @name CsrApiPortInitialize
 *
 * The CsrApiPortInitialize routine initializes the LPC Port used for
 * communications with the Client/Server Runtime (CSR) and initializes the
 * static thread that will handle connection requests and APIs.
 *
 * @param None
 *
 * @return STATUS_SUCCESS in case of success, STATUS_UNSUCCESSFUL
 *         otherwise.
 *
 * @remarks None.
 *
 *--*/
NTSTATUS
NTAPI
CsrApiPortInitialize(VOID)
{
    ULONG Size;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    HANDLE hRequestEvent, hThread;
    CLIENT_ID ClientId;
    PLIST_ENTRY ListHead, NextEntry;
    PCSR_THREAD ServerThread;

    /* Calculate how much space we'll need for the Port Name */
    Size = CsrDirectoryName.Length + sizeof(CSR_PORT_NAME) + sizeof(WCHAR);

    /* Create the buffer for it */
    CsrApiPortName.Buffer = RtlAllocateHeap(CsrHeap, 0, Size);
    if (!CsrApiPortName.Buffer) return STATUS_NO_MEMORY;

    /* Setup the rest of the empty string */
    CsrApiPortName.Length = 0;
    CsrApiPortName.MaximumLength = (USHORT)Size;
    RtlAppendUnicodeStringToString(&CsrApiPortName, &CsrDirectoryName);
    RtlAppendUnicodeToString(&CsrApiPortName, UNICODE_PATH_SEP);
    RtlAppendUnicodeToString(&CsrApiPortName, CSR_PORT_NAME);
    if (CsrDebug & 1)
    {
        DPRINT1("CSRSS: Creating %wZ port and associated threads\n", &CsrApiPortName);
        DPRINT1("CSRSS: sizeof( CONNECTINFO ) == %ld  sizeof( API_MSG ) == %ld\n",
                sizeof(CSR_CONNECTION_INFO), sizeof(CSR_API_MESSAGE));
    }

    /* FIXME: Create a Security Descriptor */

    /* Initialize the Attributes */
    InitializeObjectAttributes(&ObjectAttributes,
                               &CsrApiPortName,
                               0,
                               NULL,
                               NULL /* FIXME*/);

    /* Create the Port Object */
    Status = NtCreatePort(&CsrApiPort,
                          &ObjectAttributes,
                          LPC_MAX_DATA_LENGTH, // hack ; sizeof(CSR_CONNECTION_INFO),
                          LPC_MAX_MESSAGE_LENGTH, // hack ; sizeof(CSR_API_MESSAGE),
                          16 * PAGE_SIZE);
    if (NT_SUCCESS(Status))
    {
        /* Create the event the Port Thread will use */
        Status = NtCreateEvent(&hRequestEvent,
                               EVENT_ALL_ACCESS,
                               NULL,
                               SynchronizationEvent,
                               FALSE);
        if (NT_SUCCESS(Status))
        {
            /* Create the Request Thread */
            Status = RtlCreateUserThread(NtCurrentProcess(),
                                         NULL,
                                         TRUE,
                                         0,
                                         0,
                                         0,
                                         (PVOID)CsrApiRequestThread,
                                         (PVOID)hRequestEvent,
                                         &hThread,
                                         &ClientId);
            if (NT_SUCCESS(Status))
            {
                /* Add this as a static thread to CSRSRV */
                CsrAddStaticServerThread(hThread, &ClientId, CsrThreadIsServerThread);

                /* Get the Thread List Pointers */
                ListHead = &CsrRootProcess->ThreadList;
                NextEntry = ListHead->Flink;

                /* Start looping the list */
                while (NextEntry != ListHead)
                {
                    /* Get the Thread */
                    ServerThread = CONTAINING_RECORD(NextEntry, CSR_THREAD, Link);

                    /* Start it up */
                    Status = NtResumeThread(ServerThread->ThreadHandle, NULL);

                    /* Is this a Server Thread? */
                    if (ServerThread->Flags & CsrThreadIsServerThread)
                    {
                        /* If so, then wait for it to initialize */
                        Status = NtWaitForSingleObject(hRequestEvent, FALSE, NULL);
                        ASSERT(NT_SUCCESS(Status));
                    }

                    /* Next thread */
                    NextEntry = NextEntry->Flink;
                }

                /* We don't need this anymore */
                NtClose(hRequestEvent);
            }
        }
    }

    /* Return */
    return Status;
}

NTSTATUS WINAPI
CsrpHandleConnectionRequest(PPORT_MESSAGE Request)
{
    NTSTATUS Status;
    HANDLE ServerPort = NULL;//, ServerThread = NULL;
    PCSR_PROCESS ProcessData = NULL;
    REMOTE_PORT_VIEW RemotePortView;
//    CLIENT_ID ClientId;
    BOOLEAN AllowConnection = FALSE;
    PCSR_CONNECTION_INFO ConnectInfo;
    ServerPort = NULL;

    DPRINT("CSR: %s: Handling: %p\n", __FUNCTION__, Request);

    ConnectInfo = (PCSR_CONNECTION_INFO)(Request + 1);

    /* Save the process ID */
    RtlZeroMemory(ConnectInfo, sizeof(CSR_CONNECTION_INFO));

    CsrLockProcessByClientId(Request->ClientId.UniqueProcess, &ProcessData);
    if (!ProcessData)
    {
        DPRINT1("CSRSRV: Unknown process: %lx. Will be rejecting connection\n",
                Request->ClientId.UniqueProcess);
    }

    if ((ProcessData) && (ProcessData != CsrRootProcess))
    {
        /* Attach the Shared Section */
        Status = CsrSrvAttachSharedSection(ProcessData, ConnectInfo);
        if (NT_SUCCESS(Status))
        {
            DPRINT("Connection ok\n");
            AllowConnection = TRUE;
        }
        else
        {
            DPRINT1("Shared section map failed: %lx\n", Status);
        }
    }
    else if (ProcessData == CsrRootProcess)
    {
        AllowConnection = TRUE;
    }

    /* Release the process */
    if (ProcessData) CsrUnlockProcess(ProcessData);

    /* Setup the Port View Structure */
    RemotePortView.Length = sizeof(REMOTE_PORT_VIEW);
    RemotePortView.ViewSize = 0;
    RemotePortView.ViewBase = NULL;

    /* Save the Process ID */
    ConnectInfo->ProcessId = NtCurrentTeb()->ClientId.UniqueProcess;

    Status = NtAcceptConnectPort(&ServerPort,
                                 AllowConnection ? UlongToPtr(ProcessData->SequenceNumber) : 0,
                                 Request,
                                 AllowConnection,
                                 NULL,
                                 &RemotePortView);
    if (!NT_SUCCESS(Status))
    {
         DPRINT1("CSRSS: NtAcceptConnectPort - failed.  Status == %X\n", Status);
    }
    else if (AllowConnection)
    {
        if (CsrDebug & 2)
        {
            DPRINT1("CSRSS: ClientId: %lx.%lx has ClientView: Base=%p, Size=%lx\n",
                    Request->ClientId.UniqueProcess,
                    Request->ClientId.UniqueThread,
                    RemotePortView.ViewBase,
                    RemotePortView.ViewSize);
        }

        /* Set some Port Data in the Process */
        ProcessData->ClientPort = ServerPort;
        ProcessData->ClientViewBase = (ULONG_PTR)RemotePortView.ViewBase;
        ProcessData->ClientViewBounds = (ULONG_PTR)((ULONG_PTR)RemotePortView.ViewBase +
                                                    (ULONG_PTR)RemotePortView.ViewSize);

        /* Complete the connection */
        Status = NtCompleteConnectPort(ServerPort);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CSRSS: NtCompleteConnectPort - failed.  Status == %X\n", Status);
        }
    }
    else
    {
        DPRINT1("CSRSS: Rejecting Connection Request from ClientId: %lx.%lx\n",
                Request->ClientId.UniqueProcess,
                Request->ClientId.UniqueThread);
    }

    return Status;
}

/*++
 * @name CsrConnectToUser
 * @implemented NT4
 *
 * The CsrConnectToUser connects to the User subsystem.
 *
 * @param None
 *
 * @return A pointer to the CSR Thread
 *
 * @remarks None.
 *
 *--*/
PCSR_THREAD
NTAPI
CsrConnectToUser(VOID)
{
    NTSTATUS Status;
    ANSI_STRING DllName;
    UNICODE_STRING TempName;
    HANDLE hUser32;
    STRING StartupName;
    PTEB Teb = NtCurrentTeb();
    PCSR_THREAD CsrThread;
    BOOLEAN Connected;

    /* Check if we didn't already find it */
    if (!CsrClientThreadSetup)
    {
        /* Get the DLL Handle for user32.dll */
        RtlInitAnsiString(&DllName, "user32");
        RtlAnsiStringToUnicodeString(&TempName, &DllName, TRUE);
        Status = LdrGetDllHandle(NULL,
                                 NULL,
                                 &TempName,
                                 &hUser32);
        RtlFreeUnicodeString(&TempName);

        /* If we got teh handle, get the Client Thread Startup Entrypoint */
        if (NT_SUCCESS(Status))
        {
            RtlInitAnsiString(&StartupName,"ClientThreadSetup");
            Status = LdrGetProcedureAddress(hUser32,
                                            &StartupName,
                                            0,
                                            (PVOID)&CsrClientThreadSetup);
        }
    }

    /* Connect to user32 */
    _SEH2_TRY
    {
        Connected = CsrClientThreadSetup();
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Connected = FALSE;
    } _SEH2_END;
    
    if (!Connected)
    {
        DPRINT1("CSRSS: CsrConnectToUser failed\n");
        return NULL;
    }

    /* Save pointer to this thread in TEB */
    CsrAcquireProcessLock();
    CsrThread = CsrLocateThreadInProcess(NULL, &Teb->ClientId);
    CsrReleaseProcessLock();
    if (CsrThread) Teb->CsrClientThread = CsrThread;

    /* Return it */
    return CsrThread;
}

/*++
 * @name CsrQueryApiPort
 * @implemented NT4
 *
 * The CsrQueryApiPort routine returns a handle to the CSR API LPC port.
 *
 * @param None.
 *
 * @return A handle to the port.
 *
 * @remarks None.
 *
 *--*/
HANDLE
NTAPI
CsrQueryApiPort(VOID)
{
    DPRINT("CSRSRV: %s called\n", __FUNCTION__);
    return CsrApiPort;
}

/*++
 * @name CsrCaptureArguments
 * @implemented NT5.1
 *
 * The CsrCaptureArguments routine validates a CSR Capture Buffer and
 * re-captures it into a server CSR Capture Buffer.
 *
 * @param CsrThread
 *        Pointer to the CSR Thread performing the validation.
 *
 * @param ApiMessage
 *        Pointer to the CSR API Message containing the Capture Buffer
 *        that needs to be validated.
 *
 * @return TRUE if validation succeeded, FALSE otherwise.
 *
 * @remarks None.
 *
 *--*/
BOOLEAN
NTAPI
CsrCaptureArguments(IN PCSR_THREAD CsrThread,
                    IN PCSR_API_MESSAGE ApiMessage)
{
    PCSR_CAPTURE_BUFFER LocalCaptureBuffer = NULL, RemoteCaptureBuffer = NULL;
    ULONG LocalLength = 0, PointerCount = 0;
    SIZE_T BufferDistance = 0;
    ULONG_PTR **PointerOffsets = NULL, *CurrentPointer = NULL;

    /* Use SEH to make sure this is valid */
    _SEH2_TRY
    {
        /* Get the buffer we got from whoever called NTDLL */
        LocalCaptureBuffer = ApiMessage->CsrCaptureData;
        LocalLength = LocalCaptureBuffer->Size;

        /* Now check if the buffer is inside our mapped section */
        if (((ULONG_PTR)LocalCaptureBuffer < CsrThread->Process->ClientViewBase) ||
            (((ULONG_PTR)LocalCaptureBuffer + LocalLength) >= CsrThread->Process->ClientViewBounds))
        {
            /* Return failure */
            DPRINT1("*** CSRSS: CaptureBuffer outside of ClientView\n");
            ApiMessage->Status = STATUS_INVALID_PARAMETER;
            _SEH2_YIELD(return FALSE);
        }

        /* Check if the Length is valid */
        if (((LocalCaptureBuffer->PointerCount * 4 + sizeof(CSR_CAPTURE_BUFFER)) >
            LocalLength) ||(LocalLength > MAXWORD))
        {
            /* Return failure */
            DPRINT1("*** CSRSS: CaptureBuffer %p has bad length\n", LocalCaptureBuffer);
            DbgBreakPoint();
            ApiMessage->Status = STATUS_INVALID_PARAMETER;
            _SEH2_YIELD(return FALSE);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Return failure */
        ApiMessage->Status = STATUS_INVALID_PARAMETER;
        _SEH2_YIELD(return FALSE);
    } _SEH2_END;

    /* We validated the incoming buffer, now allocate the remote one */
    RemoteCaptureBuffer = RtlAllocateHeap(CsrHeap, 0, LocalLength);
    if (!RemoteCaptureBuffer)
    {
        /* We're out of memory */
        ApiMessage->Status = STATUS_NO_MEMORY;
        return FALSE;
    }

    /* Copy the client's buffer */
    RtlMoveMemory(RemoteCaptureBuffer, LocalCaptureBuffer, LocalLength);

    /* Copy the length */
    RemoteCaptureBuffer->Size = LocalLength;

    /* Calculate the difference between our buffer and the client's */
    BufferDistance = (ULONG_PTR)RemoteCaptureBuffer - (ULONG_PTR)LocalCaptureBuffer;

    /* Save the pointer count and offset pointer */
    PointerCount = RemoteCaptureBuffer->PointerCount;
    PointerOffsets = (ULONG_PTR**)(RemoteCaptureBuffer + 1);

    /* Start the loop */
    while (PointerCount)
    {
        /* Get the current pointer */
        if ((CurrentPointer = *PointerOffsets++))
        {
            /* Add it to the CSR Message structure */
            CurrentPointer += (ULONG_PTR)ApiMessage;

            /* Validate the bounds of the current pointer */
            if ((*CurrentPointer >= CsrThread->Process->ClientViewBase) &&
                (*CurrentPointer < CsrThread->Process->ClientViewBounds))
            {
                /* Modify the pointer to take into account its new position */
                *CurrentPointer += BufferDistance;
            }
            else
            {
                /* Invalid pointer, fail */
                DPRINT1("*** CSRSS: CaptureBuffer MessagePointer outside of ClientView\n");
                DbgBreakPoint();
                ApiMessage->Status = STATUS_INVALID_PARAMETER;
            }
        }

        /* Move to the next Pointer */
        PointerCount--;
    }

    /* Check if we got success */
    if (ApiMessage->Status != STATUS_SUCCESS)
    {
        /* Failure. Free the buffer and return*/
        RtlFreeHeap(CsrHeap, 0, RemoteCaptureBuffer);
        return FALSE;
    }
    else
    {
        /* Success, save the previous buffer */
        RemoteCaptureBuffer->PreviousCaptureBuffer = LocalCaptureBuffer;
        ApiMessage->CsrCaptureData = RemoteCaptureBuffer;
    }

    /* Success */
    return TRUE;
}

/*++
 * @name CsrReleaseCapturedArguments
 * @implemented NT5.1
 *
 * The CsrReleaseCapturedArguments routine releases a Capture Buffer
 * that was previously captured with CsrCaptureArguments.
 *
 * @param ApiMessage
 *        Pointer to the CSR API Message containing the Capture Buffer
 *        that needs to be released.
 *
 * @return None.
 *
 * @remarks None.
 *
 *--*/
VOID
NTAPI
CsrReleaseCapturedArguments(IN PCSR_API_MESSAGE ApiMessage)
{
    PCSR_CAPTURE_BUFFER RemoteCaptureBuffer, LocalCaptureBuffer;
    SIZE_T BufferDistance;
    ULONG PointerCount;
    ULONG_PTR **PointerOffsets, *CurrentPointer;

    /* Get the capture buffers */
    RemoteCaptureBuffer = ApiMessage->CsrCaptureData;
    LocalCaptureBuffer = RemoteCaptureBuffer->PreviousCaptureBuffer;

    /* Free the previous one */
    RemoteCaptureBuffer->PreviousCaptureBuffer = NULL;

    /* Find out the difference between the two buffers */
    BufferDistance = (ULONG_PTR)LocalCaptureBuffer - (ULONG_PTR)RemoteCaptureBuffer;

    /* Save the pointer count and offset pointer */
    PointerCount = RemoteCaptureBuffer->PointerCount;
    PointerOffsets = (ULONG_PTR**)(RemoteCaptureBuffer + 1);

    /* Start the loop */
    while (PointerCount)
    {
        /* Get the current pointer */
        CurrentPointer = *PointerOffsets++;
        if (CurrentPointer)
        {
            /* Add it to the CSR Message structure */
            CurrentPointer += (ULONG_PTR)ApiMessage;

            /* Modify the pointer to take into account its new position */
            *CurrentPointer += BufferDistance;
        }

        /* Move to the next Pointer */
        PointerCount--;
    }

    /* Copy the data back */
    RtlMoveMemory(LocalCaptureBuffer, RemoteCaptureBuffer, RemoteCaptureBuffer->Size);

    /* Free our allocated buffer */
    RtlFreeHeap(CsrHeap, 0, RemoteCaptureBuffer);
}


/*++
 * @name CsrValidateMessageBuffer
 * @implemented NT5.1
 *
 * The CsrValidateMessageBuffer routine validates a captured message buffer
 * present in the CSR Api Message
 *
 * @param ApiMessage
 *        Pointer to the CSR API Message containing the CSR Capture Buffer.
 *
 * @param Buffer
 *        Pointer to the message buffer to validate.
 *
 * @param ArgumentSize
 *        Size of the message to check.
 *
 * @param ArgumentCount
 *        Number of messages to check.
 *
 * @return TRUE if validation suceeded, FALSE otherwise.
 *
 * @remarks None.
 *
 *--*/
BOOLEAN
NTAPI
CsrValidateMessageBuffer(IN PCSR_API_MESSAGE ApiMessage,
                         IN PVOID *Buffer,
                         IN ULONG ArgumentSize,
                         IN ULONG ArgumentCount)
{
    PCSR_CAPTURE_BUFFER CaptureBuffer = ApiMessage->CsrCaptureData;
    SIZE_T BufferDistance;
    ULONG PointerCount, i;
    ULONG_PTR **PointerOffsets, *CurrentPointer;

    /* Make sure there are some arguments */
    if (!ArgumentCount) return FALSE;

    /* Check if didn't get a buffer and there aren't any arguments to check */
    if (!(*Buffer) && (!(ArgumentCount * ArgumentSize))) return TRUE;

    /* Check if we have no capture buffer */
    if (!CaptureBuffer)
    {
        /* In this case, check only the Process ID */
        if (NtCurrentTeb()->ClientId.UniqueProcess ==
            ApiMessage->Header.ClientId.UniqueProcess)
        {
            /* There is a match, validation succeeded */
            return TRUE;
        }
    }
    else
    {
        /* Make sure that there is still space left in the buffer */
        if ((CaptureBuffer->Size - (ULONG_PTR)*Buffer + (ULONG_PTR)CaptureBuffer) <
            (ArgumentCount * ArgumentSize))
        {
            /* Find out the difference between the two buffers */
            BufferDistance = (ULONG_PTR)Buffer - (ULONG_PTR)ApiMessage;

             /* Save the pointer count */
            PointerCount = CaptureBuffer->PointerCount;
            PointerOffsets = (ULONG_PTR**)(CaptureBuffer + 1);

            /* Start the loop */
            for (i = 0; i < PointerCount; i++)
            {
                /* Get the current pointer */
                CurrentPointer = *PointerOffsets++;

                /* Check if its' equal to the difference */
                if (*CurrentPointer == BufferDistance) return TRUE;
            }
        }
    }

    /* Failure */
    DbgPrint("CSRSRV: Bad message buffer %p\n", ApiMessage);
    DbgBreakPoint();
    return FALSE;
}

/*++
 * @name CsrValidateMessageString
 * @implemented NT5.1
 *
 * The CsrValidateMessageString validates a captured Wide-Character String
 * present in a CSR API Message.
 *
 * @param ApiMessage
 *        Pointer to the CSR API Message containing the CSR Capture Buffer.
 *
 * @param MessageString
 *        Pointer to the buffer containing the string to validate.
 *
 * @return TRUE if validation suceeded, FALSE otherwise.
 *
 * @remarks None.
 *
 *--*/
BOOLEAN
NTAPI
CsrValidateMessageString(IN PCSR_API_MESSAGE ApiMessage,
                         IN LPWSTR *MessageString)
{
    DPRINT1("CSRSRV: %s called\n", __FUNCTION__);
    return FALSE;
}

/* EOF */
