// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

// The SCS telemetry plugin for Euro Truck Simulator 2 and American Truck Simulator: collects a few channels and
// sends them to istar-wheelstar over UDP on the loopback interface. The game loads it from bin/<platform>/plugins/.
//
// Steam runs the Windows build of the game through Proton, so this is a Windows DLL, built with mingw-w64. A
// Winsock datagram to 127.0.0.1 leaves the Wine prefix and arrives at the native application.
//
// The SDK calls every function here from the game's main thread, so nothing is locked.

#include "telemetry_packet.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "scssdk_telemetry.h"

#include <algorithm>
#include <cstring>

namespace
{
    using Wheelstar::Telemetry::Event;
    using Wheelstar::Telemetry::EventKind;
    using Wheelstar::Telemetry::EventText;
    using Wheelstar::Telemetry::Game;
    using Wheelstar::Telemetry::Packet;

    Packet s_packet;
    /// The job as the last configuration event described it, sent again as JobTaken when it changes.
    Event s_job;
    SOCKET s_socket = INVALID_SOCKET;
    sockaddr_in s_target{};

    template <typename T>
    void send_datagram(const T& datagram)
    {
        if (s_socket != INVALID_SOCKET)
        {
            // A datagram nobody listens for is simply lost; there is nothing to do about the error.
            sendto(s_socket, reinterpret_cast<const char*>(&datagram), sizeof(datagram), 0,
                   reinterpret_cast<const sockaddr*>(&s_target), sizeof(s_target));
        }
    }

    void send_packet()
    {
        send_datagram(s_packet);
    }

    /// The attribute of this name in a list the SDK ends with a null name, or nothing.
    const scs_value_t* attribute(const scs_named_value_t* attributes, const char* name)
    {
        for (const scs_named_value_t* current = attributes; current->name != nullptr; ++current)
        {
            if (std::strcmp(current->name, name) == 0)
            {
                return &current->value;
            }
        }

        return nullptr;
    }

    void copy_text(EventText& target, const scs_value_t* value)
    {
        target.fill(0);

        if (value != nullptr && value->type == SCS_VALUE_TYPE_string && value->value_string.value != nullptr)
        {
            // One byte less than the field, so the text stays terminated.
            const std::size_t length = std::min(std::strlen(value->value_string.value), target.size() - 1);
            std::memcpy(target.data(), value->value_string.value, length);
        }
    }

    /// Any of the SDK's number types as a 64-bit integer; 0 for a missing attribute.
    std::int64_t integer(const scs_value_t* value)
    {
        if (value == nullptr)
        {
            return 0;
        }

        switch (value->type)
        {
            case SCS_VALUE_TYPE_s32:
                return value->value_s32.value;
            case SCS_VALUE_TYPE_u32:
                return value->value_u32.value;
            case SCS_VALUE_TYPE_s64:
                return value->value_s64.value;
            case SCS_VALUE_TYPE_u64:
                return static_cast<std::int64_t>(value->value_u64.value);
            case SCS_VALUE_TYPE_bool:
                return value->value_bool.value;
            case SCS_VALUE_TYPE_float:
                return static_cast<std::int64_t>(value->value_float.value);
            default:
                return 0;
        }
    }

    float real(const scs_value_t* value)
    {
        if (value != nullptr && value->type == SCS_VALUE_TYPE_float)
        {
            return value->value_float.value;
        }

        return static_cast<float>(integer(value));
    }

    Event event_of(EventKind kind)
    {
        Event event;
        event.game = s_packet.game;
        event.kind = kind;
        event.gameTime = s_packet.gameTime;
        return event;
    }

