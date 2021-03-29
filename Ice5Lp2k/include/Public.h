/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_Ice5Lp2k,
    0x444a1011, 0xe9c2, 0x4546, 0xae, 0x9f, 0xb8, 0xaa, 0x36, 0xad, 0xd9, 0x61);
// {444a1011-e9c2-4546-ae9fb8aa36add961}

//
// IOCTL Codes
//

#define CTL_OUT_DIRECT(Code) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, Code, METHOD_OUT_DIRECT, FILE_READ_DATA | FILE_WRITE_DATA)

#define CTL_IN_DIRECT(Code) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, Code, METHOD_IN_DIRECT, FILE_READ_DATA | FILE_WRITE_DATA)

#define CTL_BUF_DIRECT(Code) \
    CTL_CODE(FILE_DEVICE_UNKNOWN, Code, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_UC120_NOTIFICATION                  CTL_OUT_DIRECT(0x8001)
#define IOCTL_UC120_GET_CABLE_DETECTION_STATE     CTL_OUT_DIRECT(0x8002)
#define IOCTL_UC120_SET_PORT_DATA_ROLE            CTL_IN_DIRECT(0x8003)
#define IOCTL_UC120_SET_PORT_POWER_ROLE           CTL_IN_DIRECT(0x8004)
#define IOCTL_UC120_SET_PORT_VCONN_ROLE           CTL_IN_DIRECT(0x8005)
#define IOCTL_UC120_PD_MESSAGING_ENABLE           CTL_IN_DIRECT(0x8006)
#define IOCTL_UC120_SEND_HARD_RESET               CTL_BUF_DIRECT(0x8007)

//
// Structures
//

typedef enum _UC120_PORT_TYPE
{
    Uc120PortTypeDfp,
    Uc120PortTypeUfp,
    Uc120PortTypeUnknown
} UC120_PORT_TYPE, * PUC120_PORT_TYPE;

typedef enum _UC120_PORT_POWER_ROLE
{
    Uc120PortPowerRoleSink,
    Uc120PortPowerRoleSource,
    Uc120PortPowerRoleUnknown
} UC120_PORT_POWER_ROLE, * PUC120_PORT_POWER_ROLE;

typedef enum _UC120_PORT_VCONN_ROLE
{
    Uc120PortPowerVconnSink,
    Uc120PortPowerVconnSource,
    Uc120PortPowerVconnUnknown
} UC120_PORT_VCONN_ROLE, * PUC120_PORT_VCONN_ROLE;

typedef enum _UC120_PORT_PARTNER_TYPE
{
    Uc120PortPartnerTypeDfp,
    Uc120PortPartnerTypeUfp,
    Uc120PortPartnerTypePoweredCableNoUfp,
    Uc120PortPartnerTypePoweredCableUfp,
    Uc120PortPartnerTypeDebugAccessory,
    Uc120PortPartnerTypeAudioAccessory,
    Uc120PortPartnerTypePoweredAccessory,
    Uc120PortPartnerTypeUnknown
} UC120_PORT_PARTNER_TYPE, * PUC120_PORT_PARTNER_TYPE;

typedef enum _UC120_ADVERTISED_CURRENT_LEVEL
{
    Uc120AdvertisedCurrentLevelDefaultUsb,
    Uc120AdvertisedCurrentLevelPower15,
    Uc120AdvertisedCurrentLevelPower30,
    Uc120AdvertisedCurrentLevelNotAvailable,
    Uc120AdvertisedCurrentLevelUnknown
} UC120_ADVERTISED_CURRENT_LEVEL, * PUC120_ADVERTISED_CURRENT_LEVEL;

typedef enum _UC120_EVENT
{
    Uc120EventAttach,
    Uc120EventDetach,
    Uc120EventCurrentLevelChange,
    Uc120EventUnknown
} UC120_EVENT, * PUC120_EVENT;

typedef enum _UC120_PD_MESSAGING
{
    Uc120PdMessagingDisabled,
    Uc120PdMessagingEnabled,
    Uc120PdMessagingUnknown
} UC120_PD_MESSAGING, * PUC120_PD_MESSAGING;

typedef struct _UC120_ATTACH_INFORMATION
{
    UC120_PORT_TYPE PortType;
    UC120_PORT_PARTNER_TYPE PortPartnerType;
    UC120_ADVERTISED_CURRENT_LEVEL CurrentLevelInitial;
    unsigned short Orientation;
} UC120_ATTACH_INFORMATION, * PUC120_ATTACH_INFORMATION;

typedef struct _UC120_NOTIFICATION
{
    UC120_EVENT Event;
    union
    {
        UC120_ATTACH_INFORMATION AttachInformation;
        UC120_ADVERTISED_CURRENT_LEVEL CurrentLevelChanged;
    } data;
} UC120_NOTIFICATION, * PUC120_NOTIFICATION;