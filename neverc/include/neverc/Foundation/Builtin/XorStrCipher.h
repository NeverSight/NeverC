#ifndef NEVERC_FOUNDATION_BUILTIN_XORSTRCIPHER_H
#define NEVERC_FOUNDATION_BUILTIN_XORSTRCIPHER_H

#include <cstdint>

namespace neverc {
namespace xorstr {

// These helpers model unsigned size_t arithmetic for the compiler-side
// encryptors.  Keep the operation order in sync with xorstr_impl.inc.
inline std::uint64_t wordMask(unsigned WordBits) {
  if (WordBits >= 64)
    return ~std::uint64_t{0};
  return (std::uint64_t{1} << WordBits) - 1;
}

inline std::uint64_t truncateWord(std::uint64_t Value, unsigned WordBits) {
  return Value & wordMask(WordBits);
}

inline std::uint64_t rotateLeftWord(std::uint64_t Value, unsigned Shift,
                                    unsigned WordBits) {
  Value = truncateWord(Value, WordBits);
  Shift %= WordBits;
  if (Shift == 0)
    return Value;
  return truncateWord((Value << Shift) | (Value >> (WordBits - Shift)),
                      WordBits);
}

inline unsigned scheduleShift(std::uint64_t Value, unsigned Lane,
                              unsigned WordBits) {
  Value = truncateWord(Value, WordBits);
  const unsigned Offset =
      (static_cast<unsigned>(Value) + Lane) & (WordBits - 1U);
  return (static_cast<unsigned>(Value >> Offset) & (WordBits - 1U)) | 1U;
}

struct CipherSchedule {
  std::uint64_t InitialState;
  std::uint64_t Addend;
  std::uint64_t IndexStep;
  std::uint64_t Multiplier;
  unsigned ShiftA;
  unsigned ShiftB;
  unsigned ShiftC;
  unsigned StreamShiftA;
  unsigned StreamShiftB;
};

inline std::uint64_t lengthMask(std::uint64_t Key, unsigned WordBits) {
  Key = truncateWord(Key, WordBits);
  const std::uint64_t Inverted = truncateWord(~Key, WordBits);
  return truncateWord(
      rotateLeftWord(Key, scheduleShift(Key, 0, WordBits), WordBits) ^
          rotateLeftWord(Inverted, scheduleShift(Inverted, 1, WordBits),
                         WordBits),
      WordBits);
}

inline std::uint64_t sealLength(std::uint64_t Length, std::uint64_t Key,
                                unsigned WordBits) {
  return truncateWord(Length ^ lengthMask(Key, WordBits), WordBits);
}

inline CipherSchedule makeSchedule(std::uint64_t Key, std::uint64_t Length,
                                   unsigned WordBits) {
  Key = truncateWord(Key, WordBits);
  Length = truncateWord(Length, WordBits);
  const std::uint64_t Inverted = truncateWord(~Key, WordBits);

  CipherSchedule Schedule;
  Schedule.InitialState = truncateWord(
      rotateLeftWord(Key, scheduleShift(Key, 2, WordBits), WordBits) ^
          rotateLeftWord(Inverted, scheduleShift(Key, 3, WordBits), WordBits),
      WordBits);
  Schedule.InitialState =
      truncateWord(Schedule.InitialState + Length, WordBits);
  Schedule.Addend = truncateWord(
      rotateLeftWord(Key, scheduleShift(Inverted, 4, WordBits), WordBits) ^
          (Inverted >> scheduleShift(Key, 5, WordBits)),
      WordBits);
  Schedule.IndexStep =
      truncateWord(rotateLeftWord(Inverted ^ Length,
                                  scheduleShift(Key, 6, WordBits), WordBits) |
                       1U,
                   WordBits);
  Schedule.Multiplier = truncateWord(
      rotateLeftWord(Key ^ Length, scheduleShift(Inverted, 7, WordBits),
                     WordBits) ^
              rotateLeftWord(Inverted, scheduleShift(Key, 8, WordBits),
                             WordBits) |
          1U,
      WordBits);
  Schedule.ShiftA = scheduleShift(Key ^ Length, 9, WordBits);
  Schedule.ShiftB = scheduleShift(Inverted ^ Length, 10, WordBits);
  Schedule.ShiftC = scheduleShift(Key + Length, 11, WordBits);
  Schedule.StreamShiftA = scheduleShift(Key, 12, WordBits);
  Schedule.StreamShiftB = scheduleShift(Inverted, 13, WordBits);
  return Schedule;
}

inline std::uint64_t advanceState(std::uint64_t State, std::uint64_t Index,
                                  const CipherSchedule &Schedule,
                                  unsigned WordBits) {
  State = truncateWord(State, WordBits);
  Index = truncateWord(Index + 1, WordBits);
  const std::uint64_t Indexed =
      truncateWord(Index * Schedule.IndexStep, WordBits);
  State = truncateWord(State + Schedule.Addend + Indexed, WordBits);
  State = truncateWord(State ^ (State >> Schedule.ShiftA), WordBits);
  State = truncateWord(State * Schedule.Multiplier, WordBits);
  State = truncateWord(State ^ (State << Schedule.ShiftB), WordBits);
  State = truncateWord(State ^ (State >> Schedule.ShiftC), WordBits);
  return State;
}

inline std::uint8_t streamByte(std::uint64_t State,
                               const CipherSchedule &Schedule,
                               unsigned WordBits) {
  State = truncateWord(State, WordBits);
  State = truncateWord(State ^ (State >> Schedule.StreamShiftA), WordBits);
  State = truncateWord(State ^ (State << Schedule.StreamShiftB), WordBits);
  State ^= State >> 8;
  State ^= State >> 16;
  if (WordBits > 32)
    State ^= State >> 32;
  return static_cast<std::uint8_t>(State);
}

} // namespace xorstr
} // namespace neverc

#endif