    SCSAPI_VOID on_configuration(const scs_event_t, const void* const info, const scs_context_t)
    {
        const auto* const configuration = static_cast<const scs_telemetry_configuration_t*>(info);

        if (configuration == nullptr || std::strcmp(configuration->id, SCS_TELEMETRY_CONFIG_job) != 0)
        {
            return;
        }

        const scs_named_value_t* const values = configuration->attributes;
        Event job = event_of(EventKind::JobTaken);
        copy_text(job.sourceCity, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city));
        copy_text(job.sourceCompany, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_company));
        copy_text(job.destinationCity, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city));
        copy_text(job.destinationCompany, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_company));
        copy_text(job.cargo, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo));
        copy_text(job.detail, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_job_market));
        copy_text(job.sourceCityId, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_city_id));
        copy_text(job.sourceCompanyId, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_source_company_id));
        copy_text(job.destinationCityId, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_city_id));
        copy_text(job.destinationCompanyId, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_destination_company_id));
        copy_text(job.cargoId, attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_id));
        job.amount = integer(attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_income));
        job.mass = real(attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cargo_mass));
        job.plannedKilometres = real(attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_planned_distance_km));
        job.minutes =
            static_cast<std::uint32_t>(integer(attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_delivery_time)));
        job.flags = integer(attribute(values, SCS_TELEMETRY_CONFIG_ATTRIBUTE_special_job)) != 0
            ? Wheelstar::Telemetry::SpecialJob
            : 0;

        // The configuration comes again whenever anything in it changes, and empty when the job is over. Only a new
        // job is worth a message: the same destination and cargo is the job already announced.
        const bool none = job.destinationCompanyId[0] == 0;
        const bool same = job.sourceCompanyId == s_job.sourceCompanyId &&
            job.destinationCompanyId == s_job.destinationCompanyId && job.cargoId == s_job.cargoId;
        s_job = job;

        if (!none && !same)
        {
            send_datagram(job);
        }
    }

    SCSAPI_VOID on_gameplay(const scs_event_t, const void* const info, const scs_context_t)
    {
        const auto* const gameplay = static_cast<const scs_telemetry_gameplay_event_t*>(info);

        if (gameplay == nullptr)
        {
            return;
        }

        const char* const id = gameplay->id;
        const scs_named_value_t* const values = gameplay->attributes;
        Event event;

        if (std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_job_delivered) == 0)
        {
            event = event_of(EventKind::JobDelivered);
            event.amount = integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_revenue));
            event.experience =
                static_cast<std::int32_t>(integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_earned_xp)));
            event.cargoDamage = real(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_cargo_damage));
            event.kilometres = real(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_distance_km));
            event.minutes = static_cast<std::uint32_t>(
                integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_delivery_time)));
            event.flags = (integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_auto_park_used)) != 0
                               ? Wheelstar::Telemetry::AutoParked
                               : 0) |
                (integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_auto_load_used)) != 0
                     ? Wheelstar::Telemetry::AutoLoaded
                     : 0);
        }
        else if (std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_job_cancelled) == 0)
        {
            event = event_of(EventKind::JobCancelled);
            event.amount = integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_cancel_penalty));
        }
        else if (std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_player_fined) == 0)
        {
            event = event_of(EventKind::Fined);
            event.amount = integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_fine_amount));
            copy_text(event.detail, attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_fine_offence));
        }
        else if (std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_player_tollgate_paid) == 0)
        {
            event = event_of(EventKind::TollPaid);
            event.amount = integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_pay_amount));
        }
        else if (std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_player_use_ferry) == 0 ||
                 std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_player_use_train) == 0)
        {
            const bool ferry = std::strcmp(id, SCS_TELEMETRY_GAMEPLAY_EVENT_player_use_ferry) == 0;
            event = event_of(ferry ? EventKind::Ferry : EventKind::Train);
            event.amount = integer(attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_pay_amount));
            copy_text(event.sourceCity, attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_source_name));
            copy_text(event.destinationCity, attribute(values, SCS_TELEMETRY_GAMEPLAY_EVENT_ATTRIBUTE_target_name));
        }
        else
        {
            return;
        }

        // A delivery or a cancellation ends the job, so the next one is announced even if it is the same route.
        if (event.kind == EventKind::JobDelivered || event.kind == EventKind::JobCancelled)
        {
            s_job = Event{};
        }

        send_datagram(event);
    }

    SCSAPI_VOID on_frame_end(const scs_event_t, const void*, const scs_context_t)
    {
        send_packet();
    }

    SCSAPI_VOID on_pause(const scs_event_t event, const void*, const scs_context_t)
    {
        s_packet.paused = event == SCS_TELEMETRY_EVENT_paused ? 1 : 0;
        send_packet();
    }

    SCSAPI_VOID store_float(const scs_string_t, const scs_u32_t, const scs_value_t* const value,
                            const scs_context_t context)
    {
        if (value != nullptr && context != nullptr)
        {
            *static_cast<float*>(context) = value->value_float.value;
        }
    }

    SCSAPI_VOID store_gear(const scs_string_t, const scs_u32_t, const scs_value_t* const value,
                           const scs_context_t context)
    {
        if (value != nullptr && context != nullptr)
        {
            *static_cast<std::int32_t*>(context) = value->value_s32.value;
        }
    }

    SCSAPI_VOID store_u32(const scs_string_t, const scs_u32_t, const scs_value_t* const value,
                          const scs_context_t context)
    {
        if (value != nullptr && context != nullptr)
        {
            *static_cast<std::uint32_t*>(context) = value->value_u32.value;
        }
    }

    SCSAPI_VOID store_bool(const scs_string_t, const scs_u32_t, const scs_value_t* const value,
                           const scs_context_t context)
    {
        if (context != nullptr)
        {
            // The SDK calls without a value when the channel has none any more; that is off.
            *static_cast<std::uint8_t*>(context) = value != nullptr && value->value_bool.value != 0 ? 1 : 0;
        }
    }

    Game game_from_id(const char* id)
    {
        if (std::strcmp(id, SCS_GAME_ID_EUT2) == 0)
        {
            return Game::EuroTruckSimulator2;
        }

        if (std::strcmp(id, SCS_GAME_ID_ATS) == 0)
        {
            return Game::AmericanTruckSimulator;
        }

        return Game::Unknown;
    }

    bool open_socket()
    {
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return false;
        }

        s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (s_socket == INVALID_SOCKET)
        {
            WSACleanup();
            return false;
        }

        s_target.sin_family = AF_INET;
        s_target.sin_port = htons(Wheelstar::Telemetry::DefaultPort);
        s_target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return true;
    }
} // namespace

