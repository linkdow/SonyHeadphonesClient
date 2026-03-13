// ReSharper disable CppParameterMayBeConstPtrOrRef
#include <mdr/Headphones.hpp>

// Validation stubs for V1 POD types — all payloads are unconditionally valid since
// field ranges are enforced by the enum types themselves.
namespace mdr::v1
{
    bool ConnectGetProtocolInfo::Validate(const ConnectGetProtocolInfo&) { return true; }
    bool PowerGetStatus::Validate(const PowerGetStatus&)                 { return true; }
    bool NcAsmGetParam::Validate(const NcAsmGetParam&)                   { return true; }
    bool NcAsmParam::Validate(const NcAsmParam&)                         { return true; }
    bool VptGetParam::Validate(const VptGetParam&)                       { return true; }
    bool VptParam::Validate(const VptParam&)                             { return true; }
    // Deserialization stubs (needed by MDR_DEFINE_TRIVIAL_SERIALIZATION but not called directly)
    bool ConnectRetProtocolInfo::Validate(const ConnectRetProtocolInfo&) { return true; }
    bool PowerRetStatusBattery::Validate(const PowerRetStatusBattery&)   { return true; }
}

// NOLINTBEGIN
namespace mdr
{
    using namespace v1;

    // -------------------------------------------------------------------------
    // Command dispatch
    // -------------------------------------------------------------------------

    int MDRHeadphones::HandleCommandV1(Span<const UInt8> cmd, MDRCommandSeqNumber /*seq*/)
    {
        if (cmd.empty())
            return MDR_HEADPHONES_EVT_UNHANDLED;

        switch (static_cast<Command>(cmd[0]))
        {
        case Command::CONNECT_RET_PROTOCOL_INFO:
            Awake(AWAIT_PROTOCOL_INFO);
            return MDR_HEADPHONES_EVT_OK;

        case Command::POWER_RET_STATUS:
        case Command::POWER_NTFY_STATUS:
        {
            if (cmd.size() < sizeof(PowerRetStatusBattery))
                return MDR_HEADPHONES_EVT_UNHANDLED;
            PowerRetStatusBattery res;
            PowerRetStatusBattery::Deserialize(cmd.data(), res, cmd.size());
            if (res.inquiredType == PowerInquiredType::BATTERY)
            {
                mV1Battery.level    = res.batteryLevel;
                mV1Battery.charging = res.chargingStatus != 0;
                Awake(AWAIT_V1_BATTERY);
                return MDR_HEADPHONES_EVT_BATTERY;
            }
            return MDR_HEADPHONES_EVT_UNHANDLED;
        }

        case Command::NCASM_RET_PARAM:
        case Command::NCASM_NTFY_PARAM:
        {
            if (cmd.size() < sizeof(NcAsmParam))
                return MDR_HEADPHONES_EVT_UNHANDLED;
            NcAsmParam res;
            NcAsmParam::Deserialize(cmd.data(), res, cmd.size());

            mV1NcAsmMode.overwrite(res.inquiredType);
            mNcAsmEnabled.overwrite(res.ncAsmEffect == NcAsmEffect::ON ||
                                    res.ncAsmEffect == NcAsmEffect::ADJUSTMENT_COMPLETION);
            mNcAsmFocusOnVoice.overwrite(res.asmId == AsmId::VOICE);
            mNcAsmAmbientLevel.overwrite(static_cast<int>(res.asmLevel));

            Awake(AWAIT_V1_NCASM);
            return MDR_HEADPHONES_EVT_NCASM_PARAM;
        }

        case Command::VPT_RET_PARAM:
        case Command::VPT_NTFY_PARAM:
        {
            if (cmd.size() < sizeof(VptParam))
                return MDR_HEADPHONES_EVT_UNHANDLED;
            VptParam res;
            VptParam::Deserialize(cmd.data(), res, cmd.size());
            // overwrite() sets both desired and current — no explicit commit() needed.
            // Calling commit() on the *other* property would silently discard any
            // pending user change to it before it was sent to the device.
            if (res.inquiredType == VptInquiredType::VPT)
                mV1VptPreset.overwrite(static_cast<VptPresetId>(res.preset));
            else
                mV1SoundPosition.overwrite(static_cast<SoundPositionPreset>(res.preset));
            return MDR_HEADPHONES_EVT_OK;
        }

        default:
            return MDR_HEADPHONES_EVT_UNHANDLED;
        }
    }

