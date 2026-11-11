// TOTP code generation with a configurable digit length (RFC 6238 / RFC 4226).
//
// The bundled TOTP-Arduino library hardcodes 6 digits, so the truncation step is
// reimplemented here on top of the same HMAC-SHA1 primitive to also support the
// 8 digit codes used by some providers.

#ifndef TOTP_PLUS_H
#define TOTP_PLUS_H

#include <sha1.h>

#define OTP_DIGITS_DEFAULT 6
#define OTP_DIGITS_MAX     8
#define OTP_TIME_STEP      30

// Clamp any stored/received value to a digit length the device can render.
int normalizeOTPDigits(int digits) {
  return (digits == 8) ? 8 : OTP_DIGITS_DEFAULT;
}

// Digit length for an OTP slot. Entries written before digit lengths existed
// have no "D" key and fall back to 6.
int getOTPDigits(int id) {
  int digits = NVS.getInt("D" + String(id));
  if (digits == 0) {
    return OTP_DIGITS_DEFAULT;
  }
  return normalizeOTPDigits(digits);
}

// Generate the current code for a key, zero padded to `digits` characters.
String generateTOTP(uint8_t* hmacKey, int keyLength, long timeStamp, int digits) {
  digits = normalizeOTPDigits(digits);

  long steps = timeStamp / OTP_TIME_STEP;

  // STEP 0, map the number of steps in a 8-bytes array (counter value)
  uint8_t byteArray[8];
  byteArray[0] = 0x00;
  byteArray[1] = 0x00;
  byteArray[2] = 0x00;
  byteArray[3] = 0x00;
  byteArray[4] = (uint8_t)((steps >> 24) & 0xFF);
  byteArray[5] = (uint8_t)((steps >> 16) & 0xFF);
  byteArray[6] = (uint8_t)((steps >> 8) & 0xFF);
  byteArray[7] = (uint8_t)(steps & 0xFF);

  // STEP 1, get the HMAC-SHA1 hash from counter and key
  Sha1.initHmac(hmacKey, keyLength);
  Sha1.write(byteArray, 8);
  uint8_t* hash = Sha1.resultHmac();

  // STEP 2, apply dynamic truncation to obtain a 4-bytes string
  int offset = hash[19] & 0xF;
  uint32_t truncatedHash = 0;
  for (int j = 0; j < 4; ++j) {
    truncatedHash <<= 8;
    truncatedHash |= hash[offset + j];
  }

  // STEP 3, compute the OTP value
  truncatedHash &= 0x7FFFFFFF;
  truncatedHash %= (digits == 8) ? 100000000UL : 1000000UL;

  char code[OTP_DIGITS_MAX + 1];
  snprintf(code, sizeof(code), (digits == 8) ? "%08lu" : "%06lu", (unsigned long)truncatedHash);
  return String(code);
}

#endif
