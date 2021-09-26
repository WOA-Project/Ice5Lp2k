// UC120.c: UC120 internal misc handlers

#include <driver.h>
#include "UC120.tmh"

NTSTATUS UC120ReportState(
	PDEVICE_CONTEXT pDeviceContext,
	UC120_EVENT MessageType,
	UC120_PORT_TYPE Power,
	UC120_PORT_PARTNER_TYPE PartnerType,
	UC120_ADVERTISED_CURRENT_LEVEL UsbCurrentType,
	unsigned short Polarity)
{
	NTSTATUS status = STATUS_SUCCESS;
	ULONG count;
	WDFREQUEST PendingRequest;
	PUC120_NOTIFICATION pAttachInformation;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	if (pDeviceContext->Calibrated)
	{
		status = WdfIoQueueRetrieveNextRequest(pDeviceContext->DelayedIoCtlQueue, &PendingRequest);
		if (NT_SUCCESS(status))
		{
			do
			{
				status = WdfCollectionAdd(pDeviceContext->DelayedQueryIoctlRequestCol, PendingRequest);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfCollectionAdd failed %!STATUS!", status);
				}
			} while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(pDeviceContext->DelayedIoCtlQueue, &PendingRequest)));
		}

		count = WdfCollectionGetCount(pDeviceContext->DelayedQueryIoctlRequestCol);
		if (count > 0)
		{
			do
			{
				PendingRequest = NULL;
				PendingRequest = WdfCollectionGetFirstItem(pDeviceContext->DelayedQueryIoctlRequestCol);
				status = WdfRequestRetrieveOutputBuffer(PendingRequest, sizeof(UC120_NOTIFICATION), &pAttachInformation, 0);
				if (NT_SUCCESS(status))
				{
					RtlZeroMemory(pAttachInformation, sizeof(UC120_NOTIFICATION));

					pAttachInformation->Event = MessageType;

					switch (MessageType)
					{
					case Uc120EventCurrentLevelChange:
						pAttachInformation->data.CurrentLevelChanged = UsbCurrentType;
						break;
					case Uc120EventDetach:
						break;
					case Uc120EventAttach:
						pAttachInformation->data.AttachInformation.PortType = Power;
						pAttachInformation->data.AttachInformation.PortPartnerType = PartnerType;
						pAttachInformation->data.AttachInformation.CurrentLevelInitial = UsbCurrentType;
						pAttachInformation->data.AttachInformation.Orientation = Polarity;
						break;
					}

					WdfRequestCompleteWithInformation(PendingRequest, status, sizeof(UC120_NOTIFICATION));
				}
				else
				{
					WdfRequestComplete(PendingRequest, status);
				}

				WdfCollectionRemove(pDeviceContext->DelayedQueryIoctlRequestCol, PendingRequest);
				--count;
			} while (count);
		}
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return status;
}

