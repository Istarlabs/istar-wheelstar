// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>

// What the telemetry plugin sends to istar-wheelstar: a Packet per simulation frame, and an Event whenever something
// happens in the game (a job taken or delivered, a fine, a toll). This header is the only thing the two sides
// share: the plugin is built for Windows and runs inside the game, so it has no Qt.
//
// Both sides are little-endian x86, so the structs go over the wire as they are. Both have a fixed size, so a
// datagram's size alone says which of the two it is.

namespace Wheelstar::Telemetry
{
    /// Where the plugin sends to and the application listens, on the loopback interface.
    inline constexpr std::uint16_t DefaultPort = 40540;

    /// 2 added the odometer, 3 the channels after it and the events. Plugin and application come in one release, so
    /// neither side reads the other's old one.
    inline constexpr std::uint8_t ProtocolVersion = 3;

    enum class Game : std::uint8_t
    {
        Unknown = 0,
        EuroTruckSimulator2 = 1,
        AmericanTruckSimulator = 2,
    };

#pragma pack(push, 1)
    struct Packet
    {
        std::array<char, 4> magic = {'O', 'V', 'S', 'T'};
        std::uint8_t version = ProtocolVersion;
        Game game = Game::Unknown;
        /// 1 while the game is paused (menu, map screen), 0 while it simulates.
        std::uint8_t paused = 0;
        /// 1 in the last packet before the plugin goes away: the game is closing, the session is over.
        std::uint8_t closing = 0;

        /// Metres per second.
        float speed = 0;
        float engineRpm = 0;
        /// Positive forward, 0 neutral, negative reverse.
        std::int32_t gear = 0;

        /// What the simulation uses, -1 to 1. The game counts it counterclockwise: positive is left.
        float steering = 0;
        /// 0 to 1.
        float throttle = 0;
        float brake = 0;
        float clutch = 0;

        /// The truck's odometer in kilometres, as the game counts them: the map is scaled down, and the game scales
        /// every distance it shows back up (local.scale in the SDK, 19 on the roads of Euro Truck Simulator 2), so
        /// this runs that much faster than speed times time. 0 when the game does not offer the channel.
        float odometer = 0;

        /// In-game minutes since the start of the first in-game day.
        std::uint32_t gameTime = 0;
        /// The speed limit where the truck is, in metres per second, as the navigation knows it; 0 when there is
        /// none (no limit, or none known).
        float speedLimit = 0;
        /// The speed the cruise control holds, in metres per second; 0 when it is off.
        float cruiseControl = 0;
        /// Litres in the tank.
        float fuel = 0;
        /// The damage of the cargo of the current job, 0 to 1; 0 without a job.
        float cargoDamage = 0;
        /// Wear of the truck's parts, 0 (new) to 1.
        float wearEngine = 0;
        float wearTransmission = 0;
        float wearCabin = 0;
        float wearChassis = 0;
        float wearWheels = 0;
        /// The retarder's step, 0 when off.
        std::uint32_t retarderLevel = 0;
        /// 1 while the engine brake is on.
        std::uint8_t motorBrake = 0;
        std::array<std::uint8_t, 3> reserved{};
    };

    /// What an Event reports. The fields of an Event that a kind does not mention stay empty.
    enum class EventKind : std::uint8_t
    {
        Unknown = 0,
        /// A job was taken: source and destination, cargo, mass, amount (the offered income), plannedKilometres,
        /// minutes (the deadline, in game time like Packet::gameTime; 0 without one), detail (the job market),
        /// flags (SpecialJob).
        JobTaken = 1,
        /// The job was delivered: amount (revenue), experience, cargoDamage, kilometres (driven), minutes (time
        /// the job took, in game minutes), flags (AutoParked, AutoLoaded).
        JobDelivered = 2,
        /// The job was given up: amount (the penalty).
        JobCancelled = 3,
        /// A fine: detail (the offence, as the SDK names it: "speeding", "red_signal" ...), amount.
        Fined = 4,
        /// A tollgate: amount.
        TollPaid = 5,
        /// A ferry or a train: source and destination (their names), amount.
        Ferry = 6,
        Train = 7,
    };

    enum EventFlag : std::uint8_t
    {
        AutoParked = 1,
        AutoLoaded = 2,
        SpecialJob = 4,
    };

    /// Text fields are UTF-8, cut to fit and always terminated.
    using EventText = std::array<char, 64>;

    struct Event
    {
        std::array<char, 4> magic = {'O', 'V', 'E', 'V'};
        std::uint8_t version = ProtocolVersion;
        Game game = Game::Unknown;
        EventKind kind = EventKind::Unknown;
        std::uint8_t flags = 0;

        /// Packet::gameTime when it happened.
        std::uint32_t gameTime = 0;
        /// Money in the game's currency: revenue, income, fine, toll, fare or penalty.
        std::int64_t amount = 0;
        std::int32_t experience = 0;
        std::uint32_t minutes = 0;
        float cargoDamage = 0;
        /// Kilograms.
        float mass = 0;
        float kilometres = 0;
        float plannedKilometres = 0;

        /// The names as the game shows them, in its language.
        EventText sourceCity{};
        EventText sourceCompany{};
        EventText destinationCity{};
        EventText destinationCompany{};
        EventText cargo{};
        EventText detail{};
        /// The game's ids of the same ("hamburg", "posped", "gravel"), which the save game uses too.
        EventText sourceCityId{};
        EventText sourceCompanyId{};
        EventText destinationCityId{};
        EventText destinationCompanyId{};
        EventText cargoId{};
    };
#pragma pack(pop)

    static_assert(sizeof(Packet) == 88, "the layout is the wire format");
    static_assert(sizeof(Event) == 748, "the layout is the wire format");
} // namespace Wheelstar::Telemetry