extern "C"
{
    __declspec(dllexport) SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,
                                                           const scs_telemetry_init_params_t* const params)
    {
        if (version != SCS_TELEMETRY_VERSION_1_00)
        {
            return SCS_RESULT_unsupported;
        }

        const auto* const init = static_cast<const scs_telemetry_init_params_v100_t*>(params);

        if (!open_socket())
        {
            init->common.log(SCS_LOG_TYPE_error, "istar-wheelstar: cannot open the UDP socket");
            return SCS_RESULT_generic_error;
        }

        s_packet = Packet{};
        s_job = Event{};
        s_packet.game = game_from_id(init->common.game_id);
        s_packet.paused = 1; // the game starts paused

        const bool registered =
            init->register_for_event(SCS_TELEMETRY_EVENT_frame_end, on_frame_end, nullptr) == SCS_RESULT_ok &&
            init->register_for_event(SCS_TELEMETRY_EVENT_paused, on_pause, nullptr) == SCS_RESULT_ok &&
            init->register_for_event(SCS_TELEMETRY_EVENT_started, on_pause, nullptr) == SCS_RESULT_ok &&
            init->register_for_event(SCS_TELEMETRY_EVENT_configuration, on_configuration, nullptr) == SCS_RESULT_ok;

        // Gameplay events came with SDK 1.14 (game 1.35); an older game refuses them, and the rest still works.
        init->register_for_event(SCS_TELEMETRY_EVENT_gameplay, on_gameplay, nullptr);

        if (!registered)
        {
            init->common.log(SCS_LOG_TYPE_error, "istar-wheelstar: cannot register for the events");
            return SCS_RESULT_generic_error;
        }

        // A channel the game does not offer stays at zero.
        struct FloatChannel
        {
            const char* name;
            float* target;
        };

        const FloatChannel floats[] = {
            {SCS_TELEMETRY_TRUCK_CHANNEL_speed, &s_packet.speed},
            {SCS_TELEMETRY_TRUCK_CHANNEL_engine_rpm, &s_packet.engineRpm},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_steering, &s_packet.steering},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle, &s_packet.throttle},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_brake, &s_packet.brake},
            {SCS_TELEMETRY_TRUCK_CHANNEL_effective_clutch, &s_packet.clutch},
            {SCS_TELEMETRY_TRUCK_CHANNEL_odometer, &s_packet.odometer},
            {SCS_TELEMETRY_TRUCK_CHANNEL_navigation_speed_limit, &s_packet.speedLimit},
            {SCS_TELEMETRY_TRUCK_CHANNEL_cruise_control, &s_packet.cruiseControl},
            {SCS_TELEMETRY_TRUCK_CHANNEL_fuel, &s_packet.fuel},
            {SCS_TELEMETRY_JOB_CHANNEL_cargo_damage, &s_packet.cargoDamage},
            {SCS_TELEMETRY_TRUCK_CHANNEL_wear_engine, &s_packet.wearEngine},
            {SCS_TELEMETRY_TRUCK_CHANNEL_wear_transmission, &s_packet.wearTransmission},
            {SCS_TELEMETRY_TRUCK_CHANNEL_wear_cabin, &s_packet.wearCabin},
            {SCS_TELEMETRY_TRUCK_CHANNEL_wear_chassis, &s_packet.wearChassis},
            {SCS_TELEMETRY_TRUCK_CHANNEL_wear_wheels, &s_packet.wearWheels},
        };

        for (const auto& channel : floats)
        {
            init->register_for_channel(channel.name, SCS_U32_NIL, SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none,
                                       store_float, channel.target);
        }

        init->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_engine_gear, SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                                   SCS_TELEMETRY_CHANNEL_FLAG_none, store_gear, &s_packet.gear);
        init->register_for_channel(SCS_TELEMETRY_CHANNEL_game_time, SCS_U32_NIL, SCS_VALUE_TYPE_u32,
                                   SCS_TELEMETRY_CHANNEL_FLAG_none, store_u32, &s_packet.gameTime);
        init->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_retarder_level, SCS_U32_NIL, SCS_VALUE_TYPE_u32,
                                   SCS_TELEMETRY_CHANNEL_FLAG_none, store_u32, &s_packet.retarderLevel);
        init->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_motor_brake, SCS_U32_NIL, SCS_VALUE_TYPE_bool,
                                   SCS_TELEMETRY_CHANNEL_FLAG_none, store_bool, &s_packet.motorBrake);

        return SCS_RESULT_ok;
    }

    __declspec(dllexport) SCSAPI_VOID scs_telemetry_shutdown()
    {
        // Tell the application the game is closing before the socket goes away.
        s_packet.paused = 1;
        s_packet.closing = 1;
        send_packet();

        if (s_socket != INVALID_SOCKET)
        {
            closesocket(s_socket);
            s_socket = INVALID_SOCKET;
            WSACleanup();
        }
    }
}
