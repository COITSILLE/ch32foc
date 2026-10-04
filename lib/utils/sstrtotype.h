#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Split a string by one delimiter.
 * @param str Input string or NULL to continue.
 * @param delim Delimiter character.
 * @return Token pointer.
 */
char* s_strtok(char *str, const char delim);
/**
 * @brief Convert one digit to an integer.
 * @param num Digit character.
 * @return Numeric value.
 */
uint8_t s_ctoi(const char num);
/**
 * @brief Convert a string to float.
 * @param str Input string.
 * @return Parsed value.
 */
float s_atof(const char *str);
/**
 * @brief Convert a string to int.
 * @param str Input string.
 * @return Parsed value.
 */
int32_t s_atoi(const char *str);
/**
 * @brief Reverse a string in place.
 * @param str String buffer.
 * @param len String length.
 * @return None.
 */
void s_reverse(char *str, uint16_t len);
/**
 * @brief Convert an int to text.
 * @param num Source integer.
 * @param str Output buffer.
 * @param base Output base.
 * @return Written length.
 */
uint32_t s_itoa(const int32_t num, char *str, const uint8_t base);
/**
 * @brief Convert a float to text.
 * @param num Source float.
 * @param str Output buffer.
 * @param precision Decimal digits (0 = integer only, no dot).
 * @return Written length.
 */
uint32_t s_ftoa(const float num, char *str, const uint8_t precision);
/**
 * @brief Join float values with one delimiter.
 * @param str Output buffer.
 * @param delim Delimiter character.
 * @param args Input values.
 * @param argc Number of values.
 * @param precision Decimal digits.
 * @return None.
 */
void s_joinf(char *str, const char delim, const float *args, uint16_t argc, uint8_t precision);

#ifdef __cplusplus
}
#endif