NTSTATUS UC120ToggleReg4YetUnknown(PDEVICE_CONTEXT DeviceContext, UCHAR Bit)
{
	NTSTATUS status;

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
	NTSTATUS status;

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
	NTSTATUS status;

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
	NTSTATUS status;
	UCHAR Reg1Content;
	UCHAR PdMessageSize;
	WDFREQUEST Request;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	status = UC120SpiRead(&DeviceContext->SpiDevice, 1, &DeviceContext->Register1, 1u);
	if (NT_SUCCESS(status))
	{
		Reg1Content = DeviceContext->Register1;
		PdMessageSize = Reg1Content >> 5;
		if (Reg1Content >> 5 == 6)
		{
			DeviceContext->PDMessageType = 6;
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
						DeviceContext->PDMessageType = 6;
						KeSetEvent(&DeviceContext->PdEvent, 0, 0);
					}
				}
				if (PdMessageSize == 5)
				{
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
	DeviceContext->PDMessageType = DeviceContext->Register1 >> 5;
	KeSetEvent(&DeviceContext->PdEvent, 0, 0);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120FulfillIncomingMessage(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	SIZE_T messageSize;
	PUCHAR pRegisterBuffer;
	NTSTATUS status;
	PUCHAR pRequestOutBuffer;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	messageSize = DeviceContext->Register1 & 0x1F;
	pRegisterBuffer = ExAllocatePoolWithTag(512, messageSize, 'CpyT');
	if (pRegisterBuffer == NULL)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "ExAllocatePoolWithTag failed");
		status = STATUS_NO_MEMORY;
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
		if (!pRegisterBuffer)
		{
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

	UNICODE_STRING CalibrationFileString;
	OBJECT_ATTRIBUTES CalibrationFileObjectAttribute;
	HANDLE hCalibrationFile;
	IO_STATUS_BLOCK CalibrationIoStatusBlock;
	UCHAR CalibrationBlob[UC120_CALIBRATIONFILE_SIZE + 2];
	LARGE_INTEGER CalibrationFileByteOffset;
	FILE_STANDARD_INFORMATION CalibrationFileInfo;
	LONGLONG CalibrationFileSize = 0;
	UCHAR SkipCalibration = FALSE;

	// Read calibration file
	RtlInitUnicodeString(
		&CalibrationFileString, L"\\DosDevices\\C:\\DPP\\MMO\\ice5lp_2k_cal.bin");
	InitializeObjectAttributes(
		&CalibrationFileObjectAttribute, &CalibrationFileString,
		OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	if (KeGetCurrentIrql() != PASSIVE_LEVEL)
	{
		status = STATUS_INVALID_DEVICE_STATE;
		goto Exit;
	}

	status = ZwCreateFile(
		&hCalibrationFile, GENERIC_READ, &CalibrationFileObjectAttribute,
		&CalibrationIoStatusBlock, NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
		FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(
			TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
			"failed to open calibration file 0x%x, skipping calibration. Is this a "
			"FUSBC device?",
			status);
		status = STATUS_SUCCESS;
		SkipCalibration = TRUE;
	}

	if (!SkipCalibration)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "stat calibration file");
		status = ZwQueryInformationFile(
			hCalibrationFile, &CalibrationIoStatusBlock, &CalibrationFileInfo,
			sizeof(CalibrationFileInfo), FileStandardInformation);

		if (!NT_SUCCESS(status))
		{
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
		if (!NT_SUCCESS(status))
		{
			TraceEvents(
				TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
				"failed to read calibration file 0x%x", status);
			goto Exit;
		}

		// Now we have the calibration blob, initialize it accordingly
		if (CalibrationFileSize == 8)
		{
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 18, CalibrationBlob + 0, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 19, CalibrationBlob + 1, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 20, CalibrationBlob + 2, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 21, CalibrationBlob + 3, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 22, CalibrationBlob + 4, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 23, CalibrationBlob + 5, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 24, CalibrationBlob + 6, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
			status = UC120SpiWrite(&DeviceContext->SpiDevice, 25, CalibrationBlob + 7, 1);
			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			}
		}
		else if (CalibrationFileSize > 8)
		{
			// 0x02 version only
			if (CalibrationBlob[0] == 0x02)
			{
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 18, CalibrationBlob + 1 + 0, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 19, CalibrationBlob + 1 + 1, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 20, CalibrationBlob + 1 + 2, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 21, CalibrationBlob + 1 + 3, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 26, CalibrationBlob + 1 + 4, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 22, CalibrationBlob + 1 + 5, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 23, CalibrationBlob + 1 + 6, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 24, CalibrationBlob + 1 + 7, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 25, CalibrationBlob + 1 + 8, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
				status = UC120SpiWrite(&DeviceContext->SpiDevice, 27, CalibrationBlob + 1 + 9, 1);
				if (!NT_SUCCESS(status))
				{
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
				}
			}
		}
		else
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "Skipped calibration due to unrecognized file");
		}
	}

Exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return status;
}

void UC120PDMessagingEnable(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	UCHAR GoodCrcEn;
	NTSTATUS status;
	PUC120_PD_MESSAGING pPdMessaging;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UC120_PD_MESSAGING), &pPdMessaging, 0);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
		goto error;
	}

	switch (*pPdMessaging)
	{
	case Uc120PdMessagingDisabled:
		GoodCrcEn = 0;
		break;
	case Uc120PdMessagingEnabled:
		GoodCrcEn = 1;
		break;
	default:
		status = STATUS_INVALID_PARAMETER;
		goto error;
	}

	DeviceContext->Register4 = DeviceContext->Register4 & 0x7F | (GoodCrcEn << 7);
	status = UC120SpiWrite(&DeviceContext->SpiDevice, 4, &DeviceContext->Register4, 1);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
	}

	WdfRequestCompleteWithInformation(Request, status, sizeof(UC120_PD_MESSAGING));
	goto exit;

