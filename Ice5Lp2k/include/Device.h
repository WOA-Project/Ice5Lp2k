// Device.h: device context and function definitions

#include <public.h>

EXTERN_C_START

typedef struct _SPI_DEVICE_CONNECTION {
    ULONG SpiDeviceIdLow;
    ULONG SpiDeviceIdHigh;
    WDFIOTARGET SpiDeviceIoTarget;
} SPI_DEVICE_CONNECTION, * PSPI_DEVICE_CONNECTION;

typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE Device;
    SPI_DEVICE_CONNECTION SpiDevice;

    ULONG GpioDeviceIdLow;
    ULONG GpioDeviceIdHigh;

    WDFINTERRUPT Uc120Interrupt;
    WDFINTERRUPT PlugDetInterrupt;
    WDFINTERRUPT Pmic1Interrupt;
    WDFINTERRUPT Pmic2Interupt;

    WDFQUEUE DefaultIoQueue;
    WDFQUEUE DelayedIoCtlQueue;
    WDFQUEUE PdReadQueue;
    WDFQUEUE PdWriteQueue;

    WDFCOLLECTION DelayedQueryIoctlRequestCol;

    UCHAR Register0;
    UCHAR Register1;
    UCHAR Register2;
    UCHAR Register3;
    UCHAR Register4;
    UCHAR Register5;
    UCHAR Register6;
    UCHAR Register7;
    UCHAR Register8;
    UCHAR Register13;

    // Leave all internal states as is - not going to decode them
    UCHAR InternalState[26];

    KEVENT PdEvent;

    WDFWAITLOCK DeviceWaitLock;

    // When UC120 is calibrated, set to 1
    UCHAR Calibrated;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

// This is another weird thing to leave as is
typedef struct _UC120_STATE {
    unsigned int State0;
    unsigned int State1;
    unsigned int State2;
    unsigned int State3;
    unsigned int State4;
} UC120_STATE, * PUC120_STATE;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// Function to initialize the device and its callbacks
//
NTSTATUS
Ice5Lp2kCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
    );

EVT_WDF_IO_QUEUE_IO_WRITE Ice5Lp2kQueueEvtWrite;

NTSTATUS UC120SpiRead(
    _In_ PSPI_DEVICE_CONNECTION Connection,
    _In_ UCHAR RegisterAddr, _Out_ PUCHAR RegisterValue, _In_ size_t RegReadSize
);

NTSTATUS UC120SpiWrite(
    _In_ PSPI_DEVICE_CONNECTION Connection,
    _In_ UCHAR RegisterAddr, _In_ PVOID Content, _In_ size_t Size
);

EXTERN_C_END
