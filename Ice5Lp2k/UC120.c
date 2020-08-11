// UC120.c: UC120 internal misc handlers

#include <driver.h>
#include "UC120.tmh"

NTSTATUS UC120ReportState(PDEVICE_CONTEXT DeviceContext, int Param1, int Param2, int Param3, int Param4, int Param5)
{
    NTSTATUS status = STATUS_SUCCESS; // r6
    ULONG count; // r8
    WDFREQUEST PendingRequest; // [sp+18h] [bp-50h]
    PUC120_STATE Buffer; // [sp+1Ch] [bp-4Ch]

    if (DeviceContext->Calibrated)
    {
        status = WdfIoQueueRetrieveNextRequest(DeviceContext->DelayedIoCtlQueue, &PendingRequest);
        if (NT_SUCCESS(status))
        {
            do
            {
                status = WdfCollectionAdd( DeviceContext->DelayedQueryIoctlRequestCol, PendingRequest);
                if (!NT_SUCCESS(status))
                {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfCollectionAdd failed %!STATUS!", status);
                }
            } while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(DeviceContext->DelayedIoCtlQueue, &PendingRequest)));
        }

        count = WdfCollectionGetCount(DeviceContext->DelayedQueryIoctlRequestCol);
        if (count > 0)
        {
            do
            {
                PendingRequest = NULL;
                PendingRequest = WdfCollectionGetFirstItem(DeviceContext->DelayedQueryIoctlRequestCol);
                status = WdfRequestRetrieveOutputBuffer(PendingRequest, 20, &Buffer, 0);
                if (NT_SUCCESS(status))
                {
                    RtlZeroMemory(Buffer, 20);

                    Buffer->State0 = Param1;
                    if (Param1)
                    {
                        if (Param1 == 2)
                        {
                            Buffer->State1 = Param4;
                        }
                    }
                    else
                    {
                        Buffer->State1 = Param2;
                        Buffer->State2 = Param3;
                        Buffer->State3 = Param4;
                        Buffer->State4 = Param5;
                    }

                    WdfRequestCompleteWithInformation(PendingRequest, status, 20);
                }
                else
                {
                    WdfRequestComplete(PendingRequest, status);
                }

                WdfCollectionRemove(DeviceContext->DelayedQueryIoctlRequestCol, PendingRequest);
                --count;
            } while (count);
        }
    }

    return status;
}

NTSTATUS UC120ToggleReg4YetUnknown(PDEVICE_CONTEXT DeviceContext, UCHAR Bit)
{
    NTSTATUS status; // r4

    DeviceContext->Register4 ^= (DeviceContext->Register4 ^ 2 * (Bit != 0)) & 2;
    status = UC120SpiWrite(&DeviceContext->SpiDevice, 4, &DeviceContext->Register4, 1);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
    }
    return status;
}

NTSTATUS SetVConn(PDEVICE_CONTEXT DeviceContext, UCHAR Enable)
{
    NTSTATUS status; // r4

    status = UC120SpiRead(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1u);
    if (NT_SUCCESS(status))
    {
        DeviceContext->Register5 ^= (DeviceContext->Register5 ^ 32 * (Enable != 0)) & 0x20;
        status = UC120SpiWrite(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            goto exit;
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
        goto exit;
    }

exit:
    return status;
}

NTSTATUS SetPowerRole(PDEVICE_CONTEXT DeviceContext, UCHAR PowerRole)
{
    NTSTATUS status; // r4

    status = UC120SpiRead(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1u);
    if (NT_SUCCESS(status))
    {
        DeviceContext->Register5 ^= (DeviceContext->Register5 ^ (PowerRole != 0)) & 1;
        status = UC120SpiWrite(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1);
        if (!NT_SUCCESS(status))
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            goto exit;
        }
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
        goto exit;
    }

exit:
    return status;
}

void UC120ProcessIncomingPdMessage(PDEVICE_CONTEXT DeviceContext)
{
    NTSTATUS status; // r0
    UCHAR Reg1Content; // r3
    UCHAR PdMessageSize; // r5
    WDFREQUEST Request; // [sp+8h] [bp-20h]

    status = UC120SpiRead(&DeviceContext->SpiDevice, 1, &DeviceContext->Register1, 1u);
    if (NT_SUCCESS(status))
    {
        Reg1Content = DeviceContext->Register1;
        PdMessageSize = Reg1Content >> 5;
        if (Reg1Content >> 5 == 6)
        {
            DeviceContext->InternalState[22] = 6;
            KeSetEvent(&DeviceContext->PdEvent, 0, 0);
        }
        else if (PdMessageSize && PdMessageSize != 5 && PdMessageSize != 7)
        {
            // Trace wut
        }
        else if (Reg1Content & 0x1F || PdMessageSize == 5)
        {
            status = WdfIoQueueRetrieveNextRequest(DeviceContext->PdReadQueue, &Request);
            if (NT_SUCCESS(status))
            {
                if (!PdMessageSize || PdMessageSize == 7)
                {
                    UC120FulfillIncomingMessage(DeviceContext, Request);
                    if (PdMessageSize == 7)
                    {
                        DeviceContext->InternalState[22] = 6;
                        KeSetEvent(&DeviceContext->PdEvent, 0, 0);
                    }
                }
                if (PdMessageSize == 5) {
                    WdfRequestComplete(Request, 0x8000001D);
                }
            }
            else
            {
                // Trace wut
            }
        }
        else
        {
            // Trace wut
        }
    }
}

void UC120SynchronizeIncomingMessageSize(PDEVICE_CONTEXT DeviceContext)
{
    UC120SpiRead(&DeviceContext->SpiDevice, 1, &DeviceContext->Register1, 1u);
    DeviceContext->InternalState[22] = DeviceContext->Register1 >> 5;
    KeSetEvent(&DeviceContext->PdEvent, 0, 0);
}

void UC120FulfillIncomingMessage(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    SIZE_T messageSize; // r8
    PUCHAR pRegisterBuffer; // r6
    NTSTATUS status; // r4
    PUCHAR pRequestOutBuffer; // [sp+8h] [bp-90h]

    messageSize = DeviceContext->Register1 & 0x1F;
    pRegisterBuffer = ExAllocatePoolWithTag(512, messageSize, 'CpyT');
    if (pRegisterBuffer == NULL)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ExAllocatePoolWithTag failed");
        status = 0xC0000017;
        goto FailRequest;
    }

    status = UC120SpiRead(&DeviceContext->SpiDevice, 17, pRegisterBuffer, messageSize);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
        goto FailRequest;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, messageSize, &pRequestOutBuffer, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveOutputBuffer failed %!STATUS!", status);
    FailRequest:
        WdfRequestComplete(Request, status);
        if (!pRegisterBuffer) {
            return;
        }
        goto FreeReadBuffer;
    }
    memcpy(pRequestOutBuffer, pRegisterBuffer, messageSize);
    WdfRequestCompleteWithInformation(Request, status, messageSize);

FreeReadBuffer:
    ExFreePoolWithTag(pRegisterBuffer, 'CpyT');
}
