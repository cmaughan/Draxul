#pragma once

#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

// Semantic score model (plans/scoreview.md). Symbolic and renderer-free: no
// layout, no pixels, no engraving decisions — those are projections computed
// elsewhere. Durations and positions are exact rationals in whole-note units;
// onsets are computed once at import and stored, so no consumer re-derives
// timeline math from divisions/backup/forward.

namespace draxul
{
namespace notation
{

// Exact rational in whole-note units. Always normalized: den > 0 and
// gcd(|num|, den) == 1. A quarter note is {1, 4}; one 3/4 measure is {3, 4}.
struct Fraction
{
    int num = 0;
    int den = 1;

    static Fraction of(long long n, long long d)
    {
        if (d == 0)
            return Fraction{ 0, 1 };
        if (d < 0)
        {
            n = -n;
            d = -d;
        }
        const long long g = std::gcd(n < 0 ? -n : n, d);
        if (g > 1)
        {
            n /= g;
            d /= g;
        }
        return Fraction{ static_cast<int>(n), static_cast<int>(d) };
    }

    Fraction operator+(Fraction o) const
    {
        return of(static_cast<long long>(num) * o.den + static_cast<long long>(o.num) * den,
            static_cast<long long>(den) * o.den);
    }
    Fraction operator-(Fraction o) const
    {
        return of(static_cast<long long>(num) * o.den - static_cast<long long>(o.num) * den,
            static_cast<long long>(den) * o.den);
    }
    Fraction& operator+=(Fraction o)
    {
        *this = *this + o;
        return *this;
    }
    Fraction& operator-=(Fraction o)
    {
        *this = *this - o;
        return *this;
    }

    bool operator==(const Fraction& o) const = default;
    bool operator<(Fraction o) const
    {
        return static_cast<long long>(num) * o.den < static_cast<long long>(o.num) * den;
    }
    bool operator<=(Fraction o) const
    {
        return !(o < *this);
    }
    bool operator>(Fraction o) const
    {
        return o < *this;
    }
    bool operator>=(Fraction o) const
    {
        return !(*this < o);
    }

    bool is_zero() const
    {
        return num == 0;
    }
    bool is_negative() const
    {
        return num < 0;
    }
    double to_double() const
    {
        return static_cast<double>(num) / static_cast<double>(den);
    }
    std::string to_string() const
    {
        return std::to_string(num) + "/" + std::to_string(den);
    }
};

enum class Step : uint8_t
{
    C,
    D,
    E,
    F,
    G,
    A,
    B,
};

struct Pitch
{
    Step step = Step::C;
    int alter = 0; // chromatic alteration in semitones (-2..+2 in practice)
    int octave = 4; // scientific pitch notation; octave 4 contains middle C
};

enum class TieState : uint8_t
{
    None,
    Start,
    Stop,
    Both,
};

enum class Accidental : uint8_t
{
    Sharp,
    Flat,
    Natural,
    DoubleSharp,
    DoubleFlat,
    Other, // microtonal / editorial accidentals we don't model yet
};

// Tuplet ratio from MusicXML <time-modification>: actual notes played in the
// time of normal notes (triplet = 3 actual / 2 normal). The note's duration
// already includes this ratio; it is kept for notation (bracket/number).
struct TimeModification
{
    int actual_notes = 1;
    int normal_notes = 1;
};

struct Note
{
    uint64_t id = 0; // stable within the document; assigned at import; 0 = invalid
    bool is_rest = false;
    bool grace = false; // grace notes carry no duration and don't advance the voice cursor
    Pitch pitch; // meaningful only when !is_rest
    Fraction onset; // position within the measure; computed once at import
    Fraction duration; // sounding duration incl. dots/tuplets; {0,1} for grace notes
    int dots = 0; // notated augmentation dots (duration already includes them)
    int staff = 1; // 1-based within the part (piano: 1 = upper, 2 = lower)
    int voice = 1; // as encoded (MuseScore numbers staff-2 voices from 5)
    bool chord_with_prev = false; // shares onset with the previous non-chord note
    TieState tie = TieState::None;
    std::optional<Accidental> written_accidental;
    std::optional<TimeModification> tuplet;
};

struct KeySignature
{
    int fifths = 0; // circle-of-fifths position: sharps > 0, flats < 0
};

struct TimeSignature
{
    int beats = 4;
    int beat_type = 4;

    Fraction measure_duration() const
    {
        return Fraction::of(beats, beat_type);
    }
};

struct ClefChange
{
    int staff = 1;
    char sign = 'G'; // 'G', 'F', 'C', or 'P' (percussion)
    int line = 2;
    int octave_change = 0; // e.g. -1 for G8vb
    Fraction onset; // position within the measure
};

struct Measure
{
    std::string number; // MusicXML measure number attribute; may be non-numeric ("X1")
    bool implicit = false; // pickup / partial measures are exempt from fill validation
    std::optional<KeySignature> key;
    std::optional<TimeSignature> time;
    std::vector<ClefChange> clefs;
    std::vector<Note> notes; // document order; onsets carry the timeline
};

struct Part
{
    std::string id; // MusicXML part id ("P1")
    std::string name;
    int staves = 1;
    std::vector<Measure> measures;
};

struct ScoreDocument
{
    std::string title;
    std::string composer;
    std::vector<Part> parts;
};

} // namespace notation
} // namespace draxul
