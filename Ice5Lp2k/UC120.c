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

NTSTATUS UC120Calibrate(PDEVICE_CONTEXT DeviceContext)
{
    NTSTATUS status = STATUS_SUCCESS;

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
    return status;
}