error:
	WdfRequestComplete(Request, status);

exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	return;
}

void UC120IoctlExecuteHardReset(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	NTSTATUS status;

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

void UC120GetCableDetectionState(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	NTSTATUS status;
	PUC120_ATTACH_INFORMATION pAttachInformation;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UC120_ATTACH_INFORMATION), &pAttachInformation, 0);
	if (NT_SUCCESS(status))
	{
		pAttachInformation->PortType = DeviceContext->Uc120PortType;
		pAttachInformation->PortPartnerType = DeviceContext->PortPartnerType;
		pAttachInformation->CurrentLevelInitial = DeviceContext->AdvertisedCurrentLevel;
		pAttachInformation->Orientation = DeviceContext->Orientation;

		WdfRequestCompleteWithInformation(Request, status, sizeof(UC120_ATTACH_INFORMATION));
	}
	else
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveOutputBuffer failed %!STATUS!", status);
		WdfRequestComplete(Request, status);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120SetPortDataRole(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	UCHAR Role;
	NTSTATUS status;
	PUC120_PORT_TYPE buffer;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UC120_PORT_TYPE), &buffer, 0);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
		goto LABEL_11;
	}

	if (*buffer)
	{
		if (*buffer != Uc120PortTypeUfp)
		{
			status = STATUS_INVALID_PARAMETER;
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

	WdfRequestCompleteWithInformation(Request, status, sizeof(UC120_PORT_TYPE));
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

void UC120IoctlReportNewPowerRole(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	NTSTATUS status;
	UCHAR incomingRequest;
	PUC120_PORT_POWER_ROLE buffer;

	UCHAR v12;
	UCHAR v13;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	if (DeviceContext->Uc120Event)
	{
		status = STATUS_INVALID_DEVICE_STATE;
		goto exit;
	}

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UC120_PORT_POWER_ROLE), &buffer, 0);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "WdfRequestRetrieveInputBuffer failed %!STATUS!", status);
		goto exit;
	}

	status = UC120SpiRead(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1u);
	if (status < 0)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiRead failed %!STATUS!", status);
		goto exit;
	}

	incomingRequest = *buffer;
	if (!*buffer && !(DeviceContext->Register5 & 1))
	{
		goto exit;
	}

	if (incomingRequest == 1)
	{
		if (DeviceContext->Register5 & 1)
		{
			goto exit;
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
				goto exit;
			}
		}
		else
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "UC120SpiWrite failed %!STATUS!", status);
			goto exit;
		}
		goto exit;
	}

	if (!incomingRequest)
	{
		v12 = 0;
		v13 = 1;
		goto LABEL_38;
	}

	status = STATUS_INVALID_PARAMETER;
exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	WdfRequestComplete(Request, status);
}

void UC120IoctlSetVConnRoleSwitch(PDEVICE_CONTEXT DeviceContext, WDFREQUEST Request)
{
	NTSTATUS status;
	UC120_PORT_VCONN_ROLE incomingRequest;
	PUC120_PORT_VCONN_ROLE buffer;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	if (DeviceContext->Uc120Event)
	{
		status = STATUS_INVALID_DEVICE_STATE;
		goto exit;
	}

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UC120_PORT_VCONN_ROLE), &buffer, 0);
	if (!NT_SUCCESS(status))
	{
		goto exit;
	}

	if (*buffer > Uc120PortPowerVconnSource)
	{
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	status = UC120SpiRead(&DeviceContext->SpiDevice, 5, &DeviceContext->Register5, 1u);
	if (!NT_SUCCESS(status))
	{
		goto exit;
	}

	incomingRequest = *buffer;
	if (*buffer == Uc120PortPowerVconnSink && !(DeviceContext->Register5 & 0x20))
	{
		goto exit;
	}

	if (incomingRequest != Uc120PortPowerVconnSource || !(DeviceContext->Register5 & 0x20))
	{
		status = SetVConn(DeviceContext, incomingRequest != Uc120PortPowerVconnSink);
		goto exit;
	}

exit:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
	WdfRequestComplete(Request, status);
}
