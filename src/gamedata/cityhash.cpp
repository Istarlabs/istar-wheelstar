// SPDX-FileCopyrightText: 2011 Google, Inc.
// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: MIT
//
// A port of CityHash64 from CityHash 1.0.3 by Geoff Pike and Jyrki Alakuijala:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include "gamedata/cityhash.h"

#include <QtEndian>

#include <bit>
#include <utility>

namespace Wheelstar::GameData
{
    namespace
    {
        constexpr quint64 K0 = 0xc3a5c85c97cb3127ULL;
        constexpr quint64 K1 = 0xb492b66fbe98f273ULL;
        constexpr quint64 K2 = 0x9ae16a3b2f90404fULL;
        constexpr quint64 K3 = 0xc949d7c7509e6557ULL;
        constexpr quint64 KMul = 0x9ddfea08eb382d69ULL;

        quint64 fetch64(const char* data)
        {
            return qFromLittleEndian<quint64>(data);
        }

        quint64 fetch32(const char* data)
        {
            return qFromLittleEndian<quint32>(data);
        }

        quint64 rotate(quint64 value, int shift)
        {
            return shift == 0 ? value : std::rotr(value, shift);
        }

        quint64 shiftMix(quint64 value)
        {
            return value ^ (value >> 47);
        }

        /// Hash128to64 of city.h.
        quint64 hashLen16(quint64 low, quint64 high)
        {
            quint64 a = (low ^ high) * KMul;
            a ^= (a >> 47);
            quint64 b = (high ^ a) * KMul;
            b ^= (b >> 47);
            return b * KMul;
        }

        quint64 hashLen0to16(const char* s, std::size_t length)
        {
            if (length > 8)
            {
                const quint64 a = fetch64(s);
                const quint64 b = fetch64(s + length - 8);
                // RotateByAtLeast1: length is above 8 here, so the shift is never 0.
                return hashLen16(a, std::rotr(b + length, static_cast<int>(length))) ^ b;
            }

            if (length >= 4)
            {
                const quint64 a = fetch32(s);
                return hashLen16(length + (a << 3), fetch32(s + length - 4));
            }

            if (length > 0)
            {
                const auto a = static_cast<quint8>(s[0]);
                const auto b = static_cast<quint8>(s[length >> 1]);
                const auto c = static_cast<quint8>(s[length - 1]);
                const quint32 y = static_cast<quint32>(a) + (static_cast<quint32>(b) << 8);
                const quint32 z = static_cast<quint32>(length) + (static_cast<quint32>(c) << 2);
                return shiftMix((y * K2) ^ (z * K3)) * K2;
            }

            return K2;
        }

        quint64 hashLen17to32(const char* s, std::size_t length)
        {
            const quint64 a = fetch64(s) * K1;
            const quint64 b = fetch64(s + 8);
            const quint64 c = fetch64(s + length - 8) * K2;
            const quint64 d = fetch64(s + length - 16) * K0;
            return hashLen16(rotate(a - b, 43) + rotate(c, 30) + d, a + rotate(b ^ K3, 20) - c + length);
        }

        quint64 hashLen33to64(const char* s, std::size_t length)
        {
            quint64 z = fetch64(s + 24);
            quint64 a = fetch64(s) + ((length + fetch64(s + length - 16)) * K0);
            quint64 b = rotate(a + z, 52);
            quint64 c = rotate(a, 37);
            a += fetch64(s + 8);
            c += rotate(a, 7);
            a += fetch64(s + 16);
            const quint64 vf = a + z;
            const quint64 vs = b + rotate(a, 31) + c;
            a = fetch64(s + 16) + fetch64(s + length - 32);
            z = fetch64(s + length - 8);
            b = rotate(a + z, 52);
            c = rotate(a, 37);
            a += fetch64(s + length - 24);
            c += rotate(a, 7);
            a += fetch64(s + length - 16);
            const quint64 wf = a + z;
            const quint64 ws = b + rotate(a, 31) + c;
            const quint64 r = shiftMix(((vf + ws) * K2) + ((wf + vs) * K0));
            return shiftMix((r * K0) + vs) * K2;
        }

        std::pair<quint64, quint64> weakHashLen32WithSeeds(quint64 w, quint64 x, quint64 y, quint64 z, quint64 a,
                                                           quint64 b)
        {
            a += w;
            b = rotate(b + a + z, 21);
            const quint64 c = a;
            a += x;
            a += y;
            b += rotate(a, 44);
            return {a + z, b + c};
        }

        std::pair<quint64, quint64> weakHashLen32WithSeeds(const char* s, quint64 a, quint64 b)
        {
            return weakHashLen32WithSeeds(fetch64(s), fetch64(s + 8), fetch64(s + 16), fetch64(s + 24), a, b);
        }
    } // namespace

    quint64 cityHash64(QByteArrayView data)
    {
        const char* s = data.data();
        auto length = static_cast<std::size_t>(data.size());

        if (length <= 16)
        {
            return hashLen0to16(s, length);
        }

        if (length <= 32)
        {
            return hashLen17to32(s, length);
        }

        if (length <= 64)
        {
            return hashLen33to64(s, length);
        }

        // Over 64 bytes: the end first, then 64-byte chunks with 56 bytes of state.
        quint64 x = fetch64(s + length - 40);
        quint64 y = fetch64(s + length - 16) + fetch64(s + length - 56);
        quint64 z = hashLen16(fetch64(s + length - 48) + length, fetch64(s + length - 24));
        std::pair<quint64, quint64> v = weakHashLen32WithSeeds(s + length - 64, length, z);
        std::pair<quint64, quint64> w = weakHashLen32WithSeeds(s + length - 32, y + K1, x);
        x = (x * K1) + fetch64(s);

        // Down to a multiple of 64; above 64 bytes that is never 0, so the loop runs at least once.
        length = (length - 1) & ~static_cast<std::size_t>(63);

        while (length != 0)
        {
            x = rotate(x + y + v.first + fetch64(s + 8), 37) * K1;
            y = rotate(y + v.second + fetch64(s + 48), 42) * K1;
            x ^= w.second;
            y += v.first + fetch64(s + 40);
            z = rotate(z + w.first, 33) * K1;
            v = weakHashLen32WithSeeds(s, v.second * K1, x + w.first);
            w = weakHashLen32WithSeeds(s + 32, z + w.second, y + fetch64(s + 16));
            std::swap(z, x);
            s += 64;
            length -= 64;
        }

        return hashLen16(hashLen16(v.first, w.first) + (shiftMix(y) * K1) + z, hashLen16(v.second, w.second) + x);
    }
} // namespace Wheelstar::GameData