    // -------------------------------------------------------------------------
    // Init — protocol handshake + initial state query
    // -------------------------------------------------------------------------

    MDRTask MDRHeadphones::RequestInitV1()
    {
        // Protocol handshake (V1 devices respond with a simple version byte)
        SendCommandACK(ConnectGetProtocolInfo, ConnectGetProtocolInfo{});
        co_await Await(AWAIT_PROTOCOL_INFO);

        // NC/ASM initial state
        SendCommandACK(NcAsmGetParam, NcAsmGetParam{});
        co_await Await(AWAIT_V1_NCASM);

        // VPT initial state
        VptGetParam vptReq;
        vptReq.inquiredType = VptInquiredType::VPT;
        SendCommandACK(VptGetParam, vptReq);
        // VPT response is fire-and-forget (NTFY-style) — no dedicated awaiter needed

        co_return MDR_HEADPHONES_TASK_INIT_OK;
    }

    // -------------------------------------------------------------------------
    // Sync — poll values that the device won't push automatically
    // -------------------------------------------------------------------------

    MDRTask MDRHeadphones::RequestSyncV1()
    {
        SendCommandACK(PowerGetStatus, PowerGetStatus{});
        co_await Await(AWAIT_V1_BATTERY);

        co_return MDR_HEADPHONES_TASK_SYNC_OK;
    }

    // -------------------------------------------------------------------------
    // Commit — push all dirty V1 properties to the device
    // -------------------------------------------------------------------------

    MDRTask MDRHeadphones::RequestCommitV1()
    {
        // NC/ASM — rebuild the 8-byte V1 payload from the shared properties
        if (mV1NcAsmMode.dirty() || mNcAsmEnabled.dirty() ||
            mNcAsmAmbientLevel.dirty() || mNcAsmFocusOnVoice.dirty())
        {
            NcAsmParam res{};
            res.command = Command::NCASM_SET_PARAM;

            // Determine the combined mode from enabled + stored V1 mode
            if (!mNcAsmEnabled.desired)
                res.inquiredType = NcAsmInquiredType::NO_USE;
            else
                res.inquiredType = mV1NcAsmMode.desired;

            res.ncAsmEffect = mNcAsmEnabled.desired ? NcAsmEffect::ON : NcAsmEffect::OFF;

            const bool hasAsm = (res.inquiredType == NcAsmInquiredType::AMBIENT_SOUND ||
                                  res.inquiredType == NcAsmInquiredType::NOISE_CANCELLING_AND_AMBIENT_SOUND);

            res.ncAsmSettingType = hasAsm ? NcAsmSettingType::LEVEL_ADJUSTMENT : NcAsmSettingType::ON_OFF;
            res.asmLevel         = static_cast<UInt8>(mNcAsmAmbientLevel.desired);
            res.ncDualSingle     = res.asmLevel == 0 ? NcDualSingle::DUAL : NcDualSingle::SINGLE;
            res.asmSettingType   = NcAsmSettingType::LEVEL_ADJUSTMENT;
            res.asmId            = mNcAsmFocusOnVoice.desired ? AsmId::VOICE : AsmId::NORMAL;

            SendCommandACK(NcAsmParam, res);
            mV1NcAsmMode.commit();
            mNcAsmEnabled.commit();
            mNcAsmAmbientLevel.commit();
            mNcAsmFocusOnVoice.commit();
        }

        // VPT
        if (mV1VptPreset.dirty())
        {
            VptParam res{};
            res.command      = Command::VPT_SET_PARAM;
            res.inquiredType = VptInquiredType::VPT;
            res.preset       = static_cast<UInt8>(mV1VptPreset.desired);
            SendCommandACK(VptParam, res);
            mV1VptPreset.commit();
        }

        // Sound Position
        if (mV1SoundPosition.dirty())
        {
            VptParam res{};
            res.command      = Command::VPT_SET_PARAM;
            res.inquiredType = VptInquiredType::SOUND_POSITION;
            res.preset       = static_cast<UInt8>(mV1SoundPosition.desired);
            SendCommandACK(VptParam, res);
            mV1SoundPosition.commit();
        }

        co_return MDR_HEADPHONES_TASK_COMMIT_OK;
    }

} // namespace mdr
// NOLINTEND
