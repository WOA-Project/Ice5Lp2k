// UC120.c: UC120 internal misc handlers

#include <driver.h>
#include "UC120.tmh"

NTSTATUS UC120ReportState(PDEVICE_CONTEXT DeviceContext, int MessageType, int Power, int PartnerType, int UsbCurrentType, int Polarity)
{
    NTSTATUS status = STATUS_SUCCESS; // r6
    ULONG count; // r8
    WDFREQUEST PendingRequest; // [sp+18h] [bp-50h]
    PUC120_STATE Buffer; // [sp+1Ch] [bp-4Ch]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

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

                    // Param1 is message type
                    // 0: PD msg
                    // 1: detach
                    // 2: current change
                    Buffer->State0 = MessageType;

                    if (MessageType)
                    {
                        if (MessageType == 2)
                        {
                            Buffer->State1 = UsbCurrentType;
                        }
                    }
                    else
                    {
                        Buffer->State1 = Power;
                        Buffer->State2 = PartnerType;
                        Buffer->State3 = UsbCurrentType;
                        Buffer->State4 = Polarity;
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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

NTSTATUS UC120ToggleReg4YetUnknown(PDEVICE_CONTEXT DeviceContext, UCHAR Bit)
{
    NTSTATUS status; // r4

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    DeviceContext->Register4 ^= (DeviceContext->Register4 ^ 2 * (Bit != 0)) & 2;
    status = UC120SpiWrite(&DeviceContext->SpiDevice, 4, &DeviceContext->Register4, 1);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

NTSTATUS SetVConn(PDEVICE_CONTEXT DeviceContext, UCHAR Enable)
{
    NTSTATUS status; // r4

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

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
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

NTSTATUS SetPowerRole(PDEVICE_CONTEXT DeviceContext, UCHAR PowerRole)
{
    NTSTATUS status; // r4

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

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
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

void UC120ProcessIncomingPdMessage(PDEVICE_CONTEXT DeviceContext)
{
    NTSTATUS status; // r0
    UCHAR Reg1Content; // r3
    UCHAR PdMessageSize; // r5
    WDFREQUEST Request; // [sp+8h] [bp-20h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

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

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120SynchronizeIncomingMessageSize(PDEVICE_CONTEXT DeviceContext)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");
    UC120SpiRead(&DeviceContext->SpiDevice, 1, &DeviceContext->Register1, 1u);
    DeviceContext->InternalState[22] = DeviceContext->Register1 >> 5;
    KeSetEvent(&DeviceContext->PdEvent, 0, 0);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120FulfillIncomingMessage(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    SIZE_T messageSize; // r8
    PUCHAR pRegisterBuffer; // r6
    NTSTATUS status; // r4
    PUCHAR pRequestOutBuffer; // [sp+8h] [bp-90h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

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
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
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

NTSTATUS UC120Calibrate(PDEVICE_CONTEXT DeviceContext)
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    UNICODE_STRING            CalibrationFileString;
    OBJECT_ATTRIBUTES         CalibrationFileObjectAttribute;
    HANDLE                    hCalibrationFile;
    IO_STATUS_BLOCK           CalibrationIoStatusBlock;
    UCHAR                     CalibrationBlob[UC120_CALIBRATIONFILE_SIZE + 2];
    LARGE_INTEGER             CalibrationFileByteOffset;
    FILE_STANDARD_INFORMATION CalibrationFileInfo;
    LONGLONG CalibrationFileSize = 0;
    UCHAR SkipCalibration = FALSE;

    // Read calibration file
    RtlInitUnicodeString(
        &CalibrationFileString, L"\\DosDevices\\C:\\DPP\\MMO\\ice5lp_2k_cal.bin");
    InitializeObjectAttributes(
        &CalibrationFileObjectAttribute, &CalibrationFileString,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    status = ZwCreateFile(
        &hCalibrationFile, GENERIC_READ, &CalibrationFileObjectAttribute,
        &CalibrationIoStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
        FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

    if (!NT_SUCCESS(status)) {
        TraceEvents(
            TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
            "failed to open calibration file 0x%x, skipping calibration. Is this a "
            "FUSBC device?",
            status);
        status = STATUS_SUCCESS;
        SkipCalibration = TRUE;
    }

    if (!SkipCalibration) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "stat calibration file");
        status = ZwQueryInformationFile(
            hCalibrationFile, &CalibrationIoStatusBlock, &CalibrationFileInfo,
            sizeof(CalibrationFileInfo), FileStandardInformation);

        if (!NT_SUCCESS(status)) {
            TraceEvents(
                TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "failed to stat calibration file 0x%x", status);
            ZwClose(hCalibrationFile);
            goto Exit;
        }

        CalibrationFileSize = CalibrationFileInfo.EndOfFile.QuadPart;
        TraceEvents(
            TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "calibration file size %lld",
            CalibrationFileSize);

        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "read calibration file");
        RtlZeroMemory(CalibrationBlob, sizeof(CalibrationBlob));
        CalibrationFileByteOffset.LowPart = 0;
        CalibrationFileByteOffset.HighPart = 0;
        status = ZwReadFile(
            hCalibrationFile, NULL, NULL, NULL, &CalibrationIoStatusBlock,
            CalibrationBlob, UC120_CALIBRATIONFILE_SIZE, &CalibrationFileByteOffset,
            NULL);

        ZwClose(hCalibrationFile);
        if (!NT_SUCCESS(status)) {
            TraceEvents(
                TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "failed to read calibration file 0x%x", status);
            goto Exit;
        }

        // Now we have the calibration blob, initialize it accordingly
        if (CalibrationFileSize == 8) {
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 18, CalibrationBlob + 0, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 19, CalibrationBlob + 1, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 20, CalibrationBlob + 2, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 21, CalibrationBlob + 3, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 22, CalibrationBlob + 4, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 23, CalibrationBlob + 5, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 24, CalibrationBlob + 6, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 25, CalibrationBlob + 7, 1);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            }
        }
        else if (CalibrationFileSize > 8) {
            // 0x02 version only
            if (CalibrationBlob[0] == 0x02) {
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 18, CalibrationBlob + 1 + 0, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 19, CalibrationBlob + 1 + 1, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 20, CalibrationBlob + 1 + 2, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 21, CalibrationBlob + 1 + 3, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 26, CalibrationBlob + 1 + 4, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 22, CalibrationBlob + 1 + 5, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 23, CalibrationBlob + 1 + 6, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 24, CalibrationBlob + 1 + 7, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 25, CalibrationBlob + 1 + 8, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
                status = UC120SpiWrite(&DeviceContext->SpiDevice, 27, CalibrationBlob + 1 + 9, 1);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
                }
            }
        } else {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Skipped calibration due to unrecognized file");
        }
    }

Exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    return status;
}

void UC120IoctlEnableGoodCRC(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    UCHAR GoodCrcEn; // r2
    NTSTATUS status; // r4
    PUCHAR buffer; // [sp+8h] [bp-20h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = WdfRequestRetrieveInputBuffer(Request, 4u, &buffer, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
        goto LABEL_11;
    }

    if (*buffer)
    {
        if (*buffer != 1)
        {
            status = 0xC000000D;
        LABEL_11:
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
            WdfRequestComplete(Request, status);
            return;
        }
        GoodCrcEn = 1;
    }
    else
    {
        GoodCrcEn = 0;
    }

    DeviceContext->Register4 = DeviceContext->Register4 & 0x7F | (GoodCrcEn << 7);
    status = UC120SpiWrite(&DeviceContext->SpiDevice, 4, &DeviceContext->Register4, 1);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
    }

    WdfRequestCompleteWithInformation(Request, status, 4u);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120IoctlExecuteHardReset(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    NTSTATUS status; // r5

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    DeviceContext->Register0 = 32;
    status = UC120SpiWrite(&DeviceContext->SpiDevice, 0, &DeviceContext->Register0, 1);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
    }

    WdfRequestComplete(Request, status);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120IoctlIsCableConnected(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    NTSTATUS status; // r6
    unsigned int* buffer; // [sp+8h] [bp-38h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = WdfRequestRetrieveOutputBuffer(Request, 0x10u, &buffer, 0);
    if (NT_SUCCESS(status))
    {
        buffer[0] = DeviceContext->InternalState[6];
        buffer[1] = DeviceContext->InternalState[10];
        buffer[2] = DeviceContext->InternalState[14];
        buffer[3] = DeviceContext->InternalState[18];
        WdfRequestCompleteWithInformation(Request, status, 0x10u);
    }
    else
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveOutputBuffer failed %!STATUS!", status);
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120IoctlReportNewDataRole(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    UCHAR Role; // r2
    NTSTATUS status; // r4
    PUCHAR buffer; // [sp+8h] [bp-20h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = WdfRequestRetrieveInputBuffer(Request, 4u, &buffer, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
        goto LABEL_11;
    }

    if (*buffer)
    {
        if (*buffer != 1)
        {
            status = 0xC000000D;
        LABEL_11:
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
            WdfRequestComplete(Request, status);
            return;
        }
        Role = 0;
    }
    else
    {
        Role = 1;
    }

    DeviceContext->Register5 ^= (DeviceContext->Register5 ^ 4 * Role) & 4;
    status = UC120SpiWrite(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
    }

    WdfRequestCompleteWithInformation(Request, status, 4u);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120IoctlReportNewPowerRole(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    NTSTATUS status; // r4
    UCHAR incomingRequest; // r1
    PUCHAR buffer; // [sp+8h] [bp-20h]

    UCHAR v12;
    UCHAR v13;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    if (DeviceContext->InternalState[2])
    {
        status = 0xC0000184;
        goto LABEL_53;
    }

    status = WdfRequestRetrieveInputBuffer(Request, 4u, &buffer, 0);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
        goto LABEL_53;
    }

    status = UC120SpiRead(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1u);
    if (status < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
        goto LABEL_53;
    }

    incomingRequest = *buffer;
    if (!*buffer && !(DeviceContext->Register5 & 1))
    {
        goto LABEL_53;
    }

    if (incomingRequest == 1)
    {
        if (DeviceContext->Register5 & 1)
        {
            goto LABEL_53;
        }
        v12 = 1;
        v13 = 0;
    LABEL_38:
        DeviceContext->Register5 ^= (DeviceContext->Register5 ^ v12) & 1;
        status = UC120SpiWrite(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1);
        if (NT_SUCCESS(status))
        {
            DeviceContext->Register5 ^= (DeviceContext->Register5 ^ (v13 << 6)) & 0x40;
            status = UC120SpiWrite(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1);
            if (NT_SUCCESS(status))
            {
                goto LABEL_53;
            }
        }
        else
        {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
            goto LABEL_53;
        }
        goto LABEL_53;
    }

    if (!incomingRequest)
    {
        v12 = 0;
        v13 = 1;
        goto LABEL_38;
    }

    status = 0xC000000D;
LABEL_53:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    WdfRequestComplete(Request, status);
}

void UC120IoctlSetVConnRoleSwitch(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
    NTSTATUS status; // r4
    UCHAR incomingRequest; // r2
    PUCHAR buffer; // [sp+8h] [bp-20h]

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    if (DeviceContext->InternalState[2])
    {
        status = 0xC0000184;
        goto LABEL_39;
    }

    status = WdfRequestRetrieveInputBuffer(Request, 4u, &buffer, 0);
    if (!NT_SUCCESS(status))
    {
        goto LABEL_39;
    }

    if (*buffer && *buffer != 1)
    {
        status = 0xC000000D;
        goto LABEL_39;
    }

    status = UC120SpiRead(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1u);
    if (!NT_SUCCESS(status))
    {
        goto LABEL_39;
    }

    incomingRequest = *buffer;
    if (!*buffer && !(DeviceContext->Register5 & 0x20))
    {
        goto LABEL_39;
    }

    if (incomingRequest != 1 || !(DeviceContext->Register5 & 0x20))
    {
        status = SetVConn(DeviceContext, incomingRequest != 0);
        goto LABEL_39;
    }

LABEL_39:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
    WdfRequestComplete(Request, status);
}
