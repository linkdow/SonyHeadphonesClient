#pragma once
#include "Protocol.hpp"
#pragma pack(push, 1)

// Extracted from Plutoberth/SonyHeadphonesClient and reverse engineering of the
// legacy MDR V1 protocol used by WH-1000XM4, WH-1000XM3, WF-1000XM4, etc.
// Unlike V2, all V1 commands share a single DATA_MDR (0x0C) packet type with no
// Table 1 / Table 2 distinction. All payloads are fixed-size PODs — no codegen needed.
namespace mdr::v1
{
#pragma region Enums
    enum class Command : UInt8
    {
        CONNECT_GET_PROTOCOL_INFO = 0x00,
        CONNECT_RET_PROTOCOL_INFO = 0x01,

        POWER_GET_STATUS          = 0x22,
        POWER_RET_STATUS          = 0x23,
        POWER_NTFY_STATUS         = 0x25,

        NCASM_GET_PARAM           = 0x66,
        NCASM_RET_PARAM           = 0x67,
        NCASM_SET_PARAM           = 0x68,
        NCASM_NTFY_PARAM          = 0x69,

        VPT_GET_PARAM             = 0x46,
        VPT_RET_PARAM             = 0x47,
        VPT_SET_PARAM             = 0x48,
        VPT_NTFY_PARAM            = 0x49,
    };

    // Which NC/ASM mode combination is in use
    enum class NcAsmInquiredType : UInt8
    {
        NO_USE                             = 0x00,
        NOISE_CANCELLING                   = 0x01,
        NOISE_CANCELLING_AND_AMBIENT_SOUND = 0x02,
        AMBIENT_SOUND                      = 0x03,
    };

    // Whether the NC/ASM effect is active
    enum class NcAsmEffect : UInt8
    {
        OFF                    = 0x00,
        ON                     = 0x01,
        ADJUSTMENT_IN_PROGRESS = 0x10,
        ADJUSTMENT_COMPLETION  = 0x11,
    };

    enum class NcAsmSettingType : UInt8
    {
        ON_OFF           = 0x00,
        LEVEL_ADJUSTMENT = 0x01,
    };

    // Used when ASM level == 0 (DUAL) vs > 0 (SINGLE)
    enum class NcDualSingle : UInt8
    {
        DUAL   = 0x00,
        SINGLE = 0x01,
    };

    enum class AsmId : UInt8
    {
        VOICE  = 0x00,
        NORMAL = 0x01,
    };

    enum class VptInquiredType : UInt8
    {
        VPT            = 0x01,
        SOUND_POSITION = 0x02,
    };

    enum class VptPresetId : UInt8
    {
        OFF              = 0x00,
        OUTDOOR_FESTIVAL = 0x01,
        ARENA            = 0x02,
        CONCERT_HALL     = 0x03,
        CLUB             = 0x04,
    };

    enum class SoundPositionPreset : UInt8
    {
        OFF         = 0x00,
        FRONT_LEFT  = 0x01,
        FRONT_RIGHT = 0x02,
        FRONT       = 0x03,
        REAR_LEFT   = 0x11,
        REAR_RIGHT  = 0x12,
    };

    enum class PowerInquiredType : UInt8
    {
        BATTERY = 0x00,
    };
#pragma endregion

#pragma region Payloads

    struct ConnectGetProtocolInfo
    {
        Command command = Command::CONNECT_GET_PROTOCOL_INFO;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(ConnectGetProtocolInfo);
    };

    struct ConnectRetProtocolInfo
    {
        Command command;
        UInt8   protocolVersion;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(ConnectRetProtocolInfo);
    };

    struct PowerGetStatus
    {
        Command          command      = Command::POWER_GET_STATUS;
        PowerInquiredType inquiredType = PowerInquiredType::BATTERY;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(PowerGetStatus);
    };

    struct PowerRetStatusBattery
    {
        Command           command;
        PowerInquiredType inquiredType;
        UInt8             batteryLevel;   // 0..100
        UInt8             chargingStatus; // 0 = not charging, 1 = charging
        MDR_DEFINE_TRIVIAL_SERIALIZATION(PowerRetStatusBattery);
    };

    struct NcAsmGetParam
    {
        Command           command      = Command::NCASM_GET_PARAM;
        NcAsmInquiredType inquiredType = NcAsmInquiredType::NOISE_CANCELLING_AND_AMBIENT_SOUND;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(NcAsmGetParam);
    };

    // Used for RET, SET, and NTFY — all share the same 8-byte layout
    struct NcAsmParam
    {
        Command           command;
        NcAsmInquiredType inquiredType;
        NcAsmEffect       ncAsmEffect;
        NcAsmSettingType  ncAsmSettingType;
        NcDualSingle      ncDualSingle;
        NcAsmSettingType  asmSettingType;
        AsmId             asmId;
        UInt8             asmLevel; // 0..19 (MAX_STEPS_WH_1000_XM3 = 19)
        MDR_DEFINE_TRIVIAL_SERIALIZATION(NcAsmParam);
    };

    struct VptGetParam
    {
        Command         command      = Command::VPT_GET_PARAM;
        VptInquiredType inquiredType = VptInquiredType::VPT;
        MDR_DEFINE_TRIVIAL_SERIALIZATION(VptGetParam);
    };

    // Used for RET, SET, and NTFY — all share the same 3-byte layout
    struct VptParam
    {
        Command         command;
        VptInquiredType inquiredType;
        UInt8           preset; // VptPresetId or SoundPositionPreset depending on inquiredType
        MDR_DEFINE_TRIVIAL_SERIALIZATION(VptParam);
    };

#pragma endregion

} // namespace mdr::v1

#pragma pack(pop)

// MDRTraits specializations — all V1 commands use DATA_MDR (0x0C)
namespace mdr
{
    template<> struct MDRTraits<v1::ConnectGetProtocolInfo>  { static constexpr MDRDataType kDataType = MDRDataType::DATA_MDR; };
    template<> struct MDRTraits<v1::PowerGetStatus>          { static constexpr MDRDataType kDataType = MDRDataType::DATA_MDR; };
    template<> struct MDRTraits<v1::NcAsmGetParam>           { static constexpr MDRDataType kDataType = MDRDataType::DATA_MDR; };
    template<> struct MDRTraits<v1::NcAsmParam>              { static constexpr MDRDataType kDataType = MDRDataType::DATA_MDR; };
    template<> struct MDRTraits<v1::VptGetParam>             { static constexpr MDRDataType kDataType = MDRDataType::DATA_MDR; };
    template<> struct MDRTraits<v1::VptParam>                { static constexpr MDRDataType kDataType = MDRDataType::DATA_MDR; };
}